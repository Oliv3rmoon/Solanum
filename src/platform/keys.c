/*
 * Solanum - a USB keyboard (platform.h, plat_keyboard_*): its keys stand for the controller's buttons, and what is
 * typed on it goes into the on-screen keyboard's text.
 *
 * On the PS5 the console's keyboard library (libSceKeyboard) is loaded by path when Solanum starts and its functions
 * are looked up then: a title that imports it doesn't start (tools/check-imports.py), and once the HEN has freed the
 * process the console no longer loads modules for it (as ProsperoStore found), so plat_keyboard_prepare runs before
 * plat_prepare_storage. The state it reads is the one ProsperoLight and ProsperoStore measured on a console: a
 * timestamp, whether the system took the keys, whether one is plugged in, the modifiers and up to sixteen USB HID
 * usage codes held. Anything missing is no keyboard, never a crash.
 *
 * On a computer it is SDL's keyboard: its text input and its Backspace, Enter and Escape.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "platform.h"

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char typed[64];
static int typed_count;
static bool connected;
static char prepared[160]; /* how plat_keyboard_prepare went, logged once the log is open */
static bool told;

static void add(char c)
{
    if (typed_count < (int)sizeof(typed))
        typed[typed_count++] = c;
}

int plat_keyboard_text(char *out, int size)
{
    const int count = typed_count < size ? typed_count : size;
    memcpy(out, typed, (size_t)count);
    typed_count = 0;
    return count;
}

bool plat_keyboard_connected(void)
{
    return connected;
}

/* USB HID usage codes */
enum
{
    HID_A = 0x04,
    HID_Z = 0x1d,
    HID_1 = 0x1e,
    HID_0 = 0x27,
    HID_ENTER = 0x28,
    HID_ESCAPE = 0x29,
    HID_BACKSPACE = 0x2a,
    HID_TAB = 0x2b,
    HID_SPACE = 0x2c,
    HID_SLASH = 0x38,
    HID_F2 = 0x3b,
    HID_F3 = 0x3c,
    HID_F5 = 0x3e,
    HID_F10 = 0x43,
    HID_PAGE_UP = 0x4b,
    HID_DELETE = 0x4c,
    HID_PAGE_DOWN = 0x4e,
    HID_RIGHT = 0x4f,
    HID_LEFT = 0x50,
    HID_DOWN = 0x51,
    HID_UP = 0x52,
    HID_KEYPAD_ENTER = 0x58,
    HID_APPLICATION = 0x65
};
#define HID_SHIFT (0x02u | 0x20u) /* left and right Shift in the modifier byte */

/* The character a key types on a US layout; 0 for none. */
static char hid_char(uint16_t key, bool shift)
{
    static const char digits[] = "1234567890", shifted_digits[] = "!@#$%^&*()";
    static const char signs[] = "-=[]\\#;'`,./", shifted_signs[] = "_+{}|~:\"~<>?";
    if (key >= HID_A && key <= HID_Z)
        return (char)((shift ? 'A' : 'a') + (key - HID_A));
    if (key >= HID_1 && key <= HID_0)
        return shift ? shifted_digits[key - HID_1] : digits[key - HID_1];
    if (key == HID_SPACE)
        return ' ';
    if (key >= 0x2d && key <= 0x38)
        return shift ? shifted_signs[key - 0x2d] : signs[key - 0x2d];
    if (key == HID_ENTER || key == HID_KEYPAD_ENTER)
        return '\n';
    if (key == HID_ESCAPE)
        return 0x1b;
    if (key == HID_BACKSPACE)
        return '\b';
    if (key >= 0x59 && key <= 0x61)
        return (char)('1' + (key - 0x59));
    if (key == 0x62)
        return '0';
    if (key == 0x63)
        return '.';
    if (key == 0x54)
        return '/';
    if (key == 0x56)
        return '-';
    return 0;
}

