/*
 * Solanum - an app's page: its name, summary, author, stars, tags and license, an "attested" badge when the catalog
 * says its release's CI attests its builds, which catalog lists it, its newest release (version, date, size, what it
 * is checked against, changelog), where it is installed, a QR code to its source, and Install / Update / Open / More.
 * 0.4.0: notes on what to know first: "No license" (its repository states none), "Needs your own game files" (a port
 * that ships none), "Pre-release", and the other app of its title ID, which an install replaces after asking.
 * More holds the rest: where a new app goes, Versions (any release, older ones too), Roll back, skipping a release,
 * holding its updates, and Uninstall. An install shows here as it goes, with one of the catalog's tips.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../custom.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "draw.h"
#include "icons.h"
#include "overlay.h"
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
    BUTTON_MORE,
    BUTTON_PAGE_COUNT
};

/* what opens over the page */
enum
{
    MENU_MORE = 100,
    MENU_PLACE,
    MENU_VERSIONS,
    ASK_THIRD_PARTY,
    ASK_UNINSTALL,
    ASK_VERSION,
    ASK_ROLLBACK,
    ASK_REPLACE
};

/* More's items */
enum
{
    MORE_PLACE,
    MORE_VERSIONS,
    MORE_ROLLBACK,
    MORE_SKIP,
    MORE_HOLD,
    MORE_UNINSTALL,
    MORE_QR
};

static int button;
static int scroll, lines, generation;
static float progress_shown;
static int more_actions[8], more_count;
static int places[APPS_PLACES], place_count;
static int chosen_version = -1;
static bool installing_after_agree;
static bool replace_version; /* 0.4.0: the version chosen goes in place of another app of its title ID */
static char where_after_agree[64];
static int shown_versions = -1; /* the Versions answer the menu shows */

/* the small QR code to the source, made when the page opens */
static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
static bool qr_ok;

void page_open(int index)
{
    app.page_app = index;
    app.screen = SCREEN_PAGE;
    button = BUTTON_INSTALL;
    scroll = 0;
    progress_shown = 0;
    generation = store_generation();
    installing_after_agree = false;
    replace_version = false;
    overlay_reset(); /* nothing asked on another app's page is answered on this one */
    char url[200];
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
    snprintf(url, sizeof(url), "https://github.com/%s", index >= 0 && index < store_catalog()->app_count
                                                            ? store_catalog()->apps[index].repo
                                                            : "Oliv3rmoon/Solanum");
    qr_ok = qrcodegen_encodeText(url, temp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 8, qrcodegen_Mask_AUTO,
                                 true);
}

static void back(void)
{
    app.screen = SCREEN_STORE;
    sfx_play(SFX_BACK);
}

/* 0.4.0: another app of its title ID is installed (or one Solanum can't tell): installing this one replaces it. */
static bool occupied(const StoreApp *s)
{
    return s->looked && !s->installed.folder[0] && s->occupant != -1 && s->occupied.folder[0];
}

/* What the page offers: Install (not installed), Update (an older one installed), Open (installed). Never over another
 * app of its title ID that is mounted from an image. */
static bool can_install(int index)
{
    const StoreApp *s = store_app(index);
    return s->looked && s->checked && (s->offer == OFFER_GET || s->offer == OFFER_UPDATE) &&
           strcmp(store_catalog()->apps[index].title_id, NP_TITLE_ID) != 0 && !(occupied(s) && s->occupied.image);
}

/* 0.4.0: the question every install of an app over another of its title ID asks first (Install, Versions). */
static void replace_question(int index, char *title, size_t title_size, char *body, size_t size)
{
    const CatalogApp *a = &store_catalog()->apps[index];
    const StoreApp *s = store_app(index);
    if (s->occupant >= 0 && s->occupant < store_catalog()->app_count)
    {
        const CatalogApp *other = &store_catalog()->apps[s->occupant];
        snprintf(body, size,
                 tr("%s and %s have the same title ID, %s, and a console holds one of them. Installing %s puts it "
                    "in place of %s, in %s. Its saved data stays, and it comes back the same way."),
                 a->name, other->name, a->title_id, a->name, other->name, s->occupied.folder);
        snprintf(title, title_size, tr("Replace %s?"), other->name);
    }
    else
    {
        snprintf(body, size,
                 tr("The app in %s has this app's title ID, %s, and Solanum can't tell which app it is. Installing %s "
                    "puts it in place of that app. Its saved data stays."),
                 s->occupied.folder, a->title_id, a->name);
        snprintf(title, title_size, "%s", tr("Replace the app there?"));
    }
}

