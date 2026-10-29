/*
 * Solanum - an on-screen keyboard for Browse's search. The system's (libSceImeDialog) stops a native title
 * from starting (tools/check-imports.py), so it is drawn here: letters, digits and a few signs on a grid.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SOLANUM_KEYBOARD_H
#define SOLANUM_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opens it on `text` (kept to put back if it is cancelled). */
void keyboard_open(const char *text);
bool keyboard_active(void);
/* This frame's presses: the text edited in place. True while it is open (it takes every press); *done when
 * Done (or OPTIONS) closed it with the text kept; Circle puts the text back as it was. */
bool keyboard_input(uint32_t pressed, char *text, size_t size, bool *done);
void keyboard_draw(const char *text);

#endif
