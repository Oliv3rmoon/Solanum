/*
 * Solanum - the icons on the apps' tiles (tile_icons.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "tile_icons.h"

#include "../appicon.h"
#include "../nightshade.h"
#include "../store.h"

#include "stb_image.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libcurl and OpenSSL want more stack than a default thread of the console has (store.c). */
#define ICON_STACK (1024 * 1024)
#define ENTRIES CATALOG_MAX_APPS
#define WAITING 4      /* decoded pictures waiting for the main thread, at most (about 110 KB each) */
#define UPLOADS 2      /* textures made a frame, at most */
#define STALE_FRAMES 30 /* an icon asked for whose tile hasn't been drawn for half a second isn't fetched */

typedef enum
{
    ICON_IDLE,    /* not asked for (or its texture gave way) */
    ICON_WANTED,  /* its tile was drawn: the thread is to fetch it */
    ICON_WORKING, /* the thread has it */
    ICON_DECODED, /* pixels for the main thread */
    ICON_SHOWN,   /* its texture is kept */
    ICON_FAILED   /* until the next catalog */
} IconState;

typedef struct
{
    CatalogIcon icon; /* path empty: a free entry */
    char app[32];     /* the app's id, for the log */
    int state;        /* IconState */
    uint64_t asked;   /* the frame its tile was last drawn in */
    uint8_t *pixels;  /* ICON_DECODED: APPICON_SIDE x APPICON_SIDE RGBA */
} Entry;

static SDL_mutex *lock;
static char cache[NP_PATH_MAX];
static Entry entries[ENTRIES];
static uint64_t frame = 2;
static int waiting;
static bool running, stopping;
/* The catalog the cache keeps the icons of: its generation, and its icons' SHA-256s for the thread to prune by. */
static int generation = -1;
static char keep[ENTRIES][65];
static int keep_count;
static bool want_prune;
/* The main thread's alone. */
static AppIconLru lru;

/* ---------------------------------------------------------------- the thread */

/* The picture a tile draws from an icon's PNG: decoded, fitted to APPICON_SIDE, its corners rounded. */
static uint8_t *decode(const unsigned char *png, size_t length, char *why, size_t size)
{
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(png, (int)length, &width, &height, &channels) || width <= 0 || height <= 0 ||
        width > CATALOG_ICON_SIDE || height > CATALOG_ICON_SIDE)
    {
        snprintf(why, size, "not a picture of at most %dx%d", CATALOG_ICON_SIDE, CATALOG_ICON_SIDE);
        return NULL;
    }
    stbi_uc *rgba = stbi_load_from_memory(png, (int)length, &width, &height, &channels, 4);
    if (!rgba)
    {
        snprintf(why, size, "it couldn't be decoded");
        return NULL;
    }
    uint8_t *out = malloc(APPICON_PIXEL_BYTES);
    if (out && !appicon_fit(rgba, width, height, out))
    {
        free(out);
        out = NULL;
    }
    stbi_image_free(rgba);
    if (!out)
    {
        snprintf(why, size, "no memory for it");
        return NULL;
    }
    appicon_round(out, APPICON_SIDE, APPICON_SIDE * APPICON_RADIUS);
    return out;
}