static bool can_open(int index)
{
    const StoreApp *s = store_app(index);
    return s->looked && s->installed.folder[0];
}

static bool removable(int index)
{
    const StoreApp *s = store_app(index);
    return s->looked && s->installed.folder[0] && !s->installed.image &&
           strcmp(store_catalog()->apps[index].title_id, NP_TITLE_ID) != 0;
}

/* The places a new app can go that are there now (drives plugged in). */
static int list_places(void)
{
    place_count = 0;
    for (int i = 0; i < APPS_PLACES; ++i)
    {
#if defined(__PROSPERO__)
        if (store_free_mb(apps_place_drive(i)) < 0)
            continue;
#else
        if (i > 0)
            break;
#endif
        places[place_count++] = i;
    }
    return place_count;
}

static void place_label(int place, char *label, size_t size, char *detail, size_t detail_size)
{
    const char *drive = apps_place_drive(place);
    if (place == 0)
        snprintf(label, size, "%s", tr("Internal storage"));
    else if (strncmp(drive, "/mnt/ext", 8) == 0)
        snprintf(label, size, tr("Extended storage %c"), drive[8] + 1);
    else
        snprintf(label, size, tr("USB drive %c"), drive[8] + 1);
    const long long free_mb = store_free_mb(drive);
    if (free_mb >= 0)
        snprintf(detail, detail_size, tr("%s  ·  %lld.%lld GB free"), apps_place(place), free_mb >> 10,
                 (free_mb & 1023) * 10 >> 10);
    else
        snprintf(detail, detail_size, "%s", apps_place(place));
}

static void install_now(int index, const char *where, bool replace)
{
    sfx_play(SFX_SELECT);
    app_request_with(REQUEST_INSTALL, index, replace ? 1 : 0, where);
}

/* Install (or Update): a third-party app asks first; a new app asks where when there is a choice. */
static void begin_install(int index)
{
    const CatalogApp *a = &store_catalog()->apps[index];
    const StoreApp *s = store_app(index);
    const bool fresh = !s->installed.folder[0];
    if (a->source > 0 && !app_third_party_agreed(a->title_id))
    {
        char name[96], body[700];
        custom_name(a->source - 1, name, sizeof(name));
        snprintf(body, sizeof(body),
                 tr("%s comes from %s, a third-party catalog, not from Solanum's. Solanum's catalog doesn't vouch for "
                    "it: its download is checked only against what its own GitHub release says. Install it only if you "
                    "trust that catalog and the app's author."),
                 a->name, name);
        confirm_open(ASK_THIRD_PARTY, tr("A third-party app"), body, tr("Install"), NULL, false);
        installing_after_agree = true;
        where_after_agree[0] = '\0';
        return;
    }
    if (!fresh)
    {
        install_now(index, "", false);
        return;
    }
    /* 0.4.0: another app of its title ID is installed: this one goes in its place, once the player says so (never over
     * an image) */
    if (occupied(s))
    {
        char body[700], title[160];
        if (s->occupied.image)
        {
            snprintf(body, sizeof(body),
                     tr("%s is mounted from an image under the same title ID. Solanum can't replace an image: remove "
                        "the image first."),
                     s->occupant >= 0 ? store_catalog()->apps[s->occupant].name : tr("Another app"));
            app_message_box(body);
            return;
        }
        replace_question(index, title, sizeof(title), body, sizeof(body));
        confirm_open(ASK_REPLACE, title, body, tr("Install"), NULL, false);
        return;
    }
    const int setting = app.settings.install_place;
    if (setting == -1 && list_places() > 1)
    {
        const char *labels[APPS_PLACES], *details[APPS_PLACES];
        static char label_text[APPS_PLACES][64], detail_text[APPS_PLACES][96];
        for (int i = 0; i < place_count; ++i)
        {
            place_label(places[i], label_text[i], sizeof(label_text[i]), detail_text[i], sizeof(detail_text[i]));
            labels[i] = label_text[i];
            details[i] = detail_text[i];
        }
        menu_open(MENU_PLACE, tr("Where should it go?"), place_count, labels, details, NULL, 0);
        return;
    }
    if (setting >= 1 && setting <= APPS_PLACES && store_free_mb(apps_place_drive(setting - 1)) >= 0)
        install_now(index, apps_place(setting - 1), false);
    else
        install_now(index, "", false);
}

