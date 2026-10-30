/*
 * Solanum - host check of the background music's logic, run by tests/test_music.py.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * usage: music-check <a scratch folder>
 *
 *   - the playlist (src/playlist.c): which names are tracks, the folder read (regular WAV files only, other audio
 *     counted), name order with numbers by value, rounds that wrap, shuffles that play every file once before any
 *     plays again and never open a round with the file just played, a changed setting that keeps the song playing,
 *     and an empty folder;
 *   - the WAV reader (src/wavstream.c): mono and stereo at other rates resampled to the output's, the same samples
 *     whatever the size of the reads, chunks before the data, a data length that is wrong or missing, and every
 *     file it must refuse (8, 24 and 32 bits, float, three channels, no data, not a WAV);
 *   - the mixer (src/mixer.c): the volume curve, fades, the ramp across a block, clipping, and the interface's
 *     sounds mixed in, a flurry of them replacing the one furthest along.
 */
#define _DEFAULT_SOURCE 1 /* glibc: mkdir under -std=c11 */
#include "../src/mixer.h"
#include "../src/playlist.h"
#include "../src/wavstream.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int passed, failed;
static const char *scratch;

#define EXPECT(condition, what)                                                                                  \
    do                                                                                                           \
    {                                                                                                            \
        if (condition)                                                                                           \
            ++passed;                                                                                            \
        else                                                                                                     \
        {                                                                                                        \
            ++failed;                                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                                \
        }                                                                                                        \
    } while (0)

static void path_in(char *out, size_t size, const char *folder, const char *name)
{
    snprintf(out, size, "%s/%s", folder, name);
}

static void touch(const char *folder, const char *name)
{
    char path[1024];
    path_in(path, sizeof(path), folder, name);
    FILE *f = fopen(path, "wb");
    if (f)
        fclose(f);
}

static void make_folder(const char *name, char *out, size_t size)
{
    path_in(out, size, scratch, name);
    mkdir(out, 0777);
}

/* ---------------------------------------------------------------- WAV files to read */

typedef struct
{
    int tag;             /* the format tag: 1 PCM, 3 float; 0xFFFE extensible with PCM */
    int channels, rate, bits;
    size_t frames;
    long data_field;     /* the data chunk's length as the header says; -1 the real one */
    int extra_chunk;     /* bytes of a chunk before the data (odd sizes get their pad byte) */
    size_t chop_bytes;   /* bytes cut off the end of the file */
    int skip_data;       /* leave the data chunk out */
} WavSpec;

/* A rising ramp, so a straight-line resample of it is exact: left i * 4 + 50, right i * 4 - 50. */
static int16_t ramp(size_t frame, int channel)
{
    return (int16_t)((long)frame * 4 + (channel ? -50 : 50));
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 255), f);
    fputc((int)(v >> 8 & 255), f);
}

static void put32(FILE *f, unsigned long v)
{
    put16(f, (unsigned)(v & 0xffff));
    put16(f, (unsigned)(v >> 16 & 0xffff));
}

