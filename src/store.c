/*
 * Solanum - the store (store.h): Nightshade's Settings > Apps (headless/apps.cpp), for every catalog's apps, with a
 * queue of installs, roll backs and uninstalls.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "store.h"

#include "custom.h"
#include "drives.h"
#include "nightshade.h"
#include "prefs.h"
#include "resume.h"
#include "self_update_protocol.h"
#include "update_check.h"

#include <SDL2/SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* libcurl and OpenSSL want more stack than a default thread of the console has (Nightshade's apps.cpp). */
#define STORE_STACK (1024 * 1024)
/* How long the launch payload may take to answer. */
#define LAUNCH_ANSWER_MS 5000
/* GitHub's API is asked at most this often a refresh (it allows 60 an hour from one address). */
#define GITHUB_BUDGET 50
/* Room an install needs on the drive the helper unpacks on, beyond the ZIP: about its unpacked size again, and some. */
#define ROOM_MARGIN_MB 64

static SDL_mutex *lock;
static char data_folder[NP_PATH_MAX], cache[NP_PATH_MAX], registered[NP_PATH_MAX], homebrew[NP_PATH_MAX];
static char own_folder[256];
static char roots[40][256];
static int root_count;

/* What the screens see (under the lock). */
static Catalog catalog;
static StoreApp apps[CATALOG_MAX_APPS];
static CatalogState catalog_state = CATALOG_NONE;
static bool catalog_busy, checking, rate_limited;
static char catalog_error[160];
static int generation;
static int custom_added[CUSTOM_MAX], custom_hidden[CUSTOM_MAX];
static StoreVersions versions;

/* The thread's. */
static bool asking;       /* a thread is at work */
static bool want_catalog; /* ... and is to fetch the catalogs (again) */
static bool want_look;    /* ... look for the apps */
static bool want_github;  /* ... and ask GitHub */
static int want_versions = -1; /* ... list an app's releases */

static void log_line(void *user, const char *line)
{
    (void)user;
    np_log("%s", line);
}

void store_lock(void)
{
    SDL_LockMutex(lock);
}

void store_unlock(void)
{
    SDL_UnlockMutex(lock);
}

const Catalog *store_catalog(void)
{
    return &catalog;
}

CatalogState store_catalog_state(void)
{
    return catalog_state;
}

bool store_catalog_busy(void)
{
    return catalog_busy;
}

const char *store_catalog_error(void)
{
    return catalog_error;
}

bool store_checking(void)
{
    return checking;
}

int store_generation(void)
{
    return generation;
}

bool store_rate_limited(void)
{
    return rate_limited;
}

int store_custom_added(int index)
{
    return index >= 0 && index < CUSTOM_MAX ? custom_added[index] : 0;
}

int store_custom_hidden(int index)
{
    return index >= 0 && index < CUSTOM_MAX ? custom_hidden[index] : 0;
}

const StoreApp *store_app(int index)
{
    static const StoreApp none;
    return index >= 0 && index < catalog.app_count ? &apps[index] : &none;
}

bool store_update_wanted(int index)
{
    const StoreApp *s = store_app(index);
    /* A custom catalog's app is updated only when Solanum installed it from there, with the player's yes: never one
     * found installed some other way (a third-party catalog doesn't get to replace it). */
    if (index >= 0 && index < catalog.app_count && catalog.apps[index].source > 0 && s->by != BY_SOLANUM)
        return false;
    return s->checked && s->looked && apps_update_wanted(s->offer, s->hold, s->skip, s->release.tag);
}

int store_update_count(void)
{
    int count = 0;
    for (int i = 0; i < catalog.app_count; ++i)
        count += store_update_wanted(i);
    return count;
}

const StoreVersions *store_versions(void)
{
    return &versions;
}

const char *store_problem(int reason)
{
    switch (reason)
    {
    case GITHUB_RELEASE_NEWER: return "";
    case GITHUB_RELEASE_NONE: return "This app has no release yet.";
    case GITHUB_RELEASE_NETWORK:
    case GITHUB_RELEASE_HTTP_STATUS:
    case GITHUB_RELEASE_SUMS_UNREADABLE: return "GitHub couldn't be reached. Check the network and refresh.";
    case GITHUB_RELEASE_RATE_LIMITED:
        return "GitHub's limit for this hour was reached (60 requests an address). Try again later.";
    case GITHUB_RELEASE_NO_ZIP: return "Its newest release doesn't have the file the catalog names.";
    case GITHUB_RELEASE_NO_HASH:
    case GITHUB_RELEASE_SUMS_NO_LINE:
        return "Its newest release has no checksum for the file, and the catalog hasn't pinned it yet, so Solanum "
               "won't install it.";
    case GITHUB_RELEASE_HASHES_DIFFER:
    case GITHUB_RELEASE_SUMS_MALFORMED:
        return "Its newest release's checksums don't agree (or its file changed after the catalog pinned it), so "
               "Solanum won't install it.";
    case GITHUB_RELEASE_OWNER_CHANGED: return "Its repository moved to another account, so Solanum won't install it.";
    default: return "Its newest release couldn't be read.";
    }
}

/* ---------------------------------------------------------------- the player's decisions */

/* An app's StoreApp mirror of its prefs (under the lock). */
static void mirror_prefs(int index)
{
    const AppPrefs *prefs = prefs_get(catalog.apps[index].title_id, false);
    apps[index].hold = prefs && prefs->hold;
    str_copy(apps[index].skip, sizeof(apps[index].skip), prefs ? prefs->skip : "");
    apps[index].by = prefs ? prefs->by : BY_UNKNOWN;
}

void store_set_hold(int index, bool hold)
{
    store_lock();
    if (index >= 0 && index < catalog.app_count)
    {
        AppPrefs *prefs = prefs_get(catalog.apps[index].title_id, true);
        if (prefs)
        {
            prefs->hold = hold;
            prefs_save();
        }
        mirror_prefs(index);
        np_log("prefs: %s updates %s", catalog.apps[index].title_id, hold ? "held" : "allowed");
    }
    store_unlock();
}

void store_set_skip(int index, const char *tag)
{
    store_lock();
    if (index >= 0 && index < catalog.app_count)
    {
        AppPrefs *prefs = prefs_get(catalog.apps[index].title_id, true);
        if (prefs)
        {
            str_copy(prefs->skip, sizeof(prefs->skip), tag ? tag : "");
            prefs_save();
        }
        mirror_prefs(index);
        np_log("prefs: %s skips %s", catalog.apps[index].title_id, tag && tag[0] ? tag : "nothing");
    }
    store_unlock();
}

/* ---------------------------------------------------------------- the thread: catalogs */

static void copy_path(char *out, size_t size)
{
    path_join(out, size, cache, "catalog.json");
}

static bool read_file(const char *path, char *body, size_t capacity, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    *length = fread(body, 1, capacity, file);
    const bool whole = feof(file) && !ferror(file);
    fclose(file);
    return whole;
}

