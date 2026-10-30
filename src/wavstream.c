/*
 * Solanum - a WAV file read a block at a time (wavstream.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "wavstream.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_FRAMES 4096 /* source frames read at a time: 16 KB, so a slow drive is asked seldom */
#define ONE (UINT64_C(1) << 32) /* one source frame, in the Q32 positions below */

struct WavStream
{
    FILE *file;
    int channels;
    uint64_t left; /* source frames not read from the file yet */
    uint64_t step; /* source frames per output frame (Q32) */
    uint64_t frac; /* how far from `a` toward `b` the next output frame is (Q32) */
    int16_t a[2], b[2];
    bool have_b; /* b is a real frame; otherwise a is the last and the stream ends when it has been passed */
    bool done;
    int16_t block[BLOCK_FRAMES * 2]; /* decoded source frames, stereo */
    size_t block_frames, block_pos;
};

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

typedef struct
{
    int channels;
    uint32_t rate;
    uint64_t data_offset, data_frames;
} WavInfo;

/* The format and where the samples are; NULL on success, else the reason. */
static const char *read_header(FILE *f, WavInfo *info)
{
    uint8_t head[12], chunk[8], fmt[40];
    uint64_t size, pos = 12;
    bool have_format = false;
    if (fseek(f, 0, SEEK_END) != 0)
        return "can't be read";
    const long end = ftell(f);
    if (end < 12 || fseek(f, 0, SEEK_SET) != 0 || fread(head, 1, 12, f) != 12 || memcmp(head, "RIFF", 4) != 0 ||
        memcmp(head + 8, "WAVE", 4) != 0)
        return "not a WAV file";
    size = (uint64_t)end;
    memset(info, 0, sizeof(*info));
    while (pos + 8 <= size)
    {
        if (fseek(f, (long)pos, SEEK_SET) != 0 || fread(chunk, 1, 8, f) != 8)
            return "damaged";
        const uint32_t n = le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0)
        {
            unsigned tag, bits;
            if (n < 16)
                return "damaged";
            memset(fmt, 0, sizeof(fmt));
            if (fread(fmt, 1, n < sizeof(fmt) ? n : sizeof(fmt), f) < 16)
                return "damaged";
            tag = le16(fmt);
            if (tag == 0xFFFE && n >= 26)
                tag = le16(fmt + 24); /* WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes */
            info->channels = (int)le16(fmt + 2);
            info->rate = le32(fmt + 4);
            bits = le16(fmt + 14);
            if (tag != 1 || bits != 16)
                return "not 16-bit PCM";
            if (info->channels < 1 || info->channels > 2)
                return "more than two channels";
            if (info->rate < 8000 || info->rate > 192000)
                return "sample rate out of range";
            have_format = true;
        }
        else if (memcmp(chunk, "data", 4) == 0)
        {
            const uint64_t room = size - (pos + 8);
            uint64_t length = n;
            if (!have_format)
                return "damaged";
            /* a length of 0 or 0xFFFFFFFF (written while streaming), or one past the file's end: what is there */
            if (length == 0 || length == 0xFFFFFFFFu || length > room)
                length = room;
            info->data_offset = pos + 8;
            info->data_frames = length / ((uint64_t)info->channels * 2);
            return info->data_frames ? NULL : "no audio";
        }
        pos += 8 + (uint64_t)n + (n & 1u);
    }
    return "no audio";
}

/* The next source frame as stereo; false when the file has no more. */
static bool next_frame(WavStream *w, int16_t out[2])
{
    if (w->block_pos == w->block_frames)
    {
        uint8_t raw[BLOCK_FRAMES * 4];
        const size_t want = w->left < BLOCK_FRAMES ? (size_t)w->left : BLOCK_FRAMES;
        const size_t frame_bytes = (size_t)w->channels * 2;
        const size_t got = want ? fread(raw, frame_bytes, want, w->file) : 0;
        w->block_pos = 0;
        w->block_frames = got;
        if (got < want)
            w->left = 0; /* the file ends before its header said */
        else
            w->left -= got;
        for (size_t i = 0; i < got; ++i)
        {
            const int16_t l = (int16_t)(raw[i * frame_bytes] | raw[i * frame_bytes + 1] << 8);
            const int16_t r = w->channels == 2 ? (int16_t)(raw[i * frame_bytes + 2] | raw[i * frame_bytes + 3] << 8) : l;
            w->block[i * 2] = l;
            w->block[i * 2 + 1] = r;
        }
        if (got == 0)
            return false;
    }
    out[0] = w->block[w->block_pos * 2];
    out[1] = w->block[w->block_pos * 2 + 1];
    ++w->block_pos;
    return true;
}

WavStream *wav_open(const char *path, int out_rate, const char **why)
{
    const char *reason;
    WavInfo info;
    WavStream *w;
    FILE *f = out_rate > 0 ? fopen(path, "rb") : NULL;
    if (!f)
    {
        if (why)
            *why = "can't be opened";
        return NULL;
    }
    reason = read_header(f, &info);
    w = reason ? NULL : (WavStream *)calloc(1, sizeof(*w));
    if (!reason && !w)
        reason = "out of memory";
    if (!reason && fseek(f, (long)info.data_offset, SEEK_SET) != 0)
        reason = "can't be read";
    if (reason)
    {
        if (why)
            *why = reason;
        free(w);
        fclose(f);
        return NULL;
    }
    w->file = f;
    w->channels = info.channels;
    w->left = info.data_frames;
    w->step = ((uint64_t)info.rate << 32) / (uint64_t)out_rate;
    if (!next_frame(w, w->a))
    {
        if (why)
            *why = "no audio";
        wav_close(w);
        return NULL;
    }
    w->have_b = next_frame(w, w->b);
    return w;
}

size_t wav_read(WavStream *w, int16_t *out, size_t max_frames)
{
    size_t n = 0;
    while (w && !w->done && n < max_frames)
    {
        const int16_t *b = w->have_b ? w->b : w->a;
        const int64_t t = (int64_t)(w->frac >> 16); /* 0..65535 */
        for (int c = 0; c < 2; ++c)
            out[n * 2 + (size_t)c] = (int16_t)(w->a[c] + ((int64_t)(b[c] - w->a[c]) * t) / 65536);
        ++n;
        w->frac += w->step;
        while (w->frac >= ONE)
        {
            w->frac -= ONE;
            if (!w->have_b)
            {
                w->done = true;
                break;
            }
            memcpy(w->a, w->b, sizeof(w->a));
            w->have_b = next_frame(w, w->b);
        }
    }
    return n;
}

void wav_close(WavStream *w)
{
    if (!w)
        return;
    if (w->file)
        fclose(w->file);
    free(w);
}
