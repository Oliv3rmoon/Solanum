/*
 * Solanum - the store's tabs (L1 / R1): Browse (categories, the Nightshade family first, and search), Updates
 * (each installed app with a newer release, its changelog beside it), Installed (where each app lives and how
 * big it is) and About. The catalog's loading screen, with one of its tips; and two hidden ways to one of its
 * hidden lines: the Konami code on Browse, and the About page's version line pressed seven times.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../update.h"
#include "draw.h"
#include "icons.h"
#include "keyboard.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LIST_X (TH_MARGIN + 40)
#define CONTENT_Y 236.0f
#define STRIP_Y 944.0f /* the loading strip, above the hints */

/* ---------------------------------------------------------------- shared pieces */

static const int CATEGORY_ICONS[CATEGORY_COUNT] = {ICON_DEVICE_GAMEPAD_2, ICON_DEVICE_TV, ICON_WIFI,
                                                   ICON_PLAYER_PLAY,      ICON_SETTINGS,  ICON_CATEGORY};

/* An app's tile: its initial on Nightshade's red, the category's sign in a corner. */
void app_draw_tile(const CatalogApp *a, float x, float y, float size, float alpha)
{
    const uint32_t colors[2] = {argb_alpha(0xff5a1419u, alpha), argb_alpha(0xff1a0a0du, alpha)};
    const float stops[2] = {0.0f, 1.0f};
    draw_rrect(x, y, size, size, size * 0.2f, argb_alpha(0xff2a0d10u, alpha));
    draw_vgradient(x + size * 0.06f, y + size * 0.06f, size * 0.88f, size * 0.88f, colors, stops, 2);
    draw_rrect_outline(x, y, size, size, size * 0.2f, 2, argb_alpha(0x80e5484du, alpha));
    char initial[8] = "?";
    const unsigned char first = (unsigned char)a->name[0];
    if (first && first < 0x80)
        snprintf(initial, sizeof(initial), "%c", toupper(first));
    text_draw(x + size * 0.5f, y + size * 0.14f, size * 0.62f, FONT_BOLD, argb_alpha(0xffffffffu, alpha),
              ALIGN_CENTER, initial);
    if (a->category >= 0 && a->category < CATEGORY_COUNT)
        icon_draw(CATEGORY_ICONS[a->category], x + size * 0.70f, y + size * 0.70f, size * 0.22f,
                  argb_alpha(0xfff3a9adu, alpha));
}

/* Eight dots going round. */
void app_draw_spinner(float cx, float cy, float r, uint32_t argb)
{
    const float t = (float)(plat_ticks_us() % 1000000ull) / 1e6f;
    for (int i = 0; i < 8; ++i)
    {
        const float angle = (float)i / 8.0f * 6.2831853f;
        float fade = fmodf(t * 8.0f - (float)i + 16.0f, 8.0f) / 8.0f;
        fade = 1.0f - fade;
        draw_circle(cx + cosf(angle) * r, cy + sinf(angle) * r, r * 0.22f, argb_alpha(argb, 0.25f + 0.75f * fade));
    }
}

/* "2026-10-29" from GitHub's "2026-10-29T19:18:39Z". */
void app_release_date(const char *published, char *out, size_t size)
{
    snprintf(out, size, "%.10s", published);
}

/* What the list says of an app on its right: an install under way, an update, installed, or to get. Its colour
 * in *argb; "" for nothing. */
const char *app_status(int index, uint32_t *argb)
{
    static char text[96];
    const StoreApp *s = store_app(index);
    *argb = TH_TEXT_DIM;
    if (app.job.app == index && app.job.phase != JOB_IDLE && app.job.phase < JOB_INSTALLED)
    {
        *argb = TH_FOCUS;
        if (app.job.phase == JOB_DOWNLOADING && app.job.total)
            snprintf(text, sizeof(text), tr("Downloading %d%%"), (int)(app.job.done * 100 / app.job.total));
        else
            snprintf(text, sizeof(text), "%s", tr("Installing..."));
        return text;
    }
    if (!s->looked)
        return "";
    if (s->installed.folder[0])
    {
        if (s->installed.image)
            return tr("Installed (image)");
        if (s->checked && s->offer == OFFER_UPDATE)
        {
            *argb = TH_FOCUS;
            snprintf(text, sizeof(text), tr("Update to %s"), s->release.tag);
            return text;
        }
        *argb = TH_GOOD;
        snprintf(text, sizeof(text), tr("Installed %s"), s->installed.version);
        return text;
    }
    if (!s->checked)
        return tr("Asking GitHub...");
    if (s->offer == OFFER_GET)
    {
        *argb = TH_TEXT_SOFT;
        snprintf(text, sizeof(text), tr("Get %s"), s->release.tag);
        return text;
    }
    return tr("No release to install");
}

