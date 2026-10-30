/*
 * Solanum - host check of the store's decisions, run by tests/test_store.py.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * usage: store-check <tests/fixtures> <a scratch folder>
 *
 *   - the catalog: Oliv3rmoon/Solanum-Catalog's catalog.json as it is, and catalogs with bad entries, which
 *     are left out with a line in the log while the rest are kept;
 *   - the release's file: the asset's name from the catalog's pattern, and from the four apps' real
 *     releases/latest answers (fixtures/releases), with the checksum file's SHA-256 and GitHub's digest;
 *   - checksums: a download that isn't the release's is refused by the kit before the helper is told to
 *     unpack anything, and its resumable copy is deleted; an interrupted download resumes from its copy;
 *   - where an app is, what a release offers, the offer the helper installs from, the launch payload's
 *     conversation;
 *   - the Konami code, and a random tip or egg.
 */
#define _DEFAULT_SOURCE 1 /* glibc: lstat, strdup, usleep under -std=c11 */
#include "../src/apps.h"
#include "../src/catalog.h"
#include "../src/custom.h"
#include "../src/keep.h"
#include "../src/prefs.h"
#include "../src/nightshade.h"
#include "../src/resume.h"

#include "github_release.h"
#include "self_update.h"
#include "self_update_protocol.h"
#include "self_update_sha256.h"
#include "update_check.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passed, failed;
static const char *fixtures, *scratch;

#define EXPECT(condition, what)                                                                                  \
    do                                                                                                           \
    {                                                                                                            \
        if (condition)                                                                                           \
            ++passed;                                                                                            \
        else                                                                                                     \
        {                                                                                                        \
            ++failed;                                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                                \
        }                                                                                                        \
    } while (0)

static char *slurp(const char *name, size_t *length)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", fixtures, name);
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        printf("FAIL: no fixture %s\n", path);
        exit(2);
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (fread(text, 1, (size_t)size, file) != (size_t)size)
        exit(2);
    fclose(file);
    text[size] = '\0';
    *length = (size_t)size;
    return text;
}

static void write_file(const char *path, const char *text, size_t length)
{
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", path);
    for (char *slash = strchr(parent + 1, '/'); slash; slash = strchr(slash + 1, '/'))
    {
        *slash = '\0';
        mkdir(parent, 0755);
        *slash = '/';
    }
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(text, 1, length, file) != length)
        exit(2);
    fclose(file);
}

static void sha256_hex(const void *data, size_t length, char out[65])
{
    self_update_sha256 hash;
    unsigned char digest[32];
    self_update_sha256_start(&hash);
    self_update_sha256_add(&hash, data, length);
    self_update_sha256_finish(&hash, digest);
    self_update_sha256_hex(digest, out);
}

/* ---------------------------------------------------------------- the catalog */

static char log_lines[64][512];
static int log_count;

static void collect(void *user, const char *line)
{
    (void)user;
    if (log_count < 64)
        snprintf(log_lines[log_count++], sizeof(log_lines[0]), "%s", line);
}

static int logged(const char *part)
{
    for (int i = 0; i < log_count; ++i)
        if (strstr(log_lines[i], part))
            return 1;
    return 0;
}

static Catalog catalog;

static void check_catalog(void)
{
    size_t length;
    char *json = slurp("catalog.json", &length);
    log_count = 0;
    EXPECT(catalog_parse(json, length, &catalog, collect, NULL) == 4, "the live catalog's four apps are read");
    EXPECT(log_count == 0, "the live catalog leaves nothing out");
    EXPECT(strcmp(catalog.apps[0].id, "nightshade") == 0 && strcmp(catalog.apps[0].title_id, "PPSA99008") == 0 &&
               catalog.apps[0].family && catalog.apps[0].attested && catalog.apps[0].category == CATEGORY_EMULATOR,
           "Nightshade as the catalog lists it");
    EXPECT(strcmp(catalog.apps[0].asset, "Nightshade-v{version}.zip") == 0 &&
               strcmp(catalog.apps[0].repo, "Oliv3rmoon/Nightshade") == 0 &&
               strcmp(catalog.apps[0].checksums, "SHA256SUMS") == 0 &&
               strcmp(catalog.apps[0].folder, "PPSA99008") == 0 &&
               strcmp(catalog.apps[0].license, "GPL-3.0-or-later") == 0,
           "Nightshade's release fields");
    EXPECT(catalog.apps[1].category == CATEGORY_MEDIA && catalog.apps[2].category == CATEGORY_STREAMING &&
               strcmp(catalog.apps[3].name, "Mandrake") == 0,
           "Physalis, Datura and Mandrake follow, in order");
    EXPECT(catalog.tip_count == 8 && catalog.egg_count == 10, "eight tips and ten hidden lines");
    EXPECT(strcmp(catalog.eggs[0], "Imagine gatekeeping a homebrew store. Couldn't be us.") == 0,
           "the hidden lines are kept exactly as the catalog has them");
    EXPECT(strcmp(catalog.about_footer,
                  "No title IDs were kept under lock and key in the making of this store.") == 0,
           "the About footer");
    EXPECT(strstr(catalog.about, "open catalog") != NULL, "the catalog's about line");
    free(json);

    /* Bad entries: each left out with its reason; the good ones around them kept. */
    static const char bad[] =
        "{\"schema\":1,\"name\":\"Test\",\"apps\":["
        "{\"id\":\"good\",\"name\":\"Good\",\"category\":\"tool\",\"summary\":\"s\",\"title_id\":\"ABCD12345\","
        "\"repo\":\"o/good\",\"asset\":\"Good-{version}.zip\",\"folder\":\"ABCD12345\",\"checksums\":\"SHA256SUMS\","
        "\"attested\":false,\"license\":\"MIT\"},"
        "{\"id\":\"short\",\"name\":\"Short\",\"category\":\"tool\",\"summary\":\"s\",\"title_id\":\"ABCD1234\","
        "\"repo\":\"o/s\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\",\"attested\":true,"
        "\"license\":\"MIT\"},"
        "{\"id\":\"norepo\",\"name\":\"No repo\",\"category\":\"tool\",\"summary\":\"s\",\"title_id\":\"ABCD22222\","
        "\"repo\":\"not a repo\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\",\"attested\":true,"
        "\"license\":\"MIT\"},"
        "{\"id\":\"nolicense\",\"name\":\"No license\",\"category\":\"tool\",\"summary\":\"s\","
        "\"title_id\":\"ABCD33333\",\"repo\":\"o/n\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\","
        "\"attested\":true},"
        "{\"id\":\"cat\",\"name\":\"Cat\",\"category\":\"warez\",\"summary\":\"s\",\"title_id\":\"ABCD44444\","
        "\"repo\":\"o/c\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\",\"attested\":true,"
        "\"license\":\"MIT\"},"
        "{\"id\":\"path\",\"name\":\"Path\",\"category\":\"game\",\"summary\":\"s\",\"title_id\":\"ABCD55555\","
        "\"repo\":\"o/p\",\"asset\":\"../../evil.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\",\"attested\":true,"
        "\"license\":\"MIT\"},"
        "{\"id\":\"twice\",\"name\":\"Twice\",\"category\":\"game\",\"summary\":\"s\",\"title_id\":\"ABCD12345\","
        "\"repo\":\"o/t\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\",\"attested\":true,"
        "\"license\":\"MIT\"},"
        "{\"id\":\"flag\",\"name\":\"Flag\",\"category\":\"game\",\"summary\":\"s\",\"title_id\":\"ABCD66666\","
        "\"repo\":\"o/f\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\",\"attested\":\"yes\","
        "\"license\":\"MIT\"},"
        "\"not an object\","
        "{\"id\":\"also-good\",\"name\":\"Also good\",\"family\":true,\"category\":\"store\",\"summary\":\"s\","
        "\"title_id\":\"WXYZ00001\",\"repo\":\"o/also\",\"asset\":\"Also.zip\",\"folder\":\"WXYZ00001\","
        "\"checksums\":\"sums.txt\",\"attested\":true,\"license\":\"GPL-3.0-or-later\"}"
        "],\"tips\":[\"one\",7,\"\",\"two \\u2019quoted\\u2019\"],\"eggs\":\"not a list\"}";
    log_count = 0;
    EXPECT(catalog_parse(bad, sizeof(bad) - 1, &catalog, collect, NULL) == 2, "two good apps among bad ones");
    EXPECT(strcmp(catalog.apps[0].id, "good") == 0 && strcmp(catalog.apps[1].id, "also-good") == 0 &&
               catalog.apps[1].family && !catalog.apps[0].family,
           "the good ones are kept, in order");
    EXPECT(logged("app 1 (short) left out: title_id is not four letters and five digits"),
           "a short title ID is logged");
    EXPECT(logged("app 2 (norepo) left out: repo is not owner/name"), "a bad repository is logged");
    EXPECT(logged("app 3 (nolicense) left out: license is missing"), "a missing license is logged");
    EXPECT(logged("app 4 (cat) left out: category \"warez\" is not one Solanum knows"), "an unknown category");
    EXPECT(logged("app 5 (path) left out: asset can't name a release's file"), "a path as the asset");
    EXPECT(logged("app 6 (twice) left out: its title ID belongs to an app listed already"), "a title ID twice");
    EXPECT(logged("app 7 (flag) left out: attested is not true or false"), "attested of the wrong type");
    EXPECT(logged("app 8 left out: not an object"), "an entry that isn't an object");
    EXPECT(log_count == 11, "one log line for each left out, two for tips and one for eggs");
    EXPECT(catalog.tip_count == 2 && strcmp(catalog.tips[1], "two \xe2\x80\x99quoted\xe2\x80\x99") == 0,
           "tips: the text ones kept, escapes decoded");
    EXPECT(logged("tips entry 1 left out") && logged("tips entry 2 left out") && logged("eggs is not a list") &&
               catalog.egg_count == 0,
           "tips and eggs that aren't text are logged");

    log_count = 0;
    EXPECT(catalog_parse("{\"schema\":2,\"apps\":[]}", 22, &catalog, collect, NULL) == -1 &&
               catalog.app_count == 0 && logged("not a catalog of schema 1"),
           "another schema is refused whole");
    EXPECT(catalog_parse("<html>rate limited</html>", 25, &catalog, collect, NULL) == -1, "not JSON is refused");
    EXPECT(catalog_parse("{\"schema\":1}", 12, &catalog, collect, NULL) == -1, "no apps is refused");
    EXPECT(catalog_parse("{\"schema\":1,\"apps\":[]}", 22, &catalog, collect, NULL) == 0, "an empty list is a list");
}