/* The buttons held keys stand for: arrows move, Enter or Space is Cross, Escape or Backspace is Circle, Tab (Shift+Tab)
 * and Page Down (Page Up) are R1 (L1), / or F3 is Triangle (search), Delete or F5 is Square (refresh), F10 or the Menu
 * key is OPTIONS (Settings). As ProsperoStore maps them. */
static uint32_t hid_buttons(const uint16_t *keys, int count, uint32_t modifiers)
{
    uint32_t buttons = 0;
    for (int i = 0; i < count; ++i)
        switch (keys[i])
        {
        case HID_UP: buttons |= BIT(BTN_UP); break;
        case HID_DOWN: buttons |= BIT(BTN_DOWN); break;
        case HID_LEFT: buttons |= BIT(BTN_LEFT); break;
        case HID_RIGHT: buttons |= BIT(BTN_RIGHT); break;
        case HID_ENTER:
        case HID_KEYPAD_ENTER:
        case HID_SPACE: buttons |= BIT(BTN_CROSS); break;
        case HID_ESCAPE:
        case HID_BACKSPACE: buttons |= BIT(BTN_CIRCLE); break;
        case HID_TAB: buttons |= (modifiers & HID_SHIFT) ? BIT(BTN_L1) : BIT(BTN_R1); break;
        case HID_PAGE_DOWN: buttons |= BIT(BTN_R1); break;
        case HID_PAGE_UP: buttons |= BIT(BTN_L1); break;
        case HID_SLASH:
        case HID_F3: buttons |= BIT(BTN_TRIANGLE); break;
        case HID_DELETE:
        case HID_F5: buttons |= BIT(BTN_SQUARE); break;
        case HID_F2: buttons |= BIT(BTN_R2); break;
        case HID_F10:
        case HID_APPLICATION: buttons |= BIT(BTN_START); break;
        default: break;
        }
    return buttons;
}

/* The keys newly held: what they type. */
static void type_new(const uint16_t *keys, int count, const uint16_t *before, int before_count, uint32_t modifiers)
{
    for (int i = 0; i < count; ++i)
    {
        bool held = false;
        for (int k = 0; k < before_count && !held; ++k)
            held = before[k] == keys[i];
        if (held)
            continue;
        const char c = hid_char(keys[i], (modifiers & HID_SHIFT) != 0);
        if (c)
            add(c);
    }
}

#if defined(__PROSPERO__)

typedef struct
{
    uint64_t timestamp_us;
    uint8_t intercepted;
    uint8_t reserved0[7];
    uint8_t connected;
    uint8_t reserved1[3];
    int32_t length;
    uint32_t leds;
    uint32_t modifiers;
    uint16_t keys[16];
    uint8_t reserved2[32];
} KeyboardState;
_Static_assert(sizeof(KeyboardState) == 96, "the keyboard library's state is 96 bytes");
_Static_assert(offsetof(KeyboardState, connected) == 0x10, "connected at 0x10");
_Static_assert(offsetof(KeyboardState, keys) == 0x20, "keys at 0x20");

extern int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *option,
                                    int *result) __attribute__((weak));
extern int sceKernelDlsym(int module, const char *name, void **address) __attribute__((weak));
extern int sceSysmoduleLoadModule(uint16_t id) __attribute__((weak));
int sceUserServiceGetLoginUserIdList(int ids[4]);

typedef int (*KeyboardInit)(void);
typedef int (*KeyboardOpen)(int32_t user, int32_t type, int32_t index, const void *parameters);
typedef int (*KeyboardRead)(int32_t handle, void *data, int32_t capacity);
static KeyboardOpen keyboard_open;
static KeyboardRead keyboard_read;
static bool ready;
static int handles[2] = {-1, -1};
static uint16_t held[2][16];
static int held_count[2];
static uint64_t next_scan;