static void draw_header(void)
{
    const float y = 54;
    draw_rrect(TH_MARGIN, y + 6, 10, 34, 5, TH_FOCUS);
    text_draw(TH_MARGIN + 26, y, 40, FONT_BOLD, TH_TEXT, ALIGN_LEFT, NP_TITLE);
    text_draw(TH_MARGIN + 26 + text_width(40, FONT_BOLD, NP_TITLE) + 18, y + 13, 24, FONT_REGULAR, TH_TEXT_DIM,
              ALIGN_LEFT, tr("a Nightshade app"));
    char right[160], clock[32];
    plat_clock(clock, sizeof(clock));
    const Catalog *catalog = store_catalog();
    if (store_catalog_state() == CATALOG_COPY)
        snprintf(right, sizeof(right), "%s   %s", tr("Offline: the catalog from your last visit"), clock);
    else if (catalog->app_count == 1)
        snprintf(right, sizeof(right), "%s   %s", tr("1 app"), clock);
    else
        snprintf(right, sizeof(right), "%d %s   %s", catalog->app_count, tr("apps"), clock);
    text_draw(plat_width() - TH_MARGIN, y + 8, 28, FONT_REGULAR,
              store_catalog_state() == CATALOG_COPY ? TH_GOLD : TH_TEXT_DIM, ALIGN_RIGHT, right);
    if (update_state() == UPDATE_INSTALLED)
        text_draw(plat_width() - TH_MARGIN, y + 50, 22, FONT_REGULAR, TH_GOOD, ALIGN_RIGHT,
                  tr("A newer Solanum is installed: restart Solanum to use it"));
}

static const char *const TAB_NAMES[TAB_COUNT] = {"Browse", "Updates", "Installed", "About"};
static const int TAB_ICONS[TAB_COUNT] = {ICON_LAYOUT_GRID, ICON_DOWNLOAD, ICON_FOLDER, ICON_INFO_CIRCLE};

static void draw_tabs(void)
{
    float tx = LIST_X;
    const float ty = 138;
    draw_pad_glyph(GLYPH_L1, tx + 22, ty + 28, 26);
    tx += 64;
    for (int i = 0; i < TAB_COUNT; ++i)
    {
        char name[48];
        const int updates = i == TAB_UPDATES ? store_update_count() : 0;
        if (updates)
            snprintf(name, sizeof(name), "%s  %d", tr(TAB_NAMES[i]), updates);
        else
            snprintf(name, sizeof(name), "%s", tr(TAB_NAMES[i]));
        const float tw = text_width(26, FONT_BOLD, name) + 96;
        const bool on = i == app.tab;
        draw_rrect(tx, ty, tw, 56, 28, on ? TH_FOCUS : TH_PILL_A(0xd0));
        icon_draw(TAB_ICONS[i], tx + 20, ty + 13, 30, on ? 0xffffffffu : TH_TEXT_DIM);
        text_draw(tx + 60, ty + 14, 26, FONT_BOLD, on ? 0xffffffffu : TH_TEXT_SOFT, ALIGN_LEFT, name);
        if (updates && !on)
            draw_circle(tx + tw - 10, ty + 8, 7, TH_FOCUS);
        tx += tw + 14;
    }
    draw_pad_glyph(GLYPH_R1, tx + 30, ty + 28, 26);
}

