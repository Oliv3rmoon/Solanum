/*
 * Solanum - the store's decisions (apps.h); from Nightshade's headless/apps_release.cpp.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _DEFAULT_SOURCE 1 /* glibc: lstat, strdup, usleep under -std=c11 */
#include "apps.h"

#include "nightshade.h"
#include "self_update_protocol.h"
#include "update_check.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- where an app is */

/* A small file whole (a param.json, a record of ShadowMountPlus's), NUL-terminated; false when it can't be
 * read or is longer than `limit`. */
static bool read_small(const char *path, size_t limit, char **text, size_t *length)
{
    FILE *file = fopen(path, "rb");
    char *body;
    size_t got;
    *text = NULL;
    *length = 0;
    if (!file)
        return false;
    body = (char *)malloc(limit + 2);
    if (!body)
    {
        fclose(file);
        return false;
    }
    got = fread(body, 1, limit + 1, file);
    fclose(file);
    if (got > limit)
    {
        free(body);
        return false;
    }
    body[got] = '\0';
    *text = body;
    *length = got;
    return true;
}

static bool is_directory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool is_file(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

bool apps_read_param(const char *path, char title[10], char version[12])
{
    char *body;
    size_t length;
    char id[10];
    char content[12];
    bool ok;
    if (!read_small(path, 64 * 1024, &body, &length))
        return false;
    ok = update_check_json_string(body, length, "titleId", id, sizeof(id)) == 1 &&
         update_check_json_string(body, length, "contentVersion", content, sizeof(content)) == 1;
    free(body);
    if (!ok)
        return false;
    memcpy(title, id, sizeof(id));
    memcpy(version, content, sizeof(content));
    return true;
}

/* The folder ShadowMountPlus recorded it mounted a title from: <registered>/app/<title>/mount.lnk holds its
 * path (as the self-update helper reads it). False: none. */
static bool mounted_from(const char *registered, const char *title, char *out, size_t size)
{
    char path[512];
    char *text;
    size_t length;
    out[0] = '\0';
    if (!registered || !registered[0])
        return false;
    snprintf(path, sizeof(path), "%s/app/%s/mount.lnk", registered, title);
    if (!read_small(path, 511, &text, &length))
        return false;
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ' ||
                          text[length - 1] == '\0' || text[length - 1] == '/'))
        text[--length] = '\0';
    if (length > 1 && text[0] == '/' && strlen(text) == length && length < size)
        memcpy(out, text, length + 1);
    free(text);
    return out[0] != '\0';
}

/* The folder `folder` when it holds the title. */
static bool holds(const char *folder, const char *title, Installed *out)
{
    char path[512];
    char id[10];
    char version[12];
    if (strlen(folder) >= sizeof(out->folder))
        return false;
    snprintf(path, sizeof(path), "%s/sce_sys/param.json", folder);
    if (!apps_read_param(path, id, version) || strcmp(id, title) != 0)
        return false;
    memset(out, 0, sizeof(*out));
    str_copy(out->folder, sizeof(out->folder), folder);
    memcpy(out->version, version, sizeof(version));
    return true;
}

/* The path's parent; empty for none (or the root). */
static void parent_of(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    out[0] = '\0';
    if (!slash || slash == path || (size_t)(slash - path) >= size)
        return;
    memcpy(out, path, (size_t)(slash - path));
    out[slash - path] = '\0';
}

void apps_own_folder(const char *own_title, const char *registered, const char *install_dir, char *out,
                     size_t size)
{
    char mounted[512];
    Installed own;
    out[0] = '\0';
    if (mounted_from(registered, own_title, mounted, sizeof(mounted)) && holds(mounted, own_title, &own))
        str_copy(out, size, own.folder);
    else if (install_dir && holds(install_dir, own_title, &own))
        str_copy(out, size, own.folder);
}