static void write_wav(const char *path, WavSpec s)
{
    const size_t bytes_per_frame = (size_t)s.channels * (size_t)s.bits / 8;
    const size_t data_bytes = s.frames * bytes_per_frame;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f);
    put32(f, 4 + 8 + 16 + data_bytes);
    fwrite("WAVE", 1, 4, f);
    if (s.extra_chunk)
    {
        fwrite("LIST", 1, 4, f);
        put32(f, (unsigned long)s.extra_chunk);
        for (int i = 0; i < s.extra_chunk; ++i)
            fputc('x', f);
        if (s.extra_chunk & 1)
            fputc(0, f);
    }
    fwrite("fmt ", 1, 4, f);
    put32(f, s.tag == 0xFFFE ? 40 : 16);
    put16(f, (unsigned)s.tag);
    put16(f, (unsigned)s.channels);
    put32(f, (unsigned long)s.rate);
    put32(f, (unsigned long)s.rate * bytes_per_frame);
    put16(f, (unsigned)bytes_per_frame);
    put16(f, (unsigned)s.bits);
    if (s.tag == 0xFFFE)
    {
        put16(f, 22);
        put16(f, (unsigned)s.bits);
        put32(f, 3);
        put16(f, 1); /* the sub-format: PCM */
        for (int i = 0; i < 14; ++i)
            fputc(i == 2 ? 0x10 : i == 6 ? 0x80 : i == 8 ? 0x00 : 0, f);
    }
    if (!s.skip_data)
    {
        fwrite("data", 1, 4, f);
        put32(f, s.data_field >= 0 ? (unsigned long)s.data_field : data_bytes);
        for (size_t i = 0; i < s.frames; ++i)
            for (int c = 0; c < s.channels; ++c)
            {
                if (s.bits == 16)
                    put16(f, (unsigned)(uint16_t)ramp(i, c));
                else
                    for (int b = 0; b < s.bits / 8; ++b)
                        fputc(0x11, f);
            }
    }
    fclose(f);
    if (s.chop_bytes)
    {
        FILE *g = fopen(path, "rb");
        long size;
        if (g)
        {
            fseek(g, 0, SEEK_END);
            size = ftell(g);
            fclose(g);
            if (truncate(path, size - (long)s.chop_bytes) != 0)
                printf("could not cut %s\n", path);
        }
    }
}

static WavSpec spec(int channels, int rate, size_t frames)
{
    WavSpec s;
    memset(&s, 0, sizeof(s));
    s.tag = 1;
    s.channels = channels;
    s.rate = rate;
    s.bits = 16;
    s.frames = frames;
    s.data_field = -1;
    return s;
}

/* All of a file at the output rate, read `block` frames at a time; the frames read, in *out (malloc'ed). */
static size_t read_all(const char *path, int rate, size_t block, int16_t **out)
{
    size_t total = 0, capacity = 1 << 16;
    int16_t *frames = malloc(capacity * 2 * sizeof(int16_t));
    WavStream *w = wav_open(path, rate, NULL);
    *out = frames;
    if (!w || !frames)
        return 0;
    for (;;)
    {
        if (total + block > capacity)
        {
            capacity *= 2;
            frames = realloc(frames, capacity * 2 * sizeof(int16_t));
            *out = frames;
        }
        const size_t n = wav_read(w, frames + total * 2, block);
        total += n;
        if (n < block)
            break;
    }
    EXPECT(wav_read(w, frames, block) == 0, "nothing is read after the end");
    wav_close(w);
    return total;
}