static void save_copy(const char *path, const char *body, size_t length)
{
    char temp[NP_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.new", path);
    FILE *file = fopen(temp, "wb");
    if (!file)
        return;
    const bool written = fwrite(body, 1, length, file) == length;
    if (fclose(file) == 0 && written)
    {
        remove(path);
        rename(temp, path);
    }
    else
        remove(temp);
}

/* The newly read catalog in place of the one shown; what was known of the apps goes with it. */
static void publish(const Catalog *read, CatalogState state, const int added[CUSTOM_MAX], const int hidden[CUSTOM_MAX])
{
    store_lock();
    memcpy(&catalog, read, sizeof(catalog));
    memset(apps, 0, sizeof(apps));
    for (int i = 0; i < catalog.app_count; ++i)
        mirror_prefs(i);
    memcpy(custom_added, added, sizeof(custom_added));
    memcpy(custom_hidden, hidden, sizeof(custom_hidden));
    catalog_state = state;
    checking = true; /* until ask_github has its answers: nothing is final before (automatic updates wait for it) */
    memset(&versions, 0, sizeof(versions));
    ++generation;
    store_unlock();
}

/* A catalog's text from `url` into `body`, or else its copy from the last time it was read. 1 fetched, 2 the copy,
 * 0 neither (*why says why the fetch failed). */
static int fetch_text(const char *url, const char *copy, char *body, size_t *length, char *why, size_t why_size)
{
    int status = 0;
    const self_update_platform *platform = store_kit_platform();
    const int code = platform->fetch(platform->user, url, body, CATALOG_MAX_JSON, length, &status);
    np_log("catalog: %s: result %d, http %d, %zu bytes", url, code, status, *length);
    if (code == 0 && status == 200)
        return 1;
    if (code == SELF_UPDATE_FETCH_TOO_LARGE)
        snprintf(why, why_size, "The catalog is larger than Solanum reads.");
    else if (code != 0)
        snprintf(why, why_size, "The catalog couldn't be downloaded. Check the network.");
    else
        snprintf(why, why_size, "%s answered %d for the catalog.",
                 strncmp(url, "https://raw.githubusercontent.com/", 34) == 0 ? "GitHub" : "Its host", status);
    *length = 0;
    return read_file(copy, body, CATALOG_MAX_JSON, length) ? 2 : 0;
}

static void fetch_catalog(void)
{
    char *body = malloc(CATALOG_MAX_JSON);
    Catalog *read = malloc(sizeof(Catalog));
    Catalog *other = malloc(sizeof(Catalog));
    size_t length = 0;
    char why[160] = "", path[NP_PATH_MAX];
    int added[CUSTOM_MAX], hidden[CUSTOM_MAX];
    char urls[CUSTOM_MAX][CUSTOM_TEXT + 64];
    int custom = 0;
    memset(added, 0, sizeof(added));
    memset(hidden, 0, sizeof(hidden));
    if (!body || !read || !other)
    {
        free(body);
        free(read);
        free(other);
        return;
    }
    store_lock();
    custom = custom_count();
    for (int i = 0; i < custom; ++i)
        custom_url(i, urls[i], sizeof(urls[i]));
    store_unlock();

    /* Solanum's own */
    copy_path(path, sizeof(path));
    int got = fetch_text(CATALOG_URL, path, body, &length, why, sizeof(why));
    bool parsed = got && catalog_parse(body, length, read, got == 1 ? log_line : NULL, NULL) >= 0;
    if (got == 1 && !parsed)
        snprintf(why, sizeof(why), "The catalog couldn't be read.");
    if (got == 1 && parsed)
        save_copy(path, body, length);
    else if (got == 1)
    {
        /* fetched but unreadable: the copy, if there is one */
        parsed = read_file(path, body, CATALOG_MAX_JSON, &length) && catalog_parse(body, length, read, NULL, NULL) >= 0;
        got = parsed ? 2 : 0;
    }
    store_lock();
    const bool shown = catalog_state == CATALOG_READY || catalog_state == CATALOG_COPY;
    str_copy(catalog_error, sizeof(catalog_error), got == 1 && parsed ? "" : why);
    store_unlock();
    if (!parsed)
    {
        np_log("catalog: %s; no copy to show", why);
        if (!shown)
        {
            store_lock();
            catalog_state = CATALOG_FAILED;
            store_unlock();
        }
        free(body);
        free(read);
        free(other);
        return;
    }
    if (got == 2)
    {
        np_log("catalog: %s; showing the copy from the last start", why);
        if (shown)
        {
            /* a catalog is shown already: better than an older copy */
            free(body);
            free(read);
            free(other);
            return;
        }
    }
    np_log("catalog: %d apps, %d tips, %d hidden", read->app_count, read->tip_count, read->egg_count);

    /* The custom ones, third-party, after it */
    for (int i = 0; i < custom; ++i)
    {
        char copy[NP_PATH_MAX], reason[160];
        custom_copy_path(cache, urls[i], copy, sizeof(copy));
        const int custom_got = fetch_text(urls[i], copy, body, &length, reason, sizeof(reason));
        if (custom_got && catalog_parse(body, length, other, log_line, NULL) >= 0)
        {
            if (custom_got == 1)
                save_copy(copy, body, length);
            added[i] = catalog_merge(read, other, i + 1, &hidden[i]);
            np_log("catalog: custom %s: %d apps added, %d already listed%s", urls[i], added[i], hidden[i],
                   custom_got == 2 ? " (its copy)" : "");
        }
        else
        {
            added[i] = -1;
            np_log("catalog: custom %s: %s", urls[i], custom_got ? "not a catalog Solanum reads" : reason);
        }
    }
    publish(read, got == 1 ? CATALOG_READY : CATALOG_COPY, added, hidden);
    free(body);
    free(read);
    free(other);
}

/* ---------------------------------------------------------------- the thread: where the apps are */

/* The catalog's apps as they are now (titles may change under a new catalog: a copy, and its generation). */
static int snapshot(char titles[][10], int *gen)
{
    store_lock();
    const int count = catalog.app_count;
    for (int i = 0; i < count; ++i)
        memcpy(titles[i], catalog.apps[i].title_id, 10);
    *gen = generation;
    store_unlock();
    return count;
}

static void look(void)
{
    static char titles[CATALOG_MAX_APPS][10];
    const char *where[42];
    int gen, adopted = 0;
    for (int r = 0; r < root_count; ++r)
        where[r] = roots[r];
    const int count = snapshot(titles, &gen);
    for (int i = 0; i < count; ++i)
    {
        const Installed found = apps_find_in(titles[i], registered, where, root_count);
        np_log("apps: %s: %s%s%s%s", titles[i], found.folder[0] ? found.folder : "not installed",
               found.folder[0] ? " " : "", found.version, found.image ? " (image)" : "");
        uint64_t size = 0;
        const bool sized = found.folder[0] && !found.image && apps_folder_size(found.folder, &size);
        KeptZip previous;
        const bool kept = found.folder[0] && !found.image && keep_read(cache, titles[i], KEEP_PREVIOUS, &previous);
        store_lock();
        if (gen == generation)
        {
            apps[i].installed = found;
            apps[i].looked = true;
            apps[i].sized = sized;
            apps[i].size = size;
            apps[i].kept = kept;
            if (kept)
                apps[i].previous = previous;
            apps[i].offer = apps[i].checked ? (int)apps_decide_release(&found, apps[i].reason == GITHUB_RELEASE_NEWER,
                                                                          &apps[i].release)
                                            : OFFER_NONE;
            /* An app the catalog lists, installed some other way: Solanum looks after it from now on (its updates,
             * its roll back once it has one, its uninstall). */
            if (found.folder[0] && !found.image && strcmp(titles[i], NP_TITLE_ID) != 0 && catalog.apps[i].source == 0)
            {
                AppPrefs *prefs = prefs_get(titles[i], true);
                if (prefs && prefs->by == BY_UNKNOWN)
                {
                    prefs->by = BY_HAND;
                    ++adopted;
                    np_log("apps: %s was installed by hand; Solanum looks after it now", titles[i]);
                }
            }
            mirror_prefs(i);
        }
        store_unlock();
    }
    if (adopted)
    {
        store_lock();
        prefs_save();
        store_unlock();
    }
}

/* ---------------------------------------------------------------- the thread: releases */

/* Every app's release as the catalog pinned it: shown at once, and all a catalog app needs when GitHub isn't asked. */
static void from_pins(void)
{
    store_lock();
    for (int i = 0; i < catalog.app_count; ++i)
    {
        github_release_asset release;
        if (!apps_release_from_pin(&catalog.apps[i], 0, &release))
            continue;
        apps[i].release = release;
        apps[i].reason = GITHUB_RELEASE_NEWER;
        apps[i].source = FROM_CATALOG;
        apps[i].checked = true;
        apps[i].offer = apps[i].looked ? (int)apps_decide_release(&apps[i].installed, true, &release) : OFFER_NONE;
    }
    store_unlock();
}

static void ask_github(void)
{
    static char titles[CATALOG_MAX_APPS][10];
    static github_release_asset release;
    int gen, asked = 0;
    const int count = snapshot(titles, &gen);
    store_lock();
    checking = true;
    rate_limited = false;
    store_unlock();
    /* Installed apps first: theirs are the answers that matter most. */
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < count; ++i)
        {
            char repo[144], pattern[128], sums[64];
            CatalogApp app;
            github_release_report report;
            store_lock();
            bool same = gen == generation;
            bool wanted = false;
            if (same)
            {
                const bool installed = apps[i].looked && apps[i].installed.folder[0] && !apps[i].installed.image;
                const bool pinned = catalog.apps[i].pin_count > 0 && catalog.apps[i].source == 0;
                /* pass 0: installed ones (a release newer than the pin); pass 1: the ones with no pin */
                wanted = pass == 0 ? installed : !installed && !pinned;
                if (wanted)
                {
                    app = catalog.apps[i];
                    str_copy(repo, sizeof(repo), app.repo);
                    str_copy(pattern, sizeof(pattern), app.asset);
                    str_copy(sums, sizeof(sums), app.checksums);
                }
            }
            const bool limited = rate_limited;
            store_unlock();
            if (!same)
                break;
            if (!wanted)
                continue;
            if (limited || asked >= GITHUB_BUDGET)
            {
                /* the rest keep the catalog's pins, or say why there's nothing */
                store_lock();
                if (gen == generation && !apps[i].checked)
                {
                    apps[i].reason = limited ? GITHUB_RELEASE_RATE_LIMITED : GITHUB_RELEASE_NETWORK;
                    apps[i].checked = true;
                    apps[i].offer = OFFER_NONE;
                }
                store_unlock();
                continue;
            }
            ++asked;
            github_release_reason reason =
                github_release_asset_check(store_kit_platform(), repo, pattern, sums, &release, &report);
            /* A custom catalog's app is checked against GitHub alone; Solanum's against its pins too. */
            if (app.source == 0)
                reason = (github_release_reason)apps_check_pin(&app, reason, &release);
            np_log("apps: %s releases/latest of %s: %s, http %d, redirects %d, error %d, tag %s, %s %s", titles[i], repo,
                   github_release_reason_text(reason), report.http_status, report.redirects, report.platform_error,
                   report.tag[0] ? report.tag : "-", report.zip[0] ? report.zip : "-",
                   release.sha256[0] ? (release.pinned ? "pinned" : release.from_sums ? sums : "digest") : "-");
            store_lock();
            if (gen == generation)
            {
                if (reason == GITHUB_RELEASE_RATE_LIMITED)
                    rate_limited = true;
                if (reason == GITHUB_RELEASE_NEWER)
                {
                    apps[i].release = release;
                    apps[i].reason = reason;
                    apps[i].source = FROM_GITHUB;
                    apps[i].unpinned[0] = '\0';
                    apps[i].checked = true;
                }
                else if (apps[i].source == FROM_CATALOG && apps[i].checked)
                {
                    /* GitHub's answer can't be installed (or didn't come): the catalog's pin stays, with a word on a
                     * newer release that isn't pinned yet. */
                    if (release.tag[0] && strcmp(release.tag, apps[i].release.tag) != 0 &&
                        (reason == GITHUB_RELEASE_NO_HASH || reason == GITHUB_RELEASE_SUMS_NO_LINE))
                        str_copy(apps[i].unpinned, sizeof(apps[i].unpinned), release.tag);
                }
                else
                {
                    apps[i].release = release;
                    apps[i].reason = reason;
                    apps[i].source = FROM_GITHUB;
                    apps[i].checked = true;
                }
                apps[i].offer = apps[i].looked ? (int)apps_decide_release(&apps[i].installed,
                                                                          apps[i].reason == GITHUB_RELEASE_NEWER,
                                                                          &apps[i].release)
                                               : OFFER_NONE;
            }
            store_unlock();
        }
    np_log("apps: GitHub asked %d times this refresh%s", asked, rate_limited ? "; its hourly limit was reached" : "");
    store_lock();
    /* the apps nobody asked for: those the catalog didn't pin, past the budget */
    for (int i = 0; i < catalog.app_count && gen == generation; ++i)
        if (!apps[i].checked)
        {
            apps[i].checked = true;
            apps[i].reason = rate_limited ? GITHUB_RELEASE_RATE_LIMITED : GITHUB_RELEASE_NETWORK;
            apps[i].offer = OFFER_NONE;
        }
    checking = false;
    store_unlock();
}

