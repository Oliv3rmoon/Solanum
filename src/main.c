/*
 * Solanum - a store for PS5 homebrew from an open catalog, a Nightshade app.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Named after the genus at the heart of the nightshade family. It installs and updates the apps of the open
 * catalog at Oliv3rmoon/Solanum-Catalog from their own GitHub releases, each checked against its checksum file,
 * and opens them. The app: start-up, the main loop, and what the screens share (app.h). Its platform layer, its
 * Vulkan screen, its interface primitives and its own update come from Dulcamara, which has them from Mandrake
 * (formerly Nightshade Station: darkxex's SwanStationPS5 on BlackBearReloaded's ps5-native-app-boilerplate); its
 * installs and its Open button are Nightshade's Settings > Apps (store.h).
 */
#include "app.h"

#include "config.h"
#include "i18n.h"
#include "music.h"
#include "platform/platform.h"
#include "platform/ps5_crash.h"
#include "platform/vk/vk_probe.h"
#include "ui/draw.h"
#include "ui/icons.h"
#include "ui/overlay.h"
#include "ui/sfx.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "update.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_RATE 48000 /* the interface sounds' and the music's */
#define MUSIC_FADE_MS 800 /* the music going quiet as an app opens (Solanum closes 900 ms after) */

App app = {.dt = 1.0f / 60.0f, .page_app = -1, .tip = -1, .last_egg = -1};

/* ---------------------------------------------------------------- notices */

static char toast[400];
static uint64_t toast_until, toast_length = 2500000;
static char toast_queue[4][400];
static uint64_t toast_queue_us[4];
static int toast_queued;

static void show_toast(const char *text, uint64_t length_us)
{
    str_copy(toast, sizeof(toast), text);
    toast_length = length_us;
    toast_until = plat_ticks_us() + length_us;
}

void app_toast(const char *message)
{
    app_toast_for(message, 2.5f);
}

void app_toast_for(const char *message, float seconds)
{
    np_log("notice: %s", message);
    const uint64_t length = (uint64_t)(seconds * 1e6f);
    if (toast[0] && plat_ticks_us() < toast_until)
    {
        if (!strcmp(toast, message) || toast_queued == 4)
            return;
        toast_queue_us[toast_queued] = length;
        str_copy(toast_queue[toast_queued++], sizeof(toast_queue[0]), message);
        return;
    }
    show_toast(tr(message), length);
}

static void toast_render(void)
{
    const uint64_t now = plat_ticks_us();
    if (toast_queued && (!toast[0] || now > toast_until))
    {
        show_toast(toast_queue[0], toast_queue_us[0]);
        memmove(toast_queue[0], toast_queue[1], sizeof(toast_queue[0]) * 3);
        memmove(toast_queue_us, toast_queue_us + 1, sizeof(toast_queue_us[0]) * 3);
        --toast_queued;
    }
    if (!toast[0] || now > toast_until)
        return;
    const float shown = (float)(toast_length - (toast_until - now)) / 1e6f, left = (float)(toast_until - now) / 1e6f;
    const float a = left < 0.3f ? left / 0.3f : 1.0f, rise = shown < 0.15f ? (1.0f - shown / 0.15f) * 24.0f : 0.0f;
    const float size = 26;
    float w = text_width(size, FONT_REGULAR, toast) + 110;
    if (w > plat_width() - 2 * TH_MARGIN)
        w = plat_width() - 2 * TH_MARGIN;
    const float h = 64, x = (plat_width() - w) * 0.5f, y = plat_height() - 190 + rise;
    draw_rrect(x, y, w, h, 32, argb_alpha(TH_PILL_A(0xf0), a));
    draw_rrect_outline(x, y, w, h, 32, 2, argb_alpha(TH_DIVIDER, a));
    icon_draw(ICON_INFO_CIRCLE, x + 22, y + 18, 28, argb_alpha(TH_FOCUS, a));
    text_draw_fit(x + 62, y + 17, size, FONT_REGULAR, argb_alpha(TH_TEXT, a), ALIGN_LEFT, w - 90, toast);
}

/* ---------------------------------------------------------------- the message box */

static char box_text[800];
static bool box_open;
static float box_t;