Installed apps_find(const char *title, const char *registered, const char *own_folder, const char *homebrew)
{
    Installed found;
    char mounted[512];
    char path[512];
    char roots[2][256];
    int root_count = 0;
    memset(&found, 0, sizeof(found));
    if (!catalog_title_id_valid(title))
        return found;
    if (mounted_from(registered, title, mounted, sizeof(mounted)) && holds(mounted, title, &found))
        return found;
    /* An image: the console keeps a copy of its sce_sys beside the record. */
    if (registered && registered[0])
    {
        snprintf(path, sizeof(path), "%s/app/%s/mount_img.lnk", registered, title);
        if (is_file(path))
        {
            char id[10];
            found.image = true;
            snprintf(found.folder, sizeof(found.folder), "%s/app/%s", registered, title);
            snprintf(path, sizeof(path), "%s/sce_sys/param.json", found.folder);
            if (!apps_read_param(path, id, found.version) || strcmp(id, title) != 0)
                found.version[0] = '\0';
            return found;
        }
    }
    if (own_folder && own_folder[0])
    {
        parent_of(own_folder, roots[0], sizeof(roots[0]));
        if (roots[0][0])
            ++root_count;
    }
    if (homebrew && homebrew[0] && (root_count == 0 || strcmp(roots[0], homebrew) != 0))
        str_copy(roots[root_count++], sizeof(roots[0]), homebrew);
    /* The folder named after the title first, as the helper installs it; then any name. */
    for (int r = 0; r < root_count; ++r)
    {
        snprintf(path, sizeof(path), "%s/%s", roots[r], title);
        if (holds(path, title, &found))
            return found;
    }
    for (int r = 0; r < root_count; ++r)
    {
        DIR *directory = opendir(roots[r]);
        struct dirent *entry;
        int looked = 0;
        if (!directory)
            continue;
        while ((entry = readdir(directory)) != NULL && looked < 4096)
        {
            if (entry->d_name[0] == '.' || strcmp(entry->d_name, title) == 0)
                continue;
            ++looked;
            snprintf(path, sizeof(path), "%s/%s", roots[r], entry->d_name);
            if (is_directory(path) && holds(path, title, &found))
            {
                closedir(directory);
                return found;
            }
        }
        closedir(directory);
    }
    memset(&found, 0, sizeof(found));
    return found;
}

Installed apps_find_in(const char *title, const char *registered, const char *const *roots, int root_count)
{
    Installed found;
    char mounted[512];
    char path[512];
    memset(&found, 0, sizeof(found));
    if (!catalog_title_id_valid(title))
        return found;
    if (mounted_from(registered, title, mounted, sizeof(mounted)) && holds(mounted, title, &found))
        return found;
    if (registered && registered[0])
    {
        snprintf(path, sizeof(path), "%s/app/%s/mount_img.lnk", registered, title);
        if (is_file(path))
        {
            char id[10];
            found.image = true;
            snprintf(found.folder, sizeof(found.folder), "%s/app/%s", registered, title);
            snprintf(path, sizeof(path), "%s/sce_sys/param.json", found.folder);
            if (!apps_read_param(path, id, found.version) || strcmp(id, title) != 0)
                found.version[0] = '\0';
            return found;
        }
    }
    for (int r = 0; r < root_count; ++r)
    {
        snprintf(path, sizeof(path), "%s/%s", roots[r], title);
        if (holds(path, title, &found))
            return found;
    }
    for (int r = 0; r < root_count; ++r)
    {
        DIR *directory = opendir(roots[r]);
        struct dirent *entry;
        int looked = 0;
        if (!directory)
            continue;
        while ((entry = readdir(directory)) != NULL && looked < 4096)
        {
            if (entry->d_name[0] == '.' || strcmp(entry->d_name, title) == 0)
                continue;
            ++looked;
            snprintf(path, sizeof(path), "%s/%s", roots[r], entry->d_name);
            if (is_directory(path) && holds(path, title, &found))
            {
                closedir(directory);
                return found;
            }
        }
        closedir(directory);
    }
    memset(&found, 0, sizeof(found));
    return found;
}

