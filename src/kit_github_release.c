/*
 * Solanum - compiles the self-update kit's GitHub release reader (kit/github_release.c) into the app.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if !defined(__PROSPERO__)
#define UPDATE_CHECK_NO_NETWORK 1
#endif
#include "../kit/github_release.c"
