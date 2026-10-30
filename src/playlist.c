/*
 * Solanum - the background music's playlist (playlist.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _DEFAULT_SOURCE 1 /* glibc: opendir and stat under -std=c11 */
#include "playlist.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool has_extension(const char *name, const char *const *extensions)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name)
        return false;
    for (int i = 0; extensions[i]; ++i)
    {
        const char *a = dot + 1, *b = extensions[i];
        while (*a && *b && tolower((unsigned char)*a) == *b)
            ++a, ++b;
        if (!*a && !*b)
            return true;
    }
    return false;
}

bool playlist_is_track(const char *name)
{
    static const char *const WAV[] = {"wav", NULL};
    const size_t length = name ? strlen(name) : 0;
    return length > 0 && length < PLAYLIST_NAME && name[0] != '.' && strchr(name, '/') == NULL &&
           has_extension(name, WAV);
}

bool playlist_is_other_audio(const char *name)
{
    static const char *const OTHER[] = {"mp3", "ogg", "oga", "opus", "flac", "m4a", "aac", "wma", "aif", "aiff", "mid", NULL};
    return name && name[0] != '.' && has_extension(name, OTHER);
}

void playlist_clear(Playlist *p)
{
    p->count = 0;
    p->position = -1;
    p->other_audio = 0;
}

bool playlist_add(Playlist *p, const char *name)
{
    if (!playlist_is_track(name) || p->count >= PLAYLIST_MAX)
        return false;
    for (int i = 0; i < p->count; ++i)
        if (strcmp(p->names[i], name) == 0)
            return false;
    snprintf(p->names[p->count++], PLAYLIST_NAME, "%s", name);
    return true;
}

/* Names by letters without regard to case, and runs of digits by value. */
static int natural_compare(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b))
        {
            size_t la = 0, lb = 0;
            int c;
            while (*a == '0' && isdigit((unsigned char)a[1]))
                ++a;
            while (*b == '0' && isdigit((unsigned char)b[1]))
                ++b;
            while (isdigit((unsigned char)a[la]))
                ++la;
            while (isdigit((unsigned char)b[lb]))
                ++lb;
            if (la != lb)
                return la < lb ? -1 : 1;
            c = strncmp(a, b, la);
            if (c != 0)
                return c < 0 ? -1 : 1;
            a += la;
            b += lb;
            continue;
        }
        {
            const int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
            if (ca != cb)
                return ca < cb ? -1 : 1;
        }
        ++a;
        ++b;
    }
    if (*a != *b)
        return *a ? 1 : -1;
    return 0;
}

static int compare_names(const void *x, const void *y)
{
    const int c = natural_compare((const char *)x, (const char *)y);
    return c != 0 ? c : strcmp((const char *)x, (const char *)y);
}

void playlist_sort(Playlist *p)
{
    if (p->count > 1)
        qsort(p->names, (size_t)p->count, PLAYLIST_NAME, compare_names);
}

int playlist_scan(Playlist *p, const char *folder)
{
    DIR *directory;
    struct dirent *entry;
    int looked = 0, other = 0;
    playlist_clear(p);
    directory = opendir(folder);
    if (!directory)
        return -1;
    while ((entry = readdir(directory)) != NULL && looked++ < 4096)
    {
        char path[1024];
        struct stat info;
        if (playlist_is_other_audio(entry->d_name))
        {
            ++other;
            continue;
        }
        if (!playlist_is_track(entry->d_name))
            continue;
        if (snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name) >= (int)sizeof(path) ||
            stat(path, &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        playlist_add(p, entry->d_name);
    }
    closedir(directory);
    playlist_sort(p);
    p->other_audio = other;
    return p->count;
}

/* SplitMix32: a counter run through a mixing function, so seeds that follow one another (a clock) give unrelated
 * shuffles. */
static uint32_t random32(Playlist *p)
{
    uint32_t z = (p->rng += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

static int random_below(Playlist *p, int n)
{
    return (int)(((uint64_t)random32(p) * (uint64_t)n) >> 32);
}

static void swap_order(Playlist *p, int i, int j)
{
    const int t = p->order[i];
    p->order[i] = p->order[j];
    p->order[j] = t;
}

/* Fisher-Yates on order[from..count). */
static void shuffle_from(Playlist *p, int from)
{
    for (int i = p->count - 1; i > from; --i)
        swap_order(p, i, from + random_below(p, i - from + 1));
}

/* A round that is about to begin does not open with the file played last, unless it is the only one. */
static void avoid_last(Playlist *p)
{
    if (p->count > 1 && p->last[0] && strcmp(p->names[p->order[0]], p->last) == 0)
        swap_order(p, 0, 1 + random_below(p, p->count - 1));
}

void playlist_start(Playlist *p, bool shuffle, uint32_t seed, const char *playing)
{
    int found = -1;
    p->shuffle = shuffle;
    p->rng = seed;
    p->position = -1;
    for (int i = 0; i < p->count; ++i)
    {
        p->order[i] = i;
        if (playing && found < 0 && strcmp(p->names[i], playing) == 0)
            found = i;
    }
    if (found >= 0)
    {
        if (shuffle)
        {
            swap_order(p, 0, found);
            shuffle_from(p, 1);
            p->position = 0;
        }
        else
            p->position = found;
        snprintf(p->last, sizeof(p->last), "%s", p->names[found]);
        return;
    }
    if (shuffle)
    {
        shuffle_from(p, 0);
        avoid_last(p);
    }
}

bool playlist_at_end(const Playlist *p)
{
    return p->count == 0 || (p->position >= 0 && p->position + 1 >= p->count);
}

const char *playlist_next(Playlist *p)
{
    if (p->count == 0)
        return NULL;
    if (p->position + 1 < p->count)
        ++p->position;
    else
    {
        /* the end of a round: another begins, shuffled anew */
        p->position = 0;
        if (p->shuffle)
        {
            shuffle_from(p, 0);
            avoid_last(p);
        }
    }
    snprintf(p->last, sizeof(p->last), "%s", p->names[p->order[p->position]]);
    return p->names[p->order[p->position]];
}

const char *playlist_current(const Playlist *p)
{
    return p->last[0] ? p->last : NULL;
}
