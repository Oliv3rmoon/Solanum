/*
 * Solanum - what Solanum decides about the catalog's apps, apart from the console: where an app is installed,
 * which release is offered for it and what that offers, the offer the self-update kit installs it from, the
 * launch payload's conversation, and the store's two hidden jokes (the Konami code, a random tip or egg).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The rules are Nightshade's Settings > Apps' (Oliv3rmoon/Nightshade, headless/apps_release.cpp), in C: an app
 * is the folder ShadowMountPlus recorded it mounted from, else a folder beside Solanum's or in /data/homebrew
 * whose param.json names its title; a release is offered to get when the app isn't installed, to update when
 * its tag names a newer content version (or no version at all); the helper installs it beside Solanum's
 * folder or in place of the app's own files. Tested on the host (tests/store_check.c).
 */
#ifndef SOLANUM_APPS_H
#define SOLANUM_APPS_H

#include "catalog.h"
#include "github_release.h"
#include "self_update.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- where an app is ---- */

typedef struct
{
    char folder[256]; /* empty: not found */
    char version[12]; /* its sce_sys/param.json's contentVersion */
    bool image;       /* ShadowMountPlus mounts it from an image: it opens, it isn't updated here */
    char name[64];    /* 0.4.0: its param.json's name (its default language's titleName); empty: none */
} Installed;

/* titleId and contentVersion of a param.json; false when it can't be read or lacks either. */
bool apps_read_param(const char *path, char title[10], char version[12]);
/* 0.4.0: the name a param.json gives the app (localizedParameters: its default language's titleName, else the first
 * one it has), in `name`; false when it has none. */
bool apps_param_name(const char *json, size_t length, char *name, size_t size);

/* 0.4.0: which app an installed folder of a shared title ID is. Two apps can have one title ID (catalog_sharing: a
 * fork that kept its original's); a console holds one of them. The installed one is the app whose pinned release
 * names it as its param.json does (pin title_name), else whose name is that name (ignoring case), else the one
 * listed first. Returns the catalog's index of that app; `index` itself when its title ID is its own, or when the
 * folder can't be told (nothing installed). */
int apps_owner(const Catalog *catalog, int index, const Installed *installed);
/* Solanum's own folder as it lies on its drive (not the PS5's mount of it, /app0): where ShadowMountPlus
 * recorded it mounted it from (<registered>/app/<title>/mount.lnk), else install_dir when the app is there.
 * Empty when neither. */
void apps_own_folder(const char *own_title, const char *registered, const char *install_dir, char *out,
                     size_t size);
/* An app's folder: where ShadowMountPlus recorded it mounted it from, else the folder beside Solanum's own
 * (own_folder's parent) or in `homebrew` whose param.json names the title, the one named after it first.
 * registered is the console's /user, homebrew /data/homebrew. */
Installed apps_find(const char *title, const char *registered, const char *own_folder, const char *homebrew);
/* The bytes of every file under a folder (at most 32 folders deep); false when it can't all be read. */
bool apps_folder_size(const char *folder, uint64_t *bytes);

/* 0.2.0: an app's folder looked for in every folder ShadowMountPlus scans, as the install helper does: `roots` are
 * folders whose children may be apps (on the console apps_console_roots's). The one ShadowMountPlus recorded first,
 * then the folder named after the title in each root, then any folder of a root. */
Installed apps_find_in(const char *title, const char *registered, const char *const *roots, int root_count);
/* The console's: <drive>/homebrew and <drive>/etaHEN/games of /data, the two extended drives and eight USB drives, and
 * the extended and USB drives themselves (the helper's default_roots). Returns how many (at most `most`). */
int apps_console_roots(char roots[][48], int most);
/* Where a new app can be put: <drive>/homebrew of /data, /mnt/ext0..1 and /mnt/usb0..7 (the helper's install-to
 * folders). */
#define APPS_PLACES 11
const char *apps_place(int index);       /* "/data/homebrew", "/mnt/ext0/homebrew"... */
const char *apps_place_drive(int index); /* "/data", "/mnt/ext0"... */

/* ---- what a release offers ---- */

/* The content version a release's tag names. Exact: the tag is one ("01.000.090") or as Nightshade names its
 * releases ("v1.005.100"). Likely: three numbers, read as a content version writes them ("1.6.1" and "v1.6.1"
 * as 01.006.001). None: no version. */
typedef enum
{
    TAG_NONE,
    TAG_LIKELY,
    TAG_EXACT
} TagKind;
TagKind apps_tag_version(const char *tag, char version[12]);

