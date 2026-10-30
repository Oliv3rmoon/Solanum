/*
 * Solanum - mixing the background music with the interface's sounds (mixer.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mixer.h"

float mixer_gain(int percent)
{
    const float p = (float)(percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100.0f;
    return p * p;
}

float mixer_ramp(float level, float seconds, float length, bool up)
{
    if (length <= 0.0f)
        return up ? 1.0f : 0.0f;
    const float step = seconds / length;
    level += up ? step : -step;
    return level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
}

int mixer_add(MixVoice *voices, int count, const int16_t *frames, size_t frame_count, float gain)
{
    int slot = -1;
    if (!frames || frame_count == 0 || count <= 0)
        return -1;
    for (int i = 0; i < count && slot < 0; ++i)
        if (!voices[i].frames)
            slot = i;
    if (slot < 0)
    {
        /* all busy: the one furthest along (the share of it already played) gives way */
        double furthest = -1.0;
        for (int i = 0; i < count; ++i)
        {
            const double done = (double)voices[i].pos / (double)voices[i].count;
            if (done > furthest)
            {
                furthest = done;
                slot = i;
            }
        }
    }
    voices[slot].frames = frames;
    voices[slot].count = frame_count;
    voices[slot].pos = 0;
    voices[slot].gain = gain;
    return slot;
}

static int16_t clip(float v)
{
    v += v >= 0.0f ? 0.5f : -0.5f;
    return v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int16_t)v;
}

int mixer_render(int16_t *out, size_t frames, const int16_t *music, float gain_from, float gain_to, MixVoice *voices,
                 int voice_count)
{
    for (size_t i = 0; i < frames; ++i)
    {
        const float g = gain_from + (gain_to - gain_from) * ((float)(i + 1) / (float)frames);
        float left = music ? (float)music[i * 2] * g : 0.0f;
        float right = music ? (float)music[i * 2 + 1] * g : 0.0f;
        for (int v = 0; v < voice_count; ++v)
        {
            MixVoice *voice = &voices[v];
            if (!voice->frames)
                continue;
            left += (float)voice->frames[voice->pos * 2] * voice->gain;
            right += (float)voice->frames[voice->pos * 2 + 1] * voice->gain;
            if (++voice->pos >= voice->count)
                voice->frames = NULL;
        }
        out[i * 2] = clip(left);
        out[i * 2 + 1] = clip(right);
    }
    int playing = 0;
    for (int v = 0; v < voice_count; ++v)
        playing += voices[v].frames != NULL;
    return playing;
}
