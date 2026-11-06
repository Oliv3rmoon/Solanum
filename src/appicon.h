/*
 * Solanum - the catalog's app icons (0.5.0): where each one is kept, how a download or a kept copy is checked, which
 * ones stay in memory, and the picture a tile draws. ui/tile_icons.h fetches and shows them.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An icon is a PNG in Solanum's catalog's repository (catalog.h, CatalogIcon), fetched from the branch catalog.json is
 * fetched from (CATALOG_ICON_BASE), and only for a tile on the screen. It is kept only when its size and SHA-256 are
 * the ones the catalog gives: in <cache>/icons/<sha256>.png, so an icon is fetched once, a later start shows it
 * without the network, and a new icon in the catalog (another SHA-256) is another file. A kept copy is checked again
 * each time it is read; one that no longer matches is deleted, and the icon is fetched again.
 *
 * In memory an icon is a texture of APPICON_SIDE pixels, with the tile's rounded corners: the size an app's page draws
 * its tile at (the lists draw it smaller, from its mips). At most APPICON_TEXTURES are kept (AppIconLru): the least
 * recently drawn gives way when another arrives, never one drawn in the last frame.
 *
 * Plain C11 with no console calls, so the host test builds it alone (tests/store_check.c).
 */
#ifndef SOLANUM_APPICON_H
#define SOLANUM_APPICON_H

#include "catalog.h"

#include <stddef.h>
#include <stdint.h>

#define APPICON_SIDE 168     /* the texture's width and height: an app's page draws its tile at 168 */
#define APPICON_RADIUS 0.2f  /* the corners' radius, of the side, as app_draw_tile rounds a tile */
#define APPICON_TEXTURES 16  /* textures kept: a list shows 6 tiles at most, an app's page 1, and some to go back to */
#define APPICON_PIXEL_BYTES (APPICON_SIDE * APPICON_SIDE * 4)
/* What one costs in memory: its pixels, which a PlatTexture keeps, and the GPU's copy with its mips (a third more). */
#define APPICON_TEXTURE_BYTES (APPICON_PIXEL_BYTES + APPICON_PIXEL_BYTES * 4 / 3)
#define APPICON_BUDGET_BYTES (APPICON_TEXTURES * APPICON_TEXTURE_BYTES)

/* As the kit's fetch (self_update_platform.fetch): a small file into body. 0 when an answer arrived (*status). */
typedef int (*appicon_fetch_fn)(void *user, const char *url, char *body, size_t capacity, size_t *length,
                                int *status);

typedef enum
{
    APPICON_NONE = 0,    /* neither kept nor fetched: *why says why */
    APPICON_KEPT = 1,    /* read from the cache, checked */
    APPICON_FETCHED = 2  /* fetched, checked, and kept */
} AppIconFrom;

/* Where the icon is fetched from: CATALOG_ICON_BASE and its path. Its length, 0 when it doesn't fit. */
size_t appicon_url(const CatalogIcon *icon, char *out, size_t size);
/* Where an icon of this SHA-256 is kept: <cache>/icons/<sha256>.png. Its length, 0 when it doesn't fit. */
size_t appicon_path(const char *cache, const char *sha256, char *out, size_t size);
/* 1 when `data` is the icon the catalog names: its size, a PNG's signature, and its SHA-256. */
int appicon_check(const CatalogIcon *icon, const void *data, size_t length);
/* The kept copy into body (capacity CATALOG_ICON_BYTES or more). 1: read and checked. 0: none kept. -1: one was
 * kept, but it isn't the icon (changed since): it is deleted. */
int appicon_read(const char *cache, const CatalogIcon *icon, unsigned char *body, size_t capacity, size_t *length);
/* Keeps a fetched icon, when appicon_check accepts it (written beside it, then renamed). 1 kept. */
int appicon_keep(const char *cache, const CatalogIcon *icon, const void *data, size_t length);
/* The icon's PNG into body (capacity CATALOG_ICON_BYTES or more): the kept copy when there is one that checks,
 * otherwise fetched with `fetch` (NULL: offline), checked, and kept. A download that isn't the icon is never kept. */
AppIconFrom appicon_load(const char *cache, const CatalogIcon *icon, appicon_fetch_fn fetch, void *user,
                         unsigned char *body, size_t capacity, size_t *length, char *why, size_t why_size);
/* Deletes the kept icons whose SHA-256 isn't one of `keep` (the catalog's now), and unfinished copies. How many. */
int appicon_prune(const char *cache, const char (*keep)[65], int count);

/* A picture of width x height (RGBA, straight alpha, at most CATALOG_ICON_SIDE each way) shrunk or grown into
 * APPICON_SIDE x APPICON_SIDE (out: APPICON_PIXEL_BYTES): whole, centred, by the area each pixel covers (alpha
 * weighted, so no dark fringes); a picture that isn't square leaves the rest clear. 1 done. */
int appicon_fit(const uint8_t *rgba, int width, int height, uint8_t *out);
/* Rounds a square picture's corners (radius in pixels, anti-aliased): what is outside them becomes clear. */
void appicon_round(uint8_t *rgba, int side, float radius);

/* ---- the textures in memory ---- */

typedef struct
{
    char sha256[65];
    void *texture;  /* the caller's (a PlatTexture) */
    uint64_t used;  /* the frame it was last drawn in */
} AppIconSlot;

typedef struct
{
    AppIconSlot slot[APPICON_TEXTURES];
    int count;
} AppIconLru;

/* The texture of an icon, marked drawn in `frame`; NULL when it isn't kept. */
void *appicon_lru_get(AppIconLru *lru, const char *sha256, uint64_t frame);
/* Keeps a texture. When all slots are taken, the least recently drawn one not drawn in `frame` or the frame before
 * gives way: its texture in *evicted (to be freed) and its SHA-256 in evicted_sha. 0 when none may give way (the
 * texture isn't kept). */
int appicon_lru_put(AppIconLru *lru, const char *sha256, void *texture, uint64_t frame, void **evicted,
                    char evicted_sha[65]);

#endif