typedef struct
{
    char *body;
    size_t capacity, used;
} TextSink;

static int on_text(void *user, const void *data, size_t size)
{
    TextSink *sink = (TextSink *)user;
    if (size > sink->capacity - sink->used)
        return 0;
    memcpy(sink->body + sink->used, data, size);
    sink->used += size;
    return 1;
}

/* An app's releases, for Versions: one request, then each file's SHA-256 from the catalog's pin, GitHub's digest or
 * the release's checksum file. */
static void list_versions(int index)
{
    char url[256], repo[144], pattern[128], sums_name[64];
    CatalogApp app;
    int gen, status = 0;
    size_t length = 0;
    store_lock();
    const bool known = index >= 0 && index < catalog.app_count;
    if (known)
    {
        app = catalog.apps[index];
        str_copy(repo, sizeof(repo), app.repo);
        str_copy(pattern, sizeof(pattern), app.asset);
        str_copy(sums_name, sizeof(sums_name), app.checksums);
    }
    gen = generation;
    store_unlock();
    if (!known)
        return;
    char *answer = malloc(GITHUB_RELEASE_MAX_ANSWER);
    github_release_asset *list = calloc(GITHUB_RELEASE_LIST_MAX, sizeof(github_release_asset));
    github_release_files *files = calloc(GITHUB_RELEASE_LIST_MAX, sizeof(github_release_files));
    const char *error = "";
    int count = -1;
    if (!answer || !list || !files)
        error = "Solanum ran out of memory.";
    else if (github_release_list_url(url, sizeof(url), repo, GITHUB_RELEASE_LIST_MAX) == 0)
        error = "Its repository can't be asked.";
    else
    {
        const self_update_platform *platform = store_kit_platform();
        const int code = platform->fetch(platform->user, url, answer, GITHUB_RELEASE_MAX_ANSWER, &length, &status);
        np_log("versions: %s: result %d, http %d, %zu bytes", url, code, status, length);
        if (code != 0)
            error = "GitHub couldn't be reached. Check the network.";
        else if (status == 403 || status == 429)
            error = "GitHub's limit for this hour was reached (60 requests an address). Try again later.";
        else if (status != 200)
            error = "GitHub didn't list its releases.";
        else if ((count = github_release_list_read(answer, length, repo, pattern, sums_name, list, files,
                                                   GITHUB_RELEASE_LIST_MAX)) < 0)
            error = "GitHub's list of its releases couldn't be read.";
    }
    for (int i = 0; i < count; ++i)
    {
        /* the catalog's pin first: it must agree with GitHub's digest when there is one */
        if (app.source == 0)
        {
            const int pin = apps_pin_for(&app, list[i].tag, list[i].name);
            if (pin >= 0)
            {
                if ((list[i].sha256[0] && strcmp(list[i].sha256, app.pins[pin].sha256) != 0) ||
                    list[i].size != app.pins[pin].size)
                    list[i].sha256[0] = '\0'; /* changed after it was pinned: not offered */
                else
                {
                    str_copy(list[i].sha256, sizeof(list[i].sha256), app.pins[pin].sha256);
                    list[i].pinned = 1;
                }
                continue;
            }
        }
        if (list[i].sha256[0] || files[i].sums_files != 1 || files[i].sums_size == 0 ||
            files[i].sums_size > GITHUB_RELEASE_MAX_SUMS)
            continue;
        /* the release's checksum file, from github.com as the file will be */
        char sums_url[512], text[GITHUB_RELEASE_MAX_SUMS + 1], sha[65];
        const int written = snprintf(sums_url, sizeof(sums_url), "https://github.com/%s/releases/download/%s/%s",
                                     repo, list[i].tag, sums_name);
        if (written <= 0 || (size_t)written >= sizeof(sums_url) || !self_update_url_allowed(sums_url, 0))
            continue;
        TextSink sink = {text, sizeof(text) - 1, 0};
        int got_status = 0;
        if (store_kit_range(NULL, sums_url, 0, sizeof(text) - 1, on_text, &sink, &got_status) != 0)
            continue;
        text[sink.used] = '\0';
        if (github_release_sums(text, sink.used, list[i].name, sha) == 1)
        {
            str_copy(list[i].sha256, sizeof(list[i].sha256), sha);
            list[i].from_sums = 1;
        }
    }
    store_lock();
    if (gen == generation)
    {
        memset(&versions, 0, sizeof(versions));
        versions.app = index;
        versions.generation = gen;
        versions.state = count >= 0 ? VERSIONS_READY : VERSIONS_FAILED;
        versions.count = count > 0 ? count : 0;
        for (int i = 0; i < versions.count; ++i)
            versions.list[i] = list[i];
        str_copy(versions.error, sizeof(versions.error), error);
    }
    store_unlock();
    free(answer);
    free(list);
    free(files);
}

