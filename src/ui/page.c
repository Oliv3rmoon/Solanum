/*
 * Solanum - an app's page: its name, summary and license, an "attested" badge when the catalog says its release's
 * CI attests its builds, its newest release (version, date, size, changelog), where it is installed, and
 * Install / Update / Open. An install shows here as it goes, with one of the catalog's tips.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PAGE_X (TH_MARGIN + 40)

enum Button
{
    BUTTON_INSTALL, /* Install, or Update */
    BUTTON_OPEN,
    BUTTON_PAGE_COUNT
};

static int button;
static int scroll, lines, generation;
static float progress_shown;

void page_open(int index)
{
    app.page_app = index;
    app.screen = SCREEN_PAGE;
    button = BUTTON_INSTALL;
    scroll = 0;
    progress_shown = 0;
    generation = store_generation();
}

static void back(void)
{
    app.screen = SCREEN_STORE;
    sfx_play(SFX_BACK);
}

/* What the page offers: Install (not installed), Update (an older one installed), Open (installed). */
static bool can_install(int index)
{
    const StoreApp *s = store_app(index);
    return s->looked && s->checked && (s->offer == OFFER_GET || s->offer == OFFER_UPDATE) &&
           strcmp(store_catalog()->apps[index].title_id, NP_TITLE_ID) != 0;
}

static bool can_open(int index)
{
    const StoreApp *s = store_app(index);
    return s->looked && s->installed.folder[0];
}

static bool this_job_running(int index)
{
    return app.job.app == index && app.job.phase != JOB_IDLE && app.job.phase < JOB_INSTALLED;
}

static void info_row(float x, float y, float w, const char *label, const char *value, uint32_t argb, int weight)
{
    text_draw(x, y, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, label);
    text_draw_fit(x + 230, y, 24, weight, argb, ALIGN_LEFT, w - 230, value);
}

static void draw_progress(int index, float x, float y, float w)
{
    const StoreJob *job = &app.job;
    char what[200], done[32], total[32];
    float fraction = 0.0f;
    (void)index;
    switch (job->phase)
    {
    case JOB_STARTING: snprintf(what, sizeof(what), "%s", tr("Starting the install helper...")); break;
    case JOB_DOWNLOADING:
        app_format_size(job->done, done, sizeof(done));
        app_format_size(job->total, total, sizeof(total));
        if (job->total)
            snprintf(what, sizeof(what), tr("Downloading  ·  %s of %s%s%s%s"), done, total,
                     job->time_left[0] ? "  ·  " : "", job->time_left, job->reused ? tr("  ·  resumed") : "");
        else
            snprintf(what, sizeof(what), tr("Downloading  ·  %s"), done);
        fraction = job->total ? (float)((double)job->done / (double)job->total) : 0.0f;
        break;
    case JOB_UNPACKING:
        snprintf(what, sizeof(what), "%s", tr("Checked against its checksum. Unpacking..."));
        fraction = job->total ? (float)((double)job->done / (double)job->total) : 0.0f;
        break;
    default: snprintf(what, sizeof(what), "%s", tr("Putting the files in place...")); fraction = 1.0f; break;
    }
    anim_approach(&progress_shown, fraction, app.dt, 8.0f);
    text_draw_fit(x, y, 26, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w, what);
    draw_rrect(x, y + 46, w, 18, 9, TH_PILL);
    if (progress_shown > 0.01f)
        draw_rrect(x, y + 46, fmaxf(18.0f, w * fminf(progress_shown, 1.0f)), 18, 9, TH_FOCUS);
    const char *tip = app_tip();
    if (tip[0])
    {
        text_draw(x, y + 90, 20, FONT_BOLD, TH_FOCUS, ALIGN_LEFT, tr("TIP"));
        app_draw_wrapped(tip, x + 56, y + 88, w - 56, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, y + 160, 0, NULL);
    }
}