static void test_wav(void)
{
    char folder[1024], path[1024];
    int16_t *out;
    const char *why;
    make_folder("wav", folder, sizeof(folder));

    /* the same rate, stereo: every frame, unchanged */
    path_in(path, sizeof(path), folder, "stereo48.wav");
    write_wav(path, spec(2, 48000, 3000));
    size_t n = read_all(path, 48000, 1000, &out);
    EXPECT(n == 3000, "48 kHz stereo: every frame comes out");
    EXPECT(n == 3000 && out[0] == ramp(0, 0) && out[1] == ramp(0, 1) && out[2999 * 2] == ramp(2999, 0) &&
               out[2999 * 2 + 1] == ramp(2999, 1),
           "48 kHz stereo: the samples are the file's");
    free(out);

    /* reads of any size give the same samples as one big read */
    int16_t *one, *odd;
    const size_t n_one = read_all(path, 48000, 5000, &one), n_odd = read_all(path, 48000, 7, &odd);
    EXPECT(n_one == n_odd && memcmp(one, odd, n_one * 4) == 0, "odd-sized reads give the same samples");
    free(one);
    free(odd);

    /* mono at half the rate: twice the frames, both channels the same, a straight line between the file's frames */
    path_in(path, sizeof(path), folder, "mono24.wav");
    write_wav(path, spec(1, 24000, 1000));
    n = read_all(path, 48000, 333, &out);
    EXPECT(n == 2000, "24 kHz mono: twice the frames");
    EXPECT(n == 2000 && out[0] == out[1] && out[2 * 777] == out[2 * 777 + 1], "mono is played on both sides");
    EXPECT(n == 2000 && out[2 * 200] == ramp(100, 0) && out[2 * 201] == ramp(100, 0) + 2,
           "24 kHz mono: every other frame is the file's, the ones between lie halfway");
    free(out);

    /* an upsampled ramp stays a ramp: each output frame is where the line through the file's frames puts it */
    path_in(path, sizeof(path), folder, "ramp.wav");
    write_wav(path, spec(2, 44100, 4410));
    n = read_all(path, 48000, 500, &out);
    EXPECT(n >= 4800 - 1 && n <= 4800 + 1, "44.1 kHz to 48 kHz: 4410 frames become about 4800");
    {
        int bad = 0;
        for (size_t k = 0; k + 2 < n; ++k)
        {
            const double position = (double)k * 44100.0 / 48000.0;
            const double want = position * 4.0 + 50.0;
            if (fabs((double)out[k * 2] - want) > 1.5)
                ++bad;
        }
        EXPECT(bad == 0, "44.1 kHz to 48 kHz: the samples lie on the file's line");
    }
    free(out);

    /* down to half: 96 kHz to 48 kHz */
    path_in(path, sizeof(path), folder, "stereo96.wav");
    write_wav(path, spec(2, 96000, 4000));
    n = read_all(path, 48000, 1024, &out);
    EXPECT(n == 2000, "96 kHz to 48 kHz: half the frames");
    EXPECT(n == 2000 && out[2 * 10] == ramp(20, 0), "96 kHz to 48 kHz: every other frame");
    free(out);

    /* other chunks first (an odd-sized one has a pad byte), and the extensible header */
    WavSpec s = spec(2, 48000, 100);
    s.extra_chunk = 7;
    path_in(path, sizeof(path), folder, "list.wav");
    write_wav(path, s);
    n = read_all(path, 48000, 64, &out);
    EXPECT(n == 100 && out[0] == ramp(0, 0), "a LIST chunk before the data is skipped");
    free(out);
    s = spec(2, 48000, 100);
    s.tag = 0xFFFE;
    path_in(path, sizeof(path), folder, "extensible.wav");
    write_wav(path, s);
    n = read_all(path, 48000, 64, &out);
    EXPECT(n == 100 && out[2 * 99] == ramp(99, 0), "WAVE_FORMAT_EXTENSIBLE PCM is read");
    free(out);

    /* a length that is wrong or not written plays what is there */
    s = spec(2, 48000, 100);
    s.data_field = 0xFFFFFFFFl;
    path_in(path, sizeof(path), folder, "streamed.wav");
    write_wav(path, s);
    n = read_all(path, 48000, 64, &out);
    EXPECT(n == 100, "a data length of 0xFFFFFFFF plays to the end of the file");
    free(out);
    s.data_field = 0;
    write_wav(path, s);
    n = read_all(path, 48000, 64, &out);
    EXPECT(n == 100, "a data length of 0 plays to the end of the file");
    free(out);
    s.data_field = 100000;
    write_wav(path, s);
    n = read_all(path, 48000, 64, &out);
    EXPECT(n == 100, "a data length past the end of the file plays what is there");
    free(out);
    s = spec(2, 48000, 100);
    s.chop_bytes = 6; /* the last frame is cut short, and the header still says 100 */
    s.data_field = 400;
    path_in(path, sizeof(path), folder, "cut.wav");
    write_wav(path, s);
    n = read_all(path, 48000, 64, &out);
    EXPECT(n == 98, "a file cut short plays the whole frames it has");
    free(out);

    /* what must be refused */
    {
        struct
        {
            const char *name;
            WavSpec spec;
        } bad[] = {{"8bit.wav", spec(2, 48000, 100)},    {"24bit.wav", spec(2, 48000, 100)},
                   {"32bit.wav", spec(2, 48000, 100)},   {"float.wav", spec(2, 48000, 100)},
                   {"three.wav", spec(3, 48000, 100)},   {"nodata.wav", spec(2, 48000, 100)},
                   {"slow.wav", spec(2, 4000, 100)},     {"empty.wav", spec(2, 48000, 0)}};
        bad[0].spec.bits = 8;
        bad[1].spec.bits = 24;
        bad[2].spec.bits = 32;
        bad[3].spec.tag = 3;
        bad[3].spec.bits = 32;
        bad[5].spec.skip_data = 1;
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        {
            char message[128];
            why = NULL;
            path_in(path, sizeof(path), folder, bad[i].name);
            write_wav(path, bad[i].spec);
            WavStream *w = wav_open(path, 48000, &why);
            snprintf(message, sizeof(message), "%s is refused, with a reason", bad[i].name);
            EXPECT(w == NULL && why && why[0], message);
            wav_close(w);
        }
    }
    path_in(path, sizeof(path), folder, "garbage.wav");
    {
        FILE *f = fopen(path, "wb");
        for (int i = 0; f && i < 200; ++i)
            fputc(i * 7, f);
        if (f)
            fclose(f);
    }
    why = NULL;
    EXPECT(wav_open(path, 48000, &why) == NULL && why, "bytes that are not a WAV file are refused");
    why = NULL;
    path_in(path, sizeof(path), folder, "missing.wav");
    EXPECT(wav_open(path, 48000, &why) == NULL && why, "a file that isn't there is refused");
    EXPECT(wav_open(path, 0, NULL) == NULL, "an output rate of 0 is refused");
    wav_close(NULL);
}