/* The thread of store_refresh: works for as long as anything is wanted, so a refresh while it runs is not
 * lost. */
static int work(void *unused)
{
    (void)unused;
    for (;;)
    {
        store_lock();
        const bool fetch = want_catalog, find = want_look, github = want_github;
        const int list = want_versions;
        want_catalog = want_look = want_github = false;
        want_versions = -1;
        if (!fetch && !find && !github && list < 0)
        {
            asking = false;
            catalog_busy = false;
            store_unlock();
            return 0;
        }
        catalog_busy = fetch;
        if (fetch && catalog_state == CATALOG_NONE)
            catalog_state = CATALOG_LOADING;
        store_unlock();
        if (fetch)
        {
            fetch_catalog();
            store_lock();
            catalog_busy = false;
            store_unlock();
        }
        if (find || fetch)
            look();
        if (fetch)
            from_pins();
        if (github || fetch)
            ask_github();
        if (list >= 0)
            list_versions(list);
    }
}

static void start_thread(void)
{
    /* under the lock */
    if (asking)
        return;
    asking = true;
    SDL_Thread *thread = SDL_CreateThreadWithStackSize(work, "store", STORE_STACK, NULL);
    if (thread)
    {
        SDL_DetachThread(thread);
        return;
    }
    np_log("apps: the thread could not start");
    asking = want_catalog = want_look = want_github = catalog_busy = false;
    want_versions = -1;
}

void store_init(const char *data, const char *registered_folder, const char *homebrew_folder)
{
    char path[NP_PATH_MAX];
    lock = SDL_CreateMutex();
    (void)store_app_file("sce_sys/param.json"); /* where Solanum's own files are, found once, before any thread */
    str_copy(data_folder, sizeof(data_folder), data);
    path_join(cache, sizeof(cache), data, "cache");
    str_copy(registered, sizeof(registered), registered_folder);
    str_copy(homebrew, sizeof(homebrew), homebrew_folder);
    char install_dir[NP_PATH_MAX];
    path_join(install_dir, sizeof(install_dir), homebrew, NP_TITLE_ID);
    apps_own_folder(NP_TITLE_ID, registered, install_dir, own_folder, sizeof(own_folder));
    np_log("apps: Solanum's folder %s", own_folder[0] ? own_folder : "(not found)");
    /* Where apps are looked for: beside Solanum, then every folder ShadowMountPlus scans. */
    root_count = 0;
    if (own_folder[0])
    {
        const char *slash = strrchr(own_folder, '/');
        if (slash && slash != own_folder && (size_t)(slash - own_folder) < sizeof(roots[0]))
        {
            snprintf(roots[0], sizeof(roots[0]), "%.*s", (int)(slash - own_folder), own_folder);
            root_count = 1;
        }
    }
#if defined(__PROSPERO__)
    char all[36][48];
    const int count = apps_console_roots(all, 36);
    for (int i = 0; i < count && root_count < 40; ++i)
        if (root_count == 0 || strcmp(all[i], roots[0]) != 0)
            str_copy(roots[root_count++], sizeof(roots[0]), all[i]);
#else
    if ((root_count == 0 || strcmp(homebrew, roots[0]) != 0) && strlen(homebrew) < sizeof(roots[0]))
        str_copy(roots[root_count++], sizeof(roots[0]), homebrew);
#endif
    path_join(path, sizeof(path), data, "apps.ini");
    prefs_load(path);
    path_join(path, sizeof(path), data, "catalogs.ini");
    custom_load(path);
    np_log("apps: %d folders looked through, %d decisions, %d custom catalogs", root_count, prefs_count(),
           custom_count());
}

void store_refresh(bool fetch_catalog_too)
{
    store_lock();
    /* After an install only where the apps are is looked at again: GitHub is asked once a refresh, an app at a
     * time, of the 60 requests an hour it allows an address. */
    want_look = true;
    want_github = want_github || fetch_catalog_too;
    want_catalog = want_catalog || fetch_catalog_too || catalog_state == CATALOG_NONE ||
                   catalog_state == CATALOG_FAILED;
    if (want_catalog)
        catalog_busy = true;
    start_thread();
    store_unlock();
}

void store_versions_ask(int index)
{
    store_lock();
    if (index >= 0 && index < catalog.app_count &&
        !(versions.app == index && versions.generation == generation &&
          (versions.state == VERSIONS_ASKING || versions.state == VERSIONS_READY)))
    {
        memset(&versions, 0, sizeof(versions));
        versions.state = VERSIONS_ASKING;
        versions.app = index;
        versions.generation = generation;
        want_versions = index;
        start_thread();
    }
    store_unlock();
}

/* ---------------------------------------------------------------- jobs */

typedef enum
{
    SLOT_FREE,
    SLOT_QUEUED,
    SLOT_RUNNING,
    SLOT_ENDED
} SlotState;

typedef struct
{
    int state; /* SlotState */
    int id, order;
    int kind;  /* JobKind */
    char title[10];
    char name[64];
    char tag[40];
    bool fresh;
    /* installs */
    self_update_offer offer;
    char asset[128];
    bool from_kept;           /* a roll back: the download is the kept ZIP */
    self_update_job job;      /* zero until its first start, as the kit asks */
    bool begun;
    ResumeDownload resume;
    self_update_platform platform;
    /* uninstalls */
    char installed[12];
    char data[72];
    SDL_Thread *thread;
    int remove_phase;         /* JobPhase, written by the thread under jobs_lock */
    char remove_error[160];
    char removed[256];
    bool home_screen;
    bool cancel;
    /* how it ended */
    StoreJob last;
} Slot;

static SDL_mutex *jobs_lock;
static Slot slots[STORE_JOBS];
static int next_id = 1, next_order = 1, parallel = 2;
static bool keep_zips = true;

void store_set_parallel(int count)
{
    parallel = count < 1 ? 1 : count > 3 ? 3 : count;
}

void store_set_keep(bool keep)
{
    keep_zips = keep;
}

static void jobs_lock_init(void)
{
    if (!jobs_lock)
        jobs_lock = SDL_CreateMutex();
}