/* ---------------------------------------------------------------- the release's file */

typedef struct
{
    const char *answer;   /* releases/latest's */
    size_t answer_length;
    int status;
    const char *sums;     /* the checksum file's */
    size_t sums_length;
    int sums_downloads;
} FakeGitHub;

static int fake_fetch(void *user, const char *url, char *body, size_t capacity, size_t *length, int *status)
{
    FakeGitHub *g = user;
    (void)url;
    *status = g->status;
    *length = g->answer_length < capacity ? g->answer_length : capacity;
    memcpy(body, g->answer, *length);
    return 0;
}

static int fake_download(void *user, const char *url, uint64_t limit, self_update_sink sink, void *sink_user)
{
    FakeGitHub *g = user;
    (void)url;
    ++g->sums_downloads;
    if (!g->sums || g->sums_length > limit)
        return -404;
    return sink(sink_user, g->sums, g->sums_length) == 1 ? 0 : -1;
}

static void check_release(void)
{
    static const struct
    {
        const char *app, *pattern, *tag, *name, *sha256;
    } APPS[] = {
        {"Nightshade", "Nightshade-v{version}.zip", "v1.010.002", "Nightshade-v1.010.002.zip",
         "9f4d939463204764c7cbaef7697ed9b5053168847024f6da9a477984d72d6286"},
        {"Physalis", "PPSA99003.zip", "01.002.001", "PPSA99003.zip",
         "3e1ddd6402f2a19ab3feee249ae2cb9f8a7a2894e64868347990a3bda785bf6a"},
        {"Datura", "PPSA99002.zip", "01.001.001", "PPSA99002.zip",
         "0eb678f8866a7905edf78ebf587f2e6f6fed27c0f8f4ec470ed0f26997ba136a"},
        {"Mandrake", "PPSA98510.zip", "1.6.1", "PPSA98510.zip",
         "338e80e3d6404bab2eec5d1401c29f9f1a7f52535f9c745d5c331a4a2bb06c5e"},
    };
    char name[128];
    EXPECT(github_release_asset_name("Nightshade-v{version}.zip", "v1.010.002", name, sizeof(name)) &&
               strcmp(name, "Nightshade-v1.010.002.zip") == 0,
           "{version} is the tag without its v");
    EXPECT(github_release_asset_name("App-{version}-{version}.zip", "1.6.1", name, sizeof(name)) &&
               strcmp(name, "App-1.6.1-1.6.1.zip") == 0,
           "{version} more than once");
    EXPECT(github_release_asset_name("PPSA99003.zip", "01.002.001", name, sizeof(name)) &&
               strcmp(name, "PPSA99003.zip") == 0,
           "a fixed name names itself");
    EXPECT(!github_release_asset_name("../{version}.zip", "1.0.0", name, sizeof(name)) && name[0] == '\0',
           "a name with a path is refused");
    EXPECT(!github_release_asset_name("x-{version}.zip", "1.0/../../a", name, sizeof(name)),
           "a tag that would make a path is refused");
    EXPECT(!github_release_asset_name("{version}.zip", "v1.0.0", name, 8), "a name too long for its room");

    for (size_t i = 0; i < sizeof(APPS) / sizeof(APPS[0]); ++i)
    {
        char file[64], what[200];
        size_t length, sums_length;
        snprintf(file, sizeof(file), "releases/%s-latest.json", APPS[i].app);
        char *json = slurp(file, &length);
        snprintf(file, sizeof(file), "releases/%s-SHA256SUMS", APPS[i].app);
        char *sums = slurp(file, &sums_length);
        github_release_asset asset;
        github_release_files files;
        snprintf(what, sizeof(what), "%s: the file its pattern names, from its real release", APPS[i].app);
        EXPECT(github_release_asset_read(json, length, APPS[i].pattern, "SHA256SUMS", &asset, &files) ==
                       GITHUB_RELEASE_NEWER &&
                   strcmp(asset.tag, APPS[i].tag) == 0 && strcmp(asset.name, APPS[i].name) == 0 && asset.size > 0 &&
                   files.sums_files == 1 && strcmp(files.digest, APPS[i].sha256) == 0,
               what);
        snprintf(what, sizeof(what), "%s: its date and notes", APPS[i].app);
        EXPECT(strncmp(asset.published, "2026-10-29T", 11) == 0 && strlen(asset.notes) > 40 &&
                   strstr(asset.notes, "**") == NULL,
               what);

        FakeGitHub github = {json, length, 200, sums, sums_length, 0};
        const self_update_platform platform = {fake_fetch, fake_download, NULL, NULL, NULL, NULL, NULL, &github};
        github_release_report report;
        char repo[64], url[256];
        snprintf(repo, sizeof(repo), "Oliv3rmoon/%s", APPS[i].app);
        snprintf(url, sizeof(url), "https://github.com/%s/releases/download/%s/%s", repo, APPS[i].tag, APPS[i].name);
        snprintf(what, sizeof(what), "%s: offered with the SHA-256 its SHA256SUMS gives", APPS[i].app);
        EXPECT(github_release_asset_check(&platform, repo, APPS[i].pattern, "SHA256SUMS", &asset, &report) ==
                       GITHUB_RELEASE_NEWER &&
                   asset.from_sums && strcmp(asset.sha256, APPS[i].sha256) == 0 && strcmp(asset.url, url) == 0 &&
                   github.sums_downloads == 1,
               what);

        /* The checksum file and GitHub's digest disagree: nothing is offered. */
        char *wrong = strdup(sums);
        wrong[0] = wrong[0] == '0' ? '1' : '0';
        github.sums = wrong;
        snprintf(what, sizeof(what), "%s: a checksum file that disagrees with GitHub's digest is refused",
                 APPS[i].app);
        EXPECT(github_release_asset_check(&platform, repo, APPS[i].pattern, "SHA256SUMS", &asset, &report) ==
                       GITHUB_RELEASE_HASHES_DIFFER &&
                   asset.sha256[0] == '\0',
               what);
        free(wrong);
        free(sums);
        free(json);
    }

    /* The pattern names a file the release doesn't have; a draft; a pre-release; a checksum file of another name. */
    size_t length;
    char *json = slurp("releases/Physalis-latest.json", &length);
    github_release_asset asset;
    github_release_files files;
    EXPECT(github_release_asset_read(json, length, "PPSA99999.zip", "SHA256SUMS", &asset, &files) ==
               GITHUB_RELEASE_NO_ZIP,
           "a file the release lacks is no offer");
    EXPECT(github_release_asset_read(json, length, "PPSA99003.zip", "CHECKSUMS.txt", &asset, &files) ==
                   GITHUB_RELEASE_NEWER &&
               files.sums_files == 0 && files.digest[0],
           "a checksum file the release lacks: GitHub's digest is what is left");
    char *edited = strdup(json);
    char *flag = strstr(edited, "\"prerelease\":false");
    memcpy(flag, "\"prerelease\":true ", 18);
    EXPECT(github_release_asset_read(edited, length, "PPSA99003.zip", "SHA256SUMS", &asset, &files) ==
               GITHUB_RELEASE_NOT_PUBLISHED,
           "a pre-release is never offered");
    free(edited);
    edited = strdup(json);
    flag = strstr(edited, "\"draft\":false");
    memcpy(flag, "\"draft\":true ", 13);
    EXPECT(github_release_asset_read(edited, length, "PPSA99003.zip", "SHA256SUMS", &asset, &files) ==
               GITHUB_RELEASE_NOT_PUBLISHED,
           "a draft is never offered");
    free(edited);
    /* No checksum file and no digest: nothing to check it against. */
    edited = strdup(json);
    for (char *at = strstr(edited, "\"digest\":\"sha256:"); at; at = strstr(at + 1, "\"digest\":\"sha256:"))
        memcpy(at + 10, "md5:", 4);
    FakeGitHub github = {edited, length, 200, NULL, 0, 0};
    const self_update_platform platform = {fake_fetch, fake_download, NULL, NULL, NULL, NULL, NULL, &github};
    github_release_report report;
    EXPECT(github_release_asset_check(&platform, "Oliv3rmoon/Physalis", "PPSA99003.zip", "CHECKSUMS.txt", &asset,
                                      &report) == GITHUB_RELEASE_NO_HASH,
           "a file with no checksum at all is never offered");
    free(edited);
    github.answer = "{\"message\":\"Not Found\"}";
    github.answer_length = strlen(github.answer);
    github.status = 404;
    EXPECT(github_release_asset_check(&platform, "Oliv3rmoon/Physalis", "PPSA99003.zip", NULL, &asset, &report) ==
               GITHUB_RELEASE_NONE,
           "no release");
    github.status = 403;
    EXPECT(github_release_asset_check(&platform, "Oliv3rmoon/Physalis", "PPSA99003.zip", NULL, &asset, &report) ==
               GITHUB_RELEASE_RATE_LIMITED,
           "GitHub's rate limit");
    free(json);
}