/* ---------------------------------------------------------------- the mixer */

static void test_mixer(void)
{
    EXPECT(mixer_gain(0) == 0.0f && mixer_gain(100) == 1.0f, "the volume curve runs from silence to full");
    EXPECT(fabsf(mixer_gain(50) - 0.25f) < 1e-6f, "50 % is a quarter of the amplitude");
    EXPECT(mixer_gain(-20) == 0.0f && mixer_gain(250) == 1.0f, "the volume is held to 0..100");
    {
        int rising = 1;
        for (int p = 1; p <= 100; ++p)
            rising = rising && mixer_gain(p) > mixer_gain(p - 1);
        EXPECT(rising, "every step of the volume is louder than the one before");
    }
    EXPECT(fabsf(mixer_ramp(0.0f, 0.5f, 1.0f, true) - 0.5f) < 1e-6f, "half a second of a one-second fade in is half");
    EXPECT(mixer_ramp(0.9f, 0.5f, 1.0f, true) == 1.0f, "a fade in stops at full");
    EXPECT(fabsf(mixer_ramp(1.0f, 0.2f, 0.8f, false) - 0.75f) < 1e-6f, "a fade out takes the level down");
    EXPECT(mixer_ramp(0.1f, 0.5f, 1.0f, false) == 0.0f, "a fade out stops at silence");
    EXPECT(mixer_ramp(0.3f, 0.01f, 0.0f, true) == 1.0f && mixer_ramp(0.3f, 0.01f, 0.0f, false) == 0.0f,
           "a fade of no length is there at once");

    /* the music alone */
    {
        const int16_t music[8] = {1000, -1000, 1000, -1000, 1000, -1000, 1000, -1000};
        int16_t out[8];
        MixVoice voices[MIXER_VOICES] = {{0}};
        EXPECT(mixer_render(out, 4, music, 1.0f, 1.0f, voices, MIXER_VOICES) == 0 && memcmp(out, music, sizeof(out)) == 0,
               "music at full gain passes through");
        mixer_render(out, 4, music, 0.0f, 1.0f, voices, MIXER_VOICES);
        EXPECT(out[0] == 250 && out[2] == 500 && out[4] == 750 && out[6] == 1000 && out[7] == -1000,
               "the gain moves in a straight line across a block, and ends where it was going");
        mixer_render(out, 4, NULL, 1.0f, 1.0f, voices, MIXER_VOICES);
        EXPECT(out[0] == 0 && out[7] == 0, "no music and no voices is silence");
    }

    /* an interface sound, mixed in and finished */
    {
        const int16_t click[6] = {200, -200, 400, -400, 600, -600};
        int16_t out[10];
        MixVoice voices[MIXER_VOICES] = {{0}};
        EXPECT(mixer_add(voices, MIXER_VOICES, click, 3, 0.5f) == 0, "a sound takes the first free voice");
        EXPECT(mixer_render(out, 2, NULL, 1.0f, 1.0f, voices, MIXER_VOICES) == 1 && out[0] == 100 && out[1] == -100 &&
                   out[2] == 200 && out[3] == -200,
               "the sound plays at its gain");
        EXPECT(mixer_render(out, 5, NULL, 1.0f, 1.0f, voices, MIXER_VOICES) == 0 && out[0] == 300 && out[1] == -300 &&
                   out[2] == 0 && out[9] == 0,
               "the sound ends mid-block, the rest is silence, and its voice is free");
        EXPECT(voices[0].frames == NULL, "a finished voice is free");
        EXPECT(mixer_add(voices, MIXER_VOICES, NULL, 3, 1.0f) == -1 && mixer_add(voices, MIXER_VOICES, click, 0, 1.0f) == -1,
               "nothing to play is not added");
    }

    /* music and a sound together are clipped, not wrapped */
    {
        const int16_t loud[2] = {30000, -30000};
        const int16_t click[2] = {20000, -20000};
        int16_t out[2];
        MixVoice voices[MIXER_VOICES] = {{0}};
        mixer_add(voices, MIXER_VOICES, click, 1, 1.0f);
        mixer_render(out, 1, loud, 1.0f, 1.0f, voices, MIXER_VOICES);
        EXPECT(out[0] == 32767 && out[1] == -32768, "a sum past 16 bits is clipped");
    }

    /* a flurry of sounds: the one furthest along gives way */
    {
        int16_t long_sound[400], out[20];
        MixVoice voices[MIXER_VOICES] = {{0}};
        memset(long_sound, 0, sizeof(long_sound));
        for (int i = 0; i < MIXER_VOICES; ++i)
            mixer_add(voices, MIXER_VOICES, long_sound, 200, 1.0f);
        voices[3].pos = 150;
        voices[1].pos = 40;
        EXPECT(mixer_add(voices, MIXER_VOICES, long_sound + 2, 100, 0.5f) == 3, "when all voices are busy the one furthest along is replaced");
        EXPECT(voices[3].pos == 0 && voices[3].count == 100 && voices[3].gain == 0.5f, "the new sound starts from its beginning");
        EXPECT(mixer_render(out, 10, NULL, 1.0f, 1.0f, voices, MIXER_VOICES) == MIXER_VOICES, "all of them go on playing");
    }
}