/* Works for as long as an icon is wanted (or the cache is to be pruned), then ends; started again when needed. */
static int work(void *unused)
{
    (void)unused;
    unsigned char *body = malloc(CATALOG_ICON_BYTES);
    for (;;)
    {
        SDL_LockMutex(lock);
        if (stopping || !body)
        {
            running = false;
            SDL_UnlockMutex(lock);
            free(body);
            return 0;
        }
        if (want_prune)
        {
            static char kept[ENTRIES][65];
            const int count = keep_count;
            memcpy(kept, keep, sizeof(kept));
            want_prune = false;
            SDL_UnlockMutex(lock);
            const int removed = appicon_prune(cache, (const char (*)[65])kept, count);
            if (removed)
                np_log("icons: %d kept icons the catalog no longer names deleted", removed);
            continue;
        }
        /* the icon whose tile was drawn last; ones whose tiles have gone from the screen are left */
        Entry *pick = NULL;
        for (int i = 0; i < ENTRIES; ++i)
        {
            Entry *e = &entries[i];
            if (e->state != ICON_WANTED)
                continue;
            if (e->asked + STALE_FRAMES < frame)
                e->state = ICON_IDLE;
            else if (!pick || e->asked > pick->asked)
                pick = e;
        }
        if (pick && waiting >= WAITING)
        {
            SDL_UnlockMutex(lock);
            SDL_Delay(16); /* the main thread makes their textures first */
            continue;
        }
        if (!pick)
        {
            running = false;
            SDL_UnlockMutex(lock);
            free(body);
            return 0;
        }
        pick->state = ICON_WORKING;
        const CatalogIcon icon = pick->icon;
        char app[32], why[300] = "";
        snprintf(app, sizeof(app), "%s", pick->app);
        SDL_UnlockMutex(lock);

        /* the kept copy, or the catalog's repository */
        const self_update_platform *platform = store_kit_platform();
        size_t length = 0;
        const AppIconFrom from = appicon_load(cache, &icon, platform->fetch, platform->user, body, CATALOG_ICON_BYTES,
                                              &length, why, sizeof(why));
        uint8_t *pixels = from != APPICON_NONE ? decode(body, length, why, sizeof(why)) : NULL;
        if (pixels)
            np_log("icons: %s %s (%zu bytes)%s%s", app, from == APPICON_KEPT ? "from the cache" : "fetched", length,
                   why[0] ? ": " : "", why);
        else
            np_log("icons: %s: %s; its letter is shown", app, why[0] ? why : "no icon");

        SDL_LockMutex(lock);
        /* a working entry is never reused, so pick is still this icon's */
        if (pixels && !stopping)
        {
            pick->pixels = pixels;
            pick->state = ICON_DECODED;
            ++waiting;
        }
        else
        {
            free(pixels);
            pick->state = pixels ? ICON_IDLE : ICON_FAILED;
        }
        SDL_UnlockMutex(lock);
    }
}

static void start_thread(void)
{
    /* under the lock */
    if (running || stopping)
        return;
    running = true;
    SDL_Thread *thread = SDL_CreateThreadWithStackSize(work, "icons", ICON_STACK, NULL);
    if (thread)
    {
        SDL_DetachThread(thread);
        return;
    }
    running = false;
    np_log("icons: the thread could not start");
}

/* ---------------------------------------------------------------- the main thread */

void tile_icons_init(const char *cache_folder)
{
    lock = SDL_CreateMutex();
    str_copy(cache, sizeof(cache), cache_folder);
    np_log("icons: kept in %s/icons; at most %d textures of %dx%d in memory (%d KB each with their pixels and mips, "
           "%d KB in all)",
           cache, APPICON_TEXTURES, APPICON_SIDE, APPICON_SIDE, APPICON_TEXTURE_BYTES / 1024,
           APPICON_BUDGET_BYTES / 1024);
}

void tile_icons_shutdown(void)
{
    if (!lock)
        return;
    SDL_LockMutex(lock);
    stopping = true;
    for (int i = 0; i < ENTRIES; ++i)
    {
        free(entries[i].pixels);
        entries[i].pixels = NULL;
        if (entries[i].state == ICON_DECODED || entries[i].state == ICON_SHOWN)
            entries[i].state = ICON_IDLE;
    }
    waiting = 0;
    SDL_UnlockMutex(lock);
    for (int i = 0; i < lru.count; ++i)
        plat_texture_free((PlatTexture *)lru.slot[i].texture);
    memset(&lru, 0, sizeof(lru));
}

static Entry *find(const char *sha256)
{
    /* under the lock */
    for (int i = 0; i < ENTRIES; ++i)
        if (entries[i].icon.path[0] && strcmp(entries[i].icon.sha256, sha256) == 0)
            return &entries[i];
    return NULL;
}

