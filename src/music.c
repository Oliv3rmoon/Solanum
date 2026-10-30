/*
 * Solanum - background music (music.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The thread keeps TARGET frames queued: it makes a CHUNK whenever the queue has room for one, from the file playing
 * (the next file's start follows the last one's end in the same block), mixed with the interface sounds that wait in
 * the mixer's voices. It makes no more than the clock allows, so a console without an audio device doesn't play a
 * song in a second.
 */
#include "music.h"

#include "mixer.h"
#include "nightshade.h"
#include "platform/platform.h"
#include "playlist.h"
#include "audiostream.h"

#include <SDL2/SDL.h>
#include <string.h>

#define CHUNK 512            /* frames made at a time (10.7 ms at 48 kHz) */
#define TARGET 3072          /* frames kept queued (64 ms): a slow frame of the interface doesn't cut the music */
#define FADE_IN_SECONDS 1.5f /* music coming in */
#define FADE_OFF_SECONDS 0.4f /* turned off in Settings */
#define RETRY_US 10000000    /* nothing playable in the folder: look again after this */
#define MUSIC_STACK (1024 * 1024) /* the thread's stack: the blocks, the file reads and stb_vorbis's scratch memory */

static struct
{
    SDL_mutex *lock;
    SDL_Thread *thread;
    char folder[NP_PATH_MAX];
    int rate;
    /* under the lock */
    bool on, shuffle, fading_out, quit, running;
    int volume;
    float fade_length;
    MixVoice voices[MIXER_VOICES];
} M = {.volume = 50};

static Playlist list;  /* the music thread's */
static Playlist probe; /* music_available's: the main thread's */

static void ensure_lock(void)
{
    if (!M.lock)
        M.lock = SDL_CreateMutex();
}

/* ---------------------------------------------------------------- the music thread */

/* The folder read again, and a new play order. */
static void refresh(bool shuffle, uint32_t seed, const char *playing)
{
    const int before = list.count;
    const int found = playlist_scan(&list, M.folder);
    if (found < 0)
        np_log("music: can't read %s", M.folder);
    else if (found != before || list.other_audio)
        np_log("music: %d music file%s in %s%s", found, found == 1 ? "" : "s", M.folder,
               list.other_audio ? " (audio files that aren't WAV, MP3 or OGG Vorbis are not played)" : "");
    playlist_start(&list, shuffle, seed, playing);
}

/* The next file that opens, or NULL when none does (every file is tried once). */
static AudioStream *open_next(bool shuffle, uint32_t seed)
{
    if (playlist_at_end(&list))
        refresh(shuffle, seed, NULL);
    for (int tries = 0; tries < list.count; ++tries)
    {
        const char *name = playlist_next(&list);
        const char *why = "can't be opened";
        char path[NP_PATH_MAX];
        AudioStream *stream;
        if (!name)
            break;
        path_join(path, sizeof(path), M.folder, name);
        stream = audio_open(path, M.rate, &why);
        if (stream)
        {
            np_log("music: playing %s", name);
            return stream;
        }
        np_log("music: %s skipped: %s", name, why);
    }
    return NULL;
}

static void publish_running(bool running)
{
    SDL_LockMutex(M.lock);
    M.running = running;
    if (!running)
        memset(M.voices, 0, sizeof(M.voices));
    SDL_UnlockMutex(M.lock);
}

