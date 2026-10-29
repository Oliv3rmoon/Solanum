/*
 * Solanum - downloads that pick up where an interrupted one stopped.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The self-update kit streams a release to the helper as it downloads it (self_update.h, the platform's
 * download). Solanum's platform keeps a copy of what arrives in its cache folder as well
 * (cache/<title>-<SHA-256's first 16>.zip.part), so a download cut off by the network goes on from where it
 * stopped next time: what the copy holds is handed to the helper first, then the rest is asked of GitHub with
 * an HTTP range (libcurl's CURLOPT_RESUME_FROM_LARGE; the release file host answers ranges with 206). The
 * kit hashes all of it as before and the helper again, so a copy that went wrong is caught like a download
 * that did: the whole is checked against the release's SHA-256 here too, and a copy that doesn't match is
 * deleted, never resumed. A server that ignores the range (answers 200) gets the copy deleted and the try
 * fails; the next one starts from the beginning.
 *
 * Plain C11 with the transfer behind a function, so the host test runs it (tests/store_check.c).
 */
#ifndef SOLANUM_RESUME_H
#define SOLANUM_RESUME_H

#include "self_update.h"

#include <stdint.h>

/* GET `url` from byte `offset` on (a range when offset > 0), at most `limit` bytes, the body to the sink in
 * order. *status: the answer's HTTP status (0 when none came). 0: the transfer completed with 200 (offset 0)
 * or 206 (offset > 0). When offset > 0 and the answer isn't 206, nothing reaches the sink. Negative: it
 * failed or the sink stopped it. */
typedef int (*resume_range_fn)(void *user, const char *url, uint64_t offset, uint64_t limit,
                               self_update_sink sink, void *sink_user, int *status);

typedef struct
{
    char part[512];       /* the copy kept between tries */
    char sha256[65];      /* what the whole must hash to (lowercase hex); empty: not checked here */
    uint64_t size;        /* its size; 0 when not known */
    resume_range_fn range;
    void *range_user;
    /* what the last call did, for the log */
    uint64_t reused;      /* bytes the copy held, handed on without downloading them again */
    uint64_t fetched;     /* bytes downloaded this time */
    int status;           /* the HTTP status of the last request, 0 when none was made */
    int mismatch;         /* the whole didn't hash to sha256 (or wasn't its size): the copy was deleted */
} ResumeDownload;

/* The download as the kit's platform makes it (self_update_platform.download), from the copy and then the
 * network. 0: all of it went to the sink (whether it matches is the kit's to say; see mismatch). Negative:
 * it stopped; the copy keeps what arrived, unless the server ignored the range. */
int resume_download(ResumeDownload *download, const char *url, uint64_t limit, self_update_sink sink,
                    void *sink_user);

/* The copy's name for a release of a title: <cache>/<title>-<first 16 of sha256>.zip.part. */
void resume_part_path(char *out, size_t size, const char *cache, const char *title, const char *sha256);
/* Deletes the title's copies in `cache` but `keep` (a release's older ones). */
void resume_forget_others(const char *cache, const char *title, const char *keep);

#endif
