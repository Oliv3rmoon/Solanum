/*
 * Solanum - the MP3 and Ogg Vorbis decoders, compiled once (codecs.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   dr_mp3 (v0.7.4, commit bad753d), public domain or MIT-0: third_party/dr_libs/
 *   stb_vorbis 1.22 (commit 1ee679c), public domain or MIT: third_party/stb_vorbis/, with its hardening patch
 *     (solanum-hardening.patch: a damaged header's lengths are not trusted)
 * Their warnings are theirs; keep them out of ours.
 */
#pragma clang diagnostic ignored "-Weverything"
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"

#define NDEBUG /* no __assert in the PS5 libc */
#include <stdlib.h>
#ifndef alloca
#define alloca __builtin_alloca /* stb_vorbis's scratch memory; not every libc declares it */
#endif

#define DR_MP3_IMPLEMENTATION
#include "codecs.h"
#include "../third_party/stb_vorbis/stb_vorbis.c"
