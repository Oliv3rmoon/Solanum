/*
 * Solanum - the icons on the apps' tiles (0.5.0): the catalog's icons (appicon.h) fetched, checked, decoded and made
 * textures for the tiles on the screen.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A tile asks for its app's icon as it is drawn (tile_icons_texture), so only the icons of tiles on the screen are
 * fetched, the most recently drawn first. A thread of its own, as the store's is (store.c), reads each one from the
 * cache or fetches it with the store's platform (store_kit_platform), checks its size and SHA-256 against the
 * catalog, decodes it with stb_image and fits it to APPICON_SIDE with the tile's rounded corners; the main thread
 * makes the texture (tile_icons_poll, two a frame at most). At most APPICON_TEXTURES textures are kept (about
 * APPICON_BUDGET_BYTES with their pixels and mips): the least recently drawn gives way, and comes back from the cache
 * when its tile is on the screen again. A tile with no icon, one on its way, or one that failed shows its letter.
 *
 * Only the main thread calls these, the last two while the screens draw (under store_lock).
 */
#ifndef SOLANUM_TILE_ICONS_H
#define SOLANUM_TILE_ICONS_H

#include "../catalog.h"
#include "../platform/platform.h"

/* `cache`: the data folder's cache (the icons go in its icons/ folder). */
void tile_icons_init(const char *cache);
/* Frees every texture; the thread stops at its next step. */
void tile_icons_shutdown(void);
/* Each frame, before the screens draw: textures for the icons the thread decoded, and, when the catalog is a new
 * one, the cache pruned to its icons and the failed ones tried again. */
void tile_icons_poll(void);
/* An app's icon for its tile, drawn now; NULL (and asked for, when it isn't on its way) when it has none, it is on
 * its way, or it failed. Solanum's catalog's apps only. */
PlatTexture *tile_icons_texture(const CatalogApp *app);
/* The textures kept now, and what they cost (pixels and the GPU's copy), for the log. */
void tile_icons_usage(int *textures, size_t *bytes);

#endif