static int music_thread(void *unused)
{
    int16_t music[CHUNK * 2], mixed[CHUNK * 2];
    AudioStream *stream = NULL;
    float level = 0.0f, gain_before = 0.0f;
    bool was_playing = false, applied_shuffle = false, running = false;
    uint64_t retry_at = 0, anchor_us = 0, produced = 0;
    (void)unused;
    for (;;)
    {
        bool on, shuffle, fading, quit;
        int volume;
        float fade_length;
        SDL_LockMutex(M.lock);
        on = M.on;
        shuffle = M.shuffle;
        fading = M.fading_out;
        quit = M.quit;
        volume = M.volume;
        fade_length = M.fade_length;
        SDL_UnlockMutex(M.lock);
        if (quit)
            break;

        const uint64_t now = plat_ticks_us();
        const bool playing = on && !fading;
        const uint32_t seed = (uint32_t)(now ^ (now >> 32)) | 1u;
        if (playing && !was_playing)
        {
            /* turned on: the folder is read again, and the music starts from the top of the list */
            refresh(shuffle, seed, NULL);
            applied_shuffle = shuffle;
            retry_at = 0;
        }
        else if (playing && shuffle != applied_shuffle)
        {
            playlist_start(&list, shuffle, seed, playlist_current(&list));
            applied_shuffle = shuffle;
        }
        was_playing = playing;

        if (playing && !stream && now >= retry_at)
        {
            stream = open_next(shuffle, seed);
            if (stream)
            {
                anchor_us = now;
                produced = 0;
            }
            else
                retry_at = now + RETRY_US;
        }
        if (!stream)
        {
            if (running)
            {
                running = false;
                publish_running(false);
            }
            plat_sleep_us(30000);
            continue;
        }
        if (!running)
        {
            running = true;
            publish_running(true);
        }

        {
            const size_t queued = plat_audio_queued_frames();
            const uint64_t allowed = (now - anchor_us) * (uint64_t)M.rate / 1000000u + TARGET;
            if (queued + CHUNK > TARGET || produced + CHUNK > allowed)
            {
                plat_sleep_us(4000);
                continue;
            }
        }

        size_t got = 0;
        while (stream && got < CHUNK)
        {
            got += audio_read(stream, music + got * 2, CHUNK - got);
            if (got < CHUNK)
            {
                /* the file ended: the next one follows at once */
                audio_close(stream);
                stream = playing ? open_next(shuffle, seed) : NULL;
            }
        }
        if (got < CHUNK)
            memset(music + got * 2, 0, (CHUNK - got) * 2 * sizeof(int16_t));

        level = mixer_ramp(level, (float)CHUNK / (float)M.rate,
                           fading ? fade_length : playing ? FADE_IN_SECONDS : FADE_OFF_SECONDS, playing);
        {
            const float gain = mixer_gain(volume) * level;
            SDL_LockMutex(M.lock);
            mixer_render(mixed, CHUNK, music, gain_before, gain, M.voices, MIXER_VOICES);
            SDL_UnlockMutex(M.lock);
            gain_before = gain;
        }
        plat_audio_push(mixed, CHUNK);
        produced += CHUNK;

        if (!playing && level <= 0.0f)
        {
            /* faded out: turned off, or an app is opening */
            audio_close(stream);
            stream = NULL;
        }
    }
    audio_close(stream);
    publish_running(false);
    return 0;
}

/* ---------------------------------------------------------------- the interface */

void music_start(const char *folder, int rate)
{
    ensure_lock();
    if (M.thread || !M.lock || rate <= 0)
        return;
    str_copy(M.folder, sizeof(M.folder), folder);
    M.rate = rate;
    M.quit = false;
    M.thread = SDL_CreateThreadWithStackSize(music_thread, "music", MUSIC_STACK, NULL);
    if (!M.thread)
        np_log("music: no thread (%s)", SDL_GetError());
}

void music_stop(void)
{
    if (!M.thread)
        return;
    SDL_LockMutex(M.lock);
    M.quit = true;
    SDL_UnlockMutex(M.lock);
    SDL_WaitThread(M.thread, NULL);
    M.thread = NULL;
}

void music_configure(bool on, bool shuffle, int volume_percent)
{
    ensure_lock();
    if (!M.lock)
        return;
    SDL_LockMutex(M.lock);
    M.on = on;
    M.shuffle = shuffle;
    M.volume = volume_percent < 0 ? 0 : volume_percent > 100 ? 100 : volume_percent;
    SDL_UnlockMutex(M.lock);
}

void music_fade_out(int milliseconds)
{
    ensure_lock();
    if (!M.lock)
        return;
    SDL_LockMutex(M.lock);
    M.fading_out = true;
    M.fade_length = (float)(milliseconds > 0 ? milliseconds : 0) / 1000.0f;
    SDL_UnlockMutex(M.lock);
}

int music_available(void)
{
    return M.folder[0] ? playlist_scan(&probe, M.folder) : -1;
}

bool music_mix(const int16_t *frames, size_t frame_count, float gain)
{
    bool taken = false;
    if (!M.lock)
        return false;
    SDL_LockMutex(M.lock);
    if (M.running)
        taken = mixer_add(M.voices, MIXER_VOICES, frames, frame_count, gain) >= 0;
    SDL_UnlockMutex(M.lock);
    return taken;
}
