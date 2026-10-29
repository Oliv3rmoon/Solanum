/*
 * Solanum - the store (store.h): Nightshade's Settings > Apps (headless/apps.cpp), for the catalog's apps.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "store.h"

#include "nightshade.h"
#include "resume.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libcurl and OpenSSL want more stack than a default thread of the console has (Nightshade's apps.cpp). */
#define STORE_STACK (1024 * 1024)
/* How long the launch payload may take to answer. */
#define LAUNCH_ANSWER_MS 5000

static SDL_mutex *lock;
static char data_folder[NP_PATH_MAX], cache[NP_PATH_MAX], registered[NP_PATH_MAX], homebrew[NP_PATH_MAX];
static char own_folder[256];

/* What the screens see (under the lock). */
static Catalog catalog;
static StoreApp apps[CATALOG_MAX_APPS];
static CatalogState catalog_state = CATALOG_NONE;
static bool catalog_busy, checking;
static char catalog_error[160];
static int generation;

/* The thread's. */
static bool asking;       /* a thread is at work */
static bool want_catalog; /* ... and is to fetch the catalog (again) */
static bool want_look;    /* ... look for the apps */
static bool want_github;  /* ... and ask GitHub */

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

const StoreApp *store_app(int index)
{
    static const StoreApp none;
    return index >= 0 && index < catalog.app_count ? &apps[index] : &none;
}

int store_update_count(void)
{
    int count = 0;
    for (int i = 0; i < catalog.app_count; ++i)
        count += apps[i].checked && apps[i].offer == OFFER_UPDATE;
    return count;
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
        return "Its newest release has no checksum for the file, so Solanum won't install it.";
    case GITHUB_RELEASE_HASHES_DIFFER:
    case GITHUB_RELEASE_SUMS_MALFORMED:
        return "Its newest release's checksums don't agree, so Solanum won't install it.";
    case GITHUB_RELEASE_OWNER_CHANGED: return "Its repository moved to another account, so Solanum won't install it.";
    default: return "Its newest release couldn't be read.";
    }
}

/* ---------------------------------------------------------------- the thread */

/* The catalog's copy from the last fetch that read. */
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

static void save_copy(const char *body, size_t length)
{
    char path[NP_PATH_MAX], temp[NP_PATH_MAX + 8];
    copy_path(path, sizeof(path));
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
static void publish(const Catalog *read, CatalogState state)
{
    store_lock();
    memcpy(&catalog, read, sizeof(catalog));
    memset(apps, 0, sizeof(apps));
    catalog_state = state;
    ++generation;
    store_unlock();
}

static void fetch_catalog(void)
{
    char *body = malloc(CATALOG_MAX_JSON);
    Catalog *read = malloc(sizeof(Catalog));
    size_t length = 0;
    int status = 0;
    char why[160] = "";
    if (!body || !read)
    {
        free(body);
        free(read);
        return;
    }
    const self_update_platform *platform = store_kit_platform();
    const int code = platform->fetch(platform->user, CATALOG_URL, body, CATALOG_MAX_JSON, &length, &status);
    np_log("catalog: " CATALOG_URL ": result %d, http %d, %zu bytes", code, status, length);
    if (code == 0 && status == 200 && catalog_parse(body, length, read, log_line, NULL) >= 0)
    {
        np_log("catalog: %d apps, %d tips, %d hidden", read->app_count, read->tip_count, read->egg_count);
        save_copy(body, length);
        publish(read, CATALOG_READY);
        store_lock();
        catalog_error[0] = '\0';
        store_unlock();
        free(body);
        free(read);
        return;
    }
    if (code == SELF_UPDATE_FETCH_TOO_LARGE)
        snprintf(why, sizeof(why), "The catalog is larger than Solanum reads.");
    else if (code != 0)
        snprintf(why, sizeof(why), "The catalog couldn't be downloaded. Check the network.");
    else if (status != 200)
        snprintf(why, sizeof(why), "GitHub answered %d for the catalog.", status);
    else
        snprintf(why, sizeof(why), "The catalog couldn't be read.");
    /* The copy from the last time it was read, when the store has nothing better to show. */
    store_lock();
    const bool shown = catalog_state == CATALOG_READY || catalog_state == CATALOG_COPY;
    str_copy(catalog_error, sizeof(catalog_error), why);
    store_unlock();
    if (!shown)
    {
        char path[NP_PATH_MAX];
        copy_path(path, sizeof(path));
        if (read_file(path, body, CATALOG_MAX_JSON, &length) && catalog_parse(body, length, read, NULL, NULL) >= 0)
        {
            np_log("catalog: %s; showing the copy from the last start", why);
            publish(read, CATALOG_COPY);
        }
        else
        {
            np_log("catalog: %s; no copy to show", why);
            store_lock();
            catalog_state = CATALOG_FAILED;
            store_unlock();
        }
    }
    free(body);
    free(read);
}

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
    int gen;
    const int count = snapshot(titles, &gen);
    for (int i = 0; i < count; ++i)
    {
        const Installed found = apps_find(titles[i], registered, own_folder, homebrew);
        np_log("apps: %s: %s%s%s%s", titles[i], found.folder[0] ? found.folder : "not installed",
               found.folder[0] ? " " : "", found.version, found.image ? " (image)" : "");
        uint64_t size = 0;
        const bool sized = found.folder[0] && !found.image && apps_folder_size(found.folder, &size);
        store_lock();
        if (gen == generation)
        {
            apps[i].installed = found;
            apps[i].looked = true;
            apps[i].sized = sized;
            apps[i].size = size;
            apps[i].offer = apps[i].checked ? (int)apps_decide(&found, apps[i].reason == GITHUB_RELEASE_NEWER,
                                                                  apps[i].release.tag)
                                            : OFFER_NONE;
        }
        store_unlock();
    }
}