/* ---------------------------------------------------------------- checksums: the kit's job */

/* The helper's side of the connection: it answers what self_update.c waits for, and keeps what it is sent. */
typedef struct
{
    const char *answers;
    size_t answered;
    char *sent;
    size_t sent_length, sent_capacity;
    int closed;
} FakeHelper;

static long helper_send(void *user, const void *data, size_t size)
{
    FakeHelper *h = user;
    if (h->sent_length + size > h->sent_capacity)
    {
        h->sent_capacity = (h->sent_length + size) * 2;
        h->sent = realloc(h->sent, h->sent_capacity);
    }
    memcpy(h->sent + h->sent_length, data, size);
    h->sent_length += size;
    return (long)size;
}

static long helper_receive(void *user, void *data, size_t size)
{
    FakeHelper *h = user;
    const size_t left = strlen(h->answers) - h->answered;
    if (left == 0 || size == 0)
        return 0;
    ((char *)data)[0] = h->answers[h->answered++];
    return 1;
}

static void helper_close(void *user)
{
    ((FakeHelper *)user)->closed = 1;
}

/* After the request's lines: the archive in pieces. 1 when its end mark (a zero length) was sent. */
static int end_mark_sent(const FakeHelper *h, int request_lines, uint64_t *archive_bytes)
{
    size_t at = 0;
    *archive_bytes = 0;
    for (int line = 0; line < request_lines; ++line)
    {
        while (at < h->sent_length && h->sent[at] != '\n')
            ++at;
        ++at;
    }
    while (at + 4 <= h->sent_length)
    {
        const unsigned char *p = (const unsigned char *)h->sent + at;
        const size_t piece = (size_t)p[0] | (size_t)p[1] << 8 | (size_t)p[2] << 16 | (size_t)p[3] << 24;
        at += 4;
        if (piece == 0)
            return 1;
        at += piece;
        *archive_bytes += piece;
    }
    return 0;
}

typedef struct
{
    FakeHelper helper;
    const char *bytes; /* what the "download" delivers */
    size_t length;
} FakeConsole;

static int console_open_helper(void *user, self_update_channel *channel)
{
    FakeConsole *c = user;
    channel->send = helper_send;
    channel->receive = helper_receive;
    channel->close = helper_close;
    channel->user = &c->helper;
    return 1;
}

static int console_download(void *user, const char *url, uint64_t limit, self_update_sink sink, void *sink_user)
{
    FakeConsole *c = user;
    (void)url;
    if (c->length > limit)
        return -1;
    for (size_t at = 0; at < c->length; at += 1000)
        if (sink(sink_user, c->bytes + at, c->length - at < 1000 ? c->length - at : 1000) != 1)
            return -1;
    return 0;
}

static uint64_t console_now(void *user)
{
    (void)user;
    static uint64_t now;
    return now += 10;
}

static self_update_phase run_job(FakeConsole *console, const char *sha256, size_t size, self_update_status *status)
{
    const self_update_platform platform = {NULL, console_download, NULL, console_open_helper, NULL, NULL,
                                           console_now, console};
    self_update_offer offer;
    self_update_job job;
    memset(&offer, 0, sizeof(offer));
    memset(&job, 0, sizeof(job));
    snprintf(offer.title, sizeof(offer.title), "PPSA99003");
    snprintf(offer.name, sizeof(offer.name), "Physalis");
    snprintf(offer.installed, sizeof(offer.installed), "none");
    snprintf(offer.available, sizeof(offer.available), "01.002.001");
    snprintf(offer.version, sizeof(offer.version), "01.002.001");
    snprintf(offer.artifact, sizeof(offer.artifact),
             "https://github.com/Oliv3rmoon/Physalis/releases/download/01.002.001/PPSA99003.zip");
    snprintf(offer.sha256, sizeof(offer.sha256), "%s", sha256);
    snprintf(offer.beside, sizeof(offer.beside), "PPSA76526");
    offer.size = size;
    if (self_update_start(&job, &platform, &offer) != 1)
        return SELF_UPDATE_IDLE;
    for (int i = 0; i < 2000; ++i)
    {
        self_update_poll(&job, status);
        if (status->phase == SELF_UPDATE_FAILED || status->phase == SELF_UPDATE_INSTALLED ||
            status->phase == SELF_UPDATE_CANCELLED)
            break;
        usleep(1000);
    }
    self_update_finish(&job);
    return status->phase;
}

static void check_checksums(void)
{
    static char release[50000];
    char right[65], wrong[65];
    uint64_t archive = 0;
    self_update_status status;
    for (size_t i = 0; i < sizeof(release); ++i)
        release[i] = (char)(i * 31 + 7);
    sha256_hex(release, sizeof(release), right);
    memcpy(wrong, right, sizeof(right));
    wrong[0] = wrong[0] == 'a' ? 'b' : 'a';

    /* A download that isn't the release: refused, and the helper never told to unpack. */
    FakeConsole bad = {{"ready\nstaged\napplying\ninstalled 01.002.001 /data/homebrew/PPSA99003\n", 0, NULL, 0, 0, 0},
                       release, sizeof(release)};
    EXPECT(run_job(&bad, wrong, sizeof(release), &status) == SELF_UPDATE_FAILED &&
               strstr(status.error, "doesn't match") != NULL,
           "the kit refuses a download whose SHA-256 isn't the release's");
    EXPECT(!end_mark_sent(&bad.helper, 10, &archive) && bad.helper.closed && archive == sizeof(release),
           "... before its end: the helper unpacks nothing (it removes what it saved when the connection ends)");
    free(bad.helper.sent);

    /* The same with the right SHA-256: the end mark goes, and the install goes through to its end. */
    FakeConsole good = {{"ready\nstaged\napplying\ninstalled 01.002.001 /data/homebrew/PPSA99003\n", 0, NULL, 0, 0, 0},
                        release, sizeof(release)};
    EXPECT(run_job(&good, right, sizeof(release), &status) == SELF_UPDATE_INSTALLED &&
               strcmp(status.version, "01.002.001") == 0 &&
               strcmp(status.folder, "/data/homebrew/PPSA99003") == 0,
           "the release itself is installed");
    EXPECT(end_mark_sent(&good.helper, 10, &archive) && archive == sizeof(release) &&
               strncmp(good.helper.sent, "PSU1\ninstall\nPPSA99003\nnone\n01.002.001\n50000\n", 44) == 0,
           "... as an install request beside Solanum, with its end mark");
    EXPECT(strstr(good.helper.sent, "\nPPSA76526\n") != NULL, "... naming Solanum as the app asking");
    free(good.helper.sent);

    /* A download shorter than the release lists: refused too. */
    FakeConsole short_one = {{"ready\n", 0, NULL, 0, 0, 0}, release, sizeof(release) - 1};
    EXPECT(run_job(&short_one, right, sizeof(release), &status) == SELF_UPDATE_FAILED &&
               !end_mark_sent(&short_one.helper, 10, &archive),
           "a download shorter than the release is refused");
    free(short_one.helper.sent);
}

/* ---------------------------------------------------------------- resuming */

typedef struct
{
    const char *bytes;
    size_t length;
    size_t cut_after;  /* the connection drops after this many bytes of one answer (0: never) */
    int ignore_range;  /* answers 200 with the whole file to a range */
    uint64_t offsets[8];
    int requests;
} FakeServer;

static int fake_range(void *user, const char *url, uint64_t offset, uint64_t limit, self_update_sink sink,
                      void *sink_user, int *status)
{
    FakeServer *s = user;
    (void)url;
    (void)limit;
    if (s->requests < 8)
        s->offsets[s->requests] = offset;
    ++s->requests;
    *status = offset > 0 && !s->ignore_range ? 206 : 200;
    if (offset > 0 && *status != 206)
        return -1;
    size_t sent = 0;
    for (size_t at = (size_t)offset; at < s->length; at += 700)
    {
        const size_t piece = s->length - at < 700 ? s->length - at : 700;
        if (s->cut_after && sent + piece > s->cut_after)
            return -(10000 + 56); /* CURLE_RECV_ERROR */
        if (sink(sink_user, s->bytes + at, piece) != 1)
            return -1;
        sent += piece;
    }
    return 0;
}

typedef struct
{
    char *data;
    size_t length;
} Received;

static int receive(void *user, const void *data, size_t size)
{
    Received *r = user;
    r->data = realloc(r->data, r->length + size);
    memcpy(r->data + r->length, data, size);
    r->length += size;
    return 1;
}

static uint64_t size_of(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 ? (uint64_t)info.st_size : 0;
}

