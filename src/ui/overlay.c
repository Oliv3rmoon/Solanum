/*
 * Solanum - menus, questions and the QR code over a screen (overlay.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "overlay.h"

#include "../app.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
    NONE,
    MENU,
    CONFIRM,
    QR
};

static int kind = NONE;
static int open_id;
static float shown;

/* the menu */
static char title[160];
static char labels[MENU_MAX][160];
static char details[MENU_MAX][200];
static bool enabled[MENU_MAX];
static int count, selected, top;
static int chosen_id = -1, chosen_item = -1;

/* the question */
static char text[900], yes_label[64], check_label[200];
static bool has_check, checked, choice_yes;
static int answered_id = -1;
static bool answered_yes, answered_checked;

/* the QR code */
static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
static bool qr_ok;
static char qr_caption[300];

static void fill(const char *t, int n, const char *const *l, const char *const *d, const bool *e)
{
    str_copy(title, sizeof(title), t ? t : "");
    count = n < 0 ? 0 : n > MENU_MAX ? MENU_MAX : n;
    for (int i = 0; i < count; ++i)
    {
        str_copy(labels[i], sizeof(labels[i]), l[i] ? l[i] : "");
        str_copy(details[i], sizeof(details[i]), d && d[i] ? d[i] : "");
        enabled[i] = e ? e[i] : true;
    }
}

void menu_open(int id, const char *t, int n, const char *const *l, const char *const *d, const bool *e, int sel)
{
    fill(t, n, l, d, e);
    kind = MENU;
    open_id = id;
    selected = sel >= 0 && sel < count ? sel : 0;
    while (selected < count - 1 && !enabled[selected])
        ++selected;
    top = 0;
    shown = 0.0f;
    chosen_id = chosen_item = -1;
}

void menu_update(int id, const char *t, int n, const char *const *l, const char *const *d, const bool *e)
{
    if (kind != MENU || open_id != id)
        return;
    fill(t, n, l, d, e);
    if (selected >= count)
        selected = count ? count - 1 : 0;
}

bool menu_is_open(int id)
{
    return kind == MENU && open_id == id;
}

bool menu_chosen(int id, int *item)
{
    if (chosen_id != id || chosen_item < 0)
        return false;
    *item = chosen_item;
    chosen_id = chosen_item = -1;
    return true;
}

void confirm_open(int id, const char *t, const char *body, const char *yes, const char *check, bool is_checked)
{
    kind = CONFIRM;
    open_id = id;
    str_copy(title, sizeof(title), t ? t : "");
    str_copy(text, sizeof(text), body ? body : "");
    str_copy(yes_label, sizeof(yes_label), yes ? yes : tr("OK"));
    has_check = check != NULL;
    str_copy(check_label, sizeof(check_label), check ? check : "");
    checked = is_checked;
    choice_yes = false; /* the safe answer first */
    shown = 0.0f;
    answered_id = -1;
}

bool confirm_answered(int id, bool *yes, bool *is_checked)
{
    if (answered_id != id)
        return false;
    *yes = answered_yes;
    *is_checked = answered_checked;
    answered_id = -1;
    return true;
}

void qr_open(const char *data, const char *caption)
{
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    qr_ok = qrcodegen_encodeText(data, temp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 12, qrcodegen_Mask_AUTO,
                                 true);
    str_copy(qr_caption, sizeof(qr_caption), caption ? caption : data);
    kind = QR;
    shown = 0.0f;
}

void qr_show(const uint8_t *code, const char *caption)
{
    const int size = code ? code[0] : 0; /* qrcodegen's first byte; qrcodegen_getSize asserts on a bad one */
    qr_ok = size >= 21 && size <= 12 * 4 + 17 && (size - 17) % 4 == 0;
    if (qr_ok)
        memcpy(qr, code, ((size_t)size * (size_t)size + 7) / 8 + 1);
    str_copy(qr_caption, sizeof(qr_caption), caption ? caption : "");
    kind = QR;
    shown = 0.0f;
}

bool overlay_active(void)
{
    return kind != NONE;
}

void overlay_reset(void)
{
    kind = NONE;
    chosen_id = chosen_item = -1;
    answered_id = -1;
}

