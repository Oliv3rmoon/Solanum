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
} Installed;

/* titleId and contentVersion of a param.json; false when it can't be read or lacks either. */
bool apps_read_param(const char *path, char title[10], char version[12]);
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

/* The self-update kit's offer that installs the release (self_update_start): beside the asking app's folder
 * when the app isn't installed, in place of its own entries when it is. False when there is nothing to
 * install: no release with a SHA-256, an image, an installed version that can't be read, or the asking app's
 * own title. */
bool apps_make_offer(const CatalogApp *app, const github_release_asset *release, const Installed *installed,
                     const char *asking, self_update_offer *offer);

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