/* ---------------------------------------------------------------- the playlist */

static int count_of(const char *const *names, int n, const char *name)
{
    int count = 0;
    for (int i = 0; i < n; ++i)
        count += strcmp(names[i], name) == 0;
    return count;
}

static void fill(Playlist *p, int tracks)
{
    char name[64];
    playlist_clear(p);
    for (int i = 0; i < tracks; ++i)
    {
        snprintf(name, sizeof(name), "track%02d.wav", i + 1);
        playlist_add(p, name);
    }
    playlist_sort(p);
}

static void test_playlist_names(void)
{
    EXPECT(playlist_is_track("song.wav") && playlist_is_track("Song.WAV") && playlist_is_track("a b.Wav") &&
               playlist_is_track("1.wav"),
           "a .wav file in any case is a track");
    EXPECT(!playlist_is_track("song.mp3") && !playlist_is_track("song.ogg") && !playlist_is_track("song.wav.txt") &&
               !playlist_is_track("song") && !playlist_is_track("wav") && !playlist_is_track(".wav") &&
               !playlist_is_track("song.wave") && !playlist_is_track(""),
           "anything else is not");
    EXPECT(!playlist_is_track(".hidden.wav") && !playlist_is_track("._song.wav"), "hidden files, and a Mac's ._ copies, are not");
    EXPECT(!playlist_is_track("sub/song.wav") && !playlist_is_track(NULL), "a path is not a name");
    {
        char longest[PLAYLIST_NAME + 8];
        memset(longest, 'a', sizeof(longest));
        memcpy(longest + PLAYLIST_NAME - 5, ".wav", 5);
        EXPECT(playlist_is_track(longest) && strlen(longest) == PLAYLIST_NAME - 1, "a name that fits is a track");
        memset(longest, 'a', sizeof(longest));
        memcpy(longest + PLAYLIST_NAME - 4, ".wav", 5);
        EXPECT(!playlist_is_track(longest), "a name that doesn't fit is not");
    }
    EXPECT(playlist_is_other_audio("a.mp3") && playlist_is_other_audio("a.OGG") && playlist_is_other_audio("a.flac") &&
               !playlist_is_other_audio("a.wav") && !playlist_is_other_audio("a.txt") && !playlist_is_other_audio("._a.mp3"),
           "audio Solanum can't decode is told apart");

    {
        static Playlist p;
        const char *want[] = {"1.wav", "2.wav", "10.wav", "a.wav", "A2.wav", "a9.wav", "a10.wav", "B.wav"};
        const char *added[] = {"10.wav", "B.wav", "a10.wav", "2.wav", "a.wav", "a9.wav", "1.wav", "A2.wav"};
        int in_order = 1;
        playlist_clear(&p);
        for (size_t i = 0; i < sizeof(added) / sizeof(added[0]); ++i)
            playlist_add(&p, added[i]);
        EXPECT(!playlist_add(&p, "2.wav") && !playlist_add(&p, "x.mp3") && p.count == 8, "a name already listed, or not a track, is not added");
        playlist_sort(&p);
        for (int i = 0; i < 8; ++i)
            in_order = in_order && strcmp(p.names[i], want[i]) == 0;
        if (!in_order)
            printf("  sorted: %s %s %s %s %s %s %s %s\n", p.names[0], p.names[1], p.names[2], p.names[3], p.names[4],
                   p.names[5], p.names[6], p.names[7]);
        EXPECT(in_order, "names sort without regard to case, with numbers by value");
    }
}