static void check_resume(void)
{
    static char release[30000];
    for (size_t i = 0; i < sizeof(release); ++i)
        release[i] = (char)(i * 7 + 3);
    FakeServer server = {release, sizeof(release), 12000, 0, {0}, 0};
    ResumeDownload d;
    memset(&d, 0, sizeof(d));
    char cache[600];
    snprintf(cache, sizeof(cache), "%s/cache", scratch);
    mkdir(cache, 0755);
    sha256_hex(release, sizeof(release), d.sha256);
    resume_part_path(d.part, sizeof(d.part), cache, "PPSA99003", d.sha256);
    d.size = sizeof(release);
    d.range = fake_range;
    d.range_user = &server;
    remove(d.part);

    /* Cut off: what arrived stays in the copy. */
    Received first = {NULL, 0};
    EXPECT(resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive,
                           &first) < 0 &&
               size_of(d.part) == first.length && first.length > 0 && first.length < sizeof(release),
           "an interrupted download keeps what arrived");
    const size_t kept = first.length;
    free(first.data);

    /* Again: the copy first, then only the rest from the network, with a range. */
    server.cut_after = 0;
    Received second = {NULL, 0};
    EXPECT(resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive,
                           &second) == 0 &&
               second.length == sizeof(release) && memcmp(second.data, release, sizeof(release)) == 0 &&
               !d.mismatch,
           "the next try hands on the whole file");
    EXPECT(server.requests == 2 && server.offsets[1] == kept && d.reused == kept &&
               d.fetched == sizeof(release) - kept && d.status == 206,
           "... asking the network only for what was missing");
    free(second.data);

    /* Complete in the copy: nothing is asked of the network. */
    Received third = {NULL, 0};
    EXPECT(resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive,
                           &third) == 0 &&
               third.length == sizeof(release) && server.requests == 2 && d.fetched == 0,
           "a whole copy is handed on without a request");
    free(third.data);

    /* A copy that isn't the release (a disk that went wrong): handed on, found out, deleted. */
    FILE *copy = fopen(d.part, "r+b");
    fputc('X', copy);
    fclose(copy);
    Received fourth = {NULL, 0};
    EXPECT(resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive,
                           &fourth) == 0 &&
               d.mismatch && size_of(d.part) == 0,
           "a copy that doesn't match the release's SHA-256 is deleted, never resumed");
    free(fourth.data);

    /* A server that ignores ranges: the copy goes, the try fails, the next starts over. */
    server.cut_after = 12000;
    Received fifth = {NULL, 0};
    (void)resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive, &fifth);
    free(fifth.data);
    server.cut_after = 0;
    server.ignore_range = 1;
    Received sixth = {NULL, 0};
    EXPECT(resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive,
                           &sixth) < 0 &&
               size_of(d.part) == 0 && d.status == 200,
           "a server that ignores the range: the copy is dropped");
    free(sixth.data);
    Received seventh = {NULL, 0};
    EXPECT(resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive,
                           &seventh) == 0 &&
               seventh.length == sizeof(release) && !d.mismatch,
           "... and the next try starts from the beginning");
    free(seventh.data);

    /* The release's own bytes, but the wrong release: the download is the one refused. */
    server.ignore_range = 0;
    remove(d.part);
    char other[65];
    memcpy(other, d.sha256, sizeof(other));
    d.sha256[5] = d.sha256[5] == 'f' ? 'e' : 'f';
    Received eighth = {NULL, 0};
    EXPECT(resume_download(&d, "https://github.com/x/y/releases/download/1/a.zip", sizeof(release), receive,
                           &eighth) == 0 &&
               d.mismatch && size_of(d.part) == 0,
           "a download that doesn't match its SHA-256 leaves no copy behind");
    free(eighth.data);
    memcpy(d.sha256, other, sizeof(other));

    /* An older release's copy goes when a newer one is downloaded. */
    char older[700];
    resume_part_path(older, sizeof(older), cache, "PPSA99003", "0123456789abcdef0123456789abcdef");
    write_file(older, "old", 3);
    write_file(d.part, "new", 3);
    char unrelated[700];
    resume_part_path(unrelated, sizeof(unrelated), cache, "PPSA99002", "0123456789abcdef0123456789abcdef");
    write_file(unrelated, "other app", 9);
    resume_forget_others(cache, "PPSA99003", d.part);
    EXPECT(size_of(older) == 0 && size_of(d.part) == 3 && size_of(unrelated) == 9,
           "an older release's copy of the same app is deleted, the rest kept");
    remove(d.part);
    remove(unrelated);
}

/* ---------------------------------------------------------------- where an app is, what is offered */

static void param(const char *folder, const char *title, const char *version)
{
    char path[900], text[200];
    snprintf(path, sizeof(path), "%s/sce_sys/param.json", folder);
    const int n = snprintf(text, sizeof(text), "{\"titleId\": \"%s\", \"contentVersion\": \"%s\"}", title, version);
    write_file(path, text, (size_t)n);
}

static void check_apps(void)
{
    char root[700], homebrew[760], user[760], folder[900], own[800];
    snprintf(root, sizeof(root), "%s/console", scratch);
    snprintf(homebrew, sizeof(homebrew), "%s/data/homebrew", root);
    snprintf(user, sizeof(user), "%s/user", root);
    snprintf(folder, sizeof(folder), "%s/PPSA76526", homebrew);
    param(folder, "PPSA76526", "00.001.000");
    snprintf(folder, sizeof(folder), "%s/PPSA99003", homebrew);
    param(folder, "PPSA99003", "01.002.000");
    snprintf(folder, sizeof(folder), "%s/Datura", homebrew);
    param(folder, "PPSA99002", "01.001.001");
    char eboot[960];
    snprintf(eboot, sizeof(eboot), "%s/PPSA99003/eboot.bin", homebrew);
    write_file(eboot, "1234567890", 10);

    char install_dir[900];
    snprintf(install_dir, sizeof(install_dir), "%s/PPSA76526", homebrew);
    apps_own_folder("PPSA76526", user, install_dir, own, sizeof(own));
    EXPECT(strcmp(own, install_dir) == 0, "Solanum's own folder");

    Installed found = apps_find("PPSA99003", user, own, homebrew);
    snprintf(folder, sizeof(folder), "%s/PPSA99003", homebrew);
    EXPECT(strcmp(found.folder, folder) == 0 && strcmp(found.version, "01.002.000") == 0 && !found.image,
           "an app in the folder named after its title");
    found = apps_find("PPSA99002", user, own, homebrew);
    snprintf(folder, sizeof(folder), "%s/Datura", homebrew);
    EXPECT(strcmp(found.folder, folder) == 0, "an app in a folder of another name");
    found = apps_find("PPSA98510", user, own, homebrew);
    EXPECT(found.folder[0] == '\0', "an app that isn't installed");

    /* ShadowMountPlus's records: a folder elsewhere, an image. */
    char elsewhere[900], record[900];
    snprintf(elsewhere, sizeof(elsewhere), "%s/mnt/usb0/homebrew/PPSA98510", root);
    param(elsewhere, "PPSA98510", "01.006.000");
    snprintf(record, sizeof(record), "%s/app/PPSA98510/mount.lnk", user);
    char line[1000];
    const int n = snprintf(line, sizeof(line), "%s/\n", elsewhere);
    write_file(record, line, (size_t)n);
    found = apps_find("PPSA98510", user, own, homebrew);
    EXPECT(strcmp(found.folder, elsewhere) == 0 && strcmp(found.version, "01.006.000") == 0,
           "an app where ShadowMountPlus mounted it from (a USB drive)");
    snprintf(record, sizeof(record), "%s/app/PPSA99008/mount_img.lnk", user);
    write_file(record, "/mnt/usb0/Nightshade.ffpkg", 26);
    snprintf(folder, sizeof(folder), "%s/app/PPSA99008", user);
    param(folder, "PPSA99008", "01.010.002");
    found = apps_find("PPSA99008", user, own, homebrew);
    EXPECT(found.image && strcmp(found.version, "01.010.002") == 0, "an app installed as an image");

    uint64_t bytes = 0;
    snprintf(folder, sizeof(folder), "%s/PPSA99003", homebrew);
    const uint64_t param_size = (uint64_t)strlen("{\"titleId\": \"PPSA99003\", \"contentVersion\": \"01.002.000\"}");
    EXPECT(apps_folder_size(folder, &bytes) && bytes == 10 + param_size, "an app's size: every file under its folder");

    char version[12];
    EXPECT(apps_tag_version("v1.010.002", version) == TAG_EXACT && strcmp(version, "01.010.002") == 0,
           "Nightshade's tag");
    EXPECT(apps_tag_version("01.002.001", version) == TAG_EXACT && strcmp(version, "01.002.001") == 0,
           "a content version as the tag");
    EXPECT(apps_tag_version("1.6.1", version) == TAG_LIKELY && strcmp(version, "01.006.001") == 0,
           "Mandrake's tag: likely 01.006.001");
    EXPECT(apps_tag_version("nightly-2026", version) == TAG_NONE, "a tag that names no version");

    Installed none;
    memset(&none, 0, sizeof(none));
    Installed old = {"/data/homebrew/PPSA99003", "01.002.000", false};
    Installed same = {"/data/homebrew/PPSA99003", "01.002.001", false};
    Installed image = {"/user/app/PPSA99003", "01.002.000", true};
    EXPECT(apps_decide(&none, true, "01.002.001") == OFFER_GET, "not installed: get it");
    EXPECT(apps_decide(&none, false, "") == OFFER_NONE, "not installed, no release: nothing");
    EXPECT(apps_decide(&old, true, "01.002.001") == OFFER_UPDATE, "an older one: update it");
    EXPECT(apps_decide(&same, true, "01.002.001") == OFFER_CURRENT, "the same: current");
    EXPECT(apps_decide(&same, true, "v1.002.000") == OFFER_CURRENT, "a newer one installed: current");
    EXPECT(apps_decide(&image, true, "01.002.001") == OFFER_NONE, "an image: nothing here");
    EXPECT(apps_decide(&old, true, "nightly") == OFFER_UPDATE, "a tag with no version: offered all the same");

    CatalogApp app;
    memset(&app, 0, sizeof(app));
    snprintf(app.title_id, sizeof(app.title_id), "PPSA99003");
    snprintf(app.name, sizeof(app.name), "Physalis");
    github_release_asset release;
    memset(&release, 0, sizeof(release));
    snprintf(release.tag, sizeof(release.tag), "01.002.001");
    snprintf(release.sha256, sizeof(release.sha256), "3e1ddd6402f2a19ab3feee249ae2cb9f8a7a2894e64868347990a3bda785bf6a");
    snprintf(release.url, sizeof(release.url),
             "https://github.com/Oliv3rmoon/Physalis/releases/download/01.002.001/PPSA99003.zip");
    release.size = 40713597;
    self_update_offer offer;
    EXPECT(apps_make_offer(&app, &release, &none, "PPSA76526", &offer) && strcmp(offer.installed, "none") == 0 &&
               strcmp(offer.available, "01.002.001") == 0 && strcmp(offer.beside, "PPSA76526") == 0 &&
               offer.size == 40713597 && strcmp(offer.sha256, release.sha256) == 0,
           "the offer for a new app: beside Solanum");
    EXPECT(apps_make_offer(&app, &release, &old, "PPSA76526", &offer) && strcmp(offer.installed, "01.002.000") == 0,
           "the offer for an update: the installed version");
    snprintf(release.tag, sizeof(release.tag), "1.6.1");
    EXPECT(apps_make_offer(&app, &release, &old, "PPSA76526", &offer) && strcmp(offer.available, "any") == 0,
           "a likely version is the ZIP's to say");
    EXPECT(!apps_make_offer(&app, &release, &image, "PPSA76526", &offer), "no offer for an image");
    EXPECT(!apps_make_offer(&app, &release, &none, "PPSA99003", &offer), "no offer of the asking app itself");
    release.sha256[0] = '\0';
    EXPECT(!apps_make_offer(&app, &release, &none, "PPSA76526", &offer), "no offer without a SHA-256");
    snprintf(release.sha256, sizeof(release.sha256), "3e1ddd6402f2a19ab3feee249ae2cb9f8a7a2894e64868347990a3bda785bf6a");
    snprintf(release.url, sizeof(release.url), "https://example.com/PPSA99003.zip");
    EXPECT(!apps_make_offer(&app, &release, &none, "PPSA76526", &offer), "no offer from anywhere but GitHub");
}

