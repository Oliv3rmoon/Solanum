/*
 * Solanum - the background music's playlist: which files, in what order.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The player puts WAV files in /data/solanum/music. They play one after another, in the order of their names (numbers
 * in a name count by value: 2 before 10), or shuffled: every file once before any plays again, and never the file
 * just played twice in a row. The folder is read again each time the list comes round, so a file added meanwhile is
 * played in the next round. Plain C11 with no console calls, so the host test builds it alone (tests/music_check.c).
 * Not thread-safe: the music's thread owns its playlist.
 */
#ifndef SOLANUM_PLAYLIST_H
#define SOLANUM_PLAYLIST_H

#include <stdbool.h>
#include <stdint.h>

#define PLAYLIST_MAX 256  /* files kept; the rest of a larger folder is left out */
#define PLAYLIST_NAME 128 /* a file name's bytes, with the NUL; a longer name is left out */

typedef struct
{
    char names[PLAYLIST_MAX][PLAYLIST_NAME]; /* sorted */
    int count;
    int order[PLAYLIST_MAX]; /* the play order: indices into names */
    int position;            /* where in `order` the file played last is; -1 before the first */
    bool shuffle;
    uint32_t rng;
    char last[PLAYLIST_NAME]; /* the file handed out last, kept when the folder is read again */
    int other_audio;          /* the last scan's audio files that are not WAV (.mp3, .ogg...): not played */
} Playlist;

/* Whether a file name is one the player plays: ends in .wav in any case, is not hidden (no leading dot, which also
 * leaves out the "._song.wav" copies a Mac leaves on a drive), and fits. */
bool playlist_is_track(const char *name);
/* An audio file this player can't decode (.mp3, .ogg, .flac, .m4a...). */
bool playlist_is_other_audio(const char *name);

/* Forgets the files (not the last one handed out). */
void playlist_clear(Playlist *p);
/* Adds a file when playlist_is_track accepts it, it isn't listed yet and there is room; true when added. */
bool playlist_add(Playlist *p, const char *name);
/* Name order, case ignored, numbers by value ("2.wav" before "10.wav"). */
void playlist_sort(Playlist *p);
/* Reads a folder: its regular files that are tracks, sorted. Returns how many, or -1 when it can't be read. */
int playlist_scan(Playlist *p, const char *folder);

/* Makes the play order. `playing` is the file playing now, or NULL: it stays first (shuffled) or the order goes on
 * after it (not shuffled), so changing the shuffle setting mid-song doesn't change the song. With NULL the next file
 * handed out is the first of a round, and a shuffled one is never the `last` handed out unless it is the only file. */
void playlist_start(Playlist *p, bool shuffle, uint32_t seed, const char *playing);
/* The file to play next, or NULL when there are none. At the end of a round it begins another (a shuffled one with a
 * new order). */
const char *playlist_next(Playlist *p);
/* Whether the list has no files, or the last file of its round was handed out: the folder is read again before the
 * next call to playlist_next, which begins the next round. */
bool playlist_at_end(const Playlist *p);
/* The file handed out last, or NULL before the first. */
const char *playlist_current(const Playlist *p);

#endif