static void open_more(int index)
{
    const StoreApp *s = store_app(index);
    const CatalogApp *a = &store_catalog()->apps[index];
    const char *labels[8], *details[8];
    static char text[8][160], detail[8][200];
    bool enabled[8];
    more_count = 0;
    const bool fresh = !s->installed.folder[0];
    const bool own = strcmp(a->title_id, NP_TITLE_ID) == 0;
#define ADD(action, label, detail_text, on)                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        more_actions[more_count] = (action);                                                                           \
        str_copy(text[more_count], sizeof(text[0]), (label));                                                          \
        str_copy(detail[more_count], sizeof(detail[0]), (detail_text));                                                \
        labels[more_count] = text[more_count];                                                                         \
        details[more_count] = detail[more_count];                                                                      \
        enabled[more_count] = (on);                                                                                    \
        ++more_count;                                                                                                  \
    } while (0)
    char line[200], extra[200];
    if (fresh && !own && can_install(index) && !occupied(s) && list_places() > 1)
        ADD(MORE_PLACE, tr("Install on..."), tr("Internal storage, an extended drive or a USB drive"), true);
    if (!own && s->looked && !s->installed.image && !(occupied(s) && s->occupied.image))
        ADD(MORE_VERSIONS, tr("Versions..."), tr("Install any release of it, an older one too"), true);
    if (s->kept && !own)
    {
        snprintf(line, sizeof(line), tr("Roll back to %s"), s->previous.tag);
        snprintf(extra, sizeof(extra), "%s", tr("The version before this one, from the copy Solanum kept"));
        ADD(MORE_ROLLBACK, line, extra, true);
    }
    if (s->checked && s->reason == GITHUB_RELEASE_NEWER && !fresh && !own)
    {
        const bool skipped = s->skip[0] && strcmp(s->skip, s->release.tag) == 0;
        snprintf(line, sizeof(line), skipped ? tr("Offer %s again") : tr("Skip %s"), s->release.tag);
        ADD(MORE_SKIP, line, skipped ? tr("Show this update again") : tr("Don't offer this update; a newer one will be"),
            s->offer == OFFER_UPDATE || skipped);
    }
    if (!fresh && !own)
        ADD(MORE_HOLD, s->hold ? tr("Allow updates") : tr("Hold updates"),
            s->hold ? tr("Offer its updates again") : tr("Never offer or install its updates, until you allow them"),
            true);
    if (removable(index))
        ADD(MORE_UNINSTALL, tr("Uninstall..."), tr("Its folder and its home-screen entry, and its data if you want"),
            true);
    snprintf(extra, sizeof(extra), "github.com/%s", a->repo);
    ADD(MORE_QR, tr("QR code to its source"), extra, true);
#undef ADD
    menu_open(MENU_MORE, a->name, more_count, labels, details, enabled, 0);
}

