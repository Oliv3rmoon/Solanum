/*
 * Solanum - the ZIPs of the version of an app installed now and of the one before it, kept so that the one before
 * can be put back in one press (Roll back), without asking GitHub:
 *
 *   <cache>/keep/<title ID>/current.zip    the release installed now, as downloaded and checked
 *   <cache>/keep/<title ID>/current.ini    what it is: tag, content version, SHA-256, size, address, file name
 *   <cache>/keep/<title ID>/previous.zip   the one before it
 *   <cache>/keep/<title ID>/previous.ini
 *
 * An install moves its download in as current (the current one becoming previous); a roll back installs previous
 * from its kept ZIP (resume.h hands a whole copy on without a byte from the network, checked against its SHA-256 by
 * the kit and the helper as any download) and swaps the two. Plain C11 with no console calls (tests/store_check.c).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SOLANUM_KEEP_H
#define SOLANUM_KEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    KEEP_CURRENT,
    KEEP_PREVIOUS
};

typedef struct
{
    char tag[40];
    char version[12]; /* the content version the helper put in place */
    char sha256[65];
    uint64_t size;
    char url[512];    /* https://github.com/<repository>/releases/download/<tag>/<file> */
    char name[128];
} KeptZip;

/* The paths of a slot's ZIP and its record. */
void keep_paths(const char *cache, const char *title, int slot, char *zip, size_t zip_size, char *record,
                size_t record_size);
/* A slot's record, when its ZIP is there at the size it says. */
bool keep_read(const char *cache, const char *title, int slot, KeptZip *out);
/* A finished install's download (`zip`, renamed into place: the same drive) as the current version, the current one
 * becoming the previous one. False when it couldn't be kept; the download is then deleted. */
bool keep_store(const char *cache, const char *title, const char *zip, const KeptZip *what);
/* After a roll back: the previous version is the current one now, and the other way round. */
bool keep_swap(const char *cache, const char *title);
/* Deletes an app's kept ZIPs (an uninstall). */
void keep_forget(const char *cache, const char *title);
/* The bytes every kept ZIP takes, and deleting them all (Installed's Clear). */
uint64_t keep_size(const char *cache);
void keep_clear(const char *cache);

#endif
