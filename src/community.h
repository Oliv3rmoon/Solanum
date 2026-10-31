/*
 * Solanum - the community: nightly, the Discord server of the Nightshade apps. About names it, with its address and a
 * QR code that opens it (ui/store_ui.c; Triangle shows the code larger), and a card shows the same once, at the first
 * start with the catalog on screen (main.c). Closing the card is kept in settings.ini as community_seen = 1.
 *
 * Plain C11 with no console calls, so the host check builds it alone (tests/community_check.c).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SOLANUM_COMMUNITY_H
#define SOLANUM_COMMUNITY_H

#include "nightshade.h"
#include "../third_party/qrcodegen/qrcodegen.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMMUNITY_NAME "nightly"
#define COMMUNITY_LINK "discord.gg/pSqgRU3YP9"
#define COMMUNITY_URL "https://" COMMUNITY_LINK
/* The address takes version 3; the code may be up to this. */
#define COMMUNITY_QR_VERSION 6
#define COMMUNITY_QR_BYTES qrcodegen_BUFFER_LEN_FOR_VERSION(COMMUNITY_QR_VERSION)
#define COMMUNITY_ABOUT_TEXTS 3

/* The QR code that opens COMMUNITY_URL, made as overlay.c's are; false when it can't be made. */
bool community_qr(uint8_t qr[COMMUNITY_QR_BYTES]);
/* What About shows of the server, top to bottom, in the language tr() has: its label, its name and its address. */
void community_about(const char *out[COMMUNITY_ABOUT_TEXTS]);
/* The words under the large code: the server and its address; the card's also say where to find it again. */
void community_caption(char *out, size_t size, bool card);
/* Whether the card is shown at this start: never closed before, and closing it can be kept (else it would come back
 * at every start). */
bool community_card_due(const Settings *s, bool can_save);
/* The card was closed: it isn't shown again (the caller saves the settings). */
void community_card_closed(Settings *s);

#endif
