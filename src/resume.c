/*
 * Solanum - downloads that pick up where an interrupted one stopped (resume.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _DEFAULT_SOURCE 1 /* glibc: lstat, strdup, usleep under -std=c11 */
#include "resume.h"

#include "self_update_sha256.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RESUME_PIECE (256u * 1024u)

typedef struct
{
    ResumeDownload *download;
    FILE *copy;
    self_update_sha256 hash;
    uint64_t total;
    self_update_sink sink;
    void *sink_user;
    int stopped; /* the sink said stop */
    int unsaved; /* the copy couldn't be written: the download goes on without it */
} Transfer;

static int forward(Transfer *t, const void *data, size_t size)
{
    self_update_sha256_add(&t->hash, data, size);
    t->total += size;
    if (t->sink(t->sink_user, data, size) != 1)
    {
        t->stopped = 1;
        return 0;
    }
    return 1;
}

/* What arrives from the network: kept in the copy, then handed on. */
static int on_network(void *user, const void *data, size_t size)
{
    Transfer *t = (Transfer *)user;
    if (t->copy && !t->unsaved && fwrite(data, 1, size, t->copy) != size)
        t->unsaved = 1;
    t->download->fetched += size;
    return forward(t, data, size);
}

static uint64_t file_size(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) ? (uint64_t)info.st_size : 0;
}

int resume_download(ResumeDownload *d, const char *url, uint64_t limit, self_update_sink sink, void *sink_user)
{
    Transfer t;
    unsigned char digest[32];
    uint64_t have;
    int result = 0;
    memset(&t, 0, sizeof(t));
    t.download = d;
    t.sink = sink;
    t.sink_user = sink_user;
    d->reused = d->fetched = 0;
    d->status = 0;
    d->mismatch = 0;
    self_update_sha256_start(&t.hash);

    /* The copy from an earlier try, when it can be one: no larger than the file. */
    have = d->part[0] ? file_size(d->part) : 0;
    if (have > limit || (d->size && have > d->size))
    {
        remove(d->part);
        have = 0;
    }
    if (have > 0)
    {
        FILE *copy = fopen(d->part, "rb");
        unsigned char *piece = (unsigned char *)malloc(RESUME_PIECE);
        uint64_t left = have;
        if (!copy || !piece)
        {
            if (copy)
                fclose(copy);
            free(piece);
            return -1;
        }
        while (left > 0 && !t.stopped)
        {
            const size_t want = left < RESUME_PIECE ? (size_t)left : RESUME_PIECE;
            if (fread(piece, 1, want, copy) != want)
                break;
            left -= want;
            if (!forward(&t, piece, want))
                break;
        }
        fclose(copy);
        free(piece);
        if (t.stopped)
            return -1; /* the helper went, or the install was cancelled: the copy stays */
        if (left > 0)
        {
            remove(d->part); /* it can't be read back: start over next time */
            return -1;
        }
        d->reused = have;
    }

    if (!d->size || have < d->size)
    {
        if (d->part[0])
            t.copy = fopen(d->part, have > 0 ? "ab" : "wb");
        result = d->range(d->range_user, url, have, limit - have, on_network, &t, &d->status);
        if (t.copy && fclose(t.copy) != 0)
            t.unsaved = 1;
        t.copy = NULL;
        if (t.unsaved && d->part[0])
            remove(d->part); /* a copy with a hole in it is worse than none */
        if (result != 0)
        {
            /* A server that answered the range with the whole file: what was handed on can't be taken back. */
            if (have > 0 && d->status != 0 && d->status != 206 && d->part[0])
                remove(d->part);
            return result < 0 ? result : -1;
        }
    }

    /* All of it went on. A whole that isn't the release's is deleted, so it is never resumed. */
    self_update_sha256_finish(&t.hash, digest);
    if ((d->sha256[0] && !self_update_sha256_matches(digest, d->sha256)) || (d->size && t.total != d->size))
    {
        d->mismatch = 1;
        if (d->part[0])
            remove(d->part);
    }
    return 0;
}

void resume_part_path(char *out, size_t size, const char *cache, const char *title, const char *sha256)
{
    snprintf(out, size, "%s/%s-%.16s.zip.part", cache, title, sha256);
}

void resume_forget_others(const char *cache, const char *title, const char *keep)
{
    DIR *directory = opendir(cache);
    struct dirent *entry;
    const size_t prefix = strlen(title);
    const char *kept = keep ? strrchr(keep, '/') : NULL;
    kept = kept ? kept + 1 : keep;
    if (!directory)
        return;
    while ((entry = readdir(directory)) != NULL)
    {
        const size_t length = strlen(entry->d_name);
        char path[768];
        if (strncmp(entry->d_name, title, prefix) != 0 || entry->d_name[prefix] != '-' || length < 9 ||
            strcmp(entry->d_name + length - 9, ".zip.part") != 0 || (kept && strcmp(entry->d_name, kept) == 0))
            continue;
        snprintf(path, sizeof(path), "%s/%s", cache, entry->d_name);
        remove(path);
    }
    closedir(directory);
}