void app_message_box(const char *message)
{
    np_log("message: %s", message);
    str_copy(box_text, sizeof(box_text), tr(message));
    box_open = true;
    box_t = 0.0f;
}

static bool message_box_input(uint32_t *pressed)
{
    if (!box_open)
        return false;
    if (*pressed & (BIT(BTN_CROSS) | BIT(BTN_CIRCLE)))
    {
        box_open = false;
        sfx_play(SFX_BACK);
    }
    *pressed = 0;
    return true;
}

int app_draw_wrapped(const char *m, float x, float y, float width, float size, int weight, uint32_t argb, int align,
                     float bottom, int skip, int *total)
{
    char line[512];
    int lines = 0, drawn = 0;
    const float step = size * 1.42f;
    while (*m)
    {
        /* the longest run of whole words that fits; a word longer than the width is cut */
        size_t best = 0;
        for (size_t i = 0;;)
        {
            size_t j = i;
            while (m[j] && m[j] != ' ' && m[j] != '\n')
                ++j;
            const size_t n = j < sizeof(line) - 1 ? j : sizeof(line) - 1;
            memcpy(line, m, n);
            line[n] = '\0';
            if (best > 0 && text_width(size, weight, line) > width)
                break;
            best = n;
            if (!m[j] || m[j] == '\n' || n == sizeof(line) - 1)
                break;
            i = j + 1;
        }
        memcpy(line, m, best);
        line[best] = '\0';
        if (lines >= skip && y + step <= bottom + 1)
        {
            const float lx = align == ALIGN_CENTER ? x + width * 0.5f : align == ALIGN_RIGHT ? x + width : x;
            text_draw_fit(lx, y, size, weight, argb, align, width, line);
            y += step;
            ++drawn;
        }
        ++lines;
        m += best;
        while (*m == ' ')
            ++m;
        if (*m == '\n')
            ++m;
        if (!total && y + step > bottom + 1)
            break;
    }
    if (total)
        *total = lines;
    return drawn;
}

static void message_box_draw(void)
{
    if (!box_open)
        return;
    box_t = fminf(box_t + app.dt * 8.0f, 1.0f);
    const float w = 960, h = 560, pad = 52, size = 28;
    const float x = (plat_width() - w) * 0.5f, y = (plat_height() - h) * 0.5f + (1.0f - box_t) * 24.0f;
    draw_rect(0, 0, (float)plat_width(), (float)plat_height(), argb_alpha(0xb0000000u, box_t));
    draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(TH_CARD_A(0xf8), box_t));
    draw_rect(x, y, w, 4, argb_alpha(TH_FOCUS, box_t));
    icon_draw(ICON_INFO_CIRCLE, x + (w - 56) * 0.5f, y + pad, 56, argb_alpha(TH_FOCUS, box_t));
    app_draw_wrapped(box_text, x + pad, y + pad + 84, w - 2 * pad, size, FONT_REGULAR, argb_alpha(TH_TEXT, box_t),
                     ALIGN_CENTER, y + h - 110, 0, NULL);
    const float hs = 26, gw = pad_glyph_width(GLYPH_CROSS, hs);
    const char *close = tr("Close");
    const float hw = gw + hs * 0.35f + text_width(hs, FONT_REGULAR, close);
    const float hx = x + (w - hw) * 0.5f, hy = y + h - pad - hs;
    draw_pad_glyph(GLYPH_CROSS, hx + gw * 0.5f, hy + hs * 0.6f, hs);
    text_draw(hx + gw + hs * 0.35f, hy, hs, FONT_REGULAR, argb_alpha(TH_TEXT, box_t), ALIGN_LEFT, close);
}

/* ---------------------------------------------------------------- shared helpers */

void app_draw_hints(const int *glyphs, const char *const *labels, int count, const char *right)
{
    draw_rect(TH_MARGIN, TH_HINT_Y - 22, plat_width() - 2 * TH_MARGIN, 2, TH_DIVIDER);
    float x = TH_MARGIN;
    for (int i = 0; i < count; ++i)
        x += draw_hint(x, TH_HINT_Y, (enum PadGlyph)glyphs[i], tr(labels[i]), 26, TH_HINT);
    if (right)
        text_draw(plat_width() - TH_MARGIN, TH_HINT_Y, 26, FONT_REGULAR, TH_HINT, ALIGN_RIGHT, tr(right));
}