static void close_it(void)
{
    kind = NONE;
}

void overlay_input(uint32_t *pressed)
{
    if (kind == NONE)
        return;
    const uint32_t p = *pressed;
    *pressed = 0;
    if (kind == QR)
    {
        if (p & (BIT(BTN_CROSS) | BIT(BTN_CIRCLE)))
        {
            sfx_play(SFX_BACK);
            close_it();
        }
        return;
    }
    if (kind == MENU)
    {
        if (p & (BIT(BTN_UP) | BIT(BTN_DOWN)))
        {
            const int step = p & BIT(BTN_UP) ? -1 : 1;
            int to = selected;
            do
                to += step;
            while (to >= 0 && to < count && !enabled[to]);
            if (to >= 0 && to < count)
            {
                selected = to;
                sfx_play(SFX_CLICK);
            }
        }
        else if ((p & BIT(BTN_CROSS)) && count > 0 && enabled[selected])
        {
            sfx_play(SFX_SELECT);
            chosen_id = open_id;
            chosen_item = selected;
            close_it();
        }
        else if (p & BIT(BTN_CIRCLE))
        {
            sfx_play(SFX_BACK);
            close_it();
        }
        return;
    }
    /* the question */
    if (p & (BIT(BTN_LEFT) | BIT(BTN_RIGHT)))
    {
        choice_yes = !choice_yes;
        sfx_play(SFX_CLICK);
    }
    if ((p & BIT(BTN_SQUARE)) && has_check)
    {
        checked = !checked;
        sfx_play(SFX_CLICK);
    }
    if (p & (BIT(BTN_CROSS) | BIT(BTN_CIRCLE)))
    {
        answered_id = open_id;
        answered_yes = (p & BIT(BTN_CROSS)) && choice_yes;
        answered_checked = has_check && checked;
        sfx_play(answered_yes ? SFX_SELECT : SFX_BACK);
        close_it();
    }
}

static void scrim(void)
{
    shown = fminf(shown + app.dt * 8.0f, 1.0f);
    draw_rect(0, 0, (float)plat_width(), (float)plat_height(), argb_alpha(0xb0000000u, shown));
    /* the screen's own hints go under a band of their own */
    draw_rect(0, TH_HINT_Y - 30, (float)plat_width(), (float)plat_height() - (TH_HINT_Y - 30), TH_BG);
}

static void draw_menu(void)
{
    const float w = 1100, row = 82, pad = 40;
    const int visible = count < 8 ? count : 8;
    const float h = 130 + (visible ? visible : 1) * row + 30;
    const float x = (plat_width() - w) * 0.5f, y = (plat_height() - h) * 0.5f - 40 + (1.0f - shown) * 24.0f;
    draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(TH_CARD_A(0xfa), shown));
    draw_rect(x, y, w, 4, argb_alpha(TH_FOCUS, shown));
    text_draw_fit(x + pad, y + 34, 34, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w - 2 * pad, title);
    top = list_top_row(top, selected, 8, count);
    for (int i = top; i < count && i < top + visible; ++i)
    {
        const float ry = y + 110 + (i - top) * row;
        const bool on = i == selected && enabled[i];
        if (on)
        {
            draw_rrect(x + 20, ry, w - 40, row - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
            draw_rect(x + 20, ry + 16, 5, row - 40, TH_FOCUS);
        }
        const uint32_t ink = !enabled[i] ? TH_TEXT_DIM : on ? TH_TEXT : TH_TEXT_SOFT;
        if (details[i][0])
        {
            text_draw_fit(x + pad + 12, ry + 8, 28, on ? FONT_BOLD : FONT_REGULAR, ink, ALIGN_LEFT, w - 2 * pad - 24,
                          labels[i]);
            text_draw_fit(x + pad + 12, ry + 44, 21, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - 2 * pad - 24,
                          details[i]);
        }
        else
            text_draw_fit(x + pad + 12, ry + 22, 28, on ? FONT_BOLD : FONT_REGULAR, ink, ALIGN_LEFT, w - 2 * pad - 24,
                          labels[i]);
    }
    if (top > 0)
        icon_draw(ICON_CHEVRON_UP, x + w - 70, y + 70, 28, TH_TEXT_DIM);
    if (top + visible < count)
        icon_draw(ICON_CHEVRON_DOWN, x + w - 70, y + h - 46, 28, TH_TEXT_DIM);
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE};
    static const char *const hints[] = {"Choose", "Close"};
    app_draw_hints(glyphs, hints, 2, NULL);
}

