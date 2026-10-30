/*
 * Solanum - background music: the WAV files in /data/solanum/music, played behind the store.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A thread of its own keeps the console's audio queue a few tens of milliseconds ahead, reading each file as it plays
 * (wavstream.h) in the order the playlist gives (playlist.h), so a slow frame of the interface never cuts the music.
 * While music plays, the interface's sounds are mixed into its blocks (mixer.h) instead of queued beside it.
 */
#ifndef SOLANUM_MUSIC_H
#define SOLANUM_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Starts the music's thread: it plays `folder`'s files at `rate` Hz (the audio device's) once music is on. */
void music_start(const char *folder, int rate);
/* Stops the thread and closes the file; the screen may then be torn down. */
void music_stop(void);

/* The settings (Settings > Interface): music on or off, shuffled or in name order, volume 0..100 %. May be called
 * before music_start and as often as the settings change. Turning music on reads the folder again. */
void music_configure(bool on, bool shuffle, int volume_percent);

/* Opening an app: the music fades to silence in `milliseconds` and stays silent. */
void music_fade_out(int milliseconds);

/* The WAV files the folder holds now, read right away; -1 when the folder can't be read. */
int music_available(void);

/* An interface sound, to mix into the music: `frames` (interleaved stereo, valid for the rest of the run) at `gain`.
 * False when no music is playing, and the sound goes to the audio queue by itself. */
bool music_mix(const int16_t *frames, size_t frame_count, float gain);

#endif