/* Nightshade's: near black, a low red glow rising from below, right of the middle, and a fainter one behind
 * (Mandrake's BACKDROP_GLOW; Dulcamara's shelf). */
void app_backdrop(void)
{
    enum
    {
        W = 960,
        H = 540
    };
    static uint8_t *grey;
    if (!grey)
    {
        grey = malloc((size_t)W * H);
        if (!grey)
        {
            draw_rect(0, 0, (float)plat_width(), (float)plat_height(), TH_BG);
            return;
        }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const float t = (float)y / (H - 1), u = (float)x / (W - 1);
                const float gx = (u - 0.60f) / 0.55f, gy = (t - 1.04f) / 0.32f;
                const float bx = (u - 0.5f) / 0.75f, by = (t - 0.46f) / 0.34f;
                const float v = 0.035f + 0.30f * expf(-(gx * gx + gy * gy)) + 0.04f * expf(-(bx * bx + by * by));
                grey[(size_t)y * W + x] = (uint8_t)(fminf(fmaxf(v, 0.0f), 1.0f) * 255.0f);
            }
        plat_backdrop_changed();
    }
    plat_draw_backdrop(grey, W, H, theme.backdrop_tint);
}

void app_next_tip(void)
{
    store_lock();
    app.tip = apps_pick(store_catalog()->tip_count, app.tip, &app.random);
    store_unlock();
}

const char *app_tip(void)
{
    const Catalog *catalog = store_catalog();
    return app.tip >= 0 && app.tip < catalog->tip_count ? catalog->tips[app.tip] : "";
}

void app_show_egg(void)
{
    const Catalog *catalog = store_catalog();
    app.last_egg = apps_pick(catalog->egg_count, app.last_egg, &app.random);
    if (app.last_egg >= 0)
        app_toast_for(catalog->eggs[app.last_egg], 4.5f); /* exactly as the catalog has it */
}

void app_format_size(uint64_t bytes, char *out, size_t size)
{
    if (bytes >= 1000ull * 1000 * 1000)
        snprintf(out, size, "%.1f GB", (double)bytes / 1e9);
    else if (bytes >= 1000ull * 1000)
        snprintf(out, size, "%.1f MB", (double)bytes / 1e6);
    else
        snprintf(out, size, "%llu KB", (unsigned long long)((bytes + 999) / 1000));
}

int app_nav_pad(void)
{
    for (int i = 0; i < NP_MAX_PADS; ++i)
        if (app.pads[i].connected)
            return i;
    return 0;
}

/* Button presses for the menus, with auto-repeat on directions and shoulders; the navigating controller's. */
static uint32_t nav_pressed(const PadState *pads)
{
    static uint32_t previous;
    static uint64_t repeat_at;
    static int repeats;
    const PadState *p = &pads[app_nav_pad()];
    uint32_t held = p->buttons;
    if (p->ly < -20000)
        held |= BIT(BTN_UP);
    if (p->ly > 20000)
        held |= BIT(BTN_DOWN);
    if (p->lx < -20000)
        held |= BIT(BTN_LEFT);
    if (p->lx > 20000)
        held |= BIT(BTN_RIGHT);
    /* the right stick scrolls the release notes, as L2 / R2 do */
    if (p->ry < -20000)
        held |= BIT(BTN_L2);
    if (p->ry > 20000)
        held |= BIT(BTN_R2);
    uint32_t pressed = held & ~previous;
    const uint32_t dirs = BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_L1) | BIT(BTN_R1) |
                          BIT(BTN_L2) | BIT(BTN_R2);
    const uint64_t now = plat_ticks_us();
    if (pressed & dirs)
    {
        repeat_at = now + 260000;
        repeats = 0;
    }
    else if ((held & dirs) && now >= repeat_at)
    {
        pressed |= held & dirs;
        repeat_at = now + (repeats < 6 ? 70000 : 40000);
        ++repeats;
    }
    previous = held;
    return pressed;
}

