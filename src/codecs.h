/*
 * Solanum - the MP3 and Ogg Vorbis decoders, as audiostream.c uses them.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dr_mp3 (David Reid, public domain or MIT-0) is vendored unmodified, and stb_vorbis (Sean Barrett, public domain or
 * MIT) with a small hardening patch (third_party/stb_vorbis/solanum-hardening.patch), in third_party/. Their
 * declarations come in here; their code is compiled once, in codecs.c, with the same options. Files come in through the callbacks and FILE pointers audiostream.c gives them, never a path the
 * libraries open themselves.
 */
#ifndef SOLANUM_CODECS_H
#define SOLANUM_CODECS_H

#include <stdio.h>

#define DR_MP3_NO_STDIO 1 /* no fopen of its own, no wide-character paths */
#define DR_MP3_NO_SIMD 1  /* the decoder is nowhere near the console's limit, and the SIMD paths would need probing */
#include "../third_party/dr_libs/dr_mp3.h"

#define STB_VORBIS_NO_PUSHDATA_API 1
#define STB_VORBIS_HEADER_ONLY 1
#include "../third_party/stb_vorbis/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#endif