void tile_icons_poll(void)
{
    if (!lock)
        return;
    const Catalog *catalog = store_catalog();
    const CatalogState state = store_catalog_state();
    SDL_LockMutex(lock);
    ++frame; /* the thread reads it, under the lock */
    /* a new catalog: the cache keeps its icons alone, and those that failed are tried again */
    if (store_generation() != generation && catalog->app_count > 0 &&
        (state == CATALOG_READY || state == CATALOG_COPY))
    {
        generation = store_generation();
        keep_count = 0;
        for (int i = 0; i < catalog->app_count && keep_count < ENTRIES; ++i)
            if (catalog->apps[i].source == 0 && catalog->apps[i].icon.path[0])
                snprintf(keep[keep_count++], sizeof(keep[0]), "%s", catalog->apps[i].icon.sha256);
        for (int i = 0; i < ENTRIES; ++i)
            if (entries[i].state == ICON_FAILED)
                entries[i].state = ICON_IDLE;
        want_prune = true;
        start_thread();
    }
    SDL_UnlockMutex(lock);

    for (int made = 0; made < UPLOADS; ++made)
    {
        uint8_t *pixels = NULL;
        char sha[65] = "";
        SDL_LockMutex(lock);
        for (int i = 0; i < ENTRIES && !pixels; ++i)
            if (entries[i].state == ICON_DECODED)
            {
                pixels = entries[i].pixels;
                entries[i].pixels = NULL;
                entries[i].state = ICON_SHOWN;
                snprintf(sha, sizeof(sha), "%s", entries[i].icon.sha256);
                --waiting;
            }
        SDL_UnlockMutex(lock);
        if (!pixels)
            break;
        PlatTexture *texture = plat_texture_create_mips(pixels, APPICON_SIDE, APPICON_SIDE);
        free(pixels);
        void *evicted = NULL;
        char evicted_sha[65];
        const bool kept = texture && appicon_lru_put(&lru, sha, texture, frame, &evicted, evicted_sha);
        if (!kept)
            plat_texture_free(texture);
        if (evicted)
            plat_texture_free((PlatTexture *)evicted);
        SDL_LockMutex(lock);
        Entry *e = find(sha);
        if (e && !kept)
            e->state = ICON_IDLE; /* asked for again when its tile is drawn */
        Entry *gone = evicted && strcmp(evicted_sha, sha) != 0 ? find(evicted_sha) : NULL;
        if (gone && gone->state == ICON_SHOWN)
            gone->state = ICON_IDLE; /* read from the cache again when its tile comes back */
        SDL_UnlockMutex(lock);
    }
}

PlatTexture *tile_icons_texture(const CatalogApp *app)
{
    if (!lock || !app || app->source != 0 || !app->icon.path[0])
        return NULL;
    PlatTexture *texture = (PlatTexture *)appicon_lru_get(&lru, app->icon.sha256, frame);
    if (texture)
        return texture;
    SDL_LockMutex(lock);
    Entry *e = find(app->icon.sha256);
    if (!e)
    {
        /* a free entry, or the one asked for longest ago among those not at work and not shown */
        for (int i = 0; i < ENTRIES; ++i)
        {
            Entry *c = &entries[i];
            if (!c->icon.path[0])
            {
                e = c;
                break;
            }
            if ((c->state == ICON_IDLE || c->state == ICON_FAILED) && (!e || c->asked < e->asked))
                e = c;
        }
        if (e)
        {
            memset(e, 0, sizeof(*e));
            e->icon = app->icon;
            snprintf(e->app, sizeof(e->app), "%s", app->id);
        }
    }
    if (e)
    {
        e->asked = frame;
        if ((e->state == ICON_IDLE || e->state == ICON_FAILED) && strcmp(e->icon.path, app->icon.path) != 0)
        {
            e->icon = app->icon; /* the same icon, moved in the catalog's repository */
            e->state = ICON_IDLE;
        }
        if (e->state == ICON_IDLE)
            e->state = ICON_WANTED;
        if (e->state == ICON_WANTED)
            start_thread();
    }
    SDL_UnlockMutex(lock);
    return NULL;
}

void tile_icons_usage(int *textures, size_t *bytes)
{
    *textures = lru.count;
    *bytes = (size_t)lru.count * APPICON_TEXTURE_BYTES;
}