void app_apply_settings(void)
{
    sfx_configure(app.settings.ui_sound, (app.settings.ui_volume + 1) * 25);
    music_configure(app.settings.music, app.settings.music_shuffle, app.settings.music_volume);
    store_set_parallel(app.settings.parallel);
    store_set_keep(app.settings.keep_versions);
    const int lang = app.settings.language >= 0 ? app.settings.language : plat_system_language();
    if (lang != i18n_get())
    {
        i18n_set(lang);
        text_language_changed();
    }
}

void app_save_settings(void)
{
    if (!app.storage_error[0] && !config_save(&app.settings, app.paths.config))
        np_log("settings: could not write %s", app.paths.config);
}

void app_open_settings(void)
{
    app.screen = SCREEN_SETTINGS;
    settings_opened();
}

void app_request(int request, int index)
{
    app_request_with(request, index, 0, "");
}

void app_request_with(int request, int index, int arg, const char *text)
{
    app.request = request;
    app.request_app = index;
    app.request_arg = arg;
    str_copy(app.request_text, sizeof(app.request_text), text ? text : "");
}

bool app_job(int index, StoreJob *out)
{
    return store_job_of(index, out);
}

/* The third-party apps the player agreed to install, this time Solanum runs. */
static char agreed[32][10];
static int agreed_count;

bool app_third_party_agreed(const char *title)
{
    for (int i = 0; i < agreed_count; ++i)
        if (strcmp(agreed[i], title) == 0)
            return true;
    return false;
}

void app_third_party_agree(const char *title)
{
    if (!app_third_party_agreed(title) && agreed_count < 32)
        str_copy(agreed[agreed_count++], sizeof(agreed[0]), title);
}

/* ---------------------------------------------------------------- what the screens asked for */

static void carry_out(void)
{
    const int request = app.request, index = app.request_app;
    char reason[256], text[400];
    app.request = REQUEST_NONE;
    switch (request)
    {
    case REQUEST_INSTALL:
        if (!store_install(index, app.request_text, app.request_arg == 1, reason, sizeof(reason)))
            app_message_box(tr(reason));
        else
            app_next_tip();
        break;
    case REQUEST_VERSION:
        if (!store_install_version(index, app.request_arg, strcmp(app.request_text, "replace") == 0, reason,
                                   sizeof(reason)))
            app_message_box(tr(reason));
        break;
    case REQUEST_ROLLBACK:
        if (!store_rollback(index, reason, sizeof(reason)))
            app_message_box(tr(reason));
        break;
    case REQUEST_UNINSTALL:
        if (!store_uninstall(index, app.request_arg != 0, reason, sizeof(reason)))
            app_message_box(tr(reason));
        break;
    case REQUEST_UPDATE_ALL:
    {
        int queued = 0;
        store_lock();
        int list[CATALOG_MAX_APPS], n = 0;
        for (int i = 0; i < store_catalog()->app_count; ++i)
            if (store_update_wanted(i))
                list[n++] = i;
        store_unlock();
        for (int i = 0; i < n; ++i)
            queued += store_install(list[i], "", false, reason, sizeof(reason));
        snprintf(text, sizeof(text), queued == 1 ? tr("1 update is in the queue.") : tr("%d updates are in the queue."),
                 queued);
        app_toast(text);
        break;
    }
    case REQUEST_CLEAR_CACHE:
        if (store_busy())
            app_toast(tr("Wait for the queue to finish first."));
        else
        {
            store_clear_cache();
            app_toast(tr("The kept versions and unfinished downloads are deleted."));
        }
        break;
    case REQUEST_CANCEL: store_cancel(app.request_arg); break;
    case REQUEST_REFRESH:
        app_next_tip();
        store_refresh(true);
        break;
    case REQUEST_OPEN:
    {
        store_lock();
        char name[64];
        str_copy(name, sizeof(name), index >= 0 && index < store_catalog()->app_count
                                         ? store_catalog()->apps[index].name
                                         : "the app");
        store_unlock();
        switch (store_open(index, reason, sizeof(reason)))
        {
        case OPEN_CLOSING:
            /* the payload opens it once Solanum has closed: a moment to read why the screen goes */
            snprintf(text, sizeof(text), tr("Opening %s..."), name);
            app_toast_for(text, 3.0f);
            app_save_settings();
            music_fade_out(MUSIC_FADE_MS);
            app.quit_at = plat_ticks_us() + 900000;
            break;
        case OPEN_NOT_LISTED:
            snprintf(text, sizeof(text),
                     tr("The console doesn't list %s yet. The homebrew loader adds a new app after its next scan or "
                        "a restart; then open it here or from the home screen."),
                     name);
            app_message_box(text);
            break;
        case OPEN_NO_LOADER:
            app_message_box(tr("No payload loader took Solanum's launch payload. Is the HEN's ELF loader (port 9021) "
                               "running? You can open the app from the home screen."));
            break;
        default:
            snprintf(text, sizeof(text), tr("%s couldn't be opened: %s."), name, reason);
            app_message_box(text);
            break;
        }
        break;
    }
    default: break;
    }
}