/* The kit's platform for an install: the console's, with downloads through the slot's resume.h. */
static const self_update_platform *base(void)
{
    return store_kit_platform();
}
static int job_fetch(void *user, const char *url, char *body, size_t capacity, size_t *length, int *status)
{
    (void)user;
    return base()->fetch(base()->user, url, body, capacity, length, status);
}
static int job_download(void *user, const char *url, uint64_t limit, self_update_sink sink, void *sink_user)
{
    Slot *slot = (Slot *)user;
    const int result = resume_download(&slot->resume, url, limit, sink, sink_user);
    np_log("install: %s: %s, %llu bytes from the copy, %llu downloaded, http %d%s", slot->title,
           result == 0 ? "downloaded" : "stopped", (unsigned long long)slot->resume.reused,
           (unsigned long long)slot->resume.fetched, slot->resume.status,
           slot->resume.mismatch ? "; it doesn't match its checksum, the copy was deleted" : "");
    return result;
}
static int job_verify(void *user, const unsigned char key[32], const unsigned char signature[64],
                      const void *message, size_t length)
{
    (void)user;
    return base()->verify ? base()->verify(base()->user, key, signature, message, length) : 0;
}
static int job_open_helper(void *user, self_update_channel *channel)
{
    (void)user;
    return base()->open_helper(base()->user, channel);
}
static int job_load_sequence(void *user, uint64_t *sequence)
{
    (void)user;
    return base()->load_sequence ? base()->load_sequence(base()->user, sequence) : 0;
}
static int job_save_sequence(void *user, uint64_t sequence)
{
    (void)user;
    return base()->save_sequence ? base()->save_sequence(base()->user, sequence) : 0;
}
static uint64_t job_now(void *user)
{
    (void)user;
    return base()->now_ms(base()->user);
}


/* The catalog's index of a title, -1 when the catalog doesn't list it (under the lock). */
static int index_of(const char *title)
{
    for (int i = 0; i < catalog.app_count; ++i)
        if (strcmp(catalog.apps[i].title_id, title) == 0)
            return i;
    return -1;
}

/* A slot's job as the screens see it (under jobs_lock). */
static void describe(Slot *slot, StoreJob *out)
{
    if (slot->state == SLOT_ENDED)
    {
        *out = slot->last;
        return;
    }
    memset(out, 0, sizeof(*out));
    out->id = slot->id;
    out->kind = slot->kind;
    out->app = -1;
    str_copy(out->title, sizeof(out->title), slot->title);
    str_copy(out->name, sizeof(out->name), slot->name);
    str_copy(out->tag, sizeof(out->tag), slot->tag);
    out->fresh = slot->fresh;
    out->data = slot->data[0] != '\0';
    if (slot->state == SLOT_QUEUED)
    {
        out->phase = JOB_QUEUED;
        out->total = slot->offer.size;
        return;
    }
    if (slot->kind == JOB_REMOVE)
    {
        out->phase = (JobPhase)slot->remove_phase;
        str_copy(out->error, sizeof(out->error), slot->remove_error);
        str_copy(out->folder, sizeof(out->folder), slot->removed);
        out->home_screen = slot->home_screen;
        return;
    }
    self_update_status status;
    self_update_poll(&slot->job, &status);
    switch (status.phase)
    {
    case SELF_UPDATE_STARTING: out->phase = JOB_STARTING; break;
    case SELF_UPDATE_DOWNLOADING: out->phase = JOB_DOWNLOADING; break;
    case SELF_UPDATE_UNPACKING: out->phase = JOB_UNPACKING; break;
    case SELF_UPDATE_READY:
    case SELF_UPDATE_APPLYING: out->phase = JOB_INSTALLING; break;
    case SELF_UPDATE_INSTALLED: out->phase = JOB_INSTALLED; break;
    case SELF_UPDATE_CANCELLED: out->phase = JOB_CANCELLED; break;
    case SELF_UPDATE_FAILED: out->phase = JOB_FAILED; break;
    default: out->phase = JOB_STARTING; break;
    }
    out->done = status.done;
    out->total = status.total;
    out->reused = slot->resume.reused;
    str_copy(out->time_left, sizeof(out->time_left), status.time_left);
    str_copy(out->error, sizeof(out->error), status.error);
    str_copy(out->version, sizeof(out->version), status.version);
    str_copy(out->folder, sizeof(out->folder), status.folder);
    /* self_update.c's and the helper's words for a download that isn't the release's */
    out->mismatch = out->phase == JOB_FAILED &&
                    (slot->resume.mismatch || strstr(status.error, "doesn't match") != NULL);
    /* self_update.c's words when the payload loader did not take the helper */
    static const char no_loader[] = "The update helper couldn't be started";
    out->no_loader = out->phase == JOB_FAILED && strncmp(status.error, no_loader, sizeof(no_loader) - 1) == 0;
}

/* ---- uninstalls: the helper's "remove" ---- */

static bool send_text(self_update_channel *channel, const char *text)
{
    for (size_t done = 0, length = strlen(text); done < length;)
    {
        const long count = channel->send(channel->user, text + done, length - done);
        if (count <= 0)
            return false;
        done += (size_t)count;
    }
    return true;
}

static bool receive_line(self_update_channel *channel, char *line, size_t size)
{
    size_t used = 0;
    for (;;)
    {
        char byte = 0;
        if (channel->receive(channel->user, &byte, 1) <= 0 || used + 1 >= size)
            return false;
        if (byte == '\n')
            break;
        line[used++] = byte;
    }
    line[used] = '\0';
    return true;
}

static void remove_status(Slot *slot, int phase, const char *error)
{
    SDL_LockMutex(jobs_lock);
    slot->remove_phase = phase;
    if (error)
        str_copy(slot->remove_error, sizeof(slot->remove_error), error);
    SDL_UnlockMutex(jobs_lock);
}

static int remove_thread(void *argument)
{
    Slot *slot = (Slot *)argument;
    self_update_channel channel;
    char request[512], line[SELF_UPDATE_LINE + 1];
    memset(&channel, 0, sizeof(channel));
    if (!path_exists(store_app_file("self-updater.elf")) || base()->open_helper(base()->user, &channel) != 1)
    {
        remove_status(slot, JOB_FAILED, "The update helper couldn't be started. Is the payload loader running?");
        return 0;
    }
    snprintf(request, sizeof(request), "%s\nremove\n%s\n%s\n%s\n%s\n%s\n", SELF_UPDATE_MAGIC, slot->title,
             slot->installed[0] ? slot->installed : "any", slot->data[0] ? slot->data : "-", slot->name, NP_TITLE_ID);
    if (!send_text(&channel, request))
    {
        channel.close(channel.user);
        remove_status(slot, JOB_FAILED, "The update helper stopped");
        return 0;
    }
    remove_status(slot, JOB_REMOVING, NULL);
    for (;;)
    {
        if (!receive_line(&channel, line, sizeof(line)))
        {
            remove_status(slot, JOB_FAILED, "The update helper stopped before it said how the uninstall went");
            break;
        }
        if (strcmp(line, "removing") == 0)
            continue;
        if (strncmp(line, "removed ", 8) == 0)
        {
            char folder[256] = "-";
            unsigned code = 0xffffffffu;
            const char *space = strrchr(line + 8, ' ');
            if (space)
            {
                snprintf(folder, sizeof(folder), "%.*s", (int)(space - (line + 8)), line + 8);
                code = (unsigned)strtoul(space + 1, NULL, 16);
            }
            SDL_LockMutex(jobs_lock);
            str_copy(slot->removed, sizeof(slot->removed), folder);
            slot->home_screen = code == 0;
            slot->remove_phase = JOB_INSTALLED;
            SDL_UnlockMutex(jobs_lock);
            break;
        }
        remove_status(slot, JOB_FAILED, strncmp(line, "fail ", 5) == 0 ? line + 5 : "The app couldn't be uninstalled");
        break;
    }
    channel.close(channel.user);
    return 0;
}

/* ---- the queue ---- */

/* A free slot, under jobs_lock; NULL when the queue is full. */
static Slot *free_slot(void)
{
    for (int i = 0; i < STORE_JOBS; ++i)
        if (slots[i].state == SLOT_FREE)
            return &slots[i];
    return NULL;
}

/* Whether an app already has a job waiting or running (under jobs_lock). */
static bool has_job(const char *title)
{
    for (int i = 0; i < STORE_JOBS; ++i)
        if ((slots[i].state == SLOT_QUEUED || slots[i].state == SLOT_RUNNING) && strcmp(slots[i].title, title) == 0)
            return true;
    return false;
}