/* What there is to do: nothing (no release found, or an image), get it, update it, or nothing newer
 * (current). A release whose tag names no version is offered to an installed app all the same: whether it
 * is newer can't be told. */
typedef enum
{
    OFFER_NONE,
    OFFER_GET,
    OFFER_UPDATE,
    OFFER_CURRENT
} Offer;
Offer apps_decide(const Installed *installed, bool found, const char *tag);
/* 0.2.0: the same for a release whose content version the catalog's workflow read (release->content): that version
 * is compared with the installed one, whatever the tag says ("v4.0", "build25" and tags of another numbering than the
 * app's contentVersion are told right). Without it, apps_decide by the tag. */
Offer apps_decide_release(const Installed *installed, bool found, const github_release_asset *release);

/* The self-update kit's offer that installs the release (self_update_start): beside the asking app's folder
 * when the app isn't installed, in place of its own entries when it is. False when there is nothing to
 * install: no release with a SHA-256, an image, an installed version that can't be read, or the asking app's
 * own title. */
bool apps_make_offer(const CatalogApp *app, const github_release_asset *release, const Installed *installed,
                     const char *asking, self_update_offer *offer);

/* ---- 0.2.0: the catalog's pinned hashes ---- */

/* The release the catalog pinned for an app (pin 0 is its newest; apps_pin_for finds one by tag), as if GitHub had
 * answered it: tag, name, size, the pinned SHA-256, the download's address and page made from the repository, and for
 * the newest its date and notes. False when the catalog pinned none. */
bool apps_release_from_pin(const CatalogApp *app, int pin, github_release_asset *out);
/* The catalog's pin of the file `asset` of release `tag`, or -1. */
int apps_pin_for(const CatalogApp *app, const char *tag, const char *asset);
/* GitHub's answer (github_release_asset_check's `reason` and `asset`) checked against the catalog's pins. A release
 * with neither a checksum file nor GitHub's digest (GITHUB_RELEASE_NO_HASH) is offered with the pinned SHA-256 when the
 * catalog pinned that very file of that tag at that size; a release whose checksum or digest differs from the pin, or
 * whose file is another size, is refused (GITHUB_RELEASE_HASHES_DIFFER: the file changed after it was pinned). The
 * reason to go by. */
int apps_check_pin(const CatalogApp *app, int reason, github_release_asset *asset);

/* ---- 0.2.0: finding and choosing ---- */

/* Whether an app matches a search: "#tag" (or "tag:tag") is one of its tags exactly; any other text is looked for,
 * ignoring case, in its name, summary, id, author, tags and category. An empty search matches all. */
bool apps_matches(const CatalogApp *app, const char *query);
/* Whether an installed app's newer release is to be offered as an update: not when updates are held, nor when the
 * player skipped that very release (skip: its tag, "" none). */
bool apps_update_wanted(int offer, bool hold, const char *skip, const char *tag);

/* ---- opening it ---- */

/* The launch payload's conversation, on a connection to it: the title to open and the one asking. Returns
 * true when the payload waits for the asking app to close and then opens the title; otherwise *reason says
 * why not (English). */
bool apps_ask_launch(const self_update_channel *channel, const char *title, const char *asking, char *reason,
                     size_t size);
/* What the payload answers for an app the console doesn't list yet (a new one the homebrew loader has not
 * scanned). */
#define APPS_NOT_LISTED "The console doesn't list the app yet"

/* ---- the hidden ones ---- */

/* The Konami code on Browse: Up Up Down Down Left Right Left Right, then Circle, Cross (BTN_* of
 * nightshade.h). Each frame's new presses go in; true on the press that ends the code (which is then
 * the code's, not the screen's). Any other press, or two at once, breaks it; a code started over in the
 * middle of a try still counts. */
#define KONAMI_LENGTH 10
typedef struct
{
    int seen[KONAMI_LENGTH]; /* the last presses, the newest last; -1 for one that isn't a single button */
    int count;
} Konami;
void konami_reset(Konami *konami);
bool konami_feed(Konami *konami, uint32_t pressed);

/* A random one of `count` lines (a tip, an egg): never the one shown last (`previous`) when there are two or
 * more. -1 when there are none. *state is the generator's (xorshift32), seeded with apps_seed. */
int apps_pick(int count, int previous, uint32_t *state);
uint32_t apps_seed(uint64_t entropy);

#endif