/* The jobs that ended since the last frame: each said once, and kept in Downloads' history. */
static void job_ended(void)
{
    char text[600];
    StoreJob ended;
    while (store_job_ended(&ended))
    {
        const StoreJob *job = &ended;
        memmove(app.history + 1, app.history, sizeof(app.history[0]) * (APP_HISTORY - 1));
        app.history[0] = ended;
        if (app.history_count < APP_HISTORY)
            ++app.history_count;
        const char *name = job->name[0] ? job->name : job->title;
        if (job->phase == JOB_INSTALLED && job->kind == JOB_REMOVE)
        {
            if (job->home_screen)
                snprintf(text, sizeof(text), tr("%s is uninstalled, and gone from the home screen."), name);
            else
                snprintf(text, sizeof(text),
                         tr("%s is uninstalled. The home screen may show it until ShadowMountPlus looks again or the "
                            "console restarts."),
                         name);
            app_toast_for(text, 5.0f);
        }
        else if (job->phase == JOB_INSTALLED)
        {
            if (job->fresh)
                snprintf(text, sizeof(text),
                         tr("%s %s is installed in %s. The homebrew loader lists it after its next scan or a restart."),
                         name, job->version, job->folder[0] ? job->folder : tr("its folder"));
            else if (job->kind == JOB_ROLLBACK)
                snprintf(text, sizeof(text), tr("%s is back to %s."), name, job->version);
            else
                snprintf(text, sizeof(text), tr("%s is updated to %s."), name, job->version);
            app_toast_for(text, 5.0f);
        }
        else if (job->phase == JOB_CANCELLED)
            app_toast(job->kind == JOB_REMOVE ? tr("The uninstall was cancelled. Nothing was changed.")
                                              : tr("The install was cancelled. Nothing was changed."));
        else if (job->mismatch)
        {
            snprintf(text, sizeof(text),
                     tr("%s wasn't installed: the download doesn't match its release's checksum, so nothing was "
                        "unpacked and nothing was changed. Try again; if it happens again, tell the app's author."),
                     name);
            app_message_box(text);
        }
        else if (job->no_loader)
            app_message_box(tr("No payload loader took Solanum's install helper. Is the HEN's ELF loader (port 9021) "
                               "running? Nothing was changed."));
        else if (job->kind == JOB_REMOVE)
        {
            snprintf(text, sizeof(text), tr("%s wasn't uninstalled: %s."), name,
                     job->error[0] ? tr(job->error) : tr("the uninstall stopped"));
            app_message_box(text);
        }
        else
        {
            snprintf(text, sizeof(text), tr("%s wasn't installed: %s. Nothing was changed."), name,
                     job->error[0] ? tr(job->error) : tr("the install stopped"));
            app_message_box(text);
        }
    }
}

/* Automatic updates (Settings > Store): once a refresh has its answers, every update the player wants goes in the
 * queue. Once a catalog. */