static void ask_github(void)
{
    static char titles[CATALOG_MAX_APPS][10];
    static github_release_asset release;
    int gen;
    const int count = snapshot(titles, &gen);
    store_lock();
    checking = true;
    store_unlock();
    for (int i = 0; i < count; ++i)
    {
        char repo[144], pattern[128], sums[64];
        github_release_report report;
        store_lock();
        const bool same = gen == generation;
        if (same)
        {
            str_copy(repo, sizeof(repo), catalog.apps[i].repo);
            str_copy(pattern, sizeof(pattern), catalog.apps[i].asset);
            str_copy(sums, sizeof(sums), catalog.apps[i].checksums);
        }
        store_unlock();
        if (!same)
            break;
        const github_release_reason reason =
            github_release_asset_check(store_kit_platform(), repo, pattern, sums, &release, &report);
        np_log("apps: %s releases/latest of %s: %s, http %d, redirects %d, error %d, tag %s, %s %s", titles[i], repo,
               github_release_reason_text(reason), report.http_status, report.redirects, report.platform_error,
               report.tag[0] ? report.tag : "-", report.zip[0] ? report.zip : "-",
               release.sha256[0] ? (release.from_sums ? sums : "digest") : "-");
        store_lock();
        if (gen == generation)
        {
            apps[i].release = release;
            apps[i].reason = reason;
            apps[i].checked = true;
            apps[i].offer = apps[i].looked
                                ? (int)apps_decide(&apps[i].installed, reason == GITHUB_RELEASE_NEWER, release.tag)
                                : OFFER_NONE;
        }
        store_unlock();
    }
    store_lock();
    checking = false;
    store_unlock();
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
        want_catalog = want_look = want_github = false;
        if (!fetch && !find && !github)
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
        if (github || fetch)
            ask_github();
    }
}

void store_init(const char *data, const char *registered_folder, const char *homebrew_folder)
{
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
}

void store_refresh(bool fetch_catalog_too)
{
    store_lock();
    want_look = want_github = true;
    want_catalog = want_catalog || fetch_catalog_too || catalog_state == CATALOG_NONE ||
                   catalog_state == CATALOG_FAILED;
    if (want_catalog)
        catalog_busy = true;
    if (asking)
    {
        store_unlock();
        return;
    }
    asking = true;
    store_unlock();
    SDL_Thread *thread = SDL_CreateThreadWithStackSize(work, "store", STORE_STACK, NULL);
    if (thread)
    {
        SDL_DetachThread(thread);
        return;
    }
    np_log("apps: the thread could not start");
    store_lock();
    asking = want_catalog = want_look = want_github = catalog_busy = false;
    store_unlock();
}

/* ---------------------------------------------------------------- installing */

static self_update_job job;  /* zero until the first install, as the kit asks */
static bool begun;
static int job_app = -1;
static char job_title[10];
static bool job_fresh, job_told;
static ResumeDownload resume;

/* The kit's platform for an install: the console's, with downloads through resume.c. */
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
    (void)user;
    const int result = resume_download(&resume, url, limit, sink, sink_user);
    np_log("install: %s: %s, %llu bytes from the copy, %llu downloaded, http %d%s", job_title,
           result == 0 ? "downloaded" : "stopped", (unsigned long long)resume.reused,
           (unsigned long long)resume.fetched, resume.status,
           resume.mismatch ? "; it doesn't match its checksum, the copy was deleted" : "");
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
static const self_update_platform job_platform = {job_fetch,         job_download,      job_verify, job_open_helper,
                                                  job_load_sequence, job_save_sequence, job_now,    NULL};

