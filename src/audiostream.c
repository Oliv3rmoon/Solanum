/*
 * Solanum - a music file read a block at a time (audiostream.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audiostream.h"

#include "codecs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_FRAMES 4096 /* source frames read at a time: 16 KB, so a slow drive is asked seldom */
#define ONE (UINT64_C(1) << 32) /* one source frame, in the Q32 positions below */
/* stb_vorbis's memory: a decoder's codebooks and one block's scratch are well under a megabyte for any music file. It
 * gets one block of this size instead of the heap: its own allocator then hands out nothing past it, and its cleanup
 * after a damaged header (which frees pointers it never set, with the heap) frees nothing. */
#define OGG_ARENA (4 << 20)

typedef enum
{
    KIND_WAV,
    KIND_MP3,
    KIND_OGG
} Kind;

struct AudioStream
{
    Kind kind;
    FILE *file;    /* WAV and MP3 */
    int channels;  /* the file's: 1 or 2 */
    uint64_t left; /* WAV: source frames not read from the file yet */
    drmp3 mp3;
    stb_vorbis *ogg;
    char *ogg_arena;
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

/* Up to BLOCK_FRAMES frames of the file as stereo into w->block; how many (0: the file has no more). */
static size_t decode_block(AudioStream *w)
{
    size_t got = 0;
    if (w->kind == KIND_WAV)
    {
        uint8_t raw[BLOCK_FRAMES * 4];
        const size_t want = w->left < BLOCK_FRAMES ? (size_t)w->left : BLOCK_FRAMES;
        const size_t frame_bytes = (size_t)w->channels * 2;
        got = want ? fread(raw, frame_bytes, want, w->file) : 0;
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
        return got;
    }
    {
        int16_t raw[BLOCK_FRAMES * 2];
        if (w->kind == KIND_MP3)
            got = (size_t)drmp3_read_pcm_frames_s16(&w->mp3, BLOCK_FRAMES, raw);
        else
        {
            const int n = stb_vorbis_get_samples_short_interleaved(w->ogg, w->channels, raw,
                                                                    BLOCK_FRAMES * w->channels);
            got = n > 0 ? (size_t)n : 0;
        }
        for (size_t i = 0; i < got; ++i)
        {
            w->block[i * 2] = raw[i * (size_t)w->channels];
            w->block[i * 2 + 1] = raw[i * (size_t)w->channels + (size_t)w->channels - 1];
        }
    }
    return got;
}

/* The next source frame as stereo; false when the file has no more. */
static bool next_frame(AudioStream *w, int16_t out[2])
{
    if (w->block_pos == w->block_frames)
    {
        w->block_pos = 0;
        w->block_frames = decode_block(w);
        if (w->block_frames == 0)
            return false;
    }
    out[0] = w->block[w->block_pos * 2];
    out[1] = w->block[w->block_pos * 2 + 1];
    ++w->block_pos;
    return true;
}

static bool has_extension(const char *path, const char *extension)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return false;
    for (++dot; *dot && *extension; ++dot, ++extension)
        if ((*dot >= 'A' && *dot <= 'Z' ? *dot + 32 : *dot) != *extension)
            return false;
    return !*dot && !*extension;
}

static size_t mp3_read(void *user, void *out, size_t bytes)
{
    return fread(out, 1, bytes, (FILE *)user);
}

static drmp3_bool32 mp3_seek(void *user, int offset, drmp3_seek_origin origin)
{
    return fseek((FILE *)user, offset, origin == DRMP3_SEEK_SET ? SEEK_SET : origin == DRMP3_SEEK_CUR ? SEEK_CUR : SEEK_END) == 0;
}

static drmp3_bool32 mp3_tell(void *user, drmp3_int64 *cursor)
{
    const long at = ftell((FILE *)user);
    if (at < 0)
        return DRMP3_FALSE;
    *cursor = at;
    return DRMP3_TRUE;
}

AudioStream *audio_open(const char *path, int out_rate, const char **why)
{
    const char *reason = NULL;
    uint64_t rate = 0;
    AudioStream *w = out_rate > 0 ? (AudioStream *)calloc(1, sizeof(*w)) : NULL;
    if (!w)
    {
        if (why)
            *why = "can't be opened";
        return NULL;
    }
    w->kind = has_extension(path, "ogg") ? KIND_OGG : has_extension(path, "mp3") ? KIND_MP3 : KIND_WAV;
    if (w->kind == KIND_OGG)
    {
        int error = 0;
        stb_vorbis_alloc arena;
        w->ogg_arena = (char *)malloc(OGG_ARENA);
        arena.alloc_buffer = w->ogg_arena;
        arena.alloc_buffer_length_in_bytes = OGG_ARENA;
        w->ogg = w->ogg_arena ? stb_vorbis_open_filename(path, &error, &arena) : NULL;
        if (!w->ogg)
            reason = error == VORBIS_file_open_failure ? "can't be opened" : "not an Ogg Vorbis file";
        else
        {
            const stb_vorbis_info info = stb_vorbis_get_info(w->ogg);
            w->channels = info.channels > 1 ? 2 : 1;
            rate = info.sample_rate;
        }
    }
    else
    {
        w->file = fopen(path, "rb");
        if (!w->file)
            reason = "can't be opened";
        else if (w->kind == KIND_MP3)
        {
            if (!drmp3_init(&w->mp3, mp3_read, mp3_seek, mp3_tell, NULL, w->file, NULL))
            {
                reason = "not an MP3 file";
                fclose(w->file);
                w->file = NULL; /* nothing was initialised: audio_close has only the memory to free */
            }
            else
            {
                w->channels = (int)w->mp3.channels;
                rate = w->mp3.sampleRate;
                if (w->channels < 1 || w->channels > 2)
                    reason = "more than two channels";
            }
        }
        else
        {
            WavInfo info;
            reason = read_header(w->file, &info);
            if (!reason && fseek(w->file, (long)info.data_offset, SEEK_SET) != 0)
                reason = "can't be read";
            if (!reason)
            {
                w->channels = info.channels;
                w->left = info.data_frames;
                rate = info.rate;
            }
        }
    }
    if (!reason && (rate < 8000 || rate > 192000))
        reason = "sample rate out of range";
    if (!reason)
    {
        w->step = (rate << 32) / (uint64_t)out_rate;
        if (!next_frame(w, w->a))
            reason = "no audio";
        else
            w->have_b = next_frame(w, w->b);
    }
    if (reason)
    {
        if (why)
            *why = reason;
        audio_close(w);
        return NULL;
    }
    return w;
}

size_t audio_read(AudioStream *w, int16_t *out, size_t max_frames)
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

void audio_close(AudioStream *w)
{
    if (!w)
        return;
    if (w->kind == KIND_MP3 && w->file)
    {
        drmp3_uninit(&w->mp3);
        fclose(w->file);
    }
    else if (w->kind == KIND_OGG && w->ogg)
        stb_vorbis_close(w->ogg);
    else if (w->kind == KIND_WAV && w->file)
        fclose(w->file);
    free(w->ogg_arena); /* after stb_vorbis_close, which has the stream's struct inside it */
    free(w);
}