/* While the catalog or the releases load, or an app installs: what is happening, and a tip. */
static void draw_strip(void)
{
    const char *what = NULL;
    if (store_catalog_busy())
        what = tr("Updating the catalog...");
    else if (store_checking())
        what = tr("Asking GitHub for the newest releases...");
    else if (app.job.app >= 0 && app.job.phase != JOB_IDLE && app.job.phase < JOB_INSTALLED)
        what = tr("Installing...");
    if (!what)
        return;
    const float x = LIST_X, y = STRIP_Y, w = plat_width() - 2 * LIST_X;
    app_draw_spinner(x + 14, y + 16, 12, TH_FOCUS);
    text_draw(x + 44, y + 2, 24, FONT_BOLD, TH_TEXT_SOFT, ALIGN_LEFT, what);
    const char *tip = app_tip();
    if (tip[0])
    {
        const float tx = x + 64 + text_width(24, FONT_BOLD, what);
        text_draw_fit(tx, y + 2, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, x + w - tx, tip);
    }
}

/* A list's row: its tile, name and summary, and its status at the right. */
static void draw_app_row(int index, float x, float y, float w, float h, bool focused)
{
    const Catalog *catalog = store_catalog();
    const CatalogApp *a = &catalog->apps[index];
    if (focused)
    {
        draw_rrect(x, y, w, h - 10, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rect(x, y + 18, 5, h - 46, TH_FOCUS);
    }
    const float tile = h - 34;
    app_draw_tile(a, x + 22, y + 12, tile, 1.0f);
    uint32_t status_argb;
    const char *status = app_status(index, &status_argb);
    const float status_w = status[0] ? text_width(24, FONT_BOLD, status) : 0;
    const float tx = x + 22 + tile + 24, room = w - (tx - x) - status_w - 60;
    text_draw_fit(tx, y + 14, 30, FONT_BOLD, focused ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT, room, a->name);
    float bx = tx + text_width(30, FONT_BOLD, a->name) + 16;
    if (a->attested && bx + 140 < tx + room)
    {
        icon_draw(ICON_CIRCLE_CHECK, bx, y + 20, 24, TH_GOOD);
        bx += 30;
        text_draw(bx, y + 20, 20, FONT_REGULAR, TH_GOOD, ALIGN_LEFT, tr("attested"));
    }
    text_draw_fit(tx, y + 56, 23, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, room, a->summary);
    if (status[0])
        text_draw(x + w - 28, y + (h - 10) * 0.5f - 14, 24, FONT_BOLD, status_argb, ALIGN_RIGHT, status);
}

/* ---------------------------------------------------------------- Browse */

enum
{
    SECTION_SEARCH,
    SECTION_FAMILY,
    SECTION_ALL,
    SECTION_CATEGORY
};

typedef struct
{
    int kind;
    int category;
    int count;
} Section;

static int section, row, list_top;
static bool in_apps;
static char query[64];
static Konami konami;
static int about_presses;

static bool contains(const char *text, const char *wanted)
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

static bool in_section(const CatalogApp *a, const Section *s)
{
    switch (s->kind)
    {
    case SECTION_SEARCH:
        return contains(a->name, query) || contains(a->summary, query) || contains(a->id, query) ||
               contains(catalog_category_title(a->category), query);
    case SECTION_FAMILY: return a->family;
    case SECTION_ALL: return true;
    default: return a->category == s->category;
    }
}

/* The sections Browse lists: Search, the Nightshade family, every app, then each category that has apps. */
static int sections(Section *out)
{
    const Catalog *catalog = store_catalog();
    int n = 0, family = 0, by[CATEGORY_COUNT] = {0};
    for (int i = 0; i < catalog->app_count; ++i)
    {
        family += catalog->apps[i].family;
        ++by[catalog->apps[i].category];
    }
    out[n++] = (Section){SECTION_SEARCH, -1, 0};
    if (family)
        out[n++] = (Section){SECTION_FAMILY, -1, family};
    out[n++] = (Section){SECTION_ALL, -1, catalog->app_count};
    for (int c = 0; c < CATEGORY_COUNT; ++c)
        if (by[c])
            out[n++] = (Section){SECTION_CATEGORY, c, by[c]};
    for (int i = 0; i < n; ++i)
        if (out[i].kind == SECTION_SEARCH)
            for (int a = 0; a < catalog->app_count; ++a)
                out[i].count += in_section(&catalog->apps[a], &out[i]);
    return n;
}

/* A section's apps: the Nightshade family first, then the catalog's order. */
static int section_apps(const Section *s, int *list)
{
    const Catalog *catalog = store_catalog();
    int n = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < catalog->app_count; ++i)
            if (catalog->apps[i].family == (pass == 0) && in_section(&catalog->apps[i], s))
                list[n++] = i;
    return n;
}

