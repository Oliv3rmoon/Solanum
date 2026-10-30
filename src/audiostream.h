/*
 * Solanum - a music file read a block at a time, for the background music.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Three kinds play, told apart by the file name's extension: WAV (16-bit PCM, read here), MP3 (dr_mp3) and Ogg
 * Vorbis (stb_vorbis), each mono or stereo at any rate from 8 to 192 kHz (MP3: up to 48). A song is minutes long,
 * so it is decoded as it plays instead of whole, and converted to the output's format: stereo, 16 bits, at the
 * output's rate, resampled with a straight line between frames. Plain C11 with no console calls, so the host test
 * builds it with the two decoders (codecs.c) and nothing else (tests/music_check.c). Not thread-safe: one thread
 * reads a stream.
 */
#ifndef SOLANUM_AUDIOSTREAM_H
#define SOLANUM_AUDIOSTREAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct AudioStream AudioStream;

/* Opens a file for playing at `out_rate`. NULL when it can't be played, with the reason (a fixed English text, for
 * the log) in *why when why isn't NULL. */
AudioStream *audio_open(const char *path, int out_rate, const char **why);

/* Up to `max_frames` frames of interleaved 16-bit stereo at the output's rate. Fewer than asked for means the file
 * ended (or its data was cut short); 0, nothing is left. */
size_t audio_read(AudioStream *stream, int16_t *out, size_t max_frames);

void audio_close(AudioStream *stream);

#endif
