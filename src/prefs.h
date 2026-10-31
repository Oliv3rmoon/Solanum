/*
 * Solanum - what the player decided for each app, and how it came to be installed, kept in the data folder's
 * apps.ini (one "<title ID>.<key> = <value>" a line):
 *
 *   PPSA12345.hold = 1        its updates are never offered, nor installed by themselves
 *   PPSA12345.skip = v1.2.3   that release is not offered; a newer one is
 *   PPSA12345.by = hand       found installed by hand and looked after by Solanum from then on ("solanum": it
 *                             installed it)
 *   PPSA12345.app = vkquake   0.4.0: the app (its catalog id) the hold and skip are for, when two apps share the title
 *                             ID; none: the app listed first (as 0.3.0, which listed only it, wrote them)
 *   PPSA12345.tag = v0.2.0-beta.1   0.4.0: the release Solanum put in place last,
 *   PPSA12345.version = 00.002.000  and the content version it put there: while the installed one is that version,
 *                             that release is what is installed, so a tag naming no version isn't offered again
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
    char app[32];     /* 0.4.0: whose hold and skip they are; empty: the title's app listed first */
    char tag[40];     /* 0.4.0: the release Solanum installed last */
    char version[12]; /* ... and the content version it put in place */
} AppPrefs;

/* Reads the file (a missing one is no decisions). */
void prefs_load(const char *path);
/* Writes it whole (through a .new file); false when it can't be written. */
bool prefs_save(void);
/* The title's decisions: NULL when there are none and `create` is false, or no room is left. */
AppPrefs *prefs_get(const char *title, bool create);
/* Forgets how an app was installed (an uninstall), and which release it was; its hold and skip stay. */
void prefs_forget_install(const char *title);
/* 0.4.0: whether a title's hold and skip are the decisions of `app` (a catalog id): the app they name, or, when they
 * name none (written before 0.4.0), the title's app listed first (`first`). */
bool prefs_decisions_for(const AppPrefs *prefs, const char *app, bool first);
int prefs_count(void);

#endif