static void section_label(const Section *s, char *out, size_t size)
{
    switch (s->kind)
    {
    case SECTION_SEARCH:
        if (query[0])
            snprintf(out, size, "\"%s\"", query);
        else
            snprintf(out, size, "%s", tr("Search"));
        break;
    case SECTION_FAMILY: snprintf(out, size, "%s", tr("Nightshade family")); break;
    case SECTION_ALL: snprintf(out, size, "%s", tr("All apps")); break;
    default: snprintf(out, size, "%s", tr(catalog_category_title(s->category))); break;
    }
}

static void browse(uint32_t pressed)
{
    Section list_sections[CATEGORY_COUNT + 3];
    int list[CATALOG_MAX_APPS];
    const int count = sections(list_sections);
    if (section >= count)
        section = count - 1;
    if (keyboard_active())
    {
        bool done = false;
        if (keyboard_input(pressed, query, sizeof(query), &done) && done)
        {
            section = 0;
            row = list_top = 0;
            in_apps = section_apps(&list_sections[0], list) > 0;
        }
        pressed = 0;
    }
    int n = section_apps(&list_sections[section], list);
    if (row >= n)
        row = n ? n - 1 : 0;

    if (pressed & BIT(BTN_TRIANGLE))
    {
        keyboard_open(query);
        section = 0;
        in_apps = false;
        sfx_play(SFX_SELECT);
    }
    else if (!in_apps)
    {
        if (pressed & (BIT(BTN_UP) | BIT(BTN_DOWN)))
        {
            const int to = section + (pressed & BIT(BTN_UP) ? -1 : 1);
            if (to >= 0 && to < count)
            {
                section = to;
                row = list_top = 0;
                sfx_play(SFX_CLICK);
            }
        }
        else if ((pressed & BIT(BTN_CROSS)) && list_sections[section].kind == SECTION_SEARCH)
        {
            keyboard_open(query);
            sfx_play(SFX_SELECT);
        }
        else if ((pressed & (BIT(BTN_RIGHT) | BIT(BTN_CROSS))) && n > 0)
        {
            in_apps = true;
            sfx_play(SFX_CLICK);
        }
    }
    else
    {
        if (pressed & (BIT(BTN_UP) | BIT(BTN_DOWN)))
        {
            const int to = row + (pressed & BIT(BTN_UP) ? -1 : 1);
            if (to >= 0 && to < n)
            {
                row = to;
                sfx_play(SFX_CLICK);
            }
        }
        else if (pressed & (BIT(BTN_LEFT) | BIT(BTN_CIRCLE)))
        {
            in_apps = false;
            sfx_play(SFX_BACK);
        }
        else if ((pressed & BIT(BTN_CROSS)) && n > 0)
        {
            sfx_play(SFX_SELECT);
            page_open(list[row]);
            return;
        }
    }
    n = section_apps(&list_sections[section], list);

    /* the sections */
    const float sx = LIST_X, sw = 400, sh = 66;
    draw_rrect(sx - 16, CONTENT_Y - 16, sw + 32, count * sh + 32, TH_RADIUS, TH_CARD_A(0xe6));
    for (int i = 0; i < count; ++i)
    {
        const Section *s = &list_sections[i];
        const float y = CONTENT_Y + i * sh;
        const bool on = i == section;
        char label[96], number[16];
        if (on)
        {
            draw_rrect(sx, y, sw, sh - 8, TH_RADIUS_SMALL, in_apps ? TH_PILL : TH_ROW_SELECTED);
            if (!in_apps)
                draw_rect(sx, y + 16, 5, sh - 40, TH_FOCUS);
        }
        const int icon = s->kind == SECTION_SEARCH   ? ICON_SEARCH
                         : s->kind == SECTION_FAMILY ? ICON_HEART
                         : s->kind == SECTION_ALL    ? ICON_LIST_DETAILS
                                                     : CATEGORY_ICONS[s->category];
        icon_draw(icon, sx + 22, y + 15, 28, on ? TH_FOCUS : TH_TEXT_DIM);
        section_label(s, label, sizeof(label));
        text_draw_fit(sx + 66, y + 15, 26, on ? FONT_BOLD : FONT_REGULAR, on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT,
                      sw - 140, label);
        if (s->kind != SECTION_SEARCH || query[0])
        {
            snprintf(number, sizeof(number), "%d", s->count);
            text_draw(sx + sw - 20, y + 16, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_RIGHT, number);
        }
    }

    /* the apps */
    const float ax = LIST_X + sw + 56, aw = plat_width() - LIST_X - ax, ah = 118;
    const int visible = 6;
    list_top = list_top_row(list_top, row, visible, n);
    if (n == 0)
    {
        const char *empty = list_sections[section].kind == SECTION_SEARCH && query[0]
                                ? tr("No app matches your search.")
                                : list_sections[section].kind == SECTION_SEARCH ? tr("Press Cross to search.")
                                                                                : tr("Nothing here yet.");
        text_draw(ax + aw * 0.5f, CONTENT_Y + 180, 30, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, empty);
    }
    for (int i = list_top; i < n && i < list_top + visible; ++i)
        draw_app_row(list[i], ax, CONTENT_Y - 8 + (i - list_top) * ah, aw, ah, in_apps && i == row);
    if (list_top > 0)
        icon_draw(ICON_CHEVRON_UP, ax + aw * 0.5f - 16, CONTENT_Y - 46, 32, TH_TEXT_DIM);
    if (list_top + visible < n)
        icon_draw(ICON_CHEVRON_DOWN, ax + aw * 0.5f - 16, CONTENT_Y - 8 + visible * ah, 32, TH_TEXT_DIM);

    if (keyboard_active())
    {
        keyboard_draw(query);
        return;
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_START};
    static const char *const labels[] = {"Open", "Search", "Refresh", "Settings"};
    app_draw_hints(glyphs, labels, 4, "Open-source homebrew only");
}