/* The drive a path is on ("/data", "/mnt/usb0"...), "" when none. */
static void drive_of(const char *path, char *out, size_t size)
{
    out[0] = '\0';
    for (int i = 0; i < APPS_PLACES; ++i)
    {
        const char *drive = apps_place_drive(i);
        const size_t n = strlen(drive);
        if (strncmp(path, drive, n) == 0 && path[n] == '/')
        {
            str_copy(out, size, drive);
            return;
        }
    }
}

long long store_free_mb(const char *path)
{
    return drive_free_mb(path);
}

/* Whether there's room for an install of `bytes`: on the drive it is unpacked on, and in the cache for its copy.
 * *why when there isn't. Unknown room is no reason to refuse; the helper looks again. */
static bool room_for(uint64_t bytes, const char *target, char *why, size_t size)
{
    char drive[24];
    const long long need = (long long)(bytes * 5 / 2 >> 20) + ROOM_MARGIN_MB;
    drive_of(target, drive, sizeof(drive));
#if defined(__PROSPERO__)
    if (drive[0])
    {
        const long long free_mb = drive_free_mb(drive);
        if (free_mb >= 0 && free_mb < need)
        {
            snprintf(why, size, "There isn't room on %s: it needs about %lld MB, %lld MB are free.", drive, need,
                     free_mb);
            return false;
        }
    }
    const long long cache_free = drive_free_mb(data_folder);
    if (cache_free >= 0 && cache_free < (long long)(bytes >> 20) + 32 && strcmp(drive, "/data") != 0)
    {
        snprintf(why, size, "There isn't room in %s for the download: it needs about %lld MB, %lld MB are free.",
                 data_folder, (long long)(bytes >> 20) + 32, cache_free);
        return false;
    }
#else
    (void)need;
    (void)why;
    (void)size;
#endif
    return true;
}

static Slot *queue_slot(const char *title, char *reason, size_t size)
{
    if (has_job(title))
    {
        str_copy(reason, size, "This app is in the queue already.");
        return NULL;
    }
    Slot *slot = free_slot();
    if (!slot)
    {
        str_copy(reason, size, "The queue is full. Let some of it finish first.");
        return NULL;
    }
    memset(slot, 0, sizeof(*slot));
    slot->id = next_id++;
    slot->order = next_order++;
    str_copy(slot->title, sizeof(slot->title), title);
    return slot;
}

/* Queues an offer (under both locks). */
static bool queue_offer(int index, int kind, const github_release_asset *release, const char *where, bool from_kept,
                        char *reason, size_t size)
{
    self_update_offer offer;
    const CatalogApp *a = &catalog.apps[index];
    const StoreApp *s = &apps[index];
    if (!apps_make_offer(a, release, &s->installed, NP_TITLE_ID, &offer))
    {
        str_copy(reason, size, "Its release is not one Solanum can install.");
        return false;
    }
    const bool fresh = strcmp(offer.installed, "none") == 0;
    if (fresh && where && where[0])
    {
        int known = 0;
        for (int i = 0; i < APPS_PLACES; ++i)
            known |= strcmp(where, apps_place(i)) == 0;
        if (!known)
        {
            str_copy(reason, size, "Solanum can't put apps there.");
            return false;
        }
        str_copy(offer.where, sizeof(offer.where), where);
    }
    const char *target = fresh ? (offer.where[0] ? offer.where : own_folder) : s->installed.folder;
    if (!from_kept && !room_for(offer.size, target, reason, size))
        return false;
    Slot *slot = queue_slot(a->title_id, reason, size);
    if (!slot)
        return false;
    slot->state = SLOT_QUEUED;
    slot->kind = kind;
    str_copy(slot->name, sizeof(slot->name), a->name);
    str_copy(slot->tag, sizeof(slot->tag), release->tag);
    str_copy(slot->asset, sizeof(slot->asset), release->name);
    slot->fresh = fresh;
    slot->offer = offer;
    slot->from_kept = from_kept;
    np_log("queue: %s %s %s (%s -> %s)%s%s", kind == JOB_ROLLBACK ? "roll back" : kind == JOB_VERSION ? "version"
                                              : fresh                ? "install"
                                                                     : "update",
           a->title_id, release->tag, offer.installed, offer.available, offer.where[0] ? " in " : "", offer.where);
    return true;
}

bool store_install(int index, const char *where, char *reason, size_t size)
{
    bool queued = false;
    reason[0] = '\0';
    if (!path_exists(store_app_file("self-updater.elf")))
    {
        str_copy(reason, size, "self-updater.elf is not in Solanum's folder. Copy the whole PPSA76526 folder.");
        np_log("install: %s", reason);
        return false;
    }
    jobs_lock_init();
    store_lock();
    SDL_LockMutex(jobs_lock);
    const bool known = index >= 0 && index < catalog.app_count && apps[index].checked && apps[index].looked &&
                       apps[index].reason == GITHUB_RELEASE_NEWER;
    if (!known)
        str_copy(reason, size, "Its release is not one Solanum can install.");
    else
        queued = queue_offer(index, apps[index].installed.folder[0] ? JOB_UPDATE : JOB_INSTALL, &apps[index].release,
                             where, false, reason, size);
    SDL_UnlockMutex(jobs_lock);
    store_unlock();
    return queued;
}

bool store_install_version(int index, int version, char *reason, size_t size)
{
    bool queued = false;
    reason[0] = '\0';
    if (!path_exists(store_app_file("self-updater.elf")))
    {
        str_copy(reason, size, "self-updater.elf is not in Solanum's folder. Copy the whole PPSA76526 folder.");
        return false;
    }
    jobs_lock_init();
    store_lock();
    SDL_LockMutex(jobs_lock);
    if (index < 0 || index >= catalog.app_count || versions.app != index || versions.generation != generation ||
        versions.state != VERSIONS_READY || version < 0 || version >= versions.count || !apps[index].looked)
        str_copy(reason, size, "That release isn't listed any more. Open Versions again.");
    else if (!versions.list[version].sha256[0])
        str_copy(reason, size, "That release has no checksum Solanum can check it against, so it won't install it.");
    else
        queued = queue_offer(index, apps[index].installed.folder[0] ? JOB_VERSION : JOB_INSTALL,
                             &versions.list[version], "", false, reason, size);
    SDL_UnlockMutex(jobs_lock);
    store_unlock();
    return queued;
}

bool store_rollback(int index, char *reason, size_t size)
{
    bool queued = false;
    reason[0] = '\0';
    jobs_lock_init();
    store_lock();
    SDL_LockMutex(jobs_lock);
    KeptZip previous;
    if (index < 0 || index >= catalog.app_count || !apps[index].looked || !apps[index].installed.folder[0])
        str_copy(reason, size, "The app isn't installed.");
    else if (!keep_read(cache, catalog.apps[index].title_id, KEEP_PREVIOUS, &previous))
        str_copy(reason, size, "Solanum didn't keep the version before this one. Versions lists the others.");
    else
    {
        github_release_asset release;
        memset(&release, 0, sizeof(release));
        str_copy(release.tag, sizeof(release.tag), previous.tag);
        str_copy(release.name, sizeof(release.name), previous.name);
        str_copy(release.sha256, sizeof(release.sha256), previous.sha256);
        str_copy(release.url, sizeof(release.url), previous.url);
        release.size = previous.size;
        queued = queue_offer(index, JOB_ROLLBACK, &release, "", true, reason, size);
        if (queued)
        {
            /* the version it held, as the helper reported it: exact, so the helper checks the ZIP is that one */
            for (int i = 0; i < STORE_JOBS; ++i)
                if (slots[i].state == SLOT_QUEUED && strcmp(slots[i].title, catalog.apps[index].title_id) == 0 &&
                    update_check_version_parse(previous.version, NULL))
                    str_copy(slots[i].offer.available, sizeof(slots[i].offer.available), previous.version);
        }
    }
    SDL_UnlockMutex(jobs_lock);
    store_unlock();
    return queued;
}