static void test_playlist_folder(void)
{
    static Playlist p;
    char folder[1024], path[1024], name[256];
    make_folder("music", folder, sizeof(folder));
    touch(folder, "b.wav");
    touch(folder, "a.WAV");
    touch(folder, "c.mp3");
    touch(folder, "d.ogg");
    touch(folder, ".hidden.wav");
    touch(folder, "._a.wav");
    touch(folder, "notes.txt");
    path_in(path, sizeof(path), folder, "folder.wav");
    mkdir(path, 0777);
    memset(name, 'n', 140);
    strcpy(name + 140, ".wav");
    touch(folder, name);
    EXPECT(playlist_scan(&p, folder) == 2, "a folder gives its regular WAV files: two of nine entries here");
    EXPECT(p.count == 2 && strcmp(p.names[0], "a.WAV") == 0 && strcmp(p.names[1], "b.wav") == 0, "they come sorted");
    EXPECT(p.other_audio == 2, "the .mp3 and .ogg are counted as audio that isn't played");
    EXPECT(playlist_scan(&p, "/nonexistent/solanum-music") == -1 && p.count == 0, "a folder that can't be read is -1 and an empty list");

    make_folder("many", folder, sizeof(folder));
    for (int i = 0; i < PLAYLIST_MAX + 44; ++i)
    {
        snprintf(name, sizeof(name), "song%03d.wav", i);
        touch(folder, name);
    }
    EXPECT(playlist_scan(&p, folder) == PLAYLIST_MAX && p.count == PLAYLIST_MAX, "a folder of 300 gives the 256 the list holds");

    make_folder("empty", folder, sizeof(folder));
    EXPECT(playlist_scan(&p, folder) == 0 && playlist_next(&p) == NULL && playlist_at_end(&p) && playlist_current(&p) == NULL,
           "an empty folder gives nothing to play");
}

