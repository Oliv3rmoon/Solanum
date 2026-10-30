/*
 * Solanum - a WAV file read a block at a time, for the background music.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The audio stack decodes 16-bit PCM WAV files (ui/sfx.c loads the interface's own sounds that way), mono or
 * stereo at any rate from 8 to 192 kHz; a song is minutes long, so it is read as it plays instead of whole, and
 * resampled to the output's rate with a straight line between frames. Plain C11 with no console calls, so the host
 * test builds it alone (tests/music_check.c). Not thread-safe: one thread reads a stream.
 */
#ifndef SOLANUM_WAVSTREAM_H
#define SOLANUM_WAVSTREAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct WavStream WavStream;

/* Opens a WAV file for playing at `out_rate`. NULL when it can't be played, with the reason (a fixed English text,
 * for the log) in *why when why isn't NULL. */
WavStream *wav_open(const char *path, int out_rate, const char **why);

/* Up to `max_frames` frames of interleaved 16-bit stereo at the output's rate. Fewer than asked for means the file
 * ended (or its data was cut short); 0, nothing is left. */
size_t wav_read(WavStream *stream, int16_t *out, size_t max_frames);

void wav_close(WavStream *stream);

#endif