bool store_uninstall(int index, bool data, char *reason, size_t size)
{
    bool queued = false;
    reason[0] = '\0';
    jobs_lock_init();
    store_lock();
    SDL_LockMutex(jobs_lock);
    if (index < 0 || index >= catalog.app_count || !apps[index].looked)
        str_copy(reason, size, "The app isn't known yet.");
    else if (!apps[index].installed.folder[0])
        str_copy(reason, size, "The app isn't installed.");
    else if (strcmp(catalog.apps[index].title_id, NP_TITLE_ID) == 0)
        str_copy(reason, size, "Solanum doesn't uninstall itself. Delete its folder by FTP.");
    else if (apps[index].installed.image)
        str_copy(reason, size, "It is installed as an image: remove the image itself.");
    else
    {
        Slot *slot = queue_slot(catalog.apps[index].title_id, reason, size);
        if (slot)
        {
            slot->state = SLOT_QUEUED;
            slot->kind = JOB_REMOVE;
            str_copy(slot->name, sizeof(slot->name), catalog.apps[index].name);
            str_copy(slot->installed, sizeof(slot->installed),
                     update_check_version_parse(apps[index].installed.version, NULL) ? apps[index].installed.version
                                                                                     : "any");
            if (data && catalog.apps[index].data[0])
                str_copy(slot->data, sizeof(slot->data), catalog.apps[index].data);
            np_log("queue: uninstall %s %s%s%s", slot->title, slot->installed, slot->data[0] ? " and " : "",
                   slot->data);
            queued = true;
        }
    }
    SDL_UnlockMutex(jobs_lock);
    store_unlock();
    return queued;
}

/* Starts a queued slot (under jobs_lock). */
static void start(Slot *slot)
{
    slot->state = SLOT_RUNNING;
    if (slot->kind == JOB_REMOVE)
    {
        slot->remove_phase = JOB_STARTING;
        slot->thread = SDL_CreateThreadWithStackSize(remove_thread, "remove", STORE_STACK, slot);
        if (!slot->thread)
        {
            slot->remove_phase = JOB_FAILED;
            str_copy(slot->remove_error, sizeof(slot->remove_error), "The uninstall could not begin");
        }
        return;
    }
    /* The copy of this release's download, kept between tries (a roll back's is its kept ZIP); an older release's
     * goes. */
    memset(&slot->resume, 0, sizeof(slot->resume));
    make_dirs(cache);
    if (slot->from_kept)
        keep_paths(cache, slot->title, KEEP_PREVIOUS, slot->resume.part, sizeof(slot->resume.part), NULL, 0);
    else
    {
        resume_part_path(slot->resume.part, sizeof(slot->resume.part), cache, slot->title, slot->offer.sha256);
        resume_forget_others(cache, slot->title, slot->resume.part);
    }
    str_copy(slot->resume.sha256, sizeof(slot->resume.sha256), slot->offer.sha256);
    slot->resume.size = slot->offer.size;
    slot->resume.range = store_kit_range;
    slot->resume.range_user = NULL;
    const self_update_platform platform = {job_fetch,         job_download,      job_verify, job_open_helper,
                                           job_load_sequence, job_save_sequence, job_now,    slot};
    slot->platform = platform;
    slot->begun = self_update_start(&slot->job, &slot->platform, &slot->offer) == 1;
    np_log("install: %s %s %s (%s -> %s) from %s, sha256 %s%s", slot->begun ? "installing" : "could not begin installing",
           slot->offer.title, slot->offer.version, slot->offer.installed, slot->offer.available,
           slot->from_kept ? "its kept ZIP" : slot->offer.artifact, slot->offer.sha256,
           slot->offer.where[0] ? "" : "");
    if (!slot->begun)
    {
        slot->state = SLOT_ENDED;
        describe(slot, &slot->last);
        slot->last.phase = JOB_FAILED;
        str_copy(slot->last.error, sizeof(slot->last.error), "The install could not begin");
    }
}

/* What a job leaves when it ends well: its ZIP kept (or swapped back, for a roll back), how the app came to be here.
 * Under jobs_lock; takes the store's lock for prefs. */
static void finished(Slot *slot, const StoreJob *job)
{
    if (slot->kind == JOB_REMOVE)
    {
        keep_forget(cache, slot->title);
        resume_forget_others(cache, slot->title, NULL);
        store_lock();
        prefs_forget_install(slot->title);
        prefs_save();
        store_unlock();
        return;
    }
    if (slot->from_kept)
    {
        if (!keep_swap(cache, slot->title))
            np_log("install: %s: its kept versions couldn't be swapped", slot->title);
    }
    else if (keep_zips)
    {
        KeptZip what;
        memset(&what, 0, sizeof(what));
        str_copy(what.tag, sizeof(what.tag), slot->tag);
        str_copy(what.version, sizeof(what.version), job->version);
        str_copy(what.sha256, sizeof(what.sha256), slot->offer.sha256);
        what.size = slot->offer.size;
        str_copy(what.url, sizeof(what.url), slot->offer.artifact);
        str_copy(what.name, sizeof(what.name), slot->asset);
        if (!keep_store(cache, slot->title, slot->resume.part, &what))
            np_log("install: %s: its ZIP couldn't be kept for a roll back", slot->title);
    }
    else
    {
        remove(slot->resume.part); /* in place now: the copy has done its job */
        keep_forget(cache, slot->title);
    }
    store_lock();
    AppPrefs *prefs = prefs_get(slot->title, true);
    if (prefs)
    {
        if (slot->fresh)
            prefs->by = BY_SOLANUM;
        else if (prefs->by == BY_UNKNOWN)
            prefs->by = BY_HAND;
        prefs_save();
    }
    store_unlock();
}

void store_poll(void)
{
    bool refresh = false;
    jobs_lock_init();
    SDL_LockMutex(jobs_lock);
    int running = 0;
    for (int i = 0; i < STORE_JOBS; ++i)
    {
        Slot *slot = &slots[i];
        if (slot->state != SLOT_RUNNING)
            continue;
        StoreJob job;
        describe(slot, &job);
        const bool done = job.phase == JOB_INSTALLED || job.phase == JOB_FAILED || job.phase == JOB_CANCELLED;
        if (!done)
        {
            ++running;
            continue;
        }
        if (slot->kind == JOB_REMOVE)
        {
            SDL_Thread *thread = slot->thread;
            slot->thread = NULL;
            SDL_UnlockMutex(jobs_lock);
            if (thread)
                SDL_WaitThread(thread, NULL);
            SDL_LockMutex(jobs_lock);
        }
        else if (slot->begun)
        {
            self_update_finish(&slot->job);
            slot->begun = false;
        }
        np_log("%s: %s %s%s%s%s", slot->kind == JOB_REMOVE ? "uninstall" : "install", slot->title,
               job.phase == JOB_INSTALLED ? (slot->kind == JOB_REMOVE ? "removed " : "installed ")
               : job.phase == JOB_CANCELLED ? "cancelled"
                                            : "failed: ",
               job.phase == JOB_INSTALLED ? (slot->kind == JOB_REMOVE ? job.folder : job.version) : job.error,
               job.folder[0] && slot->kind != JOB_REMOVE ? " in " : "", slot->kind != JOB_REMOVE ? job.folder : "");
        if (job.phase == JOB_INSTALLED)
        {
            finished(slot, &job);
            refresh = true;
        }
        slot->last = job;
        slot->state = SLOT_ENDED;
    }
    /* the next in line, while there's room */
    while (running < parallel)
    {
        Slot *next = NULL;
        for (int i = 0; i < STORE_JOBS; ++i)
            if (slots[i].state == SLOT_QUEUED && (!next || slots[i].order < next->order))
                next = &slots[i];
        if (!next)
            break;
        start(next);
        if (next->state == SLOT_RUNNING)
            ++running;
    }
    SDL_UnlockMutex(jobs_lock);
    if (refresh)
        store_refresh(false); /* the lists say what is there now, once the thread has looked again */
}