/* ---------------------------------------------------------------- opening */

static void check_launch(void)
{
    char reason[200];
    FakeHelper payload = {"waiting\n", 0, NULL, 0, 0, 0};
    self_update_channel channel = {helper_send, helper_receive, helper_close, &payload};
    EXPECT(apps_ask_launch(&channel, "PPSA99003", "PPSA76526", reason, sizeof(reason)) &&
               payload.sent_length == 25 && memcmp(payload.sent, "PSL1\nPPSA99003\nPPSA76526\n", 25) == 0,
           "the launch payload is asked to open the app once Solanum closes");
    free(payload.sent);
    FakeHelper refused = {"fail The console doesn't list the app yet\n", 0, NULL, 0, 0, 0};
    channel.user = &refused;
    EXPECT(!apps_ask_launch(&channel, "PPSA99003", "PPSA76526", reason, sizeof(reason)) &&
               strcmp(reason, APPS_NOT_LISTED) == 0,
           "an app the console doesn't list yet");
    free(refused.sent);
    FakeHelper silent = {"", 0, NULL, 0, 0, 0};
    channel.user = &silent;
    EXPECT(!apps_ask_launch(&channel, "PPSA99003", "PPSA76526", reason, sizeof(reason)) &&
               strcmp(reason, "The launch payload didn't answer") == 0,
           "a payload that doesn't answer");
    free(silent.sent);
}

/* ---------------------------------------------------------------- the hidden ones */

static int feed(Konami *k, const int *buttons, int count)
{
    int done = 0;
    for (int i = 0; i < count; ++i)
        done += konami_feed(k, buttons[i] < 0 ? 0 : BIT(buttons[i]));
    return done;
}

static void check_konami(void)
{
    static const int CODE[] = {BTN_UP,    BTN_UP,   BTN_DOWN,  BTN_DOWN,   BTN_LEFT,
                               BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_CIRCLE, BTN_CROSS};
    Konami k;
    konami_reset(&k);
    int fired = 0;
    for (int i = 0; i < 10; ++i)
    {
        const bool done = konami_feed(&k, BIT(CODE[i]));
        if (done)
            fired = i + 1;
    }
    EXPECT(fired == 10, "the code, on its last press (Cross)");
    EXPECT(!konami_feed(&k, BIT(BTN_CROSS)), "a Cross after it is only a Cross");

    konami_reset(&k);
    static const int EXTRA_UP[] = {BTN_UP,   BTN_UP,    BTN_UP,   BTN_DOWN,  BTN_DOWN,   BTN_LEFT,
                                   BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_CIRCLE, BTN_CROSS};
    EXPECT(feed(&k, EXTRA_UP, 11) == 1, "a held Up that repeats once more still counts");

    konami_reset(&k);
    static const int BROKEN[] = {BTN_UP,    BTN_UP,     BTN_DOWN,   BTN_DOWN,  BTN_LEFT,  BTN_RIGHT,
                                 BTN_LEFT,  BTN_SQUARE, BTN_RIGHT,  BTN_CIRCLE, BTN_CROSS};
    EXPECT(feed(&k, BROKEN, 11) == 0, "another button in the middle breaks it");

    konami_reset(&k);
    static const int WRONG_END[] = {BTN_UP,   BTN_UP,    BTN_DOWN, BTN_DOWN,  BTN_LEFT,
                                    BTN_RIGHT, BTN_LEFT, BTN_RIGHT, BTN_CROSS, BTN_CIRCLE};
    EXPECT(feed(&k, WRONG_END, 10) == 0, "Cross then Circle is not it");

    konami_reset(&k);
    EXPECT(feed(&k, CODE, 8) == 0 && !konami_feed(&k, BIT(BTN_CIRCLE) | BIT(BTN_CROSS)) &&
               !konami_feed(&k, BIT(BTN_CROSS)),
           "Circle and Cross pressed together are not Circle then Cross");

    konami_reset(&k);
    static const int RESTARTED[] = {BTN_UP,    BTN_UP,     BTN_DOWN, BTN_UP,    BTN_UP,    BTN_DOWN,  BTN_DOWN,
                                    BTN_LEFT,  BTN_RIGHT,  BTN_LEFT, BTN_RIGHT, BTN_CIRCLE, BTN_CROSS};
    EXPECT(feed(&k, RESTARTED, 13) == 1, "started over in the middle of a try");

    konami_reset(&k);
    EXPECT(feed(&k, CODE, 10) == 1 && feed(&k, CODE, 10) == 1, "twice in a row, twice");
    EXPECT(!konami_feed(&k, 0), "no press is nothing");
}

static void check_pick(void)
{
    uint32_t state = apps_seed(123456789);
    int seen[8] = {0};
    int previous = -1;
    int repeats = 0;
    EXPECT(apps_pick(0, -1, &state) == -1, "no lines: none");
    EXPECT(apps_pick(1, 0, &state) == 0 && apps_pick(1, -1, &state) == 0, "one line: that one, again");
    for (int i = 0; i < 4000; ++i)
    {
        const int pick = apps_pick(8, previous, &state);
        if (pick < 0 || pick >= 8)
        {
            EXPECT(0, "a pick out of range");
            return;
        }
        repeats += pick == previous;
        ++seen[pick];
        previous = pick;
    }
    EXPECT(repeats == 0, "never the same line twice in a row");
    int fewest = 4000, most = 0;
    for (int i = 0; i < 8; ++i)
    {
        fewest = seen[i] < fewest ? seen[i] : fewest;
        most = seen[i] > most ? seen[i] : most;
    }
    EXPECT(fewest > 350 && most < 650, "every line comes up, about as often as the others");
    EXPECT(apps_seed(1) != apps_seed(2) && apps_seed(0) != 0, "seeds differ, and are never 0");
    uint32_t a = apps_seed(42), b = apps_seed(42);
    EXPECT(apps_pick(10, -1, &a) == apps_pick(10, -1, &b), "the same seed picks the same");
    EXPECT(apps_pick(8, 99, &state) >= 0, "a previous one out of range is no previous");
}

/* ---------------------------------------------------------------- 0.2.0: the catalog's pins, tags, data folders */

static const char *const PIN_SHA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char *const OLD_SHA = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

