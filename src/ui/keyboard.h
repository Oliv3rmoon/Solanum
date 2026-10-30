/*
 * Solanum - an on-screen keyboard for Browse's search and for a custom catalog's address. The system's
 * (libSceImeDialog) stops a native title from starting (tools/check-imports.py), so it is drawn here: letters, digits
 * and the signs an address needs on a grid. A USB keyboard types into it too (platform.h, plat_keyboard_text).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SOLANUM_KEYBOARD_H
#define SOLANUM_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYBOARD_TEXT_MAX 320 /* the longest text it edits (a catalog's address) */

/* Opens it on `text` (kept to put back if it is cancelled), with `prompt` shown while the text is empty. */
void keyboard_open(const char *text);
void keyboard_open_prompt(const char *text, const char *prompt);
bool keyboard_active(void);
/* This frame's presses: the text edited in place. True while it is open (it takes every press); *done when
 * Done (or OPTIONS, or Enter) closed it with the text kept; Circle (or Escape) puts the text back as it was. */
bool keyboard_input(uint32_t pressed, char *text, size_t size, bool *done);
void keyboard_draw(const char *text);

#endif
