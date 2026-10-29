#!/usr/bin/env python3
# Solanum - its own update's release decision, built and run on the host (Dulcamara's).
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds tests/update_release_check.c with src/update_release.c and the boilerplate's SHA-256
under the address and undefined-behaviour sanitizers, warnings as errors, and runs it. CC names
another compiler (default: clang). Also checks that the app's own update asks only Oliv3rmoon/Solanum."""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ["-std=c11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-UNDEBUG"]


class UpdateReleaseTest(unittest.TestCase):
    def test_release_decision(self):
        cc = os.environ.get("CC", "clang")
        if not shutil.which(cc):
            self.skipTest(f"needs {cc}")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "update-release-check"
            subprocess.run([cc, *FLAGS, "-I" + str(ROOT / "src"),
                            str(ROOT / "tests/update_release_check.c"), str(ROOT / "src/update_release.c"),
                            str(ROOT / "kit/self_update_sha256.c"),
                            "-o", str(executable)], check=True)
            result = subprocess.run([str(executable), directory], capture_output=True, text=True,
                                    timeout=120, check=False)
        print(result.stdout.strip())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-3000:])
        self.assertIn("PASS", result.stdout)

    def test_the_app_asks_only_solanums_releases(self):
        header = (ROOT / "src/update_release.h").read_text(encoding="utf-8")
        self.assertIn('#define UPDATE_REPO "Oliv3rmoon/Solanum"', header)
        self.assertIn('#define UPDATE_ZIP "PPSA76526.zip"', header)
        update = (ROOT / "src/update.c").read_text(encoding="utf-8")
        self.assertIn("net_request(UPDATE_LATEST_URL", update)
        self.assertIn("release_evaluate(", update)
        self.assertIn("release_file_sha256(zip, got)", update)
        # No release list is read but releases/latest.
        self.assertIsNone(re.search(r"/releases\?per_page", update))


if __name__ == "__main__":
    unittest.main()
