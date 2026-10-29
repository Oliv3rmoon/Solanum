/*
 * Solanum - the store's console side (store.h): the kit's platform, Solanum's own folder, and the ranged
 * download that lets an interrupted install go on where it stopped (resume.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if defined(__PROSPERO__) && defined(NP_HAVE_CURL)
#include "store.h"

#include "console_curl.h"
#include "nightshade.h"
#include "self_update_protocol.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- Solanum's folder */

/* /app0 exists only inside the sandbox; once the HEN has freed Solanum the console's real file system is
 * seen, where the title is mounted at /system_ex/app/<id> (ShadowMountPlus) and stored in
 * /data/homebrew/<id> (as plat_asset_path finds the assets). */
static const char *const NAMES[] = {"self-updater.elf", "app-launch.elf", "sce_sys/param.json"};
#define NAME_COUNT (int)(sizeof(NAMES) / sizeof(NAMES[0]))
static char paths[NAME_COUNT][NP_PATH_MAX];

const char *store_app_file(const char *name)
{
    static const char *const bases[] = {"/app0", "/system_ex/app/" NP_TITLE_ID, "/data/homebrew/" NP_TITLE_ID};
    static int chosen = -1;
    if (chosen < 0)
    {
        chosen = 0;
        for (int i = 0; i < 3; ++i)
        {
            char param[NP_PATH_MAX];
            path_join(param, sizeof(param), bases[i], "sce_sys/param.json");
            if (path_exists(param))
            {
                chosen = i;
                break;
            }
        }
        for (int n = 0; n < NAME_COUNT; ++n)
            path_join(paths[n], sizeof(paths[n]), bases[chosen], NAMES[n]);
        np_log("store: Solanum's files in %s", bases[chosen]);
    }
    for (int n = 0; n < NAME_COUNT; ++n)
        if (strcmp(name, NAMES[n]) == 0)
            return paths[n];
    return "/nonexistent";
}

/* ---------------------------------------------------------------- the kit */

const self_update_platform *store_kit_platform(void)
{
    return self_update_console();
}

int store_kit_open_payload(const char *path, self_update_channel *channel)
{
    return self_update_open_payload(path, channel);
}

int store_kit_channel_timeout(const self_update_channel *channel, int milliseconds)
{
    return self_update_channel_timeout(channel, milliseconds);
}

/* ---------------------------------------------------------------- a download from where it stopped */

typedef struct
{
    CURL *easy;
    self_update_sink sink;
    void *user;
    uint64_t offset, limit, received;
    long code;
    int known;   /* code is the answer's */
    int stopped; /* the sink, or the answer, stopped it */
} Range;

static size_t on_range(char *data, size_t size, size_t count, void *user)
{
    Range *r = (Range *)user;
    const size_t bytes = size * count;
    if (!r->known)
    {
        (void)curl_easy_getinfo(r->easy, CURLINFO_RESPONSE_CODE, &r->code);
        r->known = 1;
    }
    /* A redirect's own body is not the file. */
    if (r->code >= 300 && r->code < 400)
        return bytes;
    /* The file from where it was asked for, or nothing: a server that answers a range with the whole file
     * (200) would hand on what the copy already gave. */
    if (r->code != (r->offset > 0 ? 206 : 200) || bytes > r->limit - r->received || r->sink(r->user, data, bytes) != 1)
    {
        r->stopped = 1;
        return 0;
    }
    r->received += bytes;
    return bytes;
}

int store_kit_range(void *user, const char *url, uint64_t offset, uint64_t limit, self_update_sink sink,
                    void *sink_user, int *status)
{
    char *current;
    char range[32];
    int result = -1;
    (void)user;
    *status = 0;
    if (!self_update_url_allowed(url, 0) || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return -1;
    current = (char *)malloc(SELF_UPDATE_MAX_URL);
    if (!current)
        return -1;
    snprintf(current, SELF_UPDATE_MAX_URL, "%s", url);
    snprintf(range, sizeof(range), "%llu-", (unsigned long long)offset);
    /* github.com sends the file on to its release file host: followed there alone (self_update_url_allowed),
     * asking each for the range. */
    for (int hop = 0; hop <= 5; ++hop)
    {
        Range r;
        char *next = NULL;
        memset(&r, 0, sizeof(r));
        r.easy = curl_easy_init();
        if (!r.easy)
            break;
        r.sink = sink;
        r.user = sink_user;
        r.offset = offset;
        r.limit = limit;
        console_curl_setup(r.easy); /* no signals, the console's CA list, non-blocking sockets */
        (void)curl_easy_setopt(r.easy, CURLOPT_URL, current);
        (void)curl_easy_setopt(r.easy, CURLOPT_USERAGENT, NP_NAME "/" NP_VERSION);
        (void)curl_easy_setopt(r.easy, CURLOPT_PROTOCOLS_STR, "https");
        (void)curl_easy_setopt(r.easy, CURLOPT_FOLLOWLOCATION, 0L);
        (void)curl_easy_setopt(r.easy, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
        (void)curl_easy_setopt(r.easy, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        (void)curl_easy_setopt(r.easy, CURLOPT_BUFFERSIZE, 256L * 1024L);
        /* No overall limit for a large file: give up when nothing arrives for a while. */
        (void)curl_easy_setopt(r.easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
        (void)curl_easy_setopt(r.easy, CURLOPT_LOW_SPEED_TIME, 30L);
        if (offset > 0)
            (void)curl_easy_setopt(r.easy, CURLOPT_RANGE, range);
        (void)curl_easy_setopt(r.easy, CURLOPT_WRITEFUNCTION, on_range);
        (void)curl_easy_setopt(r.easy, CURLOPT_WRITEDATA, &r);
        const CURLcode code = curl_easy_perform(r.easy);
        (void)curl_easy_getinfo(r.easy, CURLINFO_RESPONSE_CODE, &r.code);
        *status = (int)r.code;
        if (code == CURLE_OK && r.code >= 301 && r.code <= 308 && r.code != 304 && r.code != 305 && r.code != 306 &&
            curl_easy_getinfo(r.easy, CURLINFO_REDIRECT_URL, &next) == CURLE_OK && next != NULL &&
            self_update_url_allowed(next, 1) && r.received == 0)
        {
            snprintf(current, SELF_UPDATE_MAX_URL, "%s", next);
            curl_easy_cleanup(r.easy);
            continue;
        }
        curl_easy_cleanup(r.easy);
        if (code != CURLE_OK || r.stopped)
            result = -(10000 + (int)(code != CURLE_OK ? code : CURLE_WRITE_ERROR));
        else
            result = r.code == (offset > 0 ? 206 : 200) ? 0 : -(int)r.code;
        break;
    }
    free(current);
    return result;
}
#endif
