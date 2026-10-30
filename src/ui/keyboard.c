/*
 * Solanum - the on-screen keyboard (keyboard.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "keyboard.h"

#include "../app.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Four rows of ten keys, then Space, ':', '#', Delete and Done. L1 switches capitals. */
static const char *const ROWS[4] = {"1234567890", "qwertyuiop", "asdfghjkl/", "zxcvbnm-._"};
enum
{
    KEY_SPACE = 40,
    KEY_COLON = 41,
    KEY_HASH = 42,
    KEY_DELETE = 43,
    KEY_DONE = 44
};
/* the bottom row's keys and the columns they start at */
static const int BOTTOM[5] = {KEY_SPACE, KEY_COLON, KEY_HASH, KEY_DELETE, KEY_DONE};
static const int BOTTOM_COLUMN[5] = {0, 3, 4, 5, 7};
static const int BOTTOM_WIDTH[5] = {3, 1, 1, 2, 3};

static bool open_now, capitals;
static int key;
static char before[KEYBOARD_TEXT_MAX];
static char prompt_text[160];
static float shown;

void keyboard_open_prompt(const char *text, const char *prompt)
{
    open_now = true;
    key = 11; /* q */
    shown = 0.0f;
    capitals = false;
    str_copy(before, sizeof(before), text);
    str_copy(prompt_text, sizeof(prompt_text), prompt ? prompt : "");
}

void keyboard_open(const char *text)
{
    keyboard_open_prompt(text, tr("Search by name, or #tag"));
}

bool keyboard_active(void)
{
    return open_now;
}

static void type(char *text, size_t size, char c)
{
    const size_t n = strlen(text);
    if (n + 1 < size)
    {
        text[n] = c;
        text[n + 1] = '\0';
    }
}

static char key_char(int k)
{
    if (k < 40)
    {
        const char c = ROWS[k / 10][k % 10];
        return capitals ? (char)toupper((unsigned char)c) : c;
    }
    return k == KEY_SPACE ? ' ' : k == KEY_COLON ? ':' : k == KEY_HASH ? '#' : '\0';
}

static int bottom_index(int k)
{
    for (int i = 0; i < 5; ++i)
        if (BOTTOM[i] == k)
            return i;
    return 0;
}

bool keyboard_input(uint32_t pressed, char *text, size_t size, bool *done)
{
    *done = false;
    if (!open_now)
        return false;
    /* A USB keyboard's typing: its keys are the text, not the buttons they also stand for this frame. */
    char typed[64];
    const int count = plat_keyboard_text(typed, sizeof(typed));
    if (count > 0)
    {
        for (int i = 0; i < count && open_now; ++i)
        {
            const unsigned char c = (unsigned char)typed[i];
            if (c == '\b')
            {
                if (text[0])
                    text[strlen(text) - 1] = '\0';
            }
            else if (c == '\n')
            {
                open_now = false;
                *done = true;
                sfx_play(SFX_SELECT);
            }
            else if (c == 0x1b)
            {
                str_copy(text, size, before);
                open_now = false;
                sfx_play(SFX_BACK);
            }
            else if (c >= 0x20 && c < 0x7f)
                type(text, size, (char)c);
        }
        if (open_now)
            sfx_play(SFX_CLICK);
        return true;
    }
    const int row = key < 40 ? key / 10 : 4;
    const int col = key < 40 ? key % 10 : BOTTOM_COLUMN[bottom_index(key)];
    if (pressed & (BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT)))
    {
        int r = row, c = col;
        if (pressed & BIT(BTN_UP))
            r = r > 0 ? r - 1 : 4;
        if (pressed & BIT(BTN_DOWN))
            r = r < 4 ? r + 1 : 0;
        if (r == 4 && row == 4 && (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT))))
            key = BOTTOM[(bottom_index(key) + (pressed & BIT(BTN_LEFT) ? 4 : 1)) % 5];
        else
        {
            if (pressed & BIT(BTN_LEFT))
                c = c > 0 ? c - 1 : 9;
            if (pressed & BIT(BTN_RIGHT))
                c = c < 9 ? c + 1 : 0;
            if (r < 4)
                key = r * 10 + c;
            else
            {
                int b = 0;
                for (int i = 0; i < 5; ++i)
                    if (c >= BOTTOM_COLUMN[i])
                        b = i;
                key = BOTTOM[b];
            }
        }
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_L1))
    {
        capitals = !capitals;
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_CROSS))
    {
        if (key_char(key))
            type(text, size, key_char(key));
        else if (key == KEY_DELETE && text[0])
            text[strlen(text) - 1] = '\0';
        else if (key == KEY_DONE)
        {
            open_now = false;
            *done = true;
        }
        sfx_play(SFX_CLICK);
    }
    if ((pressed & BIT(BTN_SQUARE)) && text[0])
    {
        text[strlen(text) - 1] = '\0';
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_TRIANGLE))
    {
        type(text, size, ' ');
        sfx_play(SFX_CLICK);
    }
    if (pressed & (BIT(BTN_START) | BIT(BTN_R2)))
    {
        open_now = false;
        *done = true;
        sfx_play(SFX_SELECT);
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        str_copy(text, size, before);
        open_now = false;
        sfx_play(SFX_BACK);
    }
    return true;
}