static void auto_update(void)
{
    static int done_for = -1;
    char reason[256];
    if (!app.settings.auto_update)
        return;
    store_lock();
    const bool ready = (store_catalog_state() == CATALOG_READY || store_catalog_state() == CATALOG_COPY) &&
                       !store_catalog_busy() && !store_checking();
    const int generation = store_generation();
    int list[CATALOG_MAX_APPS], n = 0;
    if (ready && done_for != generation)
        for (int i = 0; i < store_catalog()->app_count; ++i)
            if (store_update_wanted(i) && strcmp(store_catalog()->apps[i].title_id, NP_TITLE_ID) != 0)
                list[n++] = i;
    store_unlock();
    if (!ready || done_for == generation)
        return;
    done_for = generation;
    int queued = 0;
    for (int i = 0; i < n; ++i)
        queued += store_install(list[i], "", false, reason, sizeof(reason));
    if (queued)
    {
        char text[160];
        snprintf(text, sizeof(text), queued == 1 ? tr("Updating 1 app by itself (Settings > Store).")
                                                 : tr("Updating %d apps by themselves (Settings > Store)."),
                 queued);
        app_toast_for(text, 4.0f);
    }
}

/* ---------------------------------------------------------------- start-up */

static void make_folders(void)
{
    const char *dirs[] = {app.paths.root, app.paths.logs, app.paths.cache, app.paths.music};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
        make_dirs(dirs[i]);
    char probe[NP_PATH_MAX];
    path_join(probe, sizeof(probe), app.paths.root, ".write-test");
    FILE *f = fopen(probe, "w");
    if (f)
    {
        fclose(f);
        remove(probe);
    }
    else
        snprintf(app.storage_error, sizeof(app.storage_error), "%s isn't writable. Is the HEN running? (%s)",
                 app.paths.root, app.sandbox_reason[0] ? app.sandbox_reason : "no reason given");
}

