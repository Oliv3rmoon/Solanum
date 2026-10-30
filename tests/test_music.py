#!/usr/bin/env python3
# Solanum - the background music's host checks.
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and runs tests/music_check.c under the address and undefined-behaviour sanitizers with warnings as errors:
the playlist (which files, name order, rounds, shuffles that play every file once before any repeats), the WAV reader
(resampling to the output's rate, odd headers, what it refuses) and the mixer (volume, fades, clipping, interface
sounds mixed into the music). CC names another compiler (default: clang). None of it needs the PS5 SDK or SDL."""

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
            sources = [ROOT / "tests/music_check.c", ROOT / "src/playlist.c", ROOT / "src/wavstream.c",
                       ROOT / "src/mixer.c"]
            subprocess.run([cc, "-std=c11", "-O1", *SANITIZE, "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-UNDEBUG",
                            "-I" + str(ROOT / "src"), *map(str, sources), "-lm", "-o", str(executable)], check=True)
            scratch = Path(directory) / "scratch"
            scratch.mkdir()
            result = subprocess.run([str(executable), str(scratch)], capture_output=True, text=True, timeout=300,
                                    check=False)
        print(result.stdout.strip())
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
        self.assertIn("PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
