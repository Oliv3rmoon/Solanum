/*
 * Solanum - the store: the catalogs, where their apps are, their releases, and installing, updating, rolling back,
 * uninstalling and opening them.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * As Nightshade's Settings > Apps (headless/apps.cpp) does it, grown: a thread of its own fetches Solanum's catalog
 * (raw.githubusercontent.com) and the player's custom ones (custom.h), and looks for each app on the console (apps.h)
 * in every folder ShadowMountPlus scans. Each app's newest release is the one the catalog pinned (its workflow
 * downloaded and hashed it), so a catalog of any size costs GitHub's API nothing; GitHub is asked (one request an app
 * of the 60 an hour it allows an address) only for the apps installed here, to see a release newer than the pin, for
 * the apps the catalog pinned nothing for, and for a custom catalog's apps, and never again once it says the hour's
 * requests are used. Drafts and pre-releases are never offered.
 *
 * Installs are the self-update kit's jobs with an install offer (kit/self_update.h): the ZIP checked against its
 * SHA-256 as it arrives (resume.h keeps a copy, so an interrupted one goes on where it stopped) and again by the
 * helper (self-updater.elf in Solanum's folder, sent to the payload loader), which puts the app's folder beside
 * Solanum's or in the folder chosen for it, or in place of the app's own files. They wait in a queue and run a few at
 * once. A finished install's ZIP is kept, with the one before it (keep.h): Roll back puts the one before back
 * without a byte from the network. Uninstall is the helper's too (remove): the folder, the home-screen entry, and
 * the data folder when the player asks. Opening one sends app-launch.elf to the payload loader, which opens it once
 * Solanum has closed.
 *
 * The screens read the store between store_lock() and store_unlock() (once a frame); the thread writes its findings
 * under the same lock.
 */
#ifndef SOLANUM_STORE_H
#define SOLANUM_STORE_H

#include "apps.h"
#include "catalog.h"
#include "github_release.h"
#include "keep.h"
#include "prefs.h"
#include "self_update.h"

#include <stdbool.h>
#include <stdint.h>

/* Folders: the data folder (cache/ holds the catalogs' copies, the downloads and the kept ZIPs; apps.ini and
 * catalogs.ini the player's decisions), and where the console keeps apps (on the PS5 /user and /data/homebrew). */
void store_init(const char *data_folder, const char *registered, const char *homebrew);
/* With `catalog`: fetches the catalogs, looks for their apps and asks GitHub what it needs to; without: only looks
 * for the apps again (after an install). On a thread of its own; a refresh asked for while one runs is done after
 * it. */
void store_refresh(bool catalog);
/* Each frame, on the main thread: starts the queue's next jobs and notices the ones that ended. */
void store_poll(void);

void store_lock(void);
void store_unlock(void);

/* ---- between store_lock and store_unlock ---- */

typedef enum
{
    CATALOG_NONE,    /* not fetched yet, and no copy from before */
    CATALOG_LOADING, /* being fetched */
    CATALOG_READY,   /* fetched */
    CATALOG_COPY,    /* the copy kept from an earlier start: the fetch failed */
    CATALOG_FAILED   /* no catalog at all */
} CatalogState;

const Catalog *store_catalog(void); /* never NULL; empty until one is read. Solanum's apps, then the custom ones'. */
CatalogState store_catalog_state(void);
bool store_catalog_busy(void);    /* the catalog is being fetched (a copy may be shown meanwhile) */
const char *store_catalog_error(void); /* why the fetch failed, "" otherwise */
bool store_checking(void);        /* releases are being asked for */
int store_generation(void);       /* changes whenever the catalog is replaced */
bool store_rate_limited(void);    /* GitHub said this hour's requests are used: the pins are shown */

/* The custom catalogs as the last refresh read them: how many apps each added (-1: it couldn't be read, not even a
 * copy) and how many it listed that another catalog lists too. */
int store_custom_added(int index);
int store_custom_hidden(int index);

typedef enum
{
    FROM_GITHUB,  /* GitHub answered this refresh */
    FROM_CATALOG  /* the catalog's pin: GitHub wasn't asked, or couldn't answer */
} ReleaseSource;

typedef struct
{
    bool looked;          /* where it is installed is known */
    Installed installed;  /* folder empty: not installed */
    bool sized;
    uint64_t size;        /* bytes under its folder, when sized */
    bool checked;         /* its release is known (the catalog's pin, or GitHub's answer) */
    int reason;           /* github_release_reason; GITHUB_RELEASE_NEWER: release is the newest */
    github_release_asset release;
    int source;           /* ReleaseSource */
    char unpinned[40];    /* GitHub's newest release when it can't be checked yet (no checksum, not pinned yet) */
    int offer;            /* Offer (apps.h) */
    /* what the player decided (prefs.h), and how it came to be here */
    bool hold;
    char skip[40];
    int by;               /* InstalledBy */
    /* the version before the installed one, kept for Roll back (keep.h) */
    bool kept;
    KeptZip previous;
} StoreApp;

const StoreApp *store_app(int index); /* the catalog's index */
/* Why an app has no release to offer, in English, for its page; "" when it has one. */
const char *store_problem(int reason);
/* An installed app with a newer release the player still wants offered (not held, not skipped). */
bool store_update_wanted(int index);
int store_update_count(void);

/* ---- what the player decides (any time) ---- */

void store_set_hold(int index, bool hold);
/* Skips the newest release ("" offers it again). */
void store_set_skip(int index, const char *tag);

/* ---- jobs: installs, updates, older versions, roll backs, uninstalls (any time) ---- */

