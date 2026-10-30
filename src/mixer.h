/*
 * Solanum - mixing the background music with the interface's sounds.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console's audio is one queue of interleaved 16-bit stereo frames, so while music plays the interface's clicks
 * cannot be queued beside it: they are mixed into the music's blocks instead. Plain C11 with no console calls, so
 * the host test builds it alone (tests/music_check.c).
 */
#ifndef SOLANUM_MIXER_H
#define SOLANUM_MIXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIXER_VOICES 6 /* interface sounds playing at once */

typedef struct
{
    const int16_t *frames; /* interleaved stereo, owned by the caller and kept while the voice plays; NULL: free */
    size_t count;
    size_t pos;
    float gain;
} MixVoice;

/* The music volume setting (0..100 %) as an amplitude: squared, so each step sounds about as big as the last. */
float mixer_gain(int percent);

/* A fade's level (0..1) after `seconds` more of a fade that takes `length` seconds from one end to the other,
 * toward 1 (up) or 0. A length of 0 or less is there at once. */
float mixer_ramp(float level, float seconds, float length, bool up);

/* Starts a sound in a free voice, or in place of the one furthest along when all are busy (a flurry of clicks keeps
 * the newest). Returns the voice, or -1 when there is nothing to play. */
int mixer_add(MixVoice *voices, int count, const int16_t *frames, size_t frame_count, float gain);

/* Writes `frames` frames to `out`: the music (NULL: none) at a gain moving in a straight line from gain_from at the
 * start to gain_to at the end, plus every voice, clipped. Voices advance, and a finished one is freed. Returns how
 * many are still playing. */
int mixer_render(int16_t *out, size_t frames, const int16_t *music, float gain_from, float gain_to, MixVoice *voices,
                 int voice_count);

#endif
