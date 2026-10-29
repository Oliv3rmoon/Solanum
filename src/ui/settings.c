/*
 * Solanum - Settings (OPTIONS): tabs (L1 / R1) of rows (up / down), each changed with left / right or Cross
 * (Dulcamara's, with the store's rows).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../config.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../update.h"
#include "draw.h"
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

static const Row INTERFACE_ROWS[] = {
    {"Interface sounds", "The clicks of the lists and the menus.", K_CHOICE, FIELD(ui_sound), UI_SOUNDS,
     N(UI_SOUNDS), NULL, A_NONE},
    {"Interface volume", "How loud those clicks are.", K_CHOICE, FIELD(ui_volume), UI_VOLUMES, N(UI_VOLUMES), NULL,
     A_NONE},
};

static const Row STORE_ROWS[] = {
    {"Refresh the catalog", "Asks for the catalog and every app's newest release again (Square on the store does "
                            "too). GitHub allows 60 requests an hour from one address.",
     K_ACTION, 0, NULL, 0, NULL, A_REFRESH},
    {"Apps are checked against their release's checksum file before anything is unpacked.", NULL, K_INFO, 0, NULL,
     0, NULL, A_NONE},
    {"New apps go beside Solanum: /data/homebrew/<title ID> when Solanum is in /data/homebrew.", NULL, K_INFO, 0,
     NULL, 0, NULL, A_NONE},
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

static const Tab TABS[] = {
    {"Interface", ICON_VOLUME, INTERFACE_ROWS, N(INTERFACE_ROWS)},
    {"Store", ICON_LAYOUT_GRID, STORE_ROWS, N(STORE_ROWS)},
    {"System", ICON_SETTINGS, SYSTEM, N(SYSTEM)},
};
#define TAB_COUNT N(TABS)

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
    (void)r;
    app_apply_settings();
}

static void run_action(int action)
{
    switch (action)
    {
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
    case UPDATE_FAILED: return update_message();
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

void settings_screen(uint32_t pressed)
{
    const Tab *t = &TABS[tab];
    if (pressed & (BIT(BTN_CIRCLE) | BIT(BTN_START)))
    {
        leave();
        app_backdrop();
        return;
    }
    if (pressed & (BIT(BTN_L1) | BIT(BTN_R1)))
    {
        tab = (tab + (pressed & BIT(BTN_R1) ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
        t = &TABS[tab];
        row = top = 0;
        bar_y = -1.0f;
        sfx_play(SFX_CLICK);
    }
    /* the first row that can be chosen */
    while (row < t->count - 1 && !selectable(&t->rows[row]))
        ++row;
    if (pressed & (BIT(BTN_UP) | BIT(BTN_DOWN)))
    {
        const int step = pressed & BIT(BTN_UP) ? -1 : 1;
        int r = row;
        do
            r += step;
        while (r >= 0 && r < t->count && !selectable(&t->rows[r]));
        if (r >= 0 && r < t->count)
        {
            row = r;
            sfx_play(SFX_CLICK);
        }
    }
    const Row *r = &t->rows[row];
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
            else
                run_action(r->action);
            sfx_play(SFX_SELECT);
        }
    }

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
    top = list_top_row(top, row, visible, t->count);
    draw_rrect(x - 20, list_y - 16, w + 40, visible * row_h + 32, TH_RADIUS, TH_CARD_A(0xe6));
    const float target = list_y + (row - top) * row_h;
    if (bar_y < 0)
        bar_y = target;
    anim_approach(&bar_y, target, app.dt, TH_SNAP);
    if (selectable(r))
    {
        draw_rrect(x, bar_y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rect(x, bar_y + 16, 5, row_h - 40, TH_FOCUS);
    }
    for (int i = top; i < t->count && i < top + visible; ++i)
    {
        const Row *it = &t->rows[i];
        const float ry = list_y + (i - top) * row_h;
        const bool on = i == row && selectable(it);
        if (it->kind == K_INFO)
        {
            text_draw_fit(x + 28, ry + 22, 26, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - 56, tr(it->label));
            continue;
        }
        const float lx = x + 28;
        text_draw_fit(lx, ry + 21, 28, on ? FONT_BOLD : FONT_REGULAR, on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT,
                      w * 0.5f, tr(it->label));
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
                text_draw_fit(right, ry + 23, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_RIGHT, w * 0.45f, value);
            else
                icon_draw(ICON_CHEVRON_RIGHT, right - 34, ry + 20, 32, on ? TH_FOCUS : TH_TEXT_DIM);
        }
    }
    /* the chosen row's explanation */
    if (selectable(r) && r->help)
        text_draw_fit(x, list_y + visible * row_h + 40, 26, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w, tr(r->help));
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
    static const char *const labels[] = {"Change", "Back"};
    app_draw_hints(glyphs, labels, 2, NP_TITLE " " NP_VERSION);
}
