/*
 * Solanum - what opens over a screen and takes its buttons until it closes: a list to choose from (an app's More,
 * Versions, where to install) and a question to answer (Uninstall, a third-party app). The screen that opened one asks
 * each frame whether it was answered.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SOLANUM_OVERLAY_H
#define SOLANUM_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#define MENU_MAX 24

/* A list: up / down, Cross chooses, Circle closes. Labels and details are copied (and translated by the caller). An
 * item that is not enabled can't be chosen. */
void menu_open(int id, const char *title, int count, const char *const *labels, const char *const *details,
               const bool *enabled, int selected);
/* Changes an open menu's items in place (Versions, once GitHub has answered); the choice is kept when it can be. */
void menu_update(int id, const char *title, int count, const char *const *labels, const char *const *details,
                 const bool *enabled);
bool menu_is_open(int id);
/* A choice made: true once, with the item. */
bool menu_chosen(int id, int *item);

/* A question: Cross on the yes button, or Circle (no). `check`, when not NULL, is a box the player ticks with
 * Square (Uninstall's "and its data"). */
void confirm_open(int id, const char *title, const char *text, const char *yes, const char *check, bool checked);
/* Answered: true once, *yes and *checked say how. */
bool confirm_answered(int id, bool *yes, bool *checked);

/* A large QR code of `text` with a caption, until Cross or Circle. */
void qr_open(const char *text, const char *caption);

bool overlay_active(void);
/* This frame's presses: taken (cleared) while an overlay is open. */
void overlay_input(uint32_t *pressed);
void overlay_draw(void);

#endif
