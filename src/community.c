/*
 * Solanum - the community (community.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "community.h"

#include "i18n.h"

#include <stdio.h>

bool community_qr(uint8_t qr[COMMUNITY_QR_BYTES])
{
    uint8_t temp[COMMUNITY_QR_BYTES];
    /* overlay.c's qr_open: medium error correction, raised while the version has room for it */
    return qrcodegen_encodeText(COMMUNITY_URL, temp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                COMMUNITY_QR_VERSION, qrcodegen_Mask_AUTO, true);
}

void community_about(const char *out[COMMUNITY_ABOUT_TEXTS])
{
    out[0] = tr("JOIN THE COMMUNITY");
    out[1] = COMMUNITY_NAME;
    out[2] = COMMUNITY_LINK;
}

void community_caption(char *out, size_t size, bool card)
{
    const int n = snprintf(out, size, tr("Join %s, the community of the Nightshade apps, on Discord: %s"),
                           COMMUNITY_NAME, COMMUNITY_LINK);
    if (card && n >= 0 && (size_t)n < size)
        snprintf(out + n, size - (size_t)n, "\n%s", tr("You can find it again in About."));
}

bool community_card_due(const Settings *s, bool can_save)
{
    return !s->community_seen && can_save;
}

void community_card_closed(Settings *s)
{
    s->community_seen = true;
}
