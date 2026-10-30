/*
 * Solanum - Settings (OPTIONS): tabs (L1 / R1) of rows (up / down), each changed with left / right or Cross
 * (Dulcamara's, with the store's rows).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../config.h"
#include "../i18n.h"
#include "../music.h"
#include "../platform/platform.h"
#include "../update.h"
#include "../custom.h"
#include "../store.h"
#include "draw.h"
#include "keyboard.h"
#include "overlay.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum Kind
{
    K_CHOICE, /* an int field, its values named */
    K_TOGGLE, /* a bool field */
    K_ACTION, /* Cross does something */
    K_INFO,   /* a line of text */
};

enum Action
{
    A_NONE,
    A_REFRESH,
    A_CHECK_UPDATE,
    A_CLOSE_APP,
    A_CLEAR_CACHE,
    A_ADD_CATALOG,
    A_REMOVE_CATALOG, /* the row's catalog */
};

enum
{
    ASK_REMOVE_CATALOG = 300
};

typedef struct
{
    const char *label;
    const char *help;
    int kind;
    size_t field;              /* offsetof(Settings, ...) for K_CHOICE / K_TOGGLE */
    const char *const *names;  /* K_CHOICE: what each value is called */
    int count;
    const int *values;         /* K_CHOICE: the stored values, NULL when they are 0..count-1 */
    int action;                /* K_ACTION */
} Row;

#define FIELD(f) offsetof(Settings, f)
#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

