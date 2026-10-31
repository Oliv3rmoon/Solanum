/*
 * Solanum - the app's state and what the screens share (main.c owns them).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The screens run between store_lock() and store_unlock() (store.h), once a frame. What would wait on the
 * network or the payload loader (installing, opening, refreshing) they ask of the main loop instead
 * (app_request), which does it after the lock is let go.
 */
#ifndef NP_APP_H
#define NP_APP_H

#include "nightshade.h"
#include "store.h"

enum Screen
{
    SCREEN_STORE,    /* the tabs: Browse, Updates, Installed, About */
    SCREEN_PAGE,     /* an app's page */
    SCREEN_SETTINGS,
    SCREEN_COUNT
};

enum Tab
{
    TAB_BROWSE,
    TAB_UPDATES,
    TAB_INSTALLED,
    TAB_DOWNLOADS,
    TAB_ABOUT,
    TAB_COUNT
};

enum Request
{
    REQUEST_NONE,
    REQUEST_INSTALL,     /* app.request_app, in app.request_text (a place, "" beside Solanum); request_arg 1: in place
                          * of another app of its title ID, which the player agreed to (0.4.0) */
    REQUEST_OPEN,        /* app.request_app */
    REQUEST_REFRESH,     /* the catalog and the releases again */
    REQUEST_CANCEL,      /* the job app.request_arg */
    REQUEST_VERSION,     /* app.request_app's release app.request_arg of Versions; request_text "replace": in place of
                          * another app of its title ID, which the player agreed to (0.4.0) */
    REQUEST_ROLLBACK,    /* app.request_app's version before */
    REQUEST_UNINSTALL,   /* app.request_app; app.request_arg 1: its data folder too */
    REQUEST_UPDATE_ALL,  /* every update the player wants */
    REQUEST_CLEAR_CACHE, /* the kept ZIPs and unfinished downloads */
};

#define APP_HISTORY 8

typedef struct
{
    Paths paths;
    Settings settings;
    enum Screen screen;
    int tab;                 /* enum Tab */
    int page_app;            /* the app page's catalog index */
    StoreJob history[APP_HISTORY]; /* the jobs that ended, the newest first, for Downloads */
    int history_count;
    char storage_error[256]; /* the data folder can't be written */
    bool sandboxed;          /* /data can't be listed */
    char sandbox_reason[200];
    float dt;                /* seconds since the last frame */
    PadState pads[NP_MAX_PADS];
    bool quit_requested;
    uint64_t quit_at;        /* opening an app: Solanum closes then (plat_ticks_us), 0 otherwise */
    int request;             /* enum Request, for the main loop */
    int request_app;
    int request_arg;
    char request_text[64];
    uint32_t random;         /* apps_pick's generator */
    int tip;                 /* the catalog's tip shown now, -1 none */
    int last_egg;
} App;

extern App app;

void app_toast(const char *message);                 /* a notice for 2.5 s */
void app_toast_for(const char *message, float seconds);
/* A message in a box in the middle of the screen, until Cross is pressed. */
void app_message_box(const char *message);
/* The controller that moves through the app: player 1's, else the first one connected. */
int app_nav_pad(void);
void app_save_settings(void);
void app_open_settings(void);
/* The settings changed what the sounds use: applied now. */
void app_apply_settings(void);
void app_request(int request, int app_index);
void app_request_with(int request, int app_index, int arg, const char *text);
/* The job of an app, waiting or running (store_job_of); false: none. */
bool app_job(int index, StoreJob *out);
/* Whether a third-party app's install has been agreed to this time (page.c asks once an app). */
bool app_third_party_agreed(const char *title);
void app_third_party_agree(const char *title);

/* The bottom hint bar: pairs of (PadGlyph, label), then an optional right-hand text. */
void app_draw_hints(const int *glyphs, const char *const *labels, int count, const char *right);
/* Nightshade's backdrop: near black with a low red glow. */
void app_backdrop(void);
/* A new random tip for the next loading screen (the catalog's tips), and the one shown now ("" none). */
void app_next_tip(void);
const char *app_tip(void);
/* One of the catalog's hidden lines, at random, as a notice. */
void app_show_egg(void);
/* Word-wrapped text from x, y, `width` wide, its first `skip` lines left out, down to `bottom`: the lines
 * drawn, and in *total all of them. */
int app_draw_wrapped(const char *text, float x, float y, float width, float size, int weight, uint32_t argb,
                     int align, float bottom, int skip, int *total);
/* "38.5 MB", "1.2 GB", "820 KB". */
void app_format_size(uint64_t bytes, char *out, size_t size);
/* An app's tile (its initial on Nightshade's red), a spinner, a release's date, and what the lists say of an
 * app on their right (ui/store_ui.c). */
void app_draw_tile(const CatalogApp *a, float x, float y, float size, float alpha);
void app_draw_spinner(float cx, float cy, float r, uint32_t argb);
void app_release_date(const char *published, char *out, size_t size);
const char *app_status(int index, uint32_t *argb);

/* Screens: each handles its buttons and draws one frame. */
void store_screen(uint32_t pressed);
void store_screen_opened(void);
void page_open(int index);
void page_screen(uint32_t pressed);
void settings_screen(uint32_t pressed);
void settings_opened(void);

#endif
