/*
 * ps5-native-app-boilerplate - What the app and the self-update helper say to each other.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The app sends the helper program to the console's payload loader; the same
 * connection then carries this conversation. Lines end with '\n'.
 *
 *   app     PSU1, update, <title ID>, <installed content version>,
 *           <new content version>, <size in bytes, 0 if unknown>, <sha256>,
 *           <app name>, <version name>            (nine lines)
 *   helper  ready                                 or  fail <reason>
 *   app     the archive in pieces: a four-byte little-endian length, then
 *           that many bytes (at most SELF_UPDATE_PIECE); a length of zero ends
 *   helper  p <bytes unpacked> <bytes to unpack>  five times a second
 *   helper  staged                                or  fail <reason>
 *   app     apply                                 or  cancel
 *   helper  applying
 *
 * After "applying" the app closes itself. The helper waits until the app is
 * gone, puts the new files in place and posts a notification. A connection
 * that ends before "apply", or "cancel" at any time before it, makes the
 * helper remove everything it wrote and leave the app untouched.
 *
 * ProsperoEden: another app's release (Nightshade's Apps, headless/apps.h) is
 * the same conversation with "install" and one line more; the app stays open:
 *
 *   app     PSU1, install, <title ID>, <its installed content version, or none>,
 *           <the release's content version, or any when it names none>,
 *           <size>, <sha256>, <app name>, <version name>,
 *           <the asking app's title ID>                   (ten lines)
 *   ...     as above, to "applying"
 *   helper  installed <content version> [<folder>]     or  fail <reason>
 *
 * The helper puts the release in the title's one installed folder (it must be
 * at that version), or with none in a new folder beside the asking app's.
 * A helper from before this answers "fail The request was refused"; the app's
 * own update is the conversation above, unchanged.
 *
 * Solanum: a new app in a folder the player chose, one of those ShadowMountPlus
 * scans (<drive>/homebrew), is "install-to" with one line more, that folder:
 *
 *   app     PSU1, install-to, ...the ten lines of install..., <the folder>
 *
 * And an app removed (Solanum's Uninstall), which sends no archive:
 *
 *   app     PSU1, remove, <title ID>, <its installed content version, or any>,
 *           <its data folder, /data/<name>, or ->, <its name>,
 *           <the asking app's title ID>                    (seven lines)
 *   helper  removing                                     (once its folder is found)
 *   helper  removed <folder> <home-screen result, 8 hex digits>   or  fail <reason>
 *
 * The helper removes the title's one installed folder (it must be found, be at
 * that version, not be the running app's, and hold no folder apps are installed
 * in), asks the console's app-install service to take the title off the home
 * screen, and removes the data folder when one is named. "00000000" is a home
 * screen that let the title go.
 */
#ifndef PS5_BOILERPLATE_SELF_UPDATE_PROTOCOL_H
#define PS5_BOILERPLATE_SELF_UPDATE_PROTOCOL_H

#define SELF_UPDATE_MAGIC "PSU1"
#define SELF_UPDATE_LINE 512u                 /* longest line either side accepts */
#define SELF_UPDATE_PIECE (1u << 20)          /* largest piece of the archive */
#define SELF_UPDATE_MAX_ARCHIVE 0x80000000ull /* 2 GiB, the catalog's own limit */
#define SELF_UPDATE_LOADER_PORT 9021

#endif