/* ---------------------------------------------------------------- Updates and Installed */

static int updates_row, updates_top, installed_row, installed_top;
static int notes_scroll, notes_lines, notes_for = -1;

static void draw_notes(int index, float x, float y, float w, float h)
{
    const StoreApp *s = store_app(index);
    char title[120], date[16], size[32];
    draw_rrect(x, y, w, h, TH_RADIUS, TH_CARD_A(0xe6));
    if (notes_for != index)
    {
        notes_for = index;
        notes_scroll = 0;
    }
    app_release_date(s->release.published, date, sizeof(date));
    app_format_size(s->release.size, size, sizeof(size));
    snprintf(title, sizeof(title), tr("What's new in %s"), s->release.tag);
    text_draw_fit(x + 32, y + 26, 30, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w - 64, title);
    char line[160];
    snprintf(line, sizeof(line), "%s  ·  %s  ·  %s", date, size,
             s->release.from_sums ? tr("checked against SHA256SUMS") : tr("checked against GitHub's digest"));
    text_draw_fit(x + 32, y + 70, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - 64, line);
    draw_rect(x + 32, y + 110, w - 64, 2, TH_DIVIDER);
    const char *notes = s->release.notes[0] ? s->release.notes : tr("The release has no notes.");
    app_draw_wrapped(notes, x + 32, y + 130, w - 64, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT, y + h - 60,
                     notes_scroll, &notes_lines);
    if (s->release.notes_truncated && notes_scroll + 10 >= notes_lines)
        text_draw_fit(x + 32, y + h - 48, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - 64,
                      tr("The rest is on the release's page on GitHub."));
    if (notes_scroll > 0)
        icon_draw(ICON_CHEVRON_UP, x + w - 56, y + 120, 28, TH_TEXT_DIM);
}

static void scroll_notes(uint32_t pressed)
{
    if (pressed & BIT(BTN_L2))
        notes_scroll = notes_scroll > 3 ? notes_scroll - 3 : 0;
    if ((pressed & BIT(BTN_R2)) && notes_scroll + 4 < notes_lines)
        notes_scroll += 3;
}