static const char *const PLACES[APPS_PLACES] = {
    "/data/homebrew",      "/mnt/ext0/homebrew",  "/mnt/ext1/homebrew",  "/mnt/usb0/homebrew",
    "/mnt/usb1/homebrew",  "/mnt/usb2/homebrew",  "/mnt/usb3/homebrew",  "/mnt/usb4/homebrew",
    "/mnt/usb5/homebrew",  "/mnt/usb6/homebrew",  "/mnt/usb7/homebrew"};
static const char *const DRIVES[APPS_PLACES] = {"/data",     "/mnt/ext0", "/mnt/ext1", "/mnt/usb0",
                                                "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4",
                                                "/mnt/usb5", "/mnt/usb6", "/mnt/usb7"};

const char *apps_place(int index)
{
    return index >= 0 && index < APPS_PLACES ? PLACES[index] : "";
}

const char *apps_place_drive(int index)
{
    return index >= 0 && index < APPS_PLACES ? DRIVES[index] : "";
}

int apps_console_roots(char roots[][48], int most)
{
    int count = 0;
    for (int d = 0; d < APPS_PLACES; ++d)
    {
        const char *forms[3] = {"%s/homebrew", "%s/etaHEN/games", "%s"};
        for (int f = 0; f < 3; ++f)
        {
            if (f == 2 && d == 0)
                continue; /* /data itself holds no apps */
            if (count == most)
                return count;
            snprintf(roots[count++], 48, forms[f], DRIVES[d]);
        }
    }
    return count;
}

static bool add_sizes(const char *folder, int depth, uint64_t *bytes, int *budget)
{
    DIR *directory;
    struct dirent *entry;
    bool whole = true;
    if (depth > 32)
        return false;
    directory = opendir(folder);
    if (!directory)
        return false;
    while ((entry = readdir(directory)) != NULL)
    {
        char path[768];
        struct stat info;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (--*budget < 0)
        {
            whole = false;
            break;
        }
        snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
        if (lstat(path, &info) != 0)
        {
            whole = false;
            continue;
        }
        if (S_ISDIR(info.st_mode))
            whole = add_sizes(path, depth + 1, bytes, budget) && whole;
        else if (S_ISREG(info.st_mode))
            *bytes += (uint64_t)info.st_size;
    }
    closedir(directory);
    return whole;
}

bool apps_folder_size(const char *folder, uint64_t *bytes)
{
    int budget = 200000;
    *bytes = 0;
    return folder && folder[0] && add_sizes(folder, 0, bytes, &budget);
}

/* ---------------------------------------------------------------- what a release offers */

TagKind apps_tag_version(const char *tag, char version[12])
{
    char text[12];
    unsigned parts[3] = {0, 0, 0};
    const char *rest = tag;
    if (!tag)
        return TAG_NONE;
    if (update_check_version_parse(tag, NULL))
    {
        if (strlen(tag) >= 12)
            return TAG_NONE;
        memcpy(version, tag, strlen(tag) + 1);
        return TAG_EXACT;
    }
    if (github_release_tag_version(tag, text))
    {
        memcpy(version, text, sizeof(text));
        return TAG_EXACT;
    }
    /* Three numbers of at most two, three and three digits, with or without a 'v'. */
    if (*rest == 'v')
        ++rest;
    for (int part = 0; part < 3; ++part)
    {
        size_t digits = 0;
        while (rest[digits] >= '0' && rest[digits] <= '9')
        {
            parts[part] = parts[part] * 10 + (unsigned)(rest[digits] - '0');
            ++digits;
        }
        if (digits == 0 || digits > (part == 0 ? 2u : 3u))
            return TAG_NONE;
        rest += digits;
        if (part < 2)
        {
            if (*rest != '.')
                return TAG_NONE;
            ++rest;
        }
    }
    if (*rest != '\0')
        return TAG_NONE;
    snprintf(text, sizeof(text), "%02u.%03u.%03u", parts[0], parts[1], parts[2]);
    memcpy(version, text, sizeof(text));
    return TAG_LIKELY;
}