/* A catalog of one app with a pinned release, its older pins, tags, author, stars and a data folder. */
static const char PINNED[] =
    "{\"schema\":1,\"apps\":["
    "{\"id\":\"kodi\",\"name\":\"Kodi\",\"category\":\"media\",\"summary\":\"s\",\"title_id\":\"PPSA99420\","
    "\"repo\":\"Notedop/xbmc-ps5\",\"asset\":\"Kodi-{version}.zip\",\"folder\":\"PPSA99420\",\"checksums\":\"SHA256SUMS\","
    "\"attested\":false,\"license\":\"GPL-2.0-or-later\",\"author\":\"Notedop\",\"stars\":42,"
    "\"tags\":[\"media-player\",\"kodi\",\"Bad Tag\",\"video\"],\"data\":\"/data/kodi\","
    "\"release\":{\"tag\":\"v22.0.1\",\"asset\":\"Kodi-22.0.1.zip\",\"size\":1000,"
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"content_version\":\"22.000.001\","
    "\"published_at\":\"2026-10-20T10:00:00Z\",\"notes\":\"## Fixed\\n- **Faster** start\"},"
    "\"pins\":[{\"tag\":\"v22.0.0\",\"asset\":\"Kodi-22.0.0.zip\",\"size\":900,"
    "\"sha256\":\"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\"},"
    "{\"tag\":\"v21.9.0\",\"asset\":\"Elsewhere.zip\",\"size\":900,"
    "\"sha256\":\"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\"}]},"
    "{\"id\":\"jelly\",\"name\":\"Jelly5\",\"category\":\"media\",\"summary\":\"s\",\"title_id\":\"PPSA99505\","
    "\"repo\":\"o/jelly5\",\"asset\":\"Jelly5-{content}.zip\",\"folder\":\".\",\"checksums\":\"SHA256SUMS\","
    "\"attested\":false,\"license\":\"GPL-3.0\",\"data\":\"/data/homebrew\","
    "\"release\":{\"tag\":\"v0.7.0\",\"asset\":\"Jelly5-0.7.0.zip\",\"size\":5,"
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}}"
    "]}";

static void check_pins(void)
{
    char name[128];
    log_count = 0;
    EXPECT(catalog_parse(PINNED, sizeof(PINNED) - 1, &catalog, collect, NULL) == 2, "pinned apps are read");
    const CatalogApp *kodi = &catalog.apps[0], *jelly = &catalog.apps[1];
    EXPECT(strcmp(kodi->author, "Notedop") == 0 && kodi->stars == 42 && strcmp(kodi->data, "/data/kodi") == 0,
           "author, stars and data folder");
    EXPECT(kodi->tag_count == 3 && strcmp(kodi->tags[0], "media-player") == 0 && strcmp(kodi->tags[2], "video") == 0 &&
               logged("tag 2 is not lowercase"),
           "tags: the good ones kept, a bad one logged");
    EXPECT(kodi->pin_count == 2 && strcmp(kodi->pins[0].tag, "v22.0.1") == 0 && kodi->pins[0].size == 1000 &&
               strcmp(kodi->pins[1].tag, "v22.0.0") == 0 && logged("an older pin can't be read"),
           "the pinned release and an older pin; a pin of another file is left out");
    EXPECT(strcmp(kodi->published, "2026-10-20T10:00:00Z") == 0 && strstr(kodi->notes, "Faster"), "its date and notes");
    EXPECT(jelly->pin_count == 0 && jelly->data[0] == '\0' && logged("isn't its file") &&
               logged("is not a folder of its own in /data"),
           "a pin whose file isn't the pattern's, and /data/homebrew as a data folder, are left out");
    EXPECT(github_release_asset_name("Jelly5-{content}.zip", "v0.7.0", name, sizeof(name)) &&
               strcmp(name, "Jelly5-00.007.000.zip") == 0 && strcmp(jelly->folder, ".") == 0,
           "{content} is the tag as a content version; '.' is a ZIP whose root is the app");
    EXPECT(!github_release_asset_name("Jelly5-{content}.zip", "nightly", name, sizeof(name)),
           "{content} of a tag that isn't three numbers names nothing");

    /* The release from the pin, with nothing asked of GitHub. */
    github_release_asset release;
    EXPECT(apps_release_from_pin(kodi, 0, &release) && release.pinned && strcmp(release.sha256, PIN_SHA) == 0 &&
               strcmp(release.url, "https://github.com/Notedop/xbmc-ps5/releases/download/v22.0.1/Kodi-22.0.1.zip") ==
                   0 &&
               release.size == 1000 && strstr(release.notes, "Faster") && !strstr(release.notes, "**"),
           "the pinned release, its address made from the repository, its notes laid out");
    EXPECT(apps_release_from_pin(kodi, 1, &release) && strcmp(release.tag, "v22.0.0") == 0 && !release.notes[0] &&
               !release.content[0],
           "an older pin");

    /* The content version the workflow read decides, whatever the tag says. */
    github_release_asset newest;
    apps_release_from_pin(kodi, 0, &newest);
    Installed older = {.folder = "/data/homebrew/PPSA99420", .version = "22.000.000"};
    Installed same = {.folder = "/data/homebrew/PPSA99420", .version = "22.000.001"};
    Installed later = {.folder = "/data/homebrew/PPSA99420", .version = "23.000.000"};
    Installed none = {.folder = ""};
    EXPECT(strcmp(newest.content, "22.000.001") == 0 && apps_decide_release(&older, true, &newest) == OFFER_UPDATE &&
               apps_decide_release(&same, true, &newest) == OFFER_CURRENT &&
               apps_decide_release(&later, true, &newest) == OFFER_CURRENT &&
               apps_decide_release(&none, true, &newest) == OFFER_GET,
           "the pinned release's content version against the installed one");
    github_release_asset odd = newest;
    snprintf(odd.tag, sizeof(odd.tag), "build25");
    EXPECT(apps_decide_release(&same, true, &odd) == OFFER_CURRENT && apps_decide(&same, true, "build25") == OFFER_UPDATE,
           "a tag that names no version is told right by its content version (by the tag alone it can't be)");
    self_update_offer offer;
    EXPECT(apps_make_offer(kodi, &newest, &older, "PPSA76526", &offer) && strcmp(offer.available, "22.000.001") == 0 &&
               strcmp(offer.installed, "22.000.000") == 0,
           "the offer names the content version, which the helper checks the ZIP holds");
    EXPECT(!apps_release_from_pin(jelly, 0, &release), "no pin, no release");
    EXPECT(apps_pin_for(kodi, "v22.0.0", "Kodi-22.0.0.zip") == 1 && apps_pin_for(kodi, "v9", NULL) == -1,
           "a pin by its tag");

    /* GitHub's answer checked against the pins. */
    github_release_asset asked;
    memset(&asked, 0, sizeof(asked));
    snprintf(asked.tag, sizeof(asked.tag), "v22.0.1");
    snprintf(asked.name, sizeof(asked.name), "Kodi-22.0.1.zip");
    asked.size = 1000;
    EXPECT(apps_check_pin(kodi, GITHUB_RELEASE_NO_HASH, &asked) == GITHUB_RELEASE_NEWER && asked.pinned &&
               strcmp(asked.sha256, PIN_SHA) == 0 && strstr(asked.url, "/releases/download/v22.0.1/Kodi-22.0.1.zip"),
           "a release with no checksum file and no digest is offered with the pinned SHA-256");
    github_release_asset other = asked;
    other.size = 999;
    other.sha256[0] = '\0';
    other.pinned = 0;
    EXPECT(apps_check_pin(kodi, GITHUB_RELEASE_NO_HASH, &other) == GITHUB_RELEASE_HASHES_DIFFER && !other.sha256[0],
           "a file of another size than the pinned one is refused");
    github_release_asset digest = asked;
    snprintf(digest.sha256, sizeof(digest.sha256), "%s", OLD_SHA);
    digest.pinned = 0;
    EXPECT(apps_check_pin(kodi, GITHUB_RELEASE_NEWER, &digest) == GITHUB_RELEASE_HASHES_DIFFER && !digest.sha256[0],
           "a checksum file or digest that disagrees with the pin: the file changed after it was pinned, refused");
    github_release_asset agrees = asked;
    snprintf(agrees.sha256, sizeof(agrees.sha256), "%s", PIN_SHA);
    agrees.pinned = 0;
    EXPECT(apps_check_pin(kodi, GITHUB_RELEASE_NEWER, &agrees) == GITHUB_RELEASE_NEWER && agrees.pinned,
           "a digest that agrees with the pin: pinned and checked twice");
    github_release_asset newer = asked;
    snprintf(newer.tag, sizeof(newer.tag), "v22.0.2");
    snprintf(newer.name, sizeof(newer.name), "Kodi-22.0.2.zip");
    newer.sha256[0] = '\0';
    newer.pinned = 0;
    EXPECT(apps_check_pin(kodi, GITHUB_RELEASE_NO_HASH, &newer) == GITHUB_RELEASE_NO_HASH,
           "a newer release the catalog hasn't pinned, with no checksum: still not offered");
    EXPECT(apps_check_pin(kodi, GITHUB_RELEASE_RATE_LIMITED, &asked) == GITHUB_RELEASE_RATE_LIMITED,
           "other answers pass through");

    EXPECT(catalog_data_folder_valid("/data/nightshade") && catalog_data_folder_valid("/data/Kodi_Data-2") &&
               !catalog_data_folder_valid("/data/etaHEN") && !catalog_data_folder_valid("/data/solanum") &&
               !catalog_data_folder_valid("/data/a/b") && !catalog_data_folder_valid("/data/..") &&
               !catalog_data_folder_valid("/user/data") && !catalog_data_folder_valid("/data/"),
           "data folders: one of its own in /data, never one the console, the HEN or Solanum keeps");
}

/* Solanum-Catalog's catalog as its pin workflow writes it (fixtures/catalog-pinned.json, 0.2.0's): every app read,
 * nothing left out, every one with its pinned release, so not one request to GitHub's API is needed to show them. */