static void draw_confirm(void)
{
    const float w = 1100, pad = 52, h = has_check ? 600 : 520;
    const float x = (plat_width() - w) * 0.5f, y = (plat_height() - h) * 0.5f + (1.0f - shown) * 24.0f;
    draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(TH_CARD_A(0xfa), shown));
    draw_rect(x, y, w, 4, argb_alpha(TH_FOCUS, shown));
    icon_draw(ICON_ALERT_TRIANGLE, x + pad, y + 40, 44, TH_GOLD);
    text_draw_fit(x + pad + 64, y + 42, 34, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w - 2 * pad - 64, title);
    app_draw_wrapped(text, x + pad, y + 120, w - 2 * pad, 27, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_LEFT,
                     y + h - (has_check ? 200 : 120), 0, NULL);
    if (has_check)
    {
        const float cy = y + h - 180;
        draw_rrect(x + pad, cy, 40, 40, 8, checked ? TH_FOCUS : TH_PILL);
        if (checked)
            icon_draw(ICON_CHECK, x + pad + 6, cy + 6, 28, 0xffffffffu);
        text_draw_fit(x + pad + 60, cy + 6, 26, FONT_REGULAR, TH_TEXT, ALIGN_LEFT, w - 2 * pad - 60, check_label);
    }
    const char *labels2[2] = {tr("Cancel"), yes_label};
    const float bw = 300, by = y + h - 110;
    for (int i = 0; i < 2; ++i)
    {
        const bool on = (i == 1) == choice_yes;
        const float bx = x + w - pad - (2 - i) * (bw + 20) + 20;
        draw_rrect(bx, by, bw, 70, 35, on ? (i == 1 ? TH_FOCUS : TH_PILL) : TH_PILL_A(0x70));
        text_draw_fit(bx + bw * 0.5f, by + 20, 28, FONT_BOLD, on ? 0xffffffffu : TH_TEXT_SOFT, ALIGN_CENTER, bw - 30,
                      labels2[i]);
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_SQUARE};
    static const char *const hints[] = {"Choose", "Cancel", "Tick the box"};
    app_draw_hints(glyphs, hints, has_check ? 3 : 2, NULL);
}

static void draw_qr(void)
{
    const float w = 760, h = 900;
    const float x = (plat_width() - w) * 0.5f, y = (plat_height() - h) * 0.5f - 20;
    draw_rrect(x, y, w, h, TH_RADIUS, argb_alpha(TH_CARD_A(0xfa), shown));
    draw_rect(x, y, w, 4, argb_alpha(TH_FOCUS, shown));
    if (qr_ok)
    {
        const int size = qrcodegen_getSize(qr), quiet = 4;
        const float side = 620, cell = side / (float)(size + 2 * quiet);
        const float qx = x + (w - side) * 0.5f, qy = y + 50;
        draw_rect(qx, qy, side, side, 0xffffffffu);
        for (int row = 0; row < size; ++row)
            for (int col = 0; col < size; ++col)
                if (qrcodegen_getModule(qr, col, row))
                    draw_rect(floorf(qx + (col + quiet) * cell), floorf(qy + (row + quiet) * cell), ceilf(cell),
                              ceilf(cell), 0xff000000u);
    }
    app_draw_wrapped(qr_caption, x + 40, y + 700, w - 80, 26, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_CENTER, y + h - 30, 0,
                     NULL);
    static const int glyphs[] = {GLYPH_CIRCLE};
    static const char *const hints[] = {"Close"};
    app_draw_hints(glyphs, hints, 1, NULL);
}

void overlay_draw(void)
{
    if (kind == NONE)
        return;
    scrim();
    if (kind == MENU)
        draw_menu();
    else if (kind == CONFIRM)
        draw_confirm();
    else
        draw_qr();
}
