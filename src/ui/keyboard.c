/*
 * Solanum - the on-screen keyboard (keyboard.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "keyboard.h"

#include "../app.h"
#include "../platform/platform.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Four rows of ten keys, then Space, Delete and Done. */
static const char *const ROWS[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-._"};
enum
{
    KEY_SPACE = 40,
    KEY_DELETE = 41,
    KEY_DONE = 42
};

static bool open_now;
static int key;
static char before[64];
static float shown;

void keyboard_open(const char *text)
{
    open_now = true;
    key = 11; /* Q */
    shown = 0.0f;
    str_copy(before, sizeof(before), text);
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

bool keyboard_input(uint32_t pressed, char *text, size_t size, bool *done)
{
    *done = false;
    if (!open_now)
        return false;
    const int row = key < 40 ? key / 10 : 4, col = key < 40 ? key % 10 : key == KEY_SPACE ? 2 : key == KEY_DELETE ? 6 : 8;
    if (pressed & (BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT)))
    {
        int r = row, c = col;
        if (pressed & BIT(BTN_UP))
            r = r > 0 ? r - 1 : 4;
        if (pressed & BIT(BTN_DOWN))
            r = r < 4 ? r + 1 : 0;
        if (r == 4 && (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT))))
        {
            /* along the bottom row: Space, Delete, Done */
            const int bottom = key == KEY_SPACE ? 0 : key == KEY_DELETE ? 1 : 2;
            const int to = (bottom + (pressed & BIT(BTN_LEFT) ? 2 : 1)) % 3;
            key = KEY_SPACE + to;
        }
        else
        {
            if (pressed & BIT(BTN_LEFT))
                c = c > 0 ? c - 1 : 9;
            if (pressed & BIT(BTN_RIGHT))
                c = c < 9 ? c + 1 : 0;
            key = r < 4 ? r * 10 + c : c < 4 ? KEY_SPACE : c < 7 ? KEY_DELETE : KEY_DONE;
        }
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_CROSS))
    {
        if (key < 40)
            type(text, size, ROWS[key / 10][key % 10]);
        else if (key == KEY_SPACE)
            type(text, size, ' ');
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

static void draw_key(float x, float y, float w, float h, const char *label, int icon, bool on)
{
    draw_rrect(x, y, w, h, 12, on ? TH_FOCUS : TH_PILL);
    if (icon)
        icon_draw(icon, x + (w - 30) * 0.5f, y + (h - 30) * 0.5f, 30, on ? 0xffffffffu : TH_TEXT_SOFT);
    else
        text_draw(x + w * 0.5f, y + (h - 30) * 0.5f, 30, FONT_BOLD, on ? 0xffffffffu : TH_TEXT_SOFT, ALIGN_CENTER,
                  label);
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
    /* the text, with a caret */
    draw_rrect(x + 40, y + 34, w - 80, 72, 14, TH_BG);
    icon_draw(ICON_SEARCH, x + 60, y + 54, 32, TH_TEXT_DIM);
    char shown_text[80];
    const bool caret = (plat_ticks_us() / 500000) % 2 == 0;
    snprintf(shown_text, sizeof(shown_text), "%s%s", text, caret ? "|" : "");
    text_draw_fit(x + 108, y + 51, 32, FONT_REGULAR, text[0] ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT, w - 180,
                  text[0] || caret ? shown_text : "Search the catalog");
    const float gx = x + 40, gy = y + 136;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 10; ++c)
        {
            char label[2] = {ROWS[r][c], '\0'};
            draw_key(gx + c * (kw + gap), gy + r * (kh + gap), kw, kh, label, 0, key == r * 10 + c);
        }
    const float by = gy + 4 * (kh + gap);
    draw_key(gx, by, 4 * kw + 3 * gap, kh, "Space", 0, key == KEY_SPACE);
    draw_key(gx + 4 * (kw + gap), by, 3 * kw + 2 * gap, kh, "Delete", 0, key == KEY_DELETE);
    draw_key(gx + 7 * (kw + gap), by, 3 * kw + 2 * gap, kh, "Done", 0, key == KEY_DONE);
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_TRIANGLE, GLYPH_START, GLYPH_CIRCLE};
    static const char *const labels[] = {"Type", "Delete", "Space", "Done", "Cancel"};
    app_draw_hints(glyphs, labels, 5, NULL);
}