/* Versions, as GitHub's answer comes in. */
static void versions_menu(int index, bool opening)
{
    const StoreVersions *v = store_versions();
    const StoreApp *s = store_app(index);
    const char *labels[GITHUB_RELEASE_LIST_MAX], *details[GITHUB_RELEASE_LIST_MAX];
    static char label_text[GITHUB_RELEASE_LIST_MAX][160], detail_text[GITHUB_RELEASE_LIST_MAX][200];
    bool enabled[GITHUB_RELEASE_LIST_MAX];
    int count = 0;
    char title[160];
    snprintf(title, sizeof(title), tr("Versions of %s"), store_catalog()->apps[index].name);
    if (v->app != index || v->state == VERSIONS_ASKING || v->state == VERSIONS_NONE)
    {
        labels[0] = tr("Asking GitHub for its releases...");
        details[0] = "";
        enabled[0] = false;
        count = 1;
    }
    else if (v->state == VERSIONS_FAILED || v->count == 0)
    {
        labels[0] = v->state == VERSIONS_FAILED ? tr(v->error) : tr("It has no release Solanum can install.");
        details[0] = "";
        enabled[0] = false;
        count = 1;
    }
    else
        for (int i = 0; i < v->count; ++i)
        {
            const github_release_asset *r = &v->list[i];
            char date[16], size[32], version[12];
            app_release_date(r->published, date, sizeof(date));
            app_format_size(r->size, size, sizeof(size));
            const bool here = s->installed.folder[0] && apps_tag_version(r->tag, version) != TAG_NONE &&
                              strcmp(version, s->installed.version) == 0;
            snprintf(label_text[i], sizeof(label_text[i]), "%s%s", r->tag, i == 0 ? tr("  (newest)") : "");
            snprintf(detail_text[i], sizeof(detail_text[i]), "%s  ·  %s  ·  %s", date[0] ? date : "?", size,
                     here                 ? tr("installed now")
                     : !r->sha256[0]      ? tr("no checksum: Solanum won't install it")
                     : r->pinned          ? tr("checked against the catalog's pin")
                     : r->from_sums       ? tr("checked against its checksum file")
                                          : tr("checked against GitHub's digest"));
            labels[i] = label_text[i];
            details[i] = detail_text[i];
            enabled[i] = r->sha256[0] && !here;
            ++count;
        }
    if (opening)
        menu_open(MENU_VERSIONS, title, count, labels, details, enabled, 0);
    else
        menu_update(MENU_VERSIONS, title, count, labels, details, enabled);
}

/* The answers to what the page opened. */
static void answers(int index)
{
    const CatalogApp *a = &store_catalog()->apps[index];
    const StoreApp *s = store_app(index);
    int item;
    bool yes, checked;
    if (menu_chosen(MENU_MORE, &item) && item >= 0 && item < more_count)
    {
        char body[700];
        switch (more_actions[item])
        {
        case MORE_PLACE:
        {
            const int saved = app.settings.install_place;
            app.settings.install_place = -1;
            begin_install(index);
            app.settings.install_place = saved;
            break;
        }
        case MORE_VERSIONS:
            store_versions_ask(index);
            shown_versions = -1;
            versions_menu(index, true);
            break;
        case MORE_ROLLBACK:
            snprintf(body, sizeof(body),
                     tr("%s %s goes back in place of %s, from the copy Solanum kept, and nothing is downloaded. Your "
                        "own files in its folder stay."),
                     a->name, s->previous.tag, s->installed.version);
            confirm_open(ASK_ROLLBACK, tr("Roll back?"), body, tr("Roll back"), NULL, false);
            break;
        case MORE_SKIP:
            store_set_skip(index, s->skip[0] && strcmp(s->skip, s->release.tag) == 0 ? "" : s->release.tag);
            break;
        case MORE_HOLD:
            store_set_hold(index, !s->hold);
            app_toast(s->hold ? tr("Its updates are held.") : tr("Its updates are offered again."));
            break;
        case MORE_UNINSTALL:
        {
            char check[200];
            snprintf(body, sizeof(body),
                     tr("Its folder (%s) and its entry on the home screen are removed. The ZIPs Solanum kept for it go "
                        "too."),
                     s->installed.folder);
            snprintf(check, sizeof(check), tr("Also delete its data: %s"), a->data);
            char title[160];
            snprintf(title, sizeof(title), tr("Uninstall %s?"), a->name);
            confirm_open(ASK_UNINSTALL, title, body, tr("Uninstall"), a->data[0] ? check : NULL, false);
            break;
        }
        case MORE_QR:
        {
            char url[200], caption[300];
            snprintf(url, sizeof(url), "https://github.com/%s", a->repo);
            snprintf(caption, sizeof(caption), tr("Scan it to open %s's source, releases and issues: %s"), a->name,
                     url);
            qr_open(url, caption);
            break;
        }
        default: break;
        }
    }
    if (menu_chosen(MENU_PLACE, &item) && item >= 0 && item < place_count)
        install_now(index, apps_place(places[item]), false);
    if (menu_is_open(MENU_VERSIONS) && shown_versions != store_versions()->state)
    {
        shown_versions = store_versions()->state;
        versions_menu(index, false);
    }
    if (menu_chosen(MENU_VERSIONS, &item))
    {
        const StoreVersions *v = store_versions();
        if (v->app == index && v->state == VERSIONS_READY && item >= 0 && item < v->count)
        {
            char body[700], title[160];
            chosen_version = item;
            replace_version = false;
            if (occupied(s))
            {
                /* 0.4.0: in place of another app of its title ID: the same question as Install's */
                char replace_title[160];
                replace_question(index, replace_title, sizeof(replace_title), body, sizeof(body));
                replace_version = true;
            }
            else if (s->installed.folder[0])
                snprintf(body, sizeof(body),
                         tr("%s %s goes in place of the installed %s. Your own files in its folder stay, and Solanum "
                            "keeps the version it replaces for Roll back."),
                         a->name, v->list[item].tag, s->installed.version);
            else
                snprintf(body, sizeof(body), tr("%s %s will be installed."), a->name, v->list[item].tag);
            snprintf(title, sizeof(title), tr("Install %s?"), v->list[item].tag);
            confirm_open(ASK_VERSION, title, body, tr("Install"), NULL, false);
        }
    }
    if (confirm_answered(ASK_VERSION, &yes, &checked) && yes)
        app_request_with(REQUEST_VERSION, index, chosen_version, replace_version ? "replace" : "");
    if (confirm_answered(ASK_ROLLBACK, &yes, &checked) && yes)
        app_request(REQUEST_ROLLBACK, index);
    if (confirm_answered(ASK_UNINSTALL, &yes, &checked) && yes)
        app_request_with(REQUEST_UNINSTALL, index, checked ? 1 : 0, "");
    if (confirm_answered(ASK_REPLACE, &yes, &checked) && yes)
        install_now(index, "", true);
    if (confirm_answered(ASK_THIRD_PARTY, &yes, &checked))
    {
        if (yes && installing_after_agree)
        {
            app_third_party_agree(a->title_id);
            installing_after_agree = false;
            begin_install(index);
        }
        installing_after_agree = false;
        (void)where_after_agree;
    }
}

