/*
 * Solanum - the catalog: the open list of PS5 homebrew Solanum installs (Oliv3rmoon/Solanum-Catalog).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * catalog.json is read with the self-update kit's bounded JSON readers (kit/update_check.h). An app the
 * catalog lists badly (a field missing, the wrong type or too long, a title ID that isn't four letters and
 * five digits, a repository that isn't owner/name, an unknown category, a file or folder name that can't be
 * one, an id or title ID listed twice) is left out with a line in the log, and the rest are kept: one bad
 * pull request never takes the store down. A text that isn't a catalog this version reads (not one JSON
 * object, a schema other than 1, no apps array) is refused whole.
 *
 * Plain C11 with no console calls, so the host test builds it alone (tests/store_check.c).
 */
#ifndef SOLANUM_CATALOG_H
#define SOLANUM_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#define CATALOG_URL "https://raw.githubusercontent.com/Oliv3rmoon/Solanum-Catalog/main/catalog.json"
#define CATALOG_SCHEMA 1
#define CATALOG_MAX_JSON (512u * 1024u) /* the whole file; it is a few KB */
#define CATALOG_MAX_APPS 128
#define CATALOG_MAX_LINES 64 /* tips, and the hidden ones */
#define CATALOG_LINE 320

/* The catalog's categories, in the order Browse lists them (after the Nightshade family). */
enum Category
{
    CATEGORY_EMULATOR,
    CATEGORY_MEDIA,
    CATEGORY_STREAMING,
    CATEGORY_GAME,
    CATEGORY_TOOL,
    CATEGORY_STORE,
    CATEGORY_COUNT
};

typedef struct
{
    char id[32];        /* short lowercase name */
    char name[64];      /* as the home screen shows it */
    bool family;        /* one of the Nightshade apps: Browse lists them first */
    int category;       /* enum Category */
    char summary[256];  /* one line */
    char title_id[10];  /* PPSA99008 */
    char repo[144];     /* owner/name on GitHub */
    char asset[128];    /* the release's ZIP; {version} is the tag without a leading 'v' */
    char folder[64];    /* the app's folder inside the ZIP */
    char checksums[64]; /* the release's checksum file, usually SHA256SUMS */
    bool attested;      /* the release's CI attests its build */
    char license[64];   /* SPDX */
} CatalogApp;

typedef struct
{
    char name[64];
    char about[400];
    CatalogApp apps[CATALOG_MAX_APPS];
    int app_count;
    char tips[CATALOG_MAX_LINES][CATALOG_LINE];  /* the loading screens' */
    int tip_count;
    char eggs[CATALOG_MAX_LINES][CATALOG_LINE];  /* shown only by the easter eggs */
    int egg_count;
    char about_footer[CATALOG_LINE];
} Catalog;

/* Where the reasons an entry was left out go, one line each ("catalog: ..."). May be NULL. */
typedef void (*catalog_log_fn)(void *user, const char *line);

/* Reads catalog.json into *out. Returns how many apps were kept (0 or more), or -1 when the text isn't a
 * catalog this version reads (*out is then empty). */
int catalog_parse(const char *json, size_t length, Catalog *out, catalog_log_fn log, void *user);

/* "emulator" -> CATEGORY_EMULATOR; -1 for anything else. */
int catalog_category(const char *text);
/* What Browse calls a category: "Emulators", "Media"... */
const char *catalog_category_title(int category);

/* 1 when text is a title ID as the catalog asks for it: four capital letters and five digits. */
int catalog_title_id_valid(const char *text);

#endif