static void check_pinned_catalog(void)
{
    size_t length;
    char *json = slurp("catalog-pinned.json", &length);
    log_count = 0;
    const int count = catalog_parse(json, length, &catalog, collect, NULL);
    EXPECT(count >= 50, "the pinned catalog's apps are read");
    for (int i = 0; i < log_count; ++i)
        printf("  logged: %s\n", log_lines[i]);
    EXPECT(log_count == 0, "the pinned catalog leaves nothing out");
    int pinned = 0, tagged = 0;
    for (int i = 0; i < catalog.app_count; ++i)
    {
        github_release_asset release;
        pinned += apps_release_from_pin(&catalog.apps[i], 0, &release) && strlen(release.sha256) == 64 &&
                  strncmp(release.url, "https://github.com/", 19) == 0;
        tagged += catalog.apps[i].tag_count > 0;
    }
    EXPECT(pinned == catalog.app_count, "every app's release is pinned, with an address on github.com");
    EXPECT(tagged == catalog.app_count, "every app has tags to search by");
    free(json);
}

static void check_merge(void)
{
    static Catalog official, custom;
    size_t length;
    char *json = slurp("catalog.json", &length);
    EXPECT(catalog_parse(json, length, &official, NULL, NULL) == 4, "Solanum's catalog");
    free(json);
    static const char third[] =
        "{\"schema\":1,\"apps\":["
        "{\"id\":\"nightshade\",\"name\":\"Fake Nightshade\",\"category\":\"emulator\",\"summary\":\"s\","
        "\"title_id\":\"ABCD00001\",\"repo\":\"evil/n\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\","
        "\"attested\":true,\"license\":\"MIT\"},"
        "{\"id\":\"other\",\"name\":\"Same title\",\"category\":\"emulator\",\"summary\":\"s\","
        "\"title_id\":\"PPSA99008\",\"repo\":\"evil/o\",\"asset\":\"a.zip\",\"folder\":\"f\",\"checksums\":\"SHA256SUMS\","
        "\"attested\":true,\"license\":\"MIT\"},"
        "{\"id\":\"newapp\",\"name\":\"New\",\"family\":true,\"category\":\"tool\",\"summary\":\"s\","
        "\"title_id\":\"ABCD00002\",\"repo\":\"someone/new\",\"asset\":\"New-{version}.zip\",\"folder\":\"f\","
        "\"checksums\":\"SHA256SUMS\",\"attested\":true,\"license\":\"MIT\",\"data\":\"/data/newapp\",\"stars\":9000,"
        "\"release\":{\"tag\":\"v1.0.0\",\"asset\":\"New-1.0.0.zip\",\"size\":10,"
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}}"
        "]}";
    EXPECT(catalog_parse(third, sizeof(third) - 1, &custom, NULL, NULL) == 3, "a custom catalog");
    int hidden = 0;
    EXPECT(catalog_merge(&official, &custom, 1, &hidden) == 1 && hidden == 2 && official.app_count == 5,
           "a custom catalog's apps whose id or title ID another catalog lists are left out");
    const CatalogApp *added = &official.apps[4];
    EXPECT(strcmp(added->id, "newapp") == 0 && added->source == 1 && !added->family && !added->attested &&
               !added->data[0] && added->pin_count == 0 && added->stars == 0,
           "a third-party app: never family, never attested, no data folder to remove, no pins, no stars");
    EXPECT(official.apps[0].source == 0 && strcmp(official.apps[0].repo, "Oliv3rmoon/Nightshade") == 0,
           "Solanum's own apps stay as they are");
}

static void check_search(void)
{
    log_count = 0;
    catalog_parse(PINNED, sizeof(PINNED) - 1, &catalog, NULL, NULL);
    const CatalogApp *kodi = &catalog.apps[0];
    EXPECT(apps_matches(kodi, "") && apps_matches(kodi, "KOD") && apps_matches(kodi, "notedop") &&
               apps_matches(kodi, "media") && apps_matches(kodi, "player"),
           "a search looks in the name, the author, the category and the tags, whatever the case");
    EXPECT(apps_matches(kodi, "#kodi") && apps_matches(kodi, "#Media-Player") && apps_matches(kodi, "tag:video") &&
               !apps_matches(kodi, "#media") && !apps_matches(kodi, "#") && !apps_matches(kodi, "emulator"),
           "#tag is a tag exactly");
    EXPECT(apps_update_wanted(OFFER_UPDATE, false, "", "v2") && !apps_update_wanted(OFFER_UPDATE, true, "", "v2") &&
               !apps_update_wanted(OFFER_UPDATE, false, "v2", "v2") && apps_update_wanted(OFFER_UPDATE, false, "v1", "v2") &&
               !apps_update_wanted(OFFER_CURRENT, false, "", "v2"),
           "updates: not when held, not the skipped release, a newer one again");
}

static void check_prefs(void)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/apps.ini", scratch);
    remove(path);
    prefs_load(path);
    EXPECT(prefs_count() == 0 && prefs_get("PPSA99003", false) == NULL, "no file, no decisions");
    AppPrefs *one = prefs_get("PPSA99003", true);
    one->hold = true;
    AppPrefs *two = prefs_get("PPSA99002", true);
    snprintf(two->skip, sizeof(two->skip), "v1.2.3");
    two->by = BY_HAND;
    EXPECT(prefs_get("not a title", true) == NULL, "only title IDs");
    EXPECT(prefs_save(), "saved");
    prefs_load(path);
    one = prefs_get("PPSA99003", false);
    two = prefs_get("PPSA99002", false);
    EXPECT(prefs_count() == 2 && one && one->hold && !one->skip[0] && two && !two->hold &&
               strcmp(two->skip, "v1.2.3") == 0 && two->by == BY_HAND,
           "read back: a hold, a skipped release, an app taken over");
    prefs_forget_install("PPSA99002");
    EXPECT(two->by == BY_UNKNOWN && strcmp(two->skip, "v1.2.3") == 0, "an uninstall forgets how, keeps the skip");
}

static int never_range(void *user, const char *url, uint64_t offset, uint64_t limit, self_update_sink sink,
                       void *sink_user, int *status)
{
    (void)url, (void)offset, (void)limit, (void)sink, (void)sink_user;
    ++*(int *)user;
    *status = 0;
    return -1;
}

static void check_keep(void)
{
    char cache[600], download[700], zip[700];
    static char first[4000], second[5000];
    snprintf(cache, sizeof(cache), "%s/keep-cache", scratch);
    mkdir(cache, 0755);
    for (size_t i = 0; i < sizeof(first); ++i)
        first[i] = (char)(i * 3 + 1);
    for (size_t i = 0; i < sizeof(second); ++i)
        second[i] = (char)(i * 5 + 2);
    KeptZip a, b, read;
    memset(&a, 0, sizeof(a));
    snprintf(a.tag, sizeof(a.tag), "v1.0.0");
    snprintf(a.version, sizeof(a.version), "01.000.000");
    sha256_hex(first, sizeof(first), a.sha256);
    a.size = sizeof(first);
    snprintf(a.url, sizeof(a.url), "https://github.com/o/app/releases/download/v1.0.0/App.zip");
    snprintf(a.name, sizeof(a.name), "App.zip");
    b = a;
    snprintf(b.tag, sizeof(b.tag), "v1.1.0");
    snprintf(b.version, sizeof(b.version), "01.001.000");
    sha256_hex(second, sizeof(second), b.sha256);
    b.size = sizeof(second);
    snprintf(b.url, sizeof(b.url), "https://github.com/o/app/releases/download/v1.1.0/App.zip");

    snprintf(download, sizeof(download), "%s/PPSA11111-a.zip.part", cache);
    write_file(download, first, sizeof(first));
    EXPECT(keep_store(cache, "PPSA11111", download, &a) && keep_read(cache, "PPSA11111", KEEP_CURRENT, &read) &&
               strcmp(read.tag, "v1.0.0") == 0 && !keep_read(cache, "PPSA11111", KEEP_PREVIOUS, &read) &&
               size_of(download) == 0,
           "an install's download is kept as the current version");
    write_file(download, second, sizeof(second));
    EXPECT(keep_store(cache, "PPSA11111", download, &b) && keep_read(cache, "PPSA11111", KEEP_CURRENT, &read) &&
               strcmp(read.tag, "v1.1.0") == 0 && keep_read(cache, "PPSA11111", KEEP_PREVIOUS, &read) &&
               strcmp(read.tag, "v1.0.0") == 0 && strcmp(read.version, "01.000.000") == 0,
           "the next update's: the one before becomes the previous version");
    EXPECT(keep_size(cache) == sizeof(first) + sizeof(second), "what the kept ZIPs take");

    /* Roll back: the previous ZIP handed on whole, not a byte asked of the network, checked as any download. */
    ResumeDownload d;
    memset(&d, 0, sizeof(d));
    int asked = 0;
    keep_paths(cache, "PPSA11111", KEEP_PREVIOUS, d.part, sizeof(d.part), NULL, 0);
    snprintf(d.sha256, sizeof(d.sha256), "%s", read.sha256);
    d.size = read.size;
    d.range = never_range;
    d.range_user = &asked;
    Received got = {NULL, 0};
    EXPECT(resume_download(&d, read.url, read.size, receive, &got) == 0 && asked == 0 && got.length == sizeof(first) &&
               memcmp(got.data, first, sizeof(first)) == 0 && !d.mismatch && d.reused == sizeof(first),
           "a roll back hands on the kept ZIP whole without asking the network");
    free(got.data);
    EXPECT(keep_swap(cache, "PPSA11111") && keep_read(cache, "PPSA11111", KEEP_CURRENT, &read) &&
               strcmp(read.tag, "v1.0.0") == 0 && keep_read(cache, "PPSA11111", KEEP_PREVIOUS, &read) &&
               strcmp(read.tag, "v1.1.0") == 0,
           "after a roll back the two swap: the next one goes forward again");

    /* A kept ZIP that went wrong is caught like a download that did. */
    keep_paths(cache, "PPSA11111", KEEP_PREVIOUS, zip, sizeof(zip), NULL, 0);
    FILE *f = fopen(zip, "r+b");
    fputc('X', f);
    fclose(f);
    memset(&d, 0, sizeof(d));
    snprintf(d.part, sizeof(d.part), "%s", zip);
    snprintf(d.sha256, sizeof(d.sha256), "%s", read.sha256);
    d.size = read.size;
    d.range = never_range;
    d.range_user = &asked;
    Received bad = {NULL, 0};
    EXPECT(resume_download(&d, read.url, read.size, receive, &bad) == 0 && d.mismatch && size_of(zip) == 0 &&
               !keep_read(cache, "PPSA11111", KEEP_PREVIOUS, &read),
           "a kept ZIP that no longer matches is deleted, never installed");
    free(bad.data);
    keep_forget(cache, "PPSA11111");
    EXPECT(!keep_read(cache, "PPSA11111", KEEP_CURRENT, &read) && keep_size(cache) == 0, "an uninstall forgets them");
}

