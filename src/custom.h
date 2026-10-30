/*
 * Solanum - custom catalogs: other people's lists of apps, added by the player in Settings > Catalogs and kept in the
 * data folder's catalogs.ini ("catalog = <what was typed>" a line, at most CUSTOM_MAX). One is either a GitHub
 * repository, owner/name (or https://github.com/owner/name), whose catalog.json Solanum reads as it reads its own
 * (https://raw.githubusercontent.com/owner/name/HEAD/catalog.json), or the HTTPS address of a catalog.json anywhere.
 *
 * A custom catalog is third-party, always: its apps are shown after Solanum's own, marked with the catalog's name,
 * never as attested or as one of the Nightshade family; an app it lists that Solanum's catalog (or an earlier custom
 * one) lists too is left out; its pins are not used, so its downloads are checked against what GitHub says (the
 * release's checksum file or GitHub's digest), never against what the catalog says; and, as for every app, the
 * download comes from the app's own GitHub release, never from the catalog's host (catalog_merge).
 *
 * Plain C11 with no console calls (tests/store_check.c). Not thread-safe: read and changed under the store's lock.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SOLANUM_CUSTOM_H
#define SOLANUM_CUSTOM_H

#include <stdbool.h>
#include <stddef.h>

#define CUSTOM_MAX 8
#define CUSTOM_TEXT 300

void custom_load(const char *path);
bool custom_save(void);
int custom_count(void);
/* The i-th catalog as the player added it ("someone/their-catalog", "https://example.com/catalog.json"). */
const char *custom_text(int index);
/* Where its catalog.json is read from. */
void custom_url(int index, char *out, size_t size);
/* Its name on screen: the repository, or the address's host. */
void custom_name(int index, char *out, size_t size);
/* Adds one; false (and *why, English) when it isn't a repository or an HTTPS address, is listed already, or there's
 * no room. */
bool custom_add(const char *text, char *why, size_t size);
void custom_remove(int index);

/* What custom_add takes, normalised: "owner/name" for a repository (also from https://github.com/owner/name), the
 * address itself for any other HTTPS one. False for anything else. */
bool custom_normalise(const char *text, char *out, size_t size);
/* The file a catalog's copy is kept in, from its address (cache/catalog-<16 hex>.json). */
void custom_copy_path(const char *cache, const char *url, char *out, size_t size);

#endif