static void test_playlist_order(void)
{
    static Playlist p;
    const char *got;
    bool ended = true;
    fill(&p, 3);
    playlist_start(&p, false, 7, NULL);
    EXPECT(!playlist_at_end(&p) && playlist_current(&p) == NULL, "before the first file nothing has played");
    {
        const char *want[] = {"track01.wav", "track02.wav", "track03.wav", "track01.wav", "track02.wav", "track03.wav", "track01.wav"};
        bool in_order = true;
        for (int i = 0; i < 7; ++i)
        {
            got = playlist_next(&p);
            in_order = in_order && got && strcmp(got, want[i]) == 0;
            if (i == 2 || i == 5)
                ended = ended && playlist_at_end(&p);
        }
        EXPECT(in_order, "in name order the files play one after another and the list comes round");
        EXPECT(ended, "the end of a round is seen after its last file");
    }
    EXPECT(strcmp(playlist_current(&p), "track01.wav") == 0, "the file playing is known");

    /* one file repeats; none is nothing */
    fill(&p, 1);
    playlist_start(&p, true, 99, NULL);
    EXPECT(strcmp(playlist_next(&p), "track01.wav") == 0 && strcmp(playlist_next(&p), "track01.wav") == 0 &&
               strcmp(playlist_next(&p), "track01.wav") == 0,
           "a single file plays again and again, shuffled or not");
    fill(&p, 0);
    playlist_start(&p, true, 1, NULL);
    EXPECT(playlist_next(&p) == NULL && playlist_at_end(&p), "no files, nothing to play");
}

static void test_playlist_shuffle(void)
{
    static Playlist p;
    enum { TRACKS = 6, ROUNDS = 4 };
    int first_counts[TRACKS] = {0}, orders_seen = 0;
    char previous_order[TRACKS][PLAYLIST_NAME];
    int all_rounds_whole = 1, no_repeat_at_seam = 1, differs = 0;

    for (uint32_t seed = 1; seed <= 600; ++seed)
    {
        const char *played[TRACKS * ROUNDS];
        fill(&p, TRACKS);
        p.last[0] = '\0';
        playlist_start(&p, true, seed, NULL);
        for (int i = 0; i < TRACKS * ROUNDS; ++i)
            played[i] = playlist_next(&p);
        for (int r = 0; r < ROUNDS; ++r)
        {
            for (int t = 0; t < TRACKS; ++t)
            {
                char name[64];
                snprintf(name, sizeof(name), "track%02d.wav", t + 1);
                if (count_of(played + r * TRACKS, TRACKS, name) != 1)
                    all_rounds_whole = 0;
            }
            if (r > 0 && strcmp(played[r * TRACKS], played[r * TRACKS - 1]) == 0)
                no_repeat_at_seam = 0;
        }
        for (int t = 0; t < TRACKS; ++t)
        {
            char name[64];
            snprintf(name, sizeof(name), "track%02d.wav", t + 1);
            if (strcmp(played[0], name) == 0)
                ++first_counts[t];
        }
        if (seed == 1)
            for (int t = 0; t < TRACKS; ++t)
                strcpy(previous_order[t], played[t]);
        else
        {
            for (int t = 0; t < TRACKS; ++t)
                if (strcmp(previous_order[t], played[t]) != 0)
                {
                    ++differs;
                    break;
                }
            ++orders_seen;
        }
    }
    EXPECT(all_rounds_whole, "a shuffled round plays every file exactly once before any plays again");
    EXPECT(no_repeat_at_seam, "a shuffled round never opens with the file just played");
    EXPECT(differs > orders_seen / 2, "different seeds give different orders");
    {
        int even = 1;
        for (int t = 0; t < TRACKS; ++t)
            even = even && first_counts[t] > 600 / TRACKS * 70 / 100 && first_counts[t] < 600 / TRACKS * 130 / 100;
        EXPECT(even, "every file is as likely to open a shuffled round as any other");
    }

    /* two files shuffled alternate: the seam rule leaves nothing else */
    fill(&p, 2);
    playlist_start(&p, true, 5, NULL);
    {
        const char *a = playlist_next(&p), *b = playlist_next(&p), *c = playlist_next(&p), *d = playlist_next(&p);
        EXPECT(strcmp(a, b) != 0 && strcmp(b, c) != 0 && strcmp(c, d) != 0 && strcmp(a, c) == 0, "two files shuffled take turns");
    }

    /* the folder read again at the end of a round: the new shuffled round still doesn't open with the file just played */
    {
        int fine = 1;
        char folder[1024];
        make_folder("rescan", folder, sizeof(folder));
        touch(folder, "one.wav");
        touch(folder, "two.wav");
        touch(folder, "three.wav");
        for (uint32_t seed = 1; seed <= 200; ++seed)
        {
            playlist_scan(&p, folder);
            playlist_start(&p, true, seed, NULL);
            for (int i = 0; i < 3; ++i)
                playlist_next(&p);
            char kept[PLAYLIST_NAME];
            snprintf(kept, sizeof(kept), "%s", playlist_current(&p));
            playlist_scan(&p, folder);
            playlist_start(&p, true, seed * 31 + 7, NULL);
            if (strcmp(playlist_next(&p), kept) == 0)
                fine = 0;
        }
        EXPECT(fine, "after the folder is read again a shuffled round still doesn't open with the file just played");
    }
}