static void check_custom(void)
{
    char out[CUSTOM_TEXT], why[160], path[600];
    EXPECT(custom_normalise("someone/their-catalog", out, sizeof(out)) && strcmp(out, "someone/their-catalog") == 0,
           "owner/name");
    EXPECT(custom_normalise(" https://github.com/someone/cat/ ", out, sizeof(out)) &&
               strcmp(out, "someone/cat") == 0,
           "a repository's page is the repository");
    EXPECT(custom_normalise("https://example.com/ps5/catalog.json", out, sizeof(out)) &&
               strcmp(out, "https://example.com/ps5/catalog.json") == 0,
           "an HTTPS address");
    EXPECT(!custom_normalise("http://example.com/catalog.json", out, sizeof(out)) &&
               !custom_normalise("https://exa mple.com/c.json", out, sizeof(out)) &&
               !custom_normalise("https://localhost/c.json", out, sizeof(out)) &&
               !custom_normalise("https://user@example.com/c.json", out, sizeof(out)) &&
               !custom_normalise("ftp://x/y", out, sizeof(out)) && !custom_normalise("", out, sizeof(out)) &&
               !custom_normalise("not a repo", out, sizeof(out)),
           "no plain HTTP, no spaces, no host without a dot, no user in the address");
    snprintf(path, sizeof(path), "%s/catalogs.ini", scratch);
    remove(path);
    custom_load(path);
    EXPECT(custom_add("someone/cat", why, sizeof(why)) && custom_add("https://example.com/c.json", why, sizeof(why)),
           "two added");
    EXPECT(!custom_add("https://github.com/someone/cat", why, sizeof(why)) && strstr(why, "already"),
           "the same one twice is refused");
    EXPECT(!custom_add("Oliv3rmoon/Solanum-Catalog", why, sizeof(why)), "Solanum's own catalog isn't a custom one");
    custom_url(0, out, sizeof(out));
    EXPECT(strcmp(out, "https://raw.githubusercontent.com/someone/cat/HEAD/catalog.json") == 0,
           "a repository's catalog.json from raw.githubusercontent.com");
    custom_name(1, out, sizeof(out));
    EXPECT(strcmp(out, "example.com") == 0, "an address is named by its host");
    EXPECT(custom_save(), "saved");
    custom_load(path);
    EXPECT(custom_count() == 2 && strcmp(custom_text(1), "https://example.com/c.json") == 0, "read back, in order");
    custom_remove(0);
    EXPECT(custom_count() == 1 && strcmp(custom_text(0), "https://example.com/c.json") == 0, "one removed");
    for (int i = 0; i < 10; ++i)
    {
        char repo[32];
        snprintf(repo, sizeof(repo), "someone/c%d", i);
        custom_add(repo, why, sizeof(why));
    }
    EXPECT(custom_count() == CUSTOM_MAX && strstr(why, "Eight"), "eight at most");
    char copy_a[700], copy_b[700];
    custom_copy_path("/c", "https://a.example/c.json", copy_a, sizeof(copy_a));
    custom_copy_path("/c", "https://b.example/c.json", copy_b, sizeof(copy_b));
    EXPECT(strcmp(copy_a, copy_b) != 0 && strncmp(copy_a, "/c/catalog-", 11) == 0, "each catalog's copy its own file");
}

static void check_versions(void)
{
    size_t length;
    char *latest = slurp("releases/Physalis-latest.json", &length);
    /* GitHub's list: the latest release, a draft, a pre-release, and one without the file */
    const size_t size = length * 4 + 64;
    char *list = malloc(size);
    char *draft = strdup(latest), *pre = strdup(latest);
    char *at = strstr(draft, "\"draft\": false");
    if (!at)
        at = strstr(draft, "\"draft\":false");
    if (at)
        memcpy(strchr(at, ':') + 1, strchr(at, ':')[1] == ' ' ? " true," : "true,", strchr(at, ':')[1] == ' ' ? 6 : 5);
    snprintf(list, size, "[%s,%s,{\"tag_name\":\"x\",\"assets\":[],\"draft\":false,\"prerelease\":false}]", latest,
             at ? draft : "{}");
    github_release_asset out[GITHUB_RELEASE_LIST_MAX];
    github_release_files files[GITHUB_RELEASE_LIST_MAX];
    const int count =
        github_release_list_read(list, strlen(list), "Oliv3rmoon/Physalis", "PPSA99003.zip", "SHA256SUMS", out, files,
                                 GITHUB_RELEASE_LIST_MAX);
    EXPECT(count == 1 && strcmp(out[0].tag, "01.002.001") == 0 && out[0].sha256[0] && files[0].sums_files == 1 &&
               strcmp(out[0].url, "https://github.com/Oliv3rmoon/Physalis/releases/download/01.002.001/PPSA99003.zip") ==
                   0,
           "a repository's releases: the published one with the file kept, its digest and address");
    EXPECT(github_release_list_read("{\"message\":\"Not Found\"}", 24, "o/r", "a.zip", "SHA256SUMS", out, files, 5) ==
                   -1 &&
               github_release_list_read("[]", 2, "o/r", "a.zip", "SHA256SUMS", out, files, 5) == 0,
           "an answer that isn't a list; an empty list");
    char url[200];
    EXPECT(github_release_list_url(url, sizeof(url), "o/r", 20) &&
               strcmp(url, "https://api.github.com/repos/o/r/releases?per_page=20") == 0,
           "the list's address");
    free(list);
    free(draft);
    free(pre);
    free(latest);
}

static void check_roots(void)
{
    char roots[40][48], folder[700];
    const int count = apps_console_roots(roots, 40);
    EXPECT(count == 32 && strcmp(roots[0], "/data/homebrew") == 0 && strcmp(roots[1], "/data/etaHEN/games") == 0 &&
               strcmp(roots[2], "/mnt/ext0/homebrew") == 0 && strcmp(roots[4], "/mnt/ext0") == 0 &&
               strcmp(roots[31], "/mnt/usb7") == 0,
           "the folders ShadowMountPlus scans, as the helper lists them");
    EXPECT(strcmp(apps_place(0), "/data/homebrew") == 0 && strcmp(apps_place(3), "/mnt/usb0/homebrew") == 0 &&
               strcmp(apps_place_drive(10), "/mnt/usb7") == 0 && apps_place(11)[0] == '\0',
           "where a new app can go");
    /* an app installed by hand on a USB drive, in a folder of any name */
    char usb[600], data[600];
    snprintf(usb, sizeof(usb), "%s/roots/usb0/homebrew", scratch);
    snprintf(data, sizeof(data), "%s/roots/data/homebrew", scratch);
    snprintf(folder, sizeof(folder), "%s/Kodi for PS5/sce_sys/param.json", usb);
    const char param[] = "{\"titleId\":\"PPSA99420\",\"contentVersion\":\"22.000.000\"}";
    write_file(folder, param, sizeof(param) - 1);
    mkdir(data, 0755);
    const char *where[2] = {data, usb};
    const Installed found = apps_find_in("PPSA99420", NULL, where, 2);
    EXPECT(strstr(found.folder, "/usb0/homebrew/Kodi for PS5") && strcmp(found.version, "22.000.000") == 0,
           "an app installed by hand on a USB drive is found, whatever its folder's name");
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: store-check <fixtures> <scratch>\n");
        return 2;
    }
    fixtures = argv[1];
    scratch = argv[2];
    check_catalog();
    check_release();
    check_checksums();
    check_resume();
    check_apps();
    check_launch();
    check_konami();
    check_pick();
    check_pins();
    check_pinned_catalog();
    check_merge();
    check_search();
    check_prefs();
    check_keep();
    check_custom();
    check_versions();
    check_roots();
    printf("%s store: %d checks, %d failed\n", failed ? "FAIL" : "PASS", passed + failed, failed);
    return failed ? 1 : 0;
}
