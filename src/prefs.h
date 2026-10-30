/*
 * Solanum - what the player decided for each app, and how it came to be installed, kept in the data folder's
 * apps.ini (one "<title ID>.<key> = <value>" a line):
 *
 *   PPSA12345.hold = 1        its updates are never offered, nor installed by themselves
 *   PPSA12345.skip = v1.2.3   that release is not offered; a newer one is
 *   PPSA12345.by = hand       found installed by hand and looked after by Solanum from then on ("solanum": it
 *                             installed it)
 *
 * Plain C11 with no console calls, so the host test builds it alone (tests/store_check.c). Not thread-safe: the store
 * reads and writes it under its lock.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SOLANUM_PREFS_H
#define SOLANUM_PREFS_H

#include <stdbool.h>
#include <stddef.h>

#define PREFS_MAX 256

typedef enum
{
    BY_UNKNOWN,
    BY_SOLANUM, /* Solanum installed it */
    BY_HAND     /* it was there, put there some other way; Solanum looks after it now */
} InstalledBy;

typedef struct
{
    char title[10];
    bool hold;
    char skip[40];
    int by; /* InstalledBy */
} AppPrefs;

/* Reads the file (a missing one is no decisions). */
void prefs_load(const char *path);
/* Writes it whole (through a .new file); false when it can't be written. */
bool prefs_save(void);
/* The title's decisions: NULL when there are none and `create` is false, or no room is left. */
AppPrefs *prefs_get(const char *title, bool create);
/* Forgets how an app was installed (an uninstall); its hold and skip stay. */
void prefs_forget_install(const char *title);
int prefs_count(void);

#endif