void page_screen(uint32_t pressed)
{
    const Catalog *catalog = store_catalog();
    const int index = app.page_app;
    /* A new catalog may have moved or dropped the app: back to the lists. */
    if (generation != store_generation() || index < 0 || index >= catalog->app_count)
    {
        app.screen = SCREEN_STORE;
        app_backdrop();
        return;
    }
    const CatalogApp *a = &catalog->apps[index];
    const StoreApp *s = store_app(index);
    const bool running = this_job_running(index);
    const bool installable = can_install(index), openable = can_open(index);

    if (running)
    {
        if (pressed & BIT(BTN_CIRCLE))
        {
            /* until the files are being put in place, nothing is changed */
            if (app.job.phase < JOB_INSTALLING)
            {
                app_request(REQUEST_CANCEL, index);
                sfx_play(SFX_BACK);
            }
        }
    }
    else
    {
        if (pressed & BIT(BTN_CIRCLE))
        {
            back();
            app_backdrop();
            return;
        }
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT)))
        {
            button = pressed & BIT(BTN_LEFT) ? BUTTON_INSTALL : BUTTON_OPEN;
            sfx_play(SFX_CLICK);
        }
        if (button == BUTTON_INSTALL && !installable && openable)
            button = BUTTON_OPEN;
        if (button == BUTTON_OPEN && !openable && installable)
            button = BUTTON_INSTALL;
        if (pressed & BIT(BTN_CROSS))
        {
            if (button == BUTTON_INSTALL && installable)
            {
                if (app.job.app >= 0 && app.job.phase < JOB_INSTALLED)
                    app_toast("Another app is being installed. One at a time.");
                else
                {
                    sfx_play(SFX_SELECT);
                    app_request(REQUEST_INSTALL, index);
                }
            }
            else if (button == BUTTON_OPEN && openable)
            {
                sfx_play(SFX_SELECT);
                app_request(REQUEST_OPEN, index);
            }
        }
    }
    if ((pressed & BIT(BTN_L2)) || (pressed & BIT(BTN_UP)))
        scroll = scroll > 3 ? scroll - 3 : 0;
    if (((pressed & BIT(BTN_R2)) || (pressed & BIT(BTN_DOWN))) && scroll + 4 < lines)
        scroll += 3;

    app_backdrop();
    /* the app */
    const float x = PAGE_X, top = 70;
    app_draw_tile(a, x, top, 168, 1.0f);
    const float nx = x + 168 + 40;
    text_draw_fit(nx, top + 6, 56, FONT_BOLD, TH_TEXT, ALIGN_LEFT, plat_width() - nx - TH_MARGIN, a->name);
    char line[400];
    snprintf(line, sizeof(line), "%s  ·  %s  ·  github.com/%s", tr(catalog_category_title(a->category)), a->license,
             a->repo);
    text_draw_fit(nx, top + 84, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, plat_width() - nx - TH_MARGIN, line);
    /* badges */
    float bx = nx;
    if (a->attested)
    {
        const char *label = tr("Attested");
        const float bw = text_width(22, FONT_BOLD, label) + 70;
        draw_rrect(bx, top + 126, bw, 42, 21, 0x3326c281u);
        icon_draw(ICON_CIRCLE_CHECK, bx + 14, top + 133, 28, TH_GOOD);
        text_draw(bx + 50, top + 134, 22, FONT_BOLD, TH_GOOD, ALIGN_LEFT, label);
        bx += bw + 14;
    }
    if (a->family)
        bx += draw_pill(bx, top + 126, 42, 22, TH_PILL, TH_TEXT_SOFT, tr("Nightshade family")) + 14;
    if (s->looked && s->installed.folder[0])
        draw_pill(bx, top + 126, 42, 22, TH_PILL, TH_GOOD, tr("Installed"));
    app_draw_wrapped(a->summary, x, top + 210, plat_width() - 2 * x, 30, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT,
                     top + 300, 0, NULL);

    /* the facts */
    const float cx = x, cy = 400, cw = 820, ch = 340;
    draw_rrect(cx - 16, cy - 16, cw + 32, ch, TH_RADIUS, TH_CARD_A(0xe6));
    char date[16], size[32], value[400];
    float ry = cy + 8;
    if (!s->checked)
        info_row(cx + 16, ry, cw - 32, tr("Newest release"), tr("Asking GitHub..."), TH_TEXT_DIM, FONT_REGULAR);
    else if (s->reason == GITHUB_RELEASE_NEWER)
    {
        app_release_date(s->release.published, date, sizeof(date));
        app_format_size(s->release.size, size, sizeof(size));
        snprintf(value, sizeof(value), "%s  ·  %s  ·  %s", s->release.tag, date[0] ? date : "?", size);
        info_row(cx + 16, ry, cw - 32, tr("Newest release"), value, TH_TEXT, FONT_BOLD);
    }
    else
        info_row(cx + 16, ry, cw - 32, tr("Newest release"), tr(store_problem(s->reason)), TH_GOLD, FONT_REGULAR);
    ry += 52;
    if (!s->looked)
        snprintf(value, sizeof(value), "%s", tr("Looking..."));
    else if (!s->installed.folder[0])
        snprintf(value, sizeof(value), "%s", tr("Not installed"));
    else
        snprintf(value, sizeof(value), "%s%s", s->installed.version[0] ? s->installed.version : "?",
                 s->checked && s->offer == OFFER_CURRENT ? tr("  ·  the newest") : "");
    info_row(cx + 16, ry, cw - 32, tr("Installed"), value, TH_TEXT_SOFT, FONT_REGULAR);
    ry += 52;
    if (s->looked && s->installed.folder[0])
    {
        if (s->sized)
        {
            app_format_size(s->size, size, sizeof(size));
            snprintf(value, sizeof(value), "%s  (%s)", s->installed.folder, size);
        }
        else
            snprintf(value, sizeof(value), "%s%s", s->installed.folder, s->installed.image ? tr("  (an image)") : "");
    }
    else
        snprintf(value, sizeof(value), tr("Beside Solanum: /data/homebrew/%s"), a->title_id);
    info_row(cx + 16, ry, cw - 32, tr("Lives in"), value, TH_TEXT_SOFT, FONT_REGULAR);
    ry += 52;
    info_row(cx + 16, ry, cw - 32, tr("Title ID"), a->title_id, TH_TEXT_SOFT, FONT_REGULAR);
    ry += 52;
    snprintf(value, sizeof(value), "%s",
             !s->checked || s->reason != GITHUB_RELEASE_NEWER ? a->checksums
             : s->release.from_sums                           ? a->checksums
                                                              : tr("GitHub's digest (the release has no checksum file)"));
    info_row(cx + 16, ry, cw - 32, tr("Checked against"), value, TH_TEXT_SOFT, FONT_REGULAR);

    /* Install / Update and Open, or the install as it goes */
    const float by = cy + ch + 24;
    if (running)
        draw_progress(index, x, by, cw);
    else
    {
        const char *install_label = s->checked && s->offer == OFFER_UPDATE ? tr("Update") : tr("Install");
        const char *labels[BUTTON_PAGE_COUNT] = {install_label, tr("Open")};
        const int icons[BUTTON_PAGE_COUNT] = {ICON_DOWNLOAD, ICON_PLAYER_PLAY};
        const bool enabled[BUTTON_PAGE_COUNT] = {installable, openable};
        float px = x;
        for (int i = 0; i < BUTTON_PAGE_COUNT; ++i)
        {
            const float pw = 300, ph = 76;
            const bool on = i == button && enabled[i];
            draw_rrect(px, by, pw, ph, 38, on ? TH_FOCUS : enabled[i] ? TH_PILL : TH_PILL_A(0x60));
            icon_draw(icons[i], px + 40, by + 22, 32, on ? 0xffffffffu : enabled[i] ? TH_TEXT_SOFT : TH_TEXT_DIM);
            text_draw(px + 90, by + 22, 30, FONT_BOLD, on ? 0xffffffffu : enabled[i] ? TH_TEXT_SOFT : TH_TEXT_DIM,
                      ALIGN_LEFT, labels[i]);
            px += pw + 24;
        }
        const char *why = "";
        if (strcmp(a->title_id, NP_TITLE_ID) == 0)
            why = tr("This is Solanum: it updates itself (Settings).");
        else if (s->looked && s->installed.image)
            why = tr("Installed as an image: update it by replacing the image.");
        else if (s->checked && s->offer == OFFER_CURRENT)
            why = tr("The installed version is the newest.");
        else if (app.job.app >= 0 && app.job.app != index && app.job.phase < JOB_INSTALLED)
            why = tr("Another app is being installed.");
        if (why[0])
            text_draw_fit(x, by + 100, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, cw, why);
    }

    /* the changelog */
    const float nx2 = cx + cw + 56, nw = plat_width() - PAGE_X - nx2, ny = cy - 16, nh = 580;
    draw_rrect(nx2, ny, nw, nh, TH_RADIUS, TH_CARD_A(0xe6));
    if (s->checked && s->reason == GITHUB_RELEASE_NEWER)
    {
        snprintf(value, sizeof(value), tr("What's new in %s"), s->release.tag);
        text_draw_fit(nx2 + 32, ny + 26, 30, FONT_BOLD, TH_TEXT, ALIGN_LEFT, nw - 64, value);
        draw_rect(nx2 + 32, ny + 76, nw - 64, 2, TH_DIVIDER);
        app_draw_wrapped(s->release.notes[0] ? s->release.notes : tr("The release has no notes."), nx2 + 32, ny + 96,
                         nw - 64, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT, ny + nh - 56, scroll, &lines);
        if (s->release.notes_truncated && scroll + 10 >= lines)
            text_draw_fit(nx2 + 32, ny + nh - 46, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, nw - 64,
                          tr("The rest is on the release's page on GitHub."));
        if (scroll > 0)
            icon_draw(ICON_CHEVRON_UP, nx2 + nw - 56, ny + 86, 28, TH_TEXT_DIM);
    }
    else
        text_draw(nx2 + nw * 0.5f, ny + nh * 0.45f, 26, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  s->checked ? tr("No release notes to show.") : tr("Asking GitHub..."));

    if (running)
    {
        static const int glyphs[] = {GLYPH_CIRCLE, GLYPH_R2};
        static const char *const labels[] = {"Cancel", "Scroll the notes"};
        app_draw_hints(glyphs, labels, app.job.phase < JOB_INSTALLING ? 2 : 0, NULL);
    }
    else
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_R2};
        static const char *const labels[] = {"Choose", "Back", "Scroll the notes"};
        app_draw_hints(glyphs, labels, 3, NULL);
    }
}