void plat_keyboard_prepare(void)
{
    int module = -1;
    if (sceSysmoduleLoadModule)
        (void)sceSysmoduleLoadModule(0x0106); /* the keyboard's system module */
    if (sceKernelLoadStartModule && sceKernelDlsym)
        for (int i = 0; i < 2 && module < 0; ++i)
            module = sceKernelLoadStartModule(i == 0 ? "/system/common/lib/libSceKeyboard.sprx" : "libSceKeyboard.sprx",
                                              0, NULL, 0, NULL, NULL);
    if (module < 0)
    {
        snprintf(prepared, sizeof(prepared), "keyboard: the keyboard library couldn't be loaded (%d); no USB keyboard",
                 module);
        return;
    }
    KeyboardInit init = NULL;
    if (sceKernelDlsym(module, "sceKeyboardInit", (void **)&init) != 0 ||
        sceKernelDlsym(module, "sceKeyboardOpen", (void **)&keyboard_open) != 0 ||
        sceKernelDlsym(module, "sceKeyboardRead", (void **)&keyboard_read) != 0 || !init || !keyboard_open ||
        !keyboard_read)
    {
        snprintf(prepared, sizeof(prepared), "keyboard: the keyboard library's functions couldn't be found");
        keyboard_open = NULL;
        keyboard_read = NULL;
        return;
    }
    const int started = init();
    ready = started >= 0;
    snprintf(prepared, sizeof(prepared), "keyboard: library ready (%d)", started);
}

void keys_poll(PadState pads[NP_MAX_PADS])
{
    if (!told && prepared[0])
    {
        told = true;
        np_log("%s", prepared);
    }
    if (!ready)
        return;
    const uint64_t now = plat_ticks_us();
    if (now >= next_scan)
    {
        /* the first signed-in user's keyboards, and every few seconds one plugged in since */
        next_scan = now + 3000000;
        int ids[4] = {-1, -1, -1, -1};
        const uint64_t parameters = 0;
        if (sceUserServiceGetLoginUserIdList(ids) == 0 && ids[0] != -1)
            for (int slot = 0; slot < 2; ++slot)
                if (handles[slot] < 0)
                {
                    handles[slot] = keyboard_open(ids[0], 0, slot, &parameters);
                    if (handles[slot] >= 0)
                        np_log("keyboard: opened keyboard %d", slot + 1);
                }
    }
    uint32_t buttons = 0;
    bool any = false;
    for (int slot = 0; slot < 2; ++slot)
    {
        KeyboardState samples[16];
        if (handles[slot] < 0)
            continue;
        const int count = keyboard_read(handles[slot], samples, 16);
        if (count <= 0)
            continue;
        const KeyboardState *latest = &samples[(count > 16 ? 16 : count) - 1];
        if (!latest->connected)
        {
            held_count[slot] = 0;
            continue;
        }
        any = true;
        if (latest->intercepted)
            continue;
        int n = 0;
        uint16_t keys[16];
        for (int k = 0; k < 16; ++k)
            if (latest->keys[k])
                keys[n++] = latest->keys[k];
        type_new(keys, n, held[slot], held_count[slot], latest->modifiers);
        memcpy(held[slot], keys, sizeof(keys));
        held_count[slot] = n;
        buttons |= hid_buttons(keys, n, latest->modifiers);
    }
    connected = any;
    if (buttons)
    {
        pads[0].buttons |= buttons;
        pads[0].connected = true;
    }
}

#else

void plat_keyboard_prepare(void)
{
    connected = true; /* a computer has one; SDL sends its typing as text by default */
}

void keys_sdl_event(const SDL_Event *event)
{
    if (event->type == SDL_TEXTINPUT)
    {
        for (const char *c = event->text.text; *c; ++c)
            if ((unsigned char)*c >= 0x20 && (unsigned char)*c < 0x7f)
                add(*c);
    }
    else if (event->type == SDL_KEYDOWN && !event->key.repeat)
    {
        switch (event->key.keysym.sym)
        {
        case SDLK_BACKSPACE: add('\b'); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: add('\n'); break;
        case SDLK_ESCAPE: add(0x1b); break;
        default: break;
        }
    }
}

void keys_poll(PadState pads[NP_MAX_PADS])
{
    (void)pads;
    (void)hid_buttons;
    (void)type_new;
    (void)prepared;
    (void)told;
}

#endif
