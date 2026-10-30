/*
 * Solanum - shared types for the frontend (Dulcamara's, from SwanStationPS5's SwanStationPS5.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything here is plain C11 so the same sources build for the PS5 and for the desktop test build.
 */
#ifndef NP_H
#define NP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NP_NAME "Solanum"   /* identifiers: the User-Agent of network requests, the Vulkan application name */
#define NP_TITLE "Solanum" /* what is shown on screen */
#define NP_TITLE_ID "PPSA76526" /* SOLAN on a phone keypad */
#define NP_VERSION "0.4.0"
#define NP_DATA "/data/solanum" /* the data folder on the console */
#define NP_PATH_MAX 512

/* Pad bits use RetroPad numbering. PlayStation names: Cross = B, Circle = A, Square = Y, Triangle = X. */
enum
{
    BTN_CROSS = 0,
    BTN_SQUARE = 1,
    BTN_SELECT = 2,
    BTN_START = 3,
    BTN_UP = 4,
    BTN_DOWN = 5,
    BTN_LEFT = 6,
    BTN_RIGHT = 7,
    BTN_CIRCLE = 8,
    BTN_TRIANGLE = 9,
    BTN_L1 = 10,
    BTN_R1 = 11,
    BTN_L2 = 12,
    BTN_R2 = 13,
    BTN_L3 = 14,
    BTN_R3 = 15,
    BTN_MENU = 16, /* touchpad click */
};
#define BIT(b) (1u << (b))
#define NP_MAX_PADS 4 /* the platform layer reads four; the first connected one moves through the store */

typedef struct
{
    bool connected;
    uint32_t buttons;
    int16_t lx, ly, rx, ry;
    uint8_t l2, r2;    /* how far the triggers are pressed, 0..255 */
    bool motion;       /* quat holds the controller's orientation */
    float quat[4];     /* x, y, z, w */
    int16_t gun_x, gun_y; /* unused here: the platform layer's light gun */
    bool gun_offscreen;
    bool touching;     /* the touchpad (one finger) */
    uint16_t touch_x, touch_y; /* 0..1919, 0..1079 */
    int16_t mouse_dx, mouse_dy;
} PadState;

enum UpscaleFilter
{
    UPSCALE_SHARP = 0,     /* nearest-neighbour prescale: crisp pixels */
    UPSCALE_SMOOTH_PIXELS, /* Scale2x/Scale3x edge smoothing */
    UPSCALE_XBR,           /* xBR: edge-directed, smoothest (2x passes) */
    UPSCALE_FILTER_COUNT
};

typedef struct
{
    /* the platform layer's picture (it reads the first group; Solanum shows no game picture, so they stay 0) */
    bool integer_scale;
    bool smooth;
    int shader;
    bool supersampling;
    int crt;
    int border;
    int crop_edges;
    bool manual_crop;
    int mc_w, mc_h, mc_x, mc_y;
    int aspect;
    int upscale;
    int upscale_filter;
    /* the store */
    int ui_sound;       /* SFX_STYLE_* in ui/sfx.h; SFX_STYLE_OFF mutes */
    int ui_volume;      /* 0..3 = 25/50/75/100 % */
    bool update_check;  /* look for a newer Solanum at start */
    /* 0.2.0 */
    int language;       /* LANG_* (i18n.h); -1 the console's */
    int install_place;  /* where new apps go: -1 ask when there's a choice, 0 beside Solanum, 1.. apps_place(n - 1) */
    int parallel;       /* installs at once, 1..3 */
    bool keep_versions; /* keep each install's ZIP and the one before it, for Roll back */
    bool auto_update;   /* update the apps by themselves after each refresh (not the held or skipped ones) */
    /* 0.3.0 */
    bool music;         /* play the WAV files in the music folder behind the store */
    bool music_shuffle; /* in a random order, every file once before any repeats; else in the order of their names */
    int music_volume;   /* 10..100 % in steps of 10 */
} Settings;

/* Data layout below the data folder (NP_DATA on the console). */
typedef struct
{
    char root[NP_PATH_MAX];
    char logs[NP_PATH_MAX];
    char cache[NP_PATH_MAX];   /* the catalog's copy, downloads (installs, the update) */
    char config[NP_PATH_MAX];  /* root/settings.ini */
    char music[NP_PATH_MAX];   /* root/music: the player's WAV files */
} Paths;

void np_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void np_log_open(const char *path);

/* Small string helpers shared by the frontend. */
void str_copy(char *dst, size_t size, const char *src);
void path_join(char *dst, size_t size, const char *a, const char *b);
bool path_exists(const char *path);
bool path_is_dir(const char *path);
bool make_dirs(const char *path);
bool file_copy(const char *from, const char *to); /* replaces `to` */
const char *path_ext(const char *path);  /* lower-case-insensitive extension without dot, "" if none */
int str_icmp(const char *a, const char *b);
/* localtime into *out (the PS5 libc has no localtime_r); false on failure */
struct tm;
bool local_time(long long when, struct tm *out);

#endif
