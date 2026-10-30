/*
 * Solanum - the catalog: the open list of PS5 homebrew Solanum installs (Oliv3rmoon/Solanum-Catalog).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * catalog.json is read with the self-update kit's bounded JSON readers (kit/update_check.h). An app the
 * catalog lists badly (a field missing, the wrong type or too long, a title ID that isn't four letters and
 * five digits, a repository that isn't owner/name, an unknown category, a file or folder name that can't be
 * one, an id listed twice, a title ID listed twice without `shares`) is left out with a line in the log, and
 * the rest are kept: one bad pull request never takes the store down.
 *
 * 0.4.0 reads what the catalog's new line adds: "none stated" as a license (No license), needs_game_files, the
 * "newest" channel (the newest release with the app's file, pre-releases included), `shares` (a later app with
 * the title ID of one listed before it), and each pin's title_name and prerelease. A text that isn't a catalog this version reads (not one JSON
 * object, a schema other than 1, no apps array) is refused whole.
 *
 * Plain C11 with no console calls, so the host test builds it alone (tests/store_check.c).
 */
#ifndef SOLANUM_CATALOG_H
#define SOLANUM_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CATALOG_URL "https://raw.githubusercontent.com/Oliv3rmoon/Solanum-Catalog/main/catalog.json"
#define CATALOG_SCHEMA 1
#define CATALOG_MAX_JSON (2u * 1024u * 1024u) /* the whole file, with each app's pinned release and its notes */
#define CATALOG_MAX_APPS 256
#define CATALOG_MAX_LINES 64 /* tips, and the hidden ones */
#define CATALOG_LINE 320
#define CATALOG_MAX_TAGS 8
#define CATALOG_TAG 24
#define CATALOG_MAX_PINS 6  /* the newest release the catalog pinned, and the ones before it */
#define CATALOG_NOTES 3072  /* the newest pinned release's notes, Markdown as GitHub has them (cut by the workflow) */

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

/* A release file the catalog's workflow downloaded and hashed (Solanum-Catalog's tools/pin_catalog.py): with it a
 * release that has no checksum file can still be checked, and one whose file changed after it was pinned is
 * refused. */
typedef struct
{
    char tag[40];
    char asset[128];   /* the file's name */
    uint64_t size;
    char sha256[65];   /* lowercase */
    char content[12];  /* the contentVersion of the app in it, as the workflow read it; empty: not known */
    char title_name[64]; /* 0.4.0: the name in its param.json, as the workflow read it; empty: not known */
    bool prerelease;   /* 0.4.0: GitHub calls the release a pre-release */
} CatalogPin;

/* The license an app's repository states when it states none: Solanum shows "No license". */
#define CATALOG_NO_LICENSE "none stated"

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
    /* 0.2.0: what the catalog's workflow and its authors add (all optional) */
    char author[48];
    char tags[CATALOG_MAX_TAGS][CATALOG_TAG]; /* lowercase: "emulator", "ps1", "media-player" */
    int tag_count;
    char data[72];      /* "/data/<name>": the folder it keeps its own files in, which Uninstall offers to remove */
    unsigned stars;     /* its repository's stars on GitHub, as the workflow last saw them */
    CatalogPin pins[CATALOG_MAX_PINS]; /* [0] is the newest release, when pin_count > 0 */
    int pin_count;
    char published[24]; /* the newest pinned release's date ("2026-10-29T19:18:39Z") */
    char notes[CATALOG_NOTES]; /* its notes */
    int source;         /* 0: Solanum's catalog; 1 and up: a custom catalog's (custom_catalog_*), third-party */
    /* 0.4.0 */
    bool needs_game_files; /* a port or launcher that runs the files of a game the player owns; it ships none */
    bool newest;        /* channel "newest": its release is the newest one with its file, pre-releases included */
    char shares[32];    /* the id of the app listed before it whose title ID it has too; empty: none */
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

/* A custom catalog's apps after Solanum's (catalog_parse of its text into `custom`): each one whose id and title ID
 * are new is added with `source`, never as one of the Nightshade family, never attested, never with a data folder
 * to remove, and without pins: its downloads are checked against what GitHub says, never what that catalog says.
 * Returns how many were added; *hidden how many were left out because another catalog lists them. */
int catalog_merge(Catalog *into, const Catalog *custom, int source, int *hidden);

/* Whether `path` may be an app's data folder: "/data/<name>", one folder directly in /data, named with letters,
 * digits, '.', '_' and '-', and none that the console, the HEN, ShadowMountPlus or Solanum keeps (homebrew, etaHEN,
 * solanum...). The install helper checks the same (helper/self_update_helper, data_folder_allowed). */
int catalog_data_folder_valid(const char *path);

/* 0.4.0: whether an app's repository states no license ("none stated"): Solanum says "No license". */
int catalog_no_license(const CatalogApp *app);

/* 0.4.0: the other apps of an app's title ID (one `shares` it with the other): their indexes in *others, at most
 * `most`; returns how many. A console holds one app of a title ID, so installing one replaces the other. */
int catalog_sharing(const Catalog *catalog, int index, int *others, int most);

/* Whether an app has `tag` (lowercase, exact). */
int catalog_has_tag(const CatalogApp *app, const char *tag);

/* "emulator" -> CATEGORY_EMULATOR; -1 for anything else. */
int catalog_category(const char *text);
/* What Browse calls a category: "Emulators", "Media"... */
const char *catalog_category_title(int category);

/* 1 when text is a title ID as the catalog asks for it: four capital letters and five digits. */
int catalog_title_id_valid(const char *text);

#endif