static bool ended(self_update_phase phase)
{
    return phase == SELF_UPDATE_INSTALLED || phase == SELF_UPDATE_FAILED || phase == SELF_UPDATE_CANCELLED;
}

bool store_install(int index, char *reason, size_t size)
{
    self_update_offer offer;
    reason[0] = '\0';
    if (!path_exists(store_app_file("self-updater.elf")))
    {
        str_copy(reason, size, "self-updater.elf is not in Solanum's folder. Copy the whole PPSA76526 folder.");
        np_log("install: %s", reason);
        return false;
    }
    if (begun)
    {
        self_update_status status;
        self_update_poll(&job, &status);
        if (!ended(status.phase))
        {
            str_copy(reason, size, "Another app is being installed.");
            return false;
        }
        self_update_finish(&job);
        begun = false;
    }
    store_lock();
    const bool known = index >= 0 && index < catalog.app_count && apps[index].checked && apps[index].looked &&
                       apps[index].reason == GITHUB_RELEASE_NEWER;
    const bool made = known && apps_make_offer(&catalog.apps[index], &apps[index].release, &apps[index].installed,
                                               NP_TITLE_ID, &offer);
    store_unlock();
    if (!made)
    {
        str_copy(reason, size, "Its release is not one Solanum can install.");
        return false;
    }
    /* The copy of this release's download, kept between tries; an older release's goes. */
    memset(&resume, 0, sizeof(resume));
    make_dirs(cache);
    resume_part_path(resume.part, sizeof(resume.part), cache, offer.title, offer.sha256);
    resume_forget_others(cache, offer.title, resume.part);
    str_copy(resume.sha256, sizeof(resume.sha256), offer.sha256);
    resume.size = offer.size;
    resume.range = store_kit_range;
    resume.range_user = NULL;
    str_copy(job_title, sizeof(job_title), offer.title);
    begun = self_update_start(&job, &job_platform, &offer) == 1;
    if (!begun)
        str_copy(reason, size, "The install could not begin.");
    job_app = index;
    job_fresh = strcmp(offer.installed, "none") == 0;
    job_told = false;
    np_log("install: %s %s %s (%s -> %s) from %s, sha256 %s", begun ? "installing" : "could not begin installing",
           offer.title, offer.version, offer.installed, offer.available, offer.artifact, offer.sha256);
    return begun;
}

void store_job(StoreJob *out)
{
    self_update_status status;
    memset(out, 0, sizeof(*out));
    out->app = -1;
    if (!begun)
        return;
    self_update_poll(&job, &status);
    out->app = job_app;
    str_copy(out->title, sizeof(out->title), job_title);
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
    default: out->phase = JOB_IDLE; break;
    }
    out->fresh = job_fresh;
    out->done = status.done;
    out->total = status.total;
    out->reused = resume.reused;
    str_copy(out->time_left, sizeof(out->time_left), status.time_left);
    str_copy(out->error, sizeof(out->error), status.error);
    str_copy(out->version, sizeof(out->version), status.version);
    str_copy(out->folder, sizeof(out->folder), status.folder);
    /* self_update.c's and the helper's words for a download that isn't the release's */
    out->mismatch = out->phase == JOB_FAILED && (resume.mismatch || strstr(status.error, "doesn't match") != NULL);
    /* self_update.c's words when the payload loader did not take the helper */
    static const char no_loader[] = "The update helper couldn't be started";
    out->no_loader = out->phase == JOB_FAILED && strncmp(status.error, no_loader, sizeof(no_loader) - 1) == 0;
    if (ended(status.phase) && !job_told)
    {
        job_told = true;
        np_log("install: %s %s%s%s%s", job_title,
               out->phase == JOB_INSTALLED ? "installed " : out->phase == JOB_CANCELLED ? "cancelled" : "failed: ",
               out->phase == JOB_INSTALLED ? status.version : status.error, status.folder[0] ? " in " : "",
               status.folder);
        if (out->phase == JOB_INSTALLED)
        {
            remove(resume.part); /* in place now: the copy has done its job */
            store_refresh(false); /* the lists say what is there now, once the thread has looked again */
        }
    }
}

void store_cancel(void)
{
    if (begun)
        self_update_cancel(&job);
}

void store_job_finish(void)
{
    if (!begun)
        return;
    self_update_status status;
    self_update_poll(&job, &status);
    if (!ended(status.phase))
        return;
    self_update_finish(&job);
    begun = false;
    job_app = -1;
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
