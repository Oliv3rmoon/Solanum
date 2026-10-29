/*
 * Solanum - compiles the self-update kit's console side into the PS5 build: libcurl's helpers
 * (kit/console_curl.c: the system resolver, the certificate list, non-blocking sockets) and the kit's platform
 * (kit/self_update_ps5.c: HTTPS, the payload loader on port 9021). The helper and the app's param.json are
 * found where Solanum's folder is (store_app_file): /app0 exists only in the sandbox, and Solanum asks the HEN
 * to free it at start.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#if defined(__PROSPERO__) && defined(NP_HAVE_CURL)
#include "store.h"
#define SELF_UPDATE_HELPER_PATH store_app_file("self-updater.elf")
#define SELF_UPDATE_PARAM_PATH store_app_file("sce_sys/param.json")
#include "../kit/console_curl.c"
#include "../kit/self_update_ps5.c"
#endif