static void info_row(float x, float y, float w, const char *label, const char *value, uint32_t argb, int weight)
{
    text_draw(x, y, 23, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, label);
    text_draw_fit(x + 230, y, 23, weight, argb, ALIGN_LEFT, w - 230, value);
}

static void draw_progress(const StoreJob *job, float x, float y, float w)
{
    char what[200], done[32], total[32];
    float fraction = 0.0f;
    switch (job->phase)
    {
    case JOB_QUEUED: snprintf(what, sizeof(what), "%s", tr("In the queue: it starts when one ahead of it ends")); break;
    case JOB_STARTING: snprintf(what, sizeof(what), "%s", tr("Starting the install helper...")); break;
    case JOB_DOWNLOADING:
        app_format_size(job->done, done, sizeof(done));
        app_format_size(job->total, total, sizeof(total));
        if (job->kind == JOB_ROLLBACK)
            snprintf(what, sizeof(what), "%s", tr("Handing over the kept copy..."));
        else if (job->total)
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
    case JOB_REMOVING: snprintf(what, sizeof(what), "%s", tr("Uninstalling...")); fraction = 0.5f; break;
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

static void draw_qr_small(float x, float y, float side)
{
    if (!qr_ok)
        return;
    const int size = qrcodegen_getSize(qr), quiet = 2;
    const float cell = side / (float)(size + 2 * quiet);
    draw_rrect(x - 4, y - 4, side + 8, side + 8, 8, 0xffffffffu);
    for (int row = 0; row < size; ++row)
        for (int col = 0; col < size; ++col)
            if (qrcodegen_getModule(qr, col, row))
                draw_rect(floorf(x + (col + quiet) * cell), floorf(y + (row + quiet) * cell), ceilf(cell), ceilf(cell),
                          0xff000000u);
}

void page_screen(uint32_t pressed)
{
    const Catalog *catalog = store_catalog();
    const int index = app.page_app;
    /* A new catalog may have moved or dropped the app: back to the lists. */
    if (generation != store_generation() || index < 0 || index >= catalog->app_count)
    {
        overlay_reset(); /* a question about the app it showed goes with it */
        app.screen = SCREEN_STORE;
        app_backdrop();
        return;
    }
    const CatalogApp *a = &catalog->apps[index];
    const StoreApp *s = store_app(index);
    StoreJob job;
    const bool has_job = app_job(index, &job);
    const bool installable = can_install(index), openable = can_open(index);
    answers(index);

    if (has_job)
    {
        if (pressed & BIT(BTN_CIRCLE))
        {
            /* until the files are being put in place, nothing is changed */
            if (job.phase < JOB_INSTALLING)
            {
                app_request_with(REQUEST_CANCEL, index, job.id, "");
                sfx_play(SFX_BACK);
            }
            else
            {
                back();
                app_backdrop();
                return;
            }
        }
        if (pressed & BIT(BTN_SQUARE))
        {
            back();
            app_backdrop();
            return;
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
            const bool enabled[BUTTON_PAGE_COUNT] = {installable, openable, true};
            int to = button;
            do
                to += pressed & BIT(BTN_LEFT) ? -1 : 1;
            while (to >= 0 && to < BUTTON_PAGE_COUNT && !enabled[to]);
            if (to >= 0 && to < BUTTON_PAGE_COUNT)
            {
                button = to;
                sfx_play(SFX_CLICK);
            }
        }
        if (button == BUTTON_INSTALL && !installable)
            button = openable ? BUTTON_OPEN : BUTTON_MORE;
        if (button == BUTTON_OPEN && !openable)
            button = installable ? BUTTON_INSTALL : BUTTON_MORE;
        if (pressed & BIT(BTN_CROSS))
        {
            if (button == BUTTON_INSTALL && installable)
                begin_install(index);
            else if (button == BUTTON_OPEN && openable)
            {
                sfx_play(SFX_SELECT);
                app_request(REQUEST_OPEN, index);
            }
            else if (button == BUTTON_MORE)
            {
                sfx_play(SFX_SELECT);
                open_more(index);
            }
        }
        if (pressed & BIT(BTN_TRIANGLE))
        {
            sfx_play(SFX_SELECT);
            open_more(index);
        }
    }
    if ((pressed & BIT(BTN_L2)) || (pressed & BIT(BTN_UP)))
        scroll = scroll > 3 ? scroll - 3 : 0;
    if (((pressed & BIT(BTN_R2)) || (pressed & BIT(BTN_DOWN))) && scroll + 4 < lines)
        scroll += 3;

    app_backdrop();
    /* the app */
    const float x = PAGE_X, top = 70;
    const float qr_side = 150, qr_x = plat_width() - PAGE_X - qr_side;
    app_draw_tile(a, x, top, 168, 1.0f);
    const float nx = x + 168 + 40, name_room = qr_x - nx - 40;
    text_draw_fit(nx, top + 6, 56, FONT_BOLD, TH_TEXT, ALIGN_LEFT, name_room, a->name);
    char line[400];
    snprintf(line, sizeof(line), "%s  ·  %s  ·  github.com/%s", tr(catalog_category_title(a->category)),
             catalog_no_license(a) ? tr("No license") : a->license, a->repo);
    text_draw_fit(nx, top + 84, 24, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, name_room, line);
    draw_qr_small(qr_x, top + 4, qr_side);
    text_draw(qr_x + qr_side * 0.5f, top + qr_side + 14, 18, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("its source"));
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
    if (a->source > 0)
    {
        char name[96], label[160];
        custom_name(a->source - 1, name, sizeof(name));
        snprintf(label, sizeof(label), tr("Third-party: %s"), name);
        bx += draw_pill(bx, top + 126, 42, 22, 0x33d4a72cu, TH_GOLD, label) + 14;
    }
    if (s->looked && s->installed.folder[0])
        bx += draw_pill(bx, top + 126, 42, 22, TH_PILL, TH_GOOD,
                        s->by == BY_HAND ? tr("Installed by hand, looked after by Solanum") : tr("Installed")) +
              14;
    if (s->hold)
        bx += draw_pill(bx, top + 126, 42, 22, TH_PILL, TH_GOLD, tr("Updates held")) + 14;
    /* 0.4.0: what to know before installing it, as far as there is room before the QR code */
    {
        const float room = qr_x - 40;
        char note[200];
        int others[4];
        const int sharing = catalog_sharing(catalog, index, others, 4);
        const char *notes[4];
        uint32_t colors[4];
        int count = 0;
        if (a->needs_game_files)
        {
            notes[count] = tr("Needs your own game files");
            colors[count++] = TH_GOLD;
        }
        if (catalog_no_license(a))
        {
            notes[count] = tr("No license");
            colors[count++] = TH_GOLD;
        }
        if (s->checked && s->reason == GITHUB_RELEASE_NEWER && s->release.prerelease)
        {
            notes[count] = tr("Pre-release");
            colors[count++] = TH_TEXT_SOFT;
        }
        if (sharing > 0)
        {
            if (s->occupant >= 0 && s->occupant < catalog->app_count)
                snprintf(note, sizeof(note), tr("Installing it replaces %s"), catalog->apps[s->occupant].name);
            else if (occupied(s))
                snprintf(note, sizeof(note), "%s", tr("Its title ID holds an app Solanum can't tell"));
            else
                snprintf(note, sizeof(note), tr("Shares its title ID with %s"), catalog->apps[others[0]].name);
            notes[count] = note;
            colors[count++] = occupied(s) ? TH_GOLD : TH_TEXT_SOFT;
        }
        for (int n = 0; n < count; ++n)
        {
            if (bx + text_width(22, FONT_REGULAR, notes[n]) + 42 > room)
                break;
            bx += draw_pill(bx, top + 126, 42, 22, colors[n] == TH_GOLD ? 0x33d4a72cu : TH_PILL, colors[n], notes[n]) +
                  14;
        }
    }
    app_draw_wrapped(a->summary, x, top + 210, qr_x - x - 40, 30, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT, top + 300, 0,
                     NULL);

    /* the facts */
    const float cx = x, cy = 384, cw = 860, ch = 420, step = 46;
    draw_rrect(cx - 16, cy - 16, cw + 32, ch, TH_RADIUS, TH_CARD_A(0xe6));
    char date[16], size[32], value[400];
    float ry = cy + 4;
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
    ry += step;
    if (!s->looked)
        snprintf(value, sizeof(value), "%s", tr("Looking..."));
    else if (!s->installed.folder[0])
        snprintf(value, sizeof(value), "%s", tr("Not installed"));
    else
        snprintf(value, sizeof(value), "%s%s%s", s->installed.version[0] ? s->installed.version : "?",
                 s->checked && s->offer == OFFER_CURRENT ? tr("  ·  the newest") : "",
                 s->skip[0] && strcmp(s->skip, s->release.tag) == 0 ? tr("  ·  this update skipped") : "");
    info_row(cx + 16, ry, cw - 32, tr("Installed"), value, TH_TEXT_SOFT, FONT_REGULAR);
    ry += step;
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
    else if (occupied(s))
        snprintf(value, sizeof(value), tr("%s is there now, in %s"),
                 s->occupant >= 0 && s->occupant < catalog->app_count ? catalog->apps[s->occupant].name
                 : s->occupied.name[0]                                ? s->occupied.name
                                                                      : tr("Another app"),
                 s->occupied.folder);
    else
        snprintf(value, sizeof(value), "%s", tr("Not installed yet: Install says where it goes"));
    info_row(cx + 16, ry, cw - 32, tr("Lives in"), value, TH_TEXT_SOFT, FONT_REGULAR);
    ry += step;
    snprintf(value, sizeof(value), "%s%s%s", a->author[0] ? a->author : tr("its repository's owner"),
             a->stars ? "  ·  " : "", "");
    if (a->stars)
    {
        char stars[48];
        snprintf(stars, sizeof(stars), tr("%u stars on GitHub"), a->stars);
        str_copy(value + strlen(value), sizeof(value) - strlen(value), stars);
    }
    info_row(cx + 16, ry, cw - 32, tr("Made by"), value, TH_TEXT_SOFT, FONT_REGULAR);
    ry += step;
    value[0] = '\0';
    for (int t = 0; t < a->tag_count; ++t)
    {
        const size_t used = strlen(value);
        snprintf(value + used, sizeof(value) - used, "%s#%s", t ? "  " : "", a->tags[t]);
    }
    info_row(cx + 16, ry, cw - 32, tr("Tags"), value[0] ? value : "-", TH_TEXT_SOFT, FONT_REGULAR);
    ry += step;
    info_row(cx + 16, ry, cw - 32, tr("Title ID"), a->title_id, TH_TEXT_SOFT, FONT_REGULAR);
    ry += step;
    snprintf(value, sizeof(value), "%s",
             !s->checked || s->reason != GITHUB_RELEASE_NEWER
                 ? a->checksums
             : s->release.pinned && s->release.from_sums ? tr("its checksum file, and the catalog's pin")
             : s->release.pinned && s->source == FROM_GITHUB ? tr("GitHub's digest, and the catalog's pin")
             : s->release.pinned                             ? tr("the SHA-256 the catalog pinned")
             : s->release.from_sums                          ? a->checksums
                                                             : tr("GitHub's digest (the release has no checksum file)"));
    info_row(cx + 16, ry, cw - 32, tr("Checked against"), value, TH_TEXT_SOFT, FONT_REGULAR);
    ry += step;
    if (s->unpinned[0])
    {
        snprintf(value, sizeof(value), tr("%s is out, but the catalog hasn't pinned it yet"), s->unpinned);
        info_row(cx + 16, ry, cw - 32, tr("Newer"), value, TH_GOLD, FONT_REGULAR);
    }

    /* Install / Update, Open and More, or the job as it goes */
    const float by = cy + ch + 10;
    if (has_job)
        draw_progress(&job, x, by, cw);
    else
    {
        const char *install_label = s->checked && s->offer == OFFER_UPDATE ? tr("Update") : tr("Install");
        const char *labels[BUTTON_PAGE_COUNT] = {install_label, tr("Open"), tr("More")};
        const int icons[BUTTON_PAGE_COUNT] = {ICON_DOWNLOAD, ICON_PLAYER_PLAY, ICON_ADJUSTMENTS};
        const bool enabled[BUTTON_PAGE_COUNT] = {installable, openable, true};
        float px = x;
        for (int i = 0; i < BUTTON_PAGE_COUNT; ++i)
        {
            const float pw = 270, ph = 76;
            const bool on = i == button && enabled[i];
            draw_rrect(px, by, pw, ph, 38, on ? TH_FOCUS : enabled[i] ? TH_PILL : TH_PILL_A(0x60));
            icon_draw(icons[i], px + 34, by + 22, 32, on ? 0xffffffffu : enabled[i] ? TH_TEXT_SOFT : TH_TEXT_DIM);
            text_draw_fit(px + 82, by + 22, 30, FONT_BOLD, on ? 0xffffffffu : enabled[i] ? TH_TEXT_SOFT : TH_TEXT_DIM,
                          ALIGN_LEFT, pw - 96, labels[i]);
            px += pw + 24;
        }
        const char *why = "";
        if (strcmp(a->title_id, NP_TITLE_ID) == 0)
            why = tr("This is Solanum: it updates itself (Settings).");
        else if (s->looked && s->installed.image)
            why = tr("Installed as an image: update it by replacing the image.");
        else if (occupied(s) && s->occupied.image)
            why = tr("Another app of its title ID is mounted from an image.");
        else if (s->hold && s->offer == OFFER_UPDATE)
            why = tr("Its updates are held (More).");
        else if (s->checked && s->offer == OFFER_CURRENT)
            why = tr("The installed version is the newest.");
        if (why[0])
            text_draw_fit(x, by + 92, 23, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, cw, why);
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

    if (has_job)
    {
        static const int glyphs[] = {GLYPH_CIRCLE, GLYPH_SQUARE, GLYPH_R2};
        static const char *const labels[] = {"Cancel", "Back", "Scroll the notes"};
        static const char *const later[] = {"Back", "Back", "Scroll the notes"};
        app_draw_hints(glyphs, job.phase < JOB_INSTALLING ? labels : later, job.phase < JOB_INSTALLING ? 3 : 2, NULL);
    }
    else
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_CIRCLE, GLYPH_R2};
        static const char *const labels[] = {"Choose", "More", "Back", "Scroll the notes"};
        app_draw_hints(glyphs, labels, 4, NULL);
    }
}