static void updates(uint32_t pressed)
{
    const Catalog *catalog = store_catalog();
    int list[CATALOG_MAX_APPS], n = 0;
    for (int i = 0; i < catalog->app_count; ++i)
        if (store_app(i)->checked && store_app(i)->offer == OFFER_UPDATE)
            list[n++] = i;
    if (updates_row >= n)
        updates_row = n ? n - 1 : 0;
    if ((pressed & (BIT(BTN_UP) | BIT(BTN_DOWN))) && n)
    {
        const int to = updates_row + (pressed & BIT(BTN_UP) ? -1 : 1);
        if (to >= 0 && to < n)
        {
            updates_row = to;
            sfx_play(SFX_CLICK);
        }
    }
    scroll_notes(pressed);
    if ((pressed & BIT(BTN_CROSS)) && n)
    {
        /* the changelog was on screen beside it; the page shows the install as it goes */
        sfx_play(SFX_SELECT);
        app_request(REQUEST_INSTALL, list[updates_row]);
        page_open(list[updates_row]);
        return;
    }
    if ((pressed & BIT(BTN_SQUARE)) && n)
    {
        sfx_play(SFX_SELECT);
        page_open(list[updates_row]);
        return;
    }

    const float x = LIST_X, w = 760, h = 118;
    const int visible = 5;
    if (n == 0)
    {
        const char *empty = store_checking() ? tr("Asking GitHub for the newest releases...")
                                             : tr("Every installed app is up to date.");
        text_draw(plat_width() * 0.5f, CONTENT_Y + 200, 32, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, empty);
    }
    else
    {
        updates_top = list_top_row(updates_top, updates_row, visible, n);
        draw_rrect(x - 16, CONTENT_Y - 16, w + 32, visible * h + 22, TH_RADIUS, TH_CARD_A(0xe6));
        for (int i = updates_top; i < n && i < updates_top + visible; ++i)
            draw_app_row(list[i], x, CONTENT_Y + (i - updates_top) * h, w, h, i == updates_row);
        draw_notes(list[updates_row], x + w + 56, CONTENT_Y - 16, plat_width() - LIST_X - (x + w + 56), 690);
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_R2, GLYPH_START};
    static const char *const labels[] = {"Update", "App page", "Scroll the notes", "Settings"};
    app_draw_hints(glyphs, labels, n ? 4 : 0, n ? NULL : "");
}

static void installed(uint32_t pressed)
{
    const Catalog *catalog = store_catalog();
    int list[CATALOG_MAX_APPS], n = 0, looked = 0;
    for (int i = 0; i < catalog->app_count; ++i)
    {
        looked += store_app(i)->looked;
        if (store_app(i)->looked && store_app(i)->installed.folder[0])
            list[n++] = i;
    }
    if (installed_row >= n)
        installed_row = n ? n - 1 : 0;
    if ((pressed & (BIT(BTN_UP) | BIT(BTN_DOWN))) && n)
    {
        const int to = installed_row + (pressed & BIT(BTN_UP) ? -1 : 1);
        if (to >= 0 && to < n)
        {
            installed_row = to;
            sfx_play(SFX_CLICK);
        }
    }
    if ((pressed & BIT(BTN_CROSS)) && n)
    {
        sfx_play(SFX_SELECT);
        page_open(list[installed_row]);
        return;
    }

    const float x = LIST_X, w = plat_width() - 2 * LIST_X, h = 112;
    const int visible = 6;
    if (n == 0)
    {
        const char *empty = looked < catalog->app_count ? tr("Looking for the catalog's apps on the console...")
                                                        : tr("None of the catalog's apps is installed yet.");
        text_draw(plat_width() * 0.5f, CONTENT_Y + 200, 32, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, empty);
        if (app.sandboxed && looked >= catalog->app_count)
            text_draw(plat_width() * 0.5f, CONTENT_Y + 256, 24, FONT_REGULAR, TH_GOLD, ALIGN_CENTER,
                      tr("Solanum couldn't look through /data (is the HEN running?), so it may not see them all."));
    }
    else
    {
        installed_top = list_top_row(installed_top, installed_row, visible, n);
        draw_rrect(x - 16, CONTENT_Y - 16, w + 32, visible * h + 22, TH_RADIUS, TH_CARD_A(0xe6));
        for (int i = installed_top; i < n && i < installed_top + visible; ++i)
        {
            const int index = list[i];
            const StoreApp *s = store_app(index);
            const CatalogApp *a = &catalog->apps[index];
            const float y = CONTENT_Y + (i - installed_top) * h;
            const bool on = i == installed_row;
            if (on)
            {
                draw_rrect(x, y, w, h - 10, TH_RADIUS_SMALL, TH_ROW_SELECTED);
                draw_rect(x, y + 18, 5, h - 46, TH_FOCUS);
            }
            app_draw_tile(a, x + 22, y + 12, h - 34, 1.0f);
            const float tx = x + 22 + h - 34 + 24;
            char version[64], size[32], where[300];
            snprintf(version, sizeof(version), "%s %s", tr("Version"), s->installed.version[0] ? s->installed.version : "?");
            text_draw_fit(tx, y + 12, 30, FONT_BOLD, on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT, 520, a->name);
            text_draw(tx + 540, y + 16, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT, version);
            if (s->sized)
                app_format_size(s->size, size, sizeof(size));
            else
                snprintf(size, sizeof(size), "%s", s->installed.image ? tr("an image") : tr("size unknown"));
            text_draw(x + w - 28, y + 16, 24, FONT_BOLD, TH_TEXT_SOFT, ALIGN_RIGHT, size);
            snprintf(where, sizeof(where), "%s %s", s->installed.image ? tr("Mounted from an image, registered in")
                                                                       : tr("Lives in"),
                     s->installed.folder);
            text_draw_fit(tx, y + 58, 22, FONT_MONO, TH_TEXT_DIM, ALIGN_LEFT, w - (tx - x) - 40, where);
        }
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_START};
    static const char *const labels[] = {"App page", "Refresh", "Settings"};
    app_draw_hints(glyphs, labels, 3, NULL);
}