static void test_playlist_setting_change(void)
{
    static Playlist p;
    int whole = 1, fine = 1;
    fill(&p, 5);

    /* name order, switched to shuffle during the third file: it keeps playing, the others follow, once each */
    for (uint32_t seed = 1; seed <= 100; ++seed)
    {
        const char *rest[4];
        playlist_start(&p, false, 1, NULL);
        playlist_next(&p);
        playlist_next(&p);
        playlist_next(&p);
        playlist_start(&p, true, seed, playlist_current(&p));
        fine = fine && strcmp(playlist_current(&p), "track03.wav") == 0;
        for (int i = 0; i < 4; ++i)
            rest[i] = playlist_next(&p);
        for (int t = 1; t <= 5; ++t)
        {
            char name[64];
            snprintf(name, sizeof(name), "track%02d.wav", t);
            if (t == 3)
                whole = whole && count_of(rest, 4, name) == 0;
            else
                whole = whole && count_of(rest, 4, name) == 1;
        }
        {
            bool wrapped = playlist_at_end(&p);
            whole = whole && wrapped;
        }
    }
    EXPECT(fine, "switching to shuffle keeps the file playing");
    EXPECT(whole, "after the switch every other file plays once before the song that was playing comes back");

    /* shuffle, switched off during a file: the list goes on in name order after it */
    playlist_start(&p, true, 42, NULL);
    playlist_next(&p);
    playlist_next(&p);
    {
        const char *now = playlist_current(&p);
        char kept[PLAYLIST_NAME];
        int index = 0;
        snprintf(kept, sizeof(kept), "%s", now);
        playlist_start(&p, false, 1, kept);
        EXPECT(strcmp(playlist_current(&p), kept) == 0, "switching to name order keeps the file playing");
        sscanf(kept, "track%d.wav", &index);
        if (index < 5)
        {
            char want[64];
            snprintf(want, sizeof(want), "track%02d.wav", index + 1);
            EXPECT(strcmp(playlist_next(&p), want) == 0, "and the next file is the one after it by name");
        }
    }
    /* a file that is gone from the folder: the list starts over from the top */
    playlist_start(&p, false, 1, "gone.wav");
    EXPECT(strcmp(playlist_next(&p), "track01.wav") == 0, "a song that is no longer listed starts the list over");
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: music-check <a scratch folder>\n");
        return 2;
    }
    scratch = argv[1];
    test_wav();
    test_mixer();
    test_playlist_names();
    test_playlist_folder();
    test_playlist_order();
    test_playlist_shuffle();
    test_playlist_setting_change();
    printf("%s: %d checks, %d failed\n", failed ? "FAIL" : "PASS", passed + failed, failed);
    return failed ? 1 : 0;
}