static void draw_key(float x, float y, float w, float h, const char *label, bool on)
{
    draw_rrect(x, y, w, h, 12, on ? TH_FOCUS : TH_PILL);
    text_draw_fit(x + w * 0.5f, y + (h - 30) * 0.5f, 30, FONT_BOLD, on ? 0xffffffffu : TH_TEXT_SOFT, ALIGN_CENTER,
                  w - 12, label);
}

void keyboard_draw(const char *text)
{
    if (!open_now)
        return;
    shown = fminf(shown + app.dt * 8.0f, 1.0f);
    const float kw = 92, kh = 70, gap = 12, w = 10 * kw + 9 * gap + 80, h = 5 * (kh + gap) + 200;
    const float x = (plat_width() - w) * 0.5f, y = plat_height() - h - 120 + (1.0f - shown) * 40.0f;
    draw_rect(0, 0, (float)plat_width(), (float)plat_height(), argb_alpha(0x90000000u, shown));
    draw_rrect(x, y, w, h, TH_RADIUS, TH_CARD_A(0xfa));
    draw_rect(x, y, w, 4, TH_FOCUS);
    /* the text, with a caret; its end when it is longer than the field */
    draw_rrect(x + 40, y + 34, w - 80, 72, 14, TH_BG);
    icon_draw(ICON_SEARCH, x + 60, y + 54, 32, TH_TEXT_DIM);
    char shown_text[KEYBOARD_TEXT_MAX + 4];
    const bool caret = (plat_ticks_us() / 500000) % 2 == 0;
    const float room = w - 180;
    const char *visible = text;
    while (*visible && text_width(32, FONT_REGULAR, visible) > room - 30)
        ++visible;
    snprintf(shown_text, sizeof(shown_text), "%s%s", visible, caret ? "|" : "");
    text_draw_fit(x + 108, y + 51, 32, FONT_REGULAR, text[0] ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT, room,
                  text[0] || caret ? shown_text : prompt_text);
    const float gx = x + 40, gy = y + 136;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 10; ++c)
        {
            char label[2] = {key_char(r * 10 + c), '\0'};
            draw_key(gx + c * (kw + gap), gy + r * (kh + gap), kw, kh, label, key == r * 10 + c);
        }
    const float by = gy + 4 * (kh + gap);
    const char *names[5] = {tr("Space"), ":", "#", tr("Delete"), tr("Done")};
    for (int i = 0; i < 5; ++i)
        draw_key(gx + BOTTOM_COLUMN[i] * (kw + gap), by, BOTTOM_WIDTH[i] * kw + (BOTTOM_WIDTH[i] - 1) * gap, kh,
                 names[i], key == BOTTOM[i]);
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_TRIANGLE, GLYPH_L1, GLYPH_START, GLYPH_CIRCLE};
    static const char *const labels[] = {"Type", "Delete", "Space", "Capitals", "Done", "Cancel"};
    app_draw_hints(glyphs, labels, 6, plat_keyboard_connected() ? "USB keyboard: type away" : NULL);
}