/* ---------------------------------------------------------------- About */

static void about(uint32_t pressed)
{
    const Catalog *catalog = store_catalog();
    /* The version line, pressed seven times: one of the catalog's hidden lines. */
    if (pressed & BIT(BTN_CROSS))
    {
        sfx_play(SFX_CLICK);
        if (++about_presses >= 7)
        {
            about_presses = 0;
            app_show_egg();
        }
    }
    const float x = LIST_X, w = plat_width() - 2 * LIST_X, y = CONTENT_Y;
    draw_rrect(x - 16, y - 16, w + 32, 560, TH_RADIUS, TH_CARD_A(0xe6));
    /* the version line: the one row that can be chosen */
    draw_rrect(x, y, w, 70, TH_RADIUS_SMALL, TH_ROW_SELECTED);
    draw_rect(x, y + 16, 5, 38, TH_FOCUS);
    char version[160];
    snprintf(version, sizeof(version), "%s %s  ·  %s", NP_TITLE, NP_VERSION,
             tr("a Nightshade app by Oliv3rmoon. Free software: GPL-3.0-or-later."));
    text_draw_fit(x + 28, y + 19, 28, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w - 56, version);
    const char *lines[] = {
        "Solanum installs and updates open-source PS5 homebrew from an open catalog, "
        "github.com/Oliv3rmoon/Solanum-Catalog. Anyone can add an app there with a pull request.",
        catalog->about,
        "Open-source homebrew only: every app links its source and its license. Solanum never offers game files, "
        "and nothing it lists downloads them.",
        "Every app comes from its own GitHub release and is checked against the release's checksum file before "
        "anything is unpacked. New apps go beside Solanum (/data/homebrew/<title ID>).",
        "Built on Dulcamara and Mandrake (formerly Nightshade Station): BlackBearReloaded's "
        "ps5-native-app-boilerplate. Installs with Nightshade's update helper, opens apps with its launch "
        "payload (after Mihawk's). Licenses: the app's licenses folder.",
    };
    float ly = y + 100;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i)
    {
        if (!lines[i][0])
            continue;
        int total = 0;
        app_draw_wrapped(tr(lines[i]), x + 28, ly, w - 56, 25, FONT_REGULAR, i == 1 ? TH_TEXT_SOFT : TH_TEXT_DIM,
                         ALIGN_LEFT, y + 530, 0, &total);
        ly += total * 25 * 1.42f + 18;
    }
    /* the catalog's footer */
    if (catalog->about_footer[0])
    {
        draw_rect(plat_width() * 0.5f - 40, y + 574, 80, 2, TH_FOCUS);
        app_draw_wrapped(catalog->about_footer, x, y + 592, w, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, y + 660,
                         0, NULL);
    }
    static const int glyphs[] = {GLYPH_SQUARE, GLYPH_START};
    static const char *const labels[] = {"Refresh", "Settings"};
    app_draw_hints(glyphs, labels, 2, NP_TITLE " " NP_VERSION);
}

