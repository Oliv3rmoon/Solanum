#!/usr/bin/env python3
# Solanum - the background music's host checks.
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and runs tests/music_check.c under the address and undefined-behaviour sanitizers with warnings as errors:
the playlist (which files, name order, rounds, shuffles that play every file once before any repeats), the file
reader (WAV, MP3 and Ogg Vorbis, resampled to the output's rate; odd headers; what it refuses) and the mixer (volume,
fades, clipping, interface sounds mixed into the music). The two decoders (src/codecs.c: dr_mp3 and stb_vorbis,
third_party/) are compiled with their warnings off and checked against their recorded hashes. The MP3 and Ogg samples
in tests/fixtures/music are 0.3 s tones made with lame and oggenc (see its README). CC names another compiler
(default: clang). None of it needs the PS5 SDK or SDL."""

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SANITIZE = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


class MusicTest(unittest.TestCase):
    def test_music(self):
        cc = os.environ.get("CC", "clang")
        if not shutil.which(cc):
            self.skipTest(f"needs {cc}")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "music-check"
            codecs = Path(directory) / "codecs.o"
            # the vendored decoders: their code is theirs, so no -Werror and no -W options
            subprocess.run([cc, "-std=c11", "-O1", *SANITIZE, "-w", "-UNDEBUG", "-I" + str(ROOT / "src"), "-c",
                            str(ROOT / "src/codecs.c"), "-o", str(codecs)], check=True)
            sources = [ROOT / "tests/music_check.c", ROOT / "src/playlist.c", ROOT / "src/audiostream.c",
                       ROOT / "src/mixer.c"]
            subprocess.run([cc, "-std=c11", "-O1", *SANITIZE, "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-UNDEBUG",
                            "-I" + str(ROOT / "src"), *map(str, sources), str(codecs), "-lm", "-o", str(executable)],
                           check=True)
            scratch = Path(directory) / "scratch"
            scratch.mkdir()
            result = subprocess.run([str(executable), str(scratch), str(ROOT / "tests/fixtures/music")],
                                    capture_output=True, text=True, timeout=300, check=False)
        print(result.stdout.strip())
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
        self.assertIn("PASS", result.stdout)

    def test_the_vendored_decoders_are_the_recorded_files(self):
        for folder in ("dr_libs", "stb_vorbis"):
            record = json.loads((ROOT / "third_party" / folder / "SOURCE.json").read_text())
            self.assertRegex(record["commit"], r"^[0-9a-f]{40}$")
            for name, digest in record["files"].items():
                data = (ROOT / "third_party" / folder / name).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), digest, f"{folder}/{name} is not the pinned file")


if __name__ == "__main__":
    unittest.main()