/* The catalog's text beyond Latin-1 (curly quotes, dashes, other scripts) into the font atlases. */
static void want_text(void)
{
    static int generation = -1;
    static bool wanted[CATALOG_MAX_APPS];
    const Catalog *catalog = store_catalog();
    if (generation != store_generation())
    {
        generation = store_generation();
        memset(wanted, 0, sizeof(wanted));
        text_want_clear();
        text_want(catalog->about);
        text_want(catalog->about_footer);
        for (int i = 0; i < catalog->tip_count; ++i)
            text_want(catalog->tips[i]);
        for (int i = 0; i < catalog->egg_count; ++i)
            text_want(catalog->eggs[i]);
        for (int i = 0; i < catalog->app_count; ++i)
        {
            text_want(catalog->apps[i].name);
            text_want(catalog->apps[i].summary);
            text_want(catalog->apps[i].author);
        }
    }
    for (int i = 0; i < catalog->app_count; ++i)
        if (!wanted[i] && store_app(i)->checked)
        {
            wanted[i] = true;
            text_want(store_app(i)->release.notes);
        }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char root[NP_PATH_MAX];
    plat_default_root(root, sizeof(root));
    config_paths(&app.paths, root);
    /* The USB keyboard's library first: a process the HEN has freed can't load it any more (keys.c). */
    plat_keyboard_prepare();
    /* Unlock before SDL starts any thread: the HEN changes this process's credentials. */
    app.sandboxed = !plat_prepare_storage(app.sandbox_reason, sizeof(app.sandbox_reason));
    setenv("PS5_VIDEOOUT_PARAM_JSON", "/nonexistent", 1);
    plat_want_high_refresh(false);
    plat_want_output(2); /* 4K, 1080p when the TV lacks it */
    if (!plat_init())
    {
        char msg[300];
        snprintf(msg, sizeof(msg), NP_TITLE ": %s", plat_init_error());
        plat_notify(msg);
        for (;;)
            plat_sleep_us(1000000);
    }
    if (!text_init())
        plat_notify(NP_TITLE ": interface font missing from assets/fonts");
    theme_apply(THEME_NIGHTSHADE);

    make_folders();
    char log_path[NP_PATH_MAX];
    path_join(log_path, sizeof(log_path), app.paths.logs, "solanum.log");
    np_log_open(log_path);
    ps5_crash_install(log_path);
#if defined(__PROSPERO__)
    extern int ps5_klog_capture_stderr(const char *prefix);
    ps5_klog_capture_stderr(""); /* nc <PS5 IP> 3232 shows the log live */
#endif
    np_log(NP_TITLE " %s starting, data folder %s", NP_VERSION, app.paths.root);
    np_log("screen: %s", plat_screen_info());
    np_log(app.sandboxed ? "storage: sandboxed (%s)" : "storage: unlocked%s", app.sandboxed ? app.sandbox_reason : "");
    vk_probe(app.paths.root);

    config_defaults(&app.settings);
    config_load(&app.settings, app.paths.config);
    plat_audio_open(AUDIO_RATE);
    sfx_init(AUDIO_RATE);
    app_apply_settings();
    music_start(app.paths.music, AUDIO_RATE);
    app.random = apps_seed(plat_ticks_us() ^ (uint64_t)SDL_GetPerformanceCounter());

#if defined(__PROSPERO__)
    store_init(app.paths.root, "/user", "/data/homebrew");
#else
    /* the computer build: a made-up console in the data folder */
    char registered[NP_PATH_MAX], homebrew[NP_PATH_MAX];
    path_join(registered, sizeof(registered), app.paths.root, "console/user");
    path_join(homebrew, sizeof(homebrew), app.paths.root, "console/data/homebrew");
    store_init(app.paths.root, registered, homebrew);
#endif
    store_refresh(true);
    app_next_tip(); /* from the catalog's copy, while the catalog loads */
    if (app.settings.update_check && !app.storage_error[0])
        update_check();
    app.screen = SCREEN_STORE;
    store_screen_opened();
    if (app.storage_error[0])
        app_message_box(app.storage_error);

    bool quit = false;
    PadState pads[NP_MAX_PADS];
    uint64_t last = plat_ticks_us();
    const uint64_t opened = last;
    bool update_offered = false;
    while (!quit)
    {
        const uint64_t now = plat_ticks_us();
        app.dt = (float)(now - last) / 1e6f;
        if (app.dt > 0.1f)
            app.dt = 0.1f;
        last = now;

        /* a newer Solanum found at start installs by itself, once, a second after the app opened (as Mandrake) */
        if (!update_offered && update_state() == UPDATE_AVAILABLE && now - opened >= 1000000)
        {
            update_offered = true;
            app_toast_for("A newer Solanum is available and will be installed on your next restart.", 4.0f);
            update_install();
        }

        plat_poll(pads, &quit);
        if (app.quit_requested || (app.quit_at && now >= app.quit_at))
            quit = true;
        memcpy(app.pads, pads, sizeof(app.pads));
        uint32_t pressed = nav_pressed(pads);
        if (!message_box_input(&pressed))
            overlay_input(&pressed);
        store_poll();
        job_ended();
        auto_update();

        store_lock();
        want_text();
        plat_begin_frame(0);
        static const char *const names[SCREEN_COUNT] = {"store", "page", "settings"};
        ps5_crash_step(names[app.screen]);
        switch (app.screen)
        {
        case SCREEN_STORE: store_screen(pressed); break;
        case SCREEN_PAGE: page_screen(pressed); break;
        case SCREEN_SETTINGS: settings_screen(pressed); break;
        default: break;
        }
        overlay_draw();
        message_box_draw();
        toast_render();
        store_unlock();
        plat_end_frame();
        if (app.request != REQUEST_NONE)
            carry_out();
    }

    /* Only the desktop build gets here, and the PS5's when Solanum closes itself (Close, or opening an app). */
    ps5_crash_step("shutdown");
    np_log("shutdown: leaving the main loop");
    store_cancel_all();
    app_save_settings();
    music_stop();
#if !defined(__PROSPERO__)
    plat_audio_close();
    icons_shutdown();
    text_shutdown();
    plat_shutdown();
#else
    /* The system closes the title: exit()/_exit() raise SIGSYS on the console. LoadExec "exit" ends it normally. */
    extern int sceSystemServiceLoadExec(const char *path, const char **arguments);
    icons_shutdown();
    text_shutdown();
    plat_release();
    fflush(NULL);
    for (;;)
    {
        int rc = sceSystemServiceLoadExec("exit", NULL);
        np_log("shutdown: LoadExec exit refused rc=0x%x", (unsigned)rc);
        plat_sleep_us(1000000);
    }
#endif
    return 0;
}