/* ---------------------------------------------------------------- loading */

static void loading(uint32_t pressed)
{
    const CatalogState state = store_catalog_state();
    const float cx = plat_width() * 0.5f, cy = plat_height() * 0.42f;
    text_draw(cx, cy - 150, 72, FONT_BOLD, TH_TEXT, ALIGN_CENTER, NP_TITLE);
    text_draw(cx, cy - 64, 26, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("a Nightshade app"));
    if (state == CATALOG_FAILED && !store_catalog_busy())
    {
        icon_draw(ICON_WIFI_OFF, cx - 32, cy + 10, 64, TH_FOCUS);
        text_draw(cx, cy + 100, 30, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("The catalog couldn't be loaded."));
        text_draw(cx, cy + 150, 26, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, store_catalog_error());
        if (pressed & BIT(BTN_CROSS))
        {
            sfx_play(SFX_SELECT);
            app_request(REQUEST_REFRESH, -1);
        }
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_START};
        static const char *const labels[] = {"Try again", "Settings"};
        app_draw_hints(glyphs, labels, 2, NP_TITLE " " NP_VERSION);
        return;
    }
    app_draw_spinner(cx, cy + 40, 30, TH_FOCUS);
    text_draw(cx, cy + 110, 30, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_CENTER, tr("Loading the catalog..."));
    const char *tip = app_tip();
    if (tip[0])
    {
        draw_rect(cx - 300, cy + 190, 600, 2, TH_DIVIDER);
        text_draw(cx, cy + 216, 22, FONT_BOLD, TH_FOCUS, ALIGN_CENTER, tr("TIP"));
        app_draw_wrapped(tip, cx - 620, cy + 256, 1240, 28, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_CENTER, cy + 420, 0,
                         NULL);
    }
    app_draw_hints(NULL, NULL, 0, NP_TITLE " " NP_VERSION);
}

/* ---------------------------------------------------------------- the screen */

void store_screen_opened(void)
{
    konami_reset(&konami);
    about_presses = 0;
}

void store_screen(uint32_t pressed)
{
    app_backdrop();
    const CatalogState state = store_catalog_state();
    const bool shown = state == CATALOG_READY || state == CATALOG_COPY;
    if ((pressed & BIT(BTN_START)) && !keyboard_active())
    {
        sfx_play(SFX_SELECT);
        app_open_settings();
        return;
    }
    if (!shown)
    {
        loading(pressed);
        return;
    }
    /* Browse's hidden one: the Konami code. Its last press is the code's. */
    if (app.tab == TAB_BROWSE && !keyboard_active() && konami_feed(&konami, pressed))
    {
        app_show_egg();
        pressed = 0;
    }
    if ((pressed & (BIT(BTN_L1) | BIT(BTN_R1))) && !keyboard_active())
    {
        app.tab = (app.tab + (pressed & BIT(BTN_R1) ? 1 : TAB_COUNT - 1)) % TAB_COUNT;
        about_presses = 0;
        konami_reset(&konami);
        sfx_play(SFX_CLICK);
        pressed &= ~(BIT(BTN_L1) | BIT(BTN_R1));
    }
    if ((pressed & BIT(BTN_SQUARE)) && app.tab != TAB_UPDATES && !keyboard_active())
    {
        sfx_play(SFX_SELECT);
        app_request(REQUEST_REFRESH, -1);
        app_toast("Asking for the catalog and the newest releases again.");
        pressed &= ~BIT(BTN_SQUARE);
    }
    draw_header();
    draw_tabs();
    switch (app.tab)
    {
    case TAB_BROWSE: browse(pressed); break;
    case TAB_UPDATES: updates(pressed); break;
    case TAB_INSTALLED: installed(pressed); break;
    default: about(pressed); break;
    }
    if (!keyboard_active())
        draw_strip();
}
