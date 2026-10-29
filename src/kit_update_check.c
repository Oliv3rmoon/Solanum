/*
 * Solanum - compiles the self-update kit's update check and bounded JSON readers (kit/update_check.c) into the
 * app. On the PS5 with its network side (update_check_read_param, which the kit's console side uses); elsewhere
 * the readers alone.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if !defined(__PROSPERO__)
#define UPDATE_CHECK_NO_NETWORK 1
#endif
#include "../kit/update_check.c"
