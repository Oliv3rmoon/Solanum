/*
 * Solanum - the store's side for the computer build (tools/build-desktop.sh): no console, no network. Its
 * answers come from a folder of fixtures (NP_FIXTURES, as tests/fixtures holds them: catalog.json and
 * releases/<name>-latest.json, releases/<name>-<file>), so the interface can be worked on, and its preview
 * scripted, without a PS5 or GitHub. Nothing is installed or opened: the helper and the launch payload need
 * the console's payload loader.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if !defined(__PROSPERO__)
#include "store.h"

#include "nightshade.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fixture a URL stands for: the catalog, a repository's releases/latest, a release's file. */
static bool fixture(const char *url, char *out, size_t size)
{
    const char *folder = SDL_getenv("NP_FIXTURES");
    const char *api = "https://api.github.com/repos/";
    const char *files = "https://github.com/";
    if (!folder || !*folder)
        return false;
    if (strcmp(url, CATALOG_URL) == 0)
    {
        snprintf(out, size, "%s/catalog.json", folder);
        return true;
    }
    if (strncmp(url, api, strlen(api)) == 0)
    {
        const char *name = strchr(url + strlen(api), '/');
        const char *end = name ? strchr(name + 1, '/') : NULL;
        if (!end)
            return false;
        snprintf(out, size, "%s/releases/%.*s-latest.json", folder, (int)(end - name - 1), name + 1);
        return true;
    }
    if (strncmp(url, files, strlen(files)) == 0)
    {
        const char *name = strchr(url + strlen(files), '/');
        const char *end = name ? strchr(name + 1, '/') : NULL;
        const char *file = strrchr(url, '/');
        if (!end || !file)
            return false;
        snprintf(out, size, "%s/releases/%.*s-%s", folder, (int)(end - name - 1), name + 1, file + 1);
        return true;
    }
    return false;
}

static int desktop_fetch(void *user, const char *url, char *body, size_t capacity, size_t *length, int *status)
{
    char path[NP_PATH_MAX];
    (void)user;
    *length = 0;
    *status = 0;
    SDL_Delay(150); /* a network's pace, so the loading screens show */
    FILE *file = fixture(url, path, sizeof(path)) ? fopen(path, "rb") : NULL;
    if (!file)
    {
        *status = 404;
        return 0;
    }
    *length = fread(body, 1, capacity, file);
    const bool whole = feof(file) || fgetc(file) == EOF;
    fclose(file);
    *status = 200;
    return whole ? 0 : SELF_UPDATE_FETCH_TOO_LARGE;
}

int store_kit_range(void *user, const char *url, uint64_t offset, uint64_t limit, self_update_sink sink,
                    void *sink_user, int *status)
{
    char path[NP_PATH_MAX], piece[65536];
    (void)user;
    *status = 0;
    FILE *file = fixture(url, path, sizeof(path)) ? fopen(path, "rb") : NULL;
    if (!file)
        return -1;
    *status = offset > 0 ? 206 : 200;
    fseek(file, (long)offset, SEEK_SET);
    uint64_t sent = 0;
    size_t got;
    while ((got = fread(piece, 1, sizeof(piece), file)) > 0)
    {
        if (sent + got > limit || sink(sink_user, piece, got) != 1)
        {
            fclose(file);
            return -1;
        }
        sent += got;
    }
    fclose(file);
    return 0;
}

static int desktop_download(void *user, const char *url, uint64_t limit, self_update_sink sink, void *sink_user)
{
    int status;
    return store_kit_range(user, url, 0, limit, sink, sink_user, &status);
}

static int desktop_open_helper(void *user, self_update_channel *channel)
{
    (void)user;
    (void)channel;
    return 0; /* no payload loader here */
}

static uint64_t desktop_now(void *user)
{
    (void)user;
    return SDL_GetTicks64();
}

const self_update_platform *store_kit_platform(void)
{
    static const self_update_platform platform = {desktop_fetch, desktop_download, NULL, desktop_open_helper,
                                                  NULL,          NULL,             desktop_now, NULL};
    return &platform;
}

const char *store_app_file(const char *name)
{
    static char paths[3][NP_PATH_MAX];
    static const char *const names[] = {"self-updater.elf", "app-launch.elf", "sce_sys/param.json"};
    const char *folder = SDL_getenv("NP_APP_FOLDER");
    for (int i = 0; i < 3; ++i)
        if (strcmp(name, names[i]) == 0)
        {
            path_join(paths[i], sizeof(paths[i]), folder && *folder ? folder : "dist/" NP_TITLE_ID, name);
            return paths[i];
        }
    return "/nonexistent";
}

int store_kit_open_payload(const char *path, self_update_channel *channel)
{
    (void)path;
    (void)channel;
    return 0;
}

int store_kit_channel_timeout(const self_update_channel *channel, int milliseconds)
{
    (void)channel;
    (void)milliseconds;
    return 0;
}
#endif