static const char *const UI_SOUNDS[] = {"Soft", "Wood", "Pop", "Chime", "Classic", "Off"};
static const char *const UI_VOLUMES[] = {"25%", "50%", "75%", "100%"};
static const char *const MUSIC_VOLUMES[] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
static const int MUSIC_VOLUME_VALUES[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
/* each language in its own words (i18n_name), the first the console's */
static const char *const LANGUAGES[] = {"The console's", "English", "Français", "Português", "Español",
                                        "日本語",        "Deutsch", "Nederlands", "Türkçe",  "Italiano",
                                        "Polski"};
static const int LANGUAGE_VALUES[] = {-1, LANG_EN, LANG_FR, LANG_PT, LANG_ES, LANG_JA,
                                      LANG_DE, LANG_NL, LANG_TR, LANG_IT, LANG_PL};
static const char *const PLACES[] = {"Ask each time", "Beside Solanum", "Internal storage", "Extended storage 1",
                                     "Extended storage 2", "USB drive 1", "USB drive 2", "USB drive 3", "USB drive 4",
                                     "USB drive 5", "USB drive 6", "USB drive 7", "USB drive 8"};
static const int PLACE_VALUES[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const char *const PARALLEL[] = {"1", "2", "3"};
static const int PARALLEL_VALUES[] = {1, 2, 3};

static const Row INTERFACE_ROWS[] = {
    {"Language", "Solanum's own words. The apps' names, summaries and notes stay as their authors wrote them.",
     K_CHOICE, FIELD(language), LANGUAGES, N(LANGUAGES), LANGUAGE_VALUES, A_NONE},
    {"Interface sounds", "The clicks of the lists and the menus.", K_CHOICE, FIELD(ui_sound), UI_SOUNDS,
     N(UI_SOUNDS), NULL, A_NONE},
    {"Interface volume", "How loud those clicks are.", K_CHOICE, FIELD(ui_volume), UI_VOLUMES, N(UI_VOLUMES), NULL,
     A_NONE},
    {"Background music", "Plays the music files you put in /data/solanum/music (WAV, MP3 or OGG) behind the store, one "
                         "after another, and fades out when an app opens.",
     K_TOGGLE, FIELD(music), NULL, 0, NULL, A_NONE},
    {"Shuffle music", "Plays the files in a random order, every one once before any plays again. Off plays them in "
                      "the order of their names.",
     K_TOGGLE, FIELD(music_shuffle), NULL, 0, NULL, A_NONE},
    {"Music volume", "How loud the music is. The interface sounds keep their own volume.", K_CHOICE,
     FIELD(music_volume), MUSIC_VOLUMES, N(MUSIC_VOLUMES), MUSIC_VOLUME_VALUES, A_NONE},
    {"Music files go in /data/solanum/music: WAV (16-bit PCM), MP3 or OGG Vorbis, mono or stereo, at any rate. Other "
     "kinds are not played.",
     NULL, K_INFO, 0, NULL, 0, NULL, A_NONE},
    {"A USB keyboard works too: arrows move, Enter chooses, Escape goes back, Tab changes tab, / searches.", NULL,
     K_INFO, 0, NULL, 0, NULL, A_NONE},
};

static const Row STORE_ROWS[] = {
    {"Install new apps on", "Where a new app goes: a folder ShadowMountPlus scans on the console, an extended drive "
                            "or a USB drive. Ask each time asks when more than one is plugged in.",
     K_CHOICE, FIELD(install_place), PLACES, N(PLACES), PLACE_VALUES, A_NONE},
    {"Installs at once", "How many installs, updates and uninstalls run at the same time; the rest wait in Downloads.",
     K_CHOICE, FIELD(parallel), PARALLEL, N(PARALLEL), PARALLEL_VALUES, A_NONE},
    {"Keep the last version", "Keeps each install's ZIP and the one before it, so More > Roll back puts the "
                              "previous version back without downloading it. Clear them in Installed.",
     K_TOGGLE, FIELD(keep_versions), NULL, 0, NULL, A_NONE},
    {"Update apps by themselves", "After each refresh, every app with a newer release is updated, except those you "
                                  "hold or whose update you skipped (an app's More).",
     K_TOGGLE, FIELD(auto_update), NULL, 0, NULL, A_NONE},
    {"Refresh the catalog", "Asks for the catalogs again, and GitHub for the installed apps' newest releases (Square "
                            "on the store does too). The catalog pins every app's release, so it costs GitHub little.",
     K_ACTION, 0, NULL, 0, NULL, A_REFRESH},
    {"Clear the cache", "Deletes the kept versions and unfinished downloads.", K_ACTION, 0, NULL, 0, NULL,
     A_CLEAR_CACHE},
    {"Logs: " NP_DATA "/logs (solanum.log, updater.log, app-launch.log).", NULL, K_INFO, 0, NULL, 0, NULL, A_NONE},
};

static const Row SYSTEM[] = {
    {"Check for updates at start", "Asks github.com/Oliv3rmoon/Solanum for a newer release when the app "
                                   "opens, and installs it (checked against its SHA256SUMS) for the next start.",
     K_TOGGLE, FIELD(update_check), NULL, 0, NULL, A_NONE},
    {"Check for updates now", "Asks GitHub now.", K_ACTION, 0, NULL, 0, NULL, A_CHECK_UPDATE},
    {"Close Solanum", "Closes the app. An install under way is cancelled first.", K_ACTION, 0, NULL, 0, NULL,
     A_CLOSE_APP},
};

typedef struct
{
    const char *name;
    int icon;
    const Row *rows;
    int count;
} Tab;

/* Catalogs: made each frame from custom.h (catalog_rows). */
static Row catalog_rows[CUSTOM_MAX + 3];
static char catalog_labels[CUSTOM_MAX][CUSTOM_TEXT + 64];
static char catalog_help[CUSTOM_MAX][200];

static const Tab TABS[] = {
    {"Interface", ICON_LANGUAGE, INTERFACE_ROWS, N(INTERFACE_ROWS)},
    {"Store", ICON_LAYOUT_GRID, STORE_ROWS, N(STORE_ROWS)},
    {"Catalogs", ICON_WORLD, NULL, 0},
    {"System", ICON_SETTINGS, SYSTEM, N(SYSTEM)},
};
#define TAB_COUNT N(TABS)
#define TAB_CATALOGS 2

static int remove_index = -1;
static char typed[KEYBOARD_TEXT_MAX];

/* The Catalogs tab's rows: Add, each custom catalog, a word on what they are. Under the store's lock. */
static int make_catalog_rows(void)
{
    int n = 0;
    catalog_rows[n++] = (Row){"Add a catalog", "A GitHub repository (owner/name) with a catalog.json, or the HTTPS "
                                               "address of one. Its apps are third-party: marked so, never attested.",
                              K_ACTION, 0, NULL, 0, NULL, A_ADD_CATALOG};
    for (int i = 0; i < custom_count(); ++i)
    {
        char name[CUSTOM_TEXT];
        custom_name(i, name, sizeof(name));
        const int added = store_custom_added(i), hidden = store_custom_hidden(i);
        if (added < 0)
            snprintf(catalog_labels[i], sizeof(catalog_labels[i]), tr("%s  (couldn't be read)"), name);
        else
            snprintf(catalog_labels[i], sizeof(catalog_labels[i]), tr("%s  (%d apps)"), name, added);
        snprintf(catalog_help[i], sizeof(catalog_help[i]),
                 hidden ? tr("Cross removes it. %d of its apps are listed by another catalog already, so they are hidden.")
                        : tr("Cross removes it. %d of its apps are hidden."),
                 hidden);
        catalog_rows[n++] = (Row){catalog_labels[i], catalog_help[i], K_ACTION, 0, NULL, 0, NULL, A_REMOVE_CATALOG};
    }
    catalog_rows[n++] = (Row){"Custom catalogs are other people's lists. Solanum checks their apps' downloads against "
                              "GitHub alone, never against what the catalog says.",
                              NULL, K_INFO, 0, NULL, 0, NULL, A_NONE};
    return n;
}

static int tab, row, top;
static float bar_y = -1.0f;

void settings_opened(void)
{
    row = 0;
    top = 0;
    bar_y = -1.0f;
}

/* ---------------------------------------------------------------- values */

static int *int_field(const Row *r)
{
    return (int *)((char *)&app.settings + r->field);
}

static int choice_index(const Row *r)
{
    const int v = *int_field(r);
    for (int i = 0; i < r->count; ++i)
        if ((r->values ? r->values[i] : i) == v)
            return i;
    return 0;
}

static void choice_set(const Row *r, int index)
{
    *int_field(r) = r->values ? r->values[index] : index;
}

static bool selectable(const Row *r)
{
    return r->kind != K_INFO;
}

static void changed(const Row *r)
{
    app_apply_settings();
    /* switching music on with nothing to play would otherwise do nothing at all */
    if (r->kind == K_TOGGLE && r->field == FIELD(music) && app.settings.music && music_available() == 0)
        app_toast("There are no music files in /data/solanum/music yet.");
}

static void run_action(int action)
{
    switch (action)
    {
    case A_CLEAR_CACHE: app_request(REQUEST_CLEAR_CACHE, -1); break;
    case A_ADD_CATALOG:
        typed[0] = '\0';
        keyboard_open_prompt(typed, tr("owner/name, or https://..."));
        break;
    case A_REFRESH:
        app_request(REQUEST_REFRESH, -1);
        app_toast("Asking for the catalog and the newest releases again.");
        break;
    case A_CHECK_UPDATE:
        if (update_state() != UPDATE_CHECKING && update_state() != UPDATE_INSTALLING)
            update_check();
        break;
    case A_CLOSE_APP:
        app_save_settings();
        app.quit_requested = true;
        break;
    default: break;
    }
}

static const char *update_text(void)
{
    static char text[160];
    switch (update_state())
    {
    case UPDATE_CHECKING: return tr("Checking...");
    case UPDATE_NONE: return tr("This is the newest version");
    case UPDATE_AVAILABLE:
        snprintf(text, sizeof(text), tr("%s is available: Cross installs it"), update_version());
        return text;
    case UPDATE_INSTALLING: return tr("Installing...");
    case UPDATE_INSTALLED: return tr("Installed: restart to use it");
    case UPDATE_FAILED: return tr(update_message());
    default: return "";
    }
}

/* ---------------------------------------------------------------- the screen */

static void leave(void)
{
    app_save_settings();
    app_apply_settings();
    sfx_play(SFX_BACK);
    app.screen = SCREEN_STORE;
}

/* A tab's rows: the Catalogs tab's are made now. */
static int tab_rows(int which, const Row **rows)
{
    if (which == TAB_CATALOGS)
    {
        *rows = catalog_rows;
        return make_catalog_rows();
    }
    *rows = TABS[which].rows;
    return TABS[which].count;
}

/* The Catalogs tab's keyboard and question: true while the keyboard is open (it takes the presses). */
static bool catalogs_input(uint32_t pressed)
{
    bool done = false, yes = false, checked = false;
    if (keyboard_active())
    {
        if (keyboard_input(pressed, typed, sizeof(typed), &done) && done && typed[0])
        {
            char why[160];
            if (custom_add(typed, why, sizeof(why)))
            {
                custom_save();
                app_toast(tr("The catalog is added. Its apps show once the catalogs are read again."));
                app_request(REQUEST_REFRESH, -1);
            }
            else
                app_message_box(tr(why));
        }
        return true;
    }
    if (confirm_answered(ASK_REMOVE_CATALOG, &yes, &checked) && yes && remove_index >= 0)
    {
        custom_remove(remove_index);
        custom_save();
        remove_index = -1;
        app_request(REQUEST_REFRESH, -1);
    }
    return false;
}

static void draw_settings(const Row *rows, int count)
{
    const Row *r = row >= 0 && row < count ? &rows[row] : NULL;
    app_backdrop();
    const float x = TH_MARGIN + 40, w = plat_width() - 2 * (TH_MARGIN + 40);
    draw_rrect(TH_MARGIN + 6, 60, 10, 34, 5, TH_FOCUS);
    text_draw(TH_MARGIN + 32, 54, 40, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Settings"));
    /* the tabs */
    float tx = x;
    const float ty = 138;
    draw_pad_glyph(GLYPH_L1, tx + 22, ty + 28, 26);
    tx += 64;
    for (int i = 0; i < TAB_COUNT; ++i)
    {
        const char *name = tr(TABS[i].name);
        const float tw = text_width(26, FONT_BOLD, name) + 96;
        const bool on = i == tab;
        draw_rrect(tx, ty, tw, 56, 28, on ? TH_FOCUS : TH_PILL_A(0xd0));
        icon_draw(TABS[i].icon, tx + 20, ty + 13, 30, on ? 0xffffffffu : TH_TEXT_DIM);
        text_draw(tx + 60, ty + 14, 26, FONT_BOLD, on ? 0xffffffffu : TH_TEXT_SOFT, ALIGN_LEFT, name);
        tx += tw + 14;
    }
    draw_pad_glyph(GLYPH_R1, tx + 30, ty + 28, 26);

    /* the rows */
    const float list_y = 236, row_h = 78;
    const int visible = 8;
    top = list_top_row(top, row, visible, count);
    draw_rrect(x - 20, list_y - 16, w + 40, visible * row_h + 32, TH_RADIUS, TH_CARD_A(0xe6));
    const float target = list_y + (row - top) * row_h;
    if (bar_y < 0)
        bar_y = target;
    anim_approach(&bar_y, target, app.dt, TH_SNAP);
    if (r && selectable(r))
    {
        draw_rrect(x, bar_y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rect(x, bar_y + 16, 5, row_h - 40, TH_FOCUS);
    }
    for (int i = top; i < count && i < top + visible; ++i)
    {
        const Row *it = &rows[i];
        const float ry = list_y + (i - top) * row_h;
        const bool on = i == row && selectable(it);
        if (it->kind == K_INFO)
        {
            app_draw_wrapped(tr(it->label), x + 28, ry + 8, w - 56, 23, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT,
                             ry + row_h - 4, 0, NULL);
            continue;
        }
        const float lx = x + 28;
        text_draw_fit(lx, ry + 21, 28, on ? FONT_BOLD : FONT_REGULAR, on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT,
                      w * 0.55f, tr(it->label));
        const float right = x + w - 24;
        if (it->kind == K_CHOICE)
            draw_choice(right, ry + 12, 48, 24, on ? TH_PILL : TH_PILL_A(0x90), on ? TH_TEXT : TH_TEXT_SOFT,
                        tr(it->names[choice_index(it)]));
        else if (it->kind == K_TOGGLE)
        {
            const bool v = *(const bool *)((const char *)&app.settings + it->field);
            draw_switch(right - 92, ry + 17, 44, v ? 1.0f : 0.0f);
        }
        else if (it->kind == K_ACTION)
        {
            const char *value = it->action == A_CHECK_UPDATE ? update_text() : "";
            if (value[0])
                text_draw_fit(right, ry + 23, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_RIGHT, w * 0.4f, value);
            else
                icon_draw(it->action == A_REMOVE_CATALOG ? ICON_X
                          : it->action == A_ADD_CATALOG  ? ICON_PLUS
                                                         : ICON_CHEVRON_RIGHT,
                          right - 34, ry + 20, 32, on ? TH_FOCUS : TH_TEXT_DIM);
        }
    }
    /* the chosen row's explanation */
    if (r && selectable(r) && r->help)
        app_draw_wrapped(tr(r->help), x, list_y + visible * row_h + 36, w, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT,
                         TH_HINT_Y - 30, 0, NULL);
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
    static const char *const labels[] = {"Change", "Back"};
    app_draw_hints(glyphs, labels, 2, NP_TITLE " " NP_VERSION);
}

void settings_screen(uint32_t pressed)
{
    const Row *rows;
    int count = tab_rows(tab, &rows);
    if (catalogs_input(pressed))
    {
        draw_settings(rows, count);
        keyboard_draw(typed);
        return;
    }
    if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_START)))
    {
        leave();
        app_backdrop();
        return;
    }
    if (pressed & (BIT(BTN_L1) | BIT(BTN_R1)))
    {
        tab = (tab + (pressed & BIT(BTN_R1) ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
        count = tab_rows(tab, &rows);
        row = top = 0;
        bar_y = -1.0f;
        sfx_play(SFX_CLICK);
    }
    if (row >= count)
        row = count - 1;
    /* the first row that can be chosen */
    while (row < count - 1 && !selectable(&rows[row]))
        ++row;
    if (pressed & (BIT(BTN_UP) | BIT(BTN_DOWN)))
    {
        const int step = pressed & BIT(BTN_UP) ? -1 : 1;
        int r = row;
        do
            r += step;
        while (r >= 0 && r < count && !selectable(&rows[r]));
        if (r >= 0 && r < count)
        {
            row = r;
            sfx_play(SFX_CLICK);
        }
    }
    const Row *r = &rows[row];
    if (selectable(r))
    {
        if (r->kind == K_CHOICE && (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_CROSS))))
        {
            const int step = pressed & BIT(BTN_LEFT) ? -1 : 1;
            choice_set(r, (choice_index(r) + step + r->count) % r->count);
            changed(r);
            sfx_play(SFX_CLICK);
        }
        else if (r->kind == K_TOGGLE && (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_CROSS))))
        {
            bool *b = (bool *)((char *)&app.settings + r->field);
            *b = !*b;
            changed(r);
            sfx_play(SFX_CLICK);
        }
        else if (r->kind == K_ACTION && (pressed & BIT(BTN_CROSS)))
        {
            if (r->action == A_CHECK_UPDATE && update_state() == UPDATE_AVAILABLE)
                update_install();
            else if (r->action == A_REMOVE_CATALOG)
            {
                char body[400], name[CUSTOM_TEXT];
                remove_index = row - 1;
                custom_name(remove_index, name, sizeof(name));
                snprintf(body, sizeof(body),
                         tr("%s goes from Solanum's catalogs, and its apps from the lists. Apps you installed from it "
                            "stay installed."),
                         name);
                confirm_open(ASK_REMOVE_CATALOG, tr("Remove this catalog?"), body, tr("Remove"), NULL, false);
            }
            else
                run_action(r->action);
            sfx_play(SFX_SELECT);
        }
    }
    count = tab_rows(tab, &rows);
    if (row >= count)
        row = count - 1;
    draw_settings(rows, count);
    if (keyboard_active())
        keyboard_draw(typed);
}
