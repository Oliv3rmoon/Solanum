/*
 * Solanum - compiles the self-update kit's download-and-install job (kit/self_update.c) and its SHA-256
 * (kit/self_update_sha256.c, also the app's own update's check against SHA256SUMS, update_release.c) into the
 * app.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if !defined(__PROSPERO__)
#define UPDATE_CHECK_NO_NETWORK 1
#endif
#include "../kit/self_update.c"