int store_jobs(StoreJob *out, int most)
{
    int count = 0;
    jobs_lock_init();
    SDL_LockMutex(jobs_lock);
    for (int pass = 0; pass < 3; ++pass)
    {
        const int wanted = pass == 0 ? SLOT_RUNNING : pass == 1 ? SLOT_QUEUED : SLOT_ENDED;
        Slot *these[STORE_JOBS];
        int n = 0;
        for (int i = 0; i < STORE_JOBS; ++i)
            if (slots[i].state == wanted)
            {
                /* in the order they were queued */
                int at = n++;
                while (at > 0 && these[at - 1]->order > slots[i].order)
                {
                    these[at] = these[at - 1];
                    --at;
                }
                these[at] = &slots[i];
            }
        for (int i = 0; i < n && count < most; ++i)
        {
            describe(these[i], &out[count]);
            out[count].app = index_of(these[i]->title);
            ++count;
        }
    }
    SDL_UnlockMutex(jobs_lock);
    return count;
}

bool store_job_of(int index, StoreJob *out)
{
    bool found = false;
    if (index < 0 || index >= catalog.app_count)
        return false;
    jobs_lock_init();
    SDL_LockMutex(jobs_lock);
    for (int i = 0; i < STORE_JOBS && !found; ++i)
        if ((slots[i].state == SLOT_QUEUED || slots[i].state == SLOT_RUNNING) &&
            strcmp(slots[i].title, catalog.apps[index].title_id) == 0)
        {
            describe(&slots[i], out);
            out->app = index;
            found = true;
        }
    SDL_UnlockMutex(jobs_lock);
    return found;
}

bool store_busy(void)
{
    bool busy = false;
    jobs_lock_init();
    SDL_LockMutex(jobs_lock);
    for (int i = 0; i < STORE_JOBS; ++i)
        busy = busy || slots[i].state == SLOT_QUEUED || slots[i].state == SLOT_RUNNING;
    SDL_UnlockMutex(jobs_lock);
    return busy;
}

bool store_job_ended(StoreJob *out)
{
    bool found = false;
    jobs_lock_init();
    SDL_LockMutex(jobs_lock);
    Slot *oldest = NULL;
    for (int i = 0; i < STORE_JOBS; ++i)
        if (slots[i].state == SLOT_ENDED && (!oldest || slots[i].order < oldest->order))
            oldest = &slots[i];
    if (oldest)
    {
        *out = oldest->last;
        out->app = -1;
        str_copy(out->name, sizeof(out->name), oldest->name);
        oldest->state = SLOT_FREE;
        found = true;
    }
    SDL_UnlockMutex(jobs_lock);
    if (found)
    {
        store_lock();
        out->app = index_of(out->title);
        store_unlock();
    }
    return found;
}

void store_cancel(int id)
{
    jobs_lock_init();
    SDL_LockMutex(jobs_lock);
    for (int i = 0; i < STORE_JOBS; ++i)
    {
        Slot *slot = &slots[i];
        if (slot->id != id)
            continue;
        if (slot->state == SLOT_QUEUED)
        {
            np_log("queue: %s left the queue", slot->title);
            describe(slot, &slot->last);
            slot->last.phase = JOB_CANCELLED;
            slot->state = SLOT_ENDED;
        }
        else if (slot->state == SLOT_RUNNING && slot->kind != JOB_REMOVE && slot->begun)
            self_update_cancel(&slot->job); /* a removal, once asked, goes to its end */
    }
    SDL_UnlockMutex(jobs_lock);
}

void store_cancel_all(void)
{
    jobs_lock_init();
    SDL_LockMutex(jobs_lock);
    int ids[STORE_JOBS], count = 0;
    for (int i = 0; i < STORE_JOBS; ++i)
        if (slots[i].state == SLOT_QUEUED || slots[i].state == SLOT_RUNNING)
            ids[count++] = slots[i].id;
    SDL_UnlockMutex(jobs_lock);
    for (int i = 0; i < count; ++i)
        store_cancel(ids[i]);
}

/* ---------------------------------------------------------------- storage */

static uint64_t parts_size(void)
{
    uint64_t bytes = 0;
    DIR *directory = opendir(cache);
    struct dirent *entry;
    if (!directory)
        return 0;
    while ((entry = readdir(directory)) != NULL)
    {
        const size_t length = strlen(entry->d_name);
        char path[NP_PATH_MAX + 260];
        struct stat info;
        if (length < 9 || strcmp(entry->d_name + length - 9, ".zip.part") != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", cache, entry->d_name);
        if (stat(path, &info) == 0 && S_ISREG(info.st_mode))
            bytes += (uint64_t)info.st_size;
    }
    closedir(directory);
    return bytes;
}

void store_storage(StoreStorage *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < APPS_PLACES; ++i)
    {
        const char *drive = apps_place_drive(i);
#if !defined(__PROSPERO__)
        if (i > 0)
            break; /* the computer build: its data folder's drive stands for /data */
        drive = data_folder;
#endif
        const long long free_mb = drive_free_mb(drive);
        if (free_mb < 0)
            continue;
        str_copy(out->path[out->count], sizeof(out->path[0]), apps_place_drive(i));
        out->free_mb[out->count++] = free_mb;
    }
    out->kept = keep_size(cache);
    out->parts = parts_size();
}

void store_clear_cache(void)
{
    if (store_busy())
        return; /* a running install's copy is in use */
    keep_clear(cache);
    store_lock();
    for (int i = 0; i < catalog.app_count; ++i)
        apps[i].kept = false;
    store_unlock();
    DIR *directory = opendir(cache);
    struct dirent *entry;
    if (!directory)
        return;
    static char names[256][128];
    int count = 0;
    while ((entry = readdir(directory)) != NULL && count < 256)
    {
        const size_t length = strlen(entry->d_name);
        if (length >= 9 && length < 128 && strcmp(entry->d_name + length - 9, ".zip.part") == 0)
            str_copy(names[count++], sizeof(names[0]), entry->d_name);
    }
    closedir(directory);
    for (int i = 0; i < count; ++i)
    {
        char path[NP_PATH_MAX + 130];
        snprintf(path, sizeof(path), "%s/%s", cache, names[i]);
        remove(path);
    }
    np_log("cache: kept ZIPs and unfinished downloads deleted");
}

/* ---------------------------------------------------------------- opening */

OpenResult store_open(int index, char *reason, size_t size)
{
    char title[10], name[64];
    self_update_channel channel;
    reason[0] = '\0';
    store_lock();
    const bool known = index >= 0 && index < catalog.app_count;
    if (known)
    {
        str_copy(title, sizeof(title), catalog.apps[index].title_id);
        str_copy(name, sizeof(name), catalog.apps[index].name);
    }
    store_unlock();
    if (!known)
        return OPEN_REFUSED;
    const char *payload = store_app_file("app-launch.elf");
    if (!path_exists(payload))
    {
        str_copy(reason, size, "app-launch.elf is not in Solanum's folder. Copy the whole PPSA76526 folder.");
        np_log("open %s: %s", title, reason);
        return OPEN_REFUSED;
    }
    memset(&channel, 0, sizeof(channel));
    if (store_kit_open_payload(payload, &channel) != 1)
    {
        np_log("open %s: no payload loader took app-launch.elf", title);
        return OPEN_NO_LOADER;
    }
    /* It answers at once; the helper's minute would hold the screen that long. */
    (void)store_kit_channel_timeout(&channel, LAUNCH_ANSWER_MS);
    const bool waiting = apps_ask_launch(&channel, title, NP_TITLE_ID, reason, size);
    channel.close(channel.user);
    np_log("open %s (%s): %s", title, name, waiting ? "the launch payload waits for Solanum to close" : reason);
    if (waiting)
        return OPEN_CLOSING;
    return strcmp(reason, APPS_NOT_LISTED) == 0 ? OPEN_NOT_LISTED : OPEN_REFUSED;
}