#define STORE_JOBS 16 /* waiting, running, and ended ones not yet said */

typedef enum
{
    JOB_INSTALL,  /* a new app */
    JOB_UPDATE,   /* its newest release over an older one */
    JOB_VERSION,  /* another release, chosen in Versions */
    JOB_ROLLBACK, /* the version before, from its kept ZIP */
    JOB_REMOVE    /* an uninstall */
} JobKind;

typedef enum
{
    JOB_IDLE,
    JOB_QUEUED,      /* waits for a free place */
    JOB_STARTING,    /* the helper is being started */
    JOB_DOWNLOADING, /* done / total bytes of the ZIP */
    JOB_UNPACKING,   /* done / total bytes unpacked */
    JOB_INSTALLING,  /* the helper puts the files in place */
    JOB_REMOVING,    /* the helper removes the app */
    JOB_INSTALLED,   /* (or removed, for JOB_REMOVE) */
    JOB_CANCELLED,
    JOB_FAILED
} JobPhase;

typedef struct
{
    int id;           /* 0: no job */
    int kind;         /* JobKind */
    int app;          /* the catalog's index, -1 when the catalog changed under it */
    char title[10];
    char name[64];
    char tag[40];     /* the release being installed */
    JobPhase phase;
    bool fresh;       /* a new install, not an update */
    uint64_t done, total, reused;
    char time_left[32];
    char error[160];  /* JOB_FAILED: why, in English */
    bool mismatch;    /* ... because the download didn't match its checksum: nothing was unpacked */
    bool no_loader;   /* ... because no payload loader took the helper */
    char version[12]; /* JOB_INSTALLED: the content version put in place */
    char folder[256]; /* ... and where (JOB_REMOVE: what was removed, "-" none) */
    bool home_screen; /* JOB_REMOVE: the home screen let the title go */
    bool data;        /* JOB_REMOVE: its data folder was asked to go too */
} StoreJob;

/* Queues the offered release of an app (Install or Update). `where`: for a new app, the folder to put it in
 * (apps_place), "" beside Solanum. False when it can't be queued; *reason says why. */
bool store_install(int index, const char *where, char *reason, size_t size);
/* Queues one of the releases Versions listed (store_versions). */
bool store_install_version(int index, int version, char *reason, size_t size);
/* Queues the version before the installed one, from its kept ZIP. */
bool store_rollback(int index, char *reason, size_t size);
/* Queues an uninstall; with `data`, its data folder (the catalog's) goes too. */
bool store_uninstall(int index, bool data, char *reason, size_t size);
/* Every job, the running ones first, then the waiting ones in their order, then the ended ones; returns how many. */
int store_jobs(StoreJob *out, int most);
/* The app's job, waiting or running (false: none). */
bool store_job_of(int index, StoreJob *out);
/* Cancels a job: a waiting one leaves the queue, a running one stops (until its files are being put in place). */
void store_cancel(int id);
void store_cancel_all(void);
/* An ended job not said yet (true: *out is it, and it is forgotten). */
bool store_job_ended(StoreJob *out);
/* How many run at once (1 to 3), and whether finished downloads are kept for Roll back. */
void store_set_parallel(int count);
void store_set_keep(bool keep);
bool store_busy(void); /* a job waits or runs */

/* ---- Versions: an app's releases on GitHub (one request) ---- */

typedef enum
{
    VERSIONS_NONE,
    VERSIONS_ASKING,
    VERSIONS_READY,
    VERSIONS_FAILED
} VersionsState;

typedef struct
{
    int state;   /* VersionsState */
    int app;     /* whose */
    int generation;
    int count;
    github_release_asset list[GITHUB_RELEASE_LIST_MAX]; /* newest first; sha256 empty: it can't be checked */
    char error[160];
} StoreVersions;

void store_versions_ask(int index);
const StoreVersions *store_versions(void);

/* ---- storage ---- */

typedef struct
{
    int count;
    char path[APPS_PLACES][24];   /* "/data", "/mnt/ext0"... the drives there now */
    long long free_mb[APPS_PLACES];
    uint64_t kept;                /* bytes of the kept ZIPs */
    uint64_t parts;               /* bytes of unfinished downloads */
} StoreStorage;

/* The drives there now and their free room (asked now), and what Solanum's cache keeps (from the last look). */
void store_storage(StoreStorage *out);
/* Deletes every kept ZIP and unfinished download. */
void store_clear_cache(void);
/* Free megabytes where `path` is, -1 when it can't be told. */
long long store_free_mb(const char *path);

/* ---- opening ---- */

typedef enum
{
    OPEN_CLOSING,    /* the launch payload waits for Solanum to close, then opens the app */
    OPEN_REFUSED,    /* *reason says why */
    OPEN_NO_LOADER,  /* no payload loader took app-launch.elf */
    OPEN_NOT_LISTED  /* the console doesn't list the app yet */
} OpenResult;
OpenResult store_open(int index, char *reason, size_t size);

/* ---- the console side (store_console.c on the PS5, store_desktop.c elsewhere) ---- */

/* A file of Solanum's own folder: /app0/<name> in the sandbox, where the app is mounted or stored once the
 * HEN has freed it. Only "self-updater.elf", "app-launch.elf" and "sce_sys/param.json" are asked for. */
const char *store_app_file(const char *name);
const self_update_platform *store_kit_platform(void);
int store_kit_range(void *user, const char *url, uint64_t offset, uint64_t limit, self_update_sink sink,
                    void *sink_user, int *status);
int store_kit_open_payload(const char *path, self_update_channel *channel);
int store_kit_channel_timeout(const self_update_channel *channel, int milliseconds);

#endif