Offer apps_decide(const Installed *installed, bool found, const char *tag)
{
    char version[12];
    int comparable = 0;
    int order;
    if (!installed->folder[0])
        return found ? OFFER_GET : OFFER_NONE;
    if (!found || installed->image)
        return OFFER_NONE;
    if (apps_tag_version(tag, version) == TAG_NONE)
        return OFFER_UPDATE;
    order = update_check_version_compare(version, installed->version, &comparable);
    return !comparable || order > 0 ? OFFER_UPDATE : OFFER_CURRENT;
}

Offer apps_decide_release(const Installed *installed, bool found, const github_release_asset *release)
{
    int comparable = 0;
    if (!release || !release->content[0] || !update_check_version_parse(release->content, NULL) ||
        !installed->folder[0] || installed->image || !found)
        return apps_decide(installed, found, release ? release->tag : NULL);
    if (!update_check_version_parse(installed->version, NULL))
        return OFFER_UPDATE; /* what is installed can't be told: as apps_decide does with a tag that names none */
    const int order = update_check_version_compare(release->content, installed->version, &comparable);
    return !comparable || order > 0 ? OFFER_UPDATE : OFFER_CURRENT;
}

bool apps_make_offer(const CatalogApp *app, const github_release_asset *release, const Installed *installed,
                     const char *asking, self_update_offer *offer)
{
    char version[12];
    bool exact;
    if (!offer || !app || !release || !installed || !asking)
        return false;
    memset(offer, 0, sizeof(*offer));
    if (!release->sha256[0] || !release->url[0] || installed->image || strcmp(app->title_id, asking) == 0 ||
        (installed->folder[0] && !update_check_version_parse(installed->version, NULL)))
        return false;
    exact = apps_tag_version(release->tag, version) == TAG_EXACT;
    /* the version the catalog's workflow read in the file: exact, and the helper checks the ZIP holds it */
    if (release->content[0] && update_check_version_parse(release->content, NULL))
    {
        str_copy(version, sizeof(version), release->content);
        exact = true;
    }
    snprintf(offer->title, sizeof(offer->title), "%s", app->title_id);
    snprintf(offer->name, sizeof(offer->name), "%s", app->name);
    snprintf(offer->installed, sizeof(offer->installed), "%s", installed->folder[0] ? installed->version : "none");
    snprintf(offer->available, sizeof(offer->available), "%s", exact ? version : "any");
    snprintf(offer->version, sizeof(offer->version), "%s", release->tag);
    snprintf(offer->artifact, sizeof(offer->artifact), "%s", release->url);
    snprintf(offer->sha256, sizeof(offer->sha256), "%s", release->sha256);
    snprintf(offer->page, sizeof(offer->page), "%s", release->page);
    snprintf(offer->beside, sizeof(offer->beside), "%s", asking);
    offer->size = release->size;
    return offer->sha256[0] != '\0' && self_update_url_allowed(offer->artifact, 0);
}

/* ---------------------------------------------------------------- the catalog's pins */

/* https://github.com/<repository>/releases/download/<tag>/<file>, as github_release_asset_check makes it: a tag and
 * a file that can be in an address (letters, digits, '.', '_' and '-'), an address a download may be made from. */
