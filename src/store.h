/*
 * Solanum - the store: the catalog, where its apps are, their releases, and installing and opening them.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * As Nightshade's Settings > Apps (headless/apps.cpp) does it: a thread of its own fetches the catalog
 * (raw.githubusercontent.com), looks for each app on the console (apps.h) and asks GitHub for its newest
 * release (api.github.com, one request an app of the 60 an hour GitHub allows an address; drafts and
 * pre-releases are never offered). An install is the self-update kit's job with an install offer
 * (kit/self_update.h): the ZIP is checked against its SHA-256 as it arrives (resume.h keeps a copy, so an
 * interrupted one goes on where it stopped) and again by the helper (self-updater.elf in Solanum's folder,
 * sent to the payload loader), which puts the app's folder beside Solanum's (/data/homebrew/<title ID> when
 * Solanum is in /data/homebrew), or in place of the app's own files. Opening one sends app-launch.elf to the
 * payload loader, which opens it once Solanum has closed.
 *
 * The screens read the store between store_lock() and store_unlock() (once a frame); the thread writes its
 * findings under the same lock.
 */
#ifndef SOLANUM_STORE_H
#define SOLANUM_STORE_H

#include "apps.h"
#include "catalog.h"
#include "github_release.h"
#include "self_update.h"

#include <stdbool.h>
#include <stdint.h>

/* Folders: the data folder (cache/ holds the catalog's copy and the downloads), and where the console keeps
 * apps (on the PS5 /user and /data/homebrew). */
void store_init(const char *data_folder, const char *registered, const char *homebrew);
/* With `catalog`: fetches the catalog, looks for its apps and asks GitHub for their releases; without: only looks
 * for the apps again (after an install). On a thread of its own; a refresh asked for while one runs is done after
 * it. */
void store_refresh(bool catalog);

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

const Catalog *store_catalog(void); /* never NULL; empty until one is read */
CatalogState store_catalog_state(void);
bool store_catalog_busy(void);    /* the catalog is being fetched (a copy may be shown meanwhile) */
const char *store_catalog_error(void); /* why the fetch failed, "" otherwise */
bool store_checking(void);        /* releases are being asked for */
int store_generation(void);       /* changes whenever the catalog is replaced */

typedef struct
{
    bool looked;          /* where it is installed is known */
    Installed installed;  /* folder empty: not installed */
    bool sized;
    uint64_t size;        /* bytes under its folder, when sized */
    bool checked;         /* GitHub was asked */
    int reason;           /* github_release_reason; GITHUB_RELEASE_NEWER: release is the newest */
    github_release_asset release;
    int offer;            /* Offer (apps.h) */
} StoreApp;

const StoreApp *store_app(int index); /* the catalog's index */
/* Why an app has no release to offer, in English, for its page; "" when it has one. */
const char *store_problem(int reason);
/* The number of installed apps a newer release is offered for. */
int store_update_count(void);

/* ---- installing (any time) ---- */

typedef enum
{
    JOB_IDLE,
    JOB_STARTING,    /* the helper is being started */
    JOB_DOWNLOADING, /* done / total bytes of the ZIP */
    JOB_UNPACKING,   /* done / total bytes unpacked */
    JOB_INSTALLING,  /* the helper puts the files in place */
    JOB_INSTALLED,
    JOB_CANCELLED,
    JOB_FAILED
} JobPhase;

typedef struct
{
    int app;          /* the catalog's index, -1 when there is no job */
    char title[10];
    JobPhase phase;
    bool fresh;       /* a new install, not an update */
    uint64_t done, total, reused;
    char time_left[32];
    char error[160];  /* JOB_FAILED: why, in English */
    bool mismatch;    /* ... because the download didn't match its checksum: nothing was unpacked */
    bool no_loader;   /* ... because no payload loader took the helper */
    char version[12]; /* JOB_INSTALLED: the content version put in place */
    char folder[256]; /* ... and where */
} StoreJob;

/* Installs (or updates) the offered release of an app. False when it can't begin; *reason says why. */
bool store_install(int index, char *reason, size_t size);
void store_job(StoreJob *out);
void store_cancel(void);
/* Forgets an ended job, so that the next can begin. */
void store_job_finish(void);

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