static bool download_url(char *out, size_t size, const char *repository, const char *tag, const char *file)
{
    char check[160];
    out[0] = '\0';
    if (!github_release_repository_valid(repository) || !tag[0] || tag[0] == '.' || strlen(tag) >= 40 ||
        !github_release_asset_name(tag, "v0", check, sizeof(check)) || !github_release_asset_name(file, "v0", check,
                                                                                                   sizeof(check)))
        return false;
    const int length = snprintf(out, size, "https://github.com/%s/releases/download/%s/%s", repository, tag, file);
    if (length <= 0 || (size_t)length >= size || !self_update_url_allowed(out, 0))
    {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool apps_release_from_pin(const CatalogApp *app, int pin, github_release_asset *out)
{
    const CatalogPin *p;
    memset(out, 0, sizeof(*out));
    if (!app || pin < 0 || pin >= app->pin_count)
        return false;
    p = &app->pins[pin];
    str_copy(out->tag, sizeof(out->tag), p->tag);
    str_copy(out->name, sizeof(out->name), p->asset);
    str_copy(out->sha256, sizeof(out->sha256), p->sha256);
    str_copy(out->content, sizeof(out->content), p->content);
    out->size = p->size;
    out->pinned = 1;
    if (!download_url(out->url, sizeof(out->url), app->repo, p->tag, p->asset))
    {
        memset(out, 0, sizeof(*out));
        return false;
    }
    snprintf(out->page, sizeof(out->page), "https://github.com/%s/releases/tag/%s", app->repo, p->tag);
    if (pin == 0)
    {
        str_copy(out->published, sizeof(out->published), app->published);
        if (app->notes[0])
            (void)github_release_notes(app->notes, strlen(app->notes), out->notes, sizeof(out->notes),
                                       GITHUB_RELEASE_ASSET_NOTE_CHARACTERS, &out->notes_truncated);
    }
    return true;
}

int apps_pin_for(const CatalogApp *app, const char *tag, const char *asset)
{
    for (int i = 0; app && tag && i < app->pin_count; ++i)
        if (strcmp(app->pins[i].tag, tag) == 0 && (!asset || strcmp(app->pins[i].asset, asset) == 0))
            return i;
    return -1;
}

int apps_check_pin(const CatalogApp *app, int reason, github_release_asset *asset)
{
    int pin;
    if (!app || !asset || (reason != GITHUB_RELEASE_NEWER && reason != GITHUB_RELEASE_NO_HASH))
        return reason;
    pin = apps_pin_for(app, asset->tag, asset->name);
    if (pin < 0)
        return reason;
    if (asset->size != app->pins[pin].size ||
        (reason == GITHUB_RELEASE_NEWER && strcmp(asset->sha256, app->pins[pin].sha256) != 0))
    {
        asset->sha256[0] = '\0';
        asset->pinned = 0;
        return GITHUB_RELEASE_HASHES_DIFFER;
    }
    if (reason == GITHUB_RELEASE_NO_HASH)
    {
        /* The check stopped before it made the addresses: made here as it makes them. */
        if (!download_url(asset->url, sizeof(asset->url), app->repo, asset->tag, asset->name))
            return GITHUB_RELEASE_BAD_TAG;
        snprintf(asset->page, sizeof(asset->page), "https://github.com/%s/releases/tag/%s", app->repo, asset->tag);
        str_copy(asset->sha256, sizeof(asset->sha256), app->pins[pin].sha256);
    }
    asset->pinned = 1;
    str_copy(asset->content, sizeof(asset->content), app->pins[pin].content);
    return GITHUB_RELEASE_NEWER;
}

/* ---------------------------------------------------------------- finding and choosing */

static bool has_text_ci(const char *text, const char *wanted)
{
    const size_t n = strlen(wanted);
    if (!n)
        return true;
    for (const char *t = text; *t; ++t)
    {
        size_t i = 0;
        while (i < n && t[i] && tolower((unsigned char)t[i]) == tolower((unsigned char)wanted[i]))
            ++i;
        if (i == n)
            return true;
    }
    return false;
}

bool apps_matches(const CatalogApp *app, const char *query)
{
    char tag[CATALOG_TAG];
    size_t n = 0;
    const char *wanted = query;
    while (*wanted == ' ')
        ++wanted;
    if (!*wanted)
        return true;
    if (wanted[0] == '#' || strncmp(wanted, "tag:", 4) == 0)
    {
        wanted += wanted[0] == '#' ? 1 : 4;
        while (*wanted && *wanted != ' ' && n + 1 < sizeof(tag))
        {
            tag[n++] = (char)tolower((unsigned char)*wanted);
            ++wanted;
        }
        tag[n] = '\0';
        return n > 0 && catalog_has_tag(app, tag);
    }
    if (has_text_ci(app->name, wanted) || has_text_ci(app->summary, wanted) || has_text_ci(app->id, wanted) ||
        has_text_ci(app->author, wanted) || has_text_ci(catalog_category_title(app->category), wanted))
        return true;
    for (int i = 0; i < app->tag_count; ++i)
        if (has_text_ci(app->tags[i], wanted))
            return true;
    return false;
}

bool apps_update_wanted(int offer, bool hold, const char *skip, const char *tag)
{
    if (offer != OFFER_UPDATE || hold)
        return false;
    return !(skip && skip[0] && tag && strcmp(skip, tag) == 0);
}

/* ---------------------------------------------------------------- opening it */

bool apps_ask_launch(const self_update_channel *channel, const char *title, const char *asking, char *reason,
                     size_t size)
{
    char request[64];
    char line[SELF_UPDATE_LINE + 1];
    size_t used = 0;
    const int length = snprintf(request, sizeof(request), "PSL1\n%s\n%s\n", title, asking);
    reason[0] = '\0';
    for (int done = 0; done < length;)
    {
        const long count = channel->send(channel->user, request + done, (size_t)(length - done));
        if (count <= 0)
        {
            str_copy(reason, size, "The launch payload stopped");
            return false;
        }
        done += (int)count;
    }
    for (;;)
    {
        char byte = 0;
        if (channel->receive(channel->user, &byte, 1) <= 0 || used >= SELF_UPDATE_LINE)
        {
            str_copy(reason, size, "The launch payload didn't answer");
            return false;
        }
        if (byte == '\n')
            break;
        line[used++] = byte;
    }
    line[used] = '\0';
    if (strcmp(line, "waiting") == 0)
        return true;
    str_copy(reason, size, strncmp(line, "fail ", 5) == 0 ? line + 5 : "The launch payload didn't answer");
    return false;
}

/* ---------------------------------------------------------------- the hidden ones */

static const int KONAMI[KONAMI_LENGTH] = {BTN_UP,    BTN_UP,   BTN_DOWN,  BTN_DOWN,   BTN_LEFT,
                                          BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_CIRCLE, BTN_CROSS};

void konami_reset(Konami *konami)
{
    memset(konami, 0, sizeof(*konami));
}

bool konami_feed(Konami *konami, uint32_t pressed)
{
    int button = -1;
    if (pressed == 0)
        return false;
    /* One button: which one. Two at once: neither. */
    if ((pressed & (pressed - 1)) == 0)
        for (int b = 0; b < 32; ++b)
            if (pressed == BIT(b))
                button = b;
    if (konami->count == KONAMI_LENGTH)
    {
        memmove(konami->seen, konami->seen + 1, sizeof(konami->seen[0]) * (KONAMI_LENGTH - 1));
        --konami->count;
    }
    konami->seen[konami->count++] = button;
    if (konami->count < KONAMI_LENGTH)
        return false;
    for (int i = 0; i < KONAMI_LENGTH; ++i)
        if (konami->seen[i] != KONAMI[i])
            return false;
    konami_reset(konami); /* once per code */
    return true;
}

uint32_t apps_seed(uint64_t entropy)
{
    /* splitmix64's finaliser, so that close start times give unrelated seeds; never 0 (xorshift's fixed point) */
    uint64_t z = entropy + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (uint32_t)z ? (uint32_t)z : 0x2545F491u;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t x = *state ? *state : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int apps_pick(int count, int previous, uint32_t *state)
{
    int pick;
    if (count <= 0)
        return -1;
    if (count == 1)
        return 0;
    if (previous < 0 || previous >= count)
        return (int)(next_random(state) % (uint32_t)count);
    /* one of the others, evenly */
    pick = (int)(next_random(state) % (uint32_t)(count - 1));
    return pick >= previous ? pick + 1 : pick;
}
