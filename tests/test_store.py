#!/usr/bin/env python3
# Solanum - the store's host checks.
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and runs, under the address and undefined-behaviour sanitizers with warnings as errors:

  tests/store_check.c   the catalog (bad entries left out with a log line), the release's file from the
                        catalog's pattern and the apps' real releases (tests/fixtures), checksums (a download
                        that isn't the release's is refused before anything is unpacked), resuming an
                        interrupted download, where an app is and what is offered, the launch payload's
                        conversation, the Konami code, and a random tip or egg;
  tests/helper_check.cpp the install helper's whole conversation (helper/self_update_helper): an app installed
                        beside Solanum or updated in its own folder, a download that isn't the one announced
                        refused, and nothing left behind.

CC and CXX name other compilers (default: clang, clang++). Also checks what the payloads write where, and that
the store asks only the catalog and the apps' own repositories."""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SANITIZE = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
KIT = ["update_check.c", "github_release.c", "self_update.c", "self_update_sha256.c"]
MINIZ = ["miniz.c", "miniz_tdef.c", "miniz_tinfl.c", "miniz_zip.c"]


def compilers():
    cc = os.environ.get("CC", "clang")
    cxx = os.environ.get("CXX", "clang++")
    return cc, cxx


class StoreTest(unittest.TestCase):
    def run_check(self, command, timeout=300):
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
        print(result.stdout.strip())
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
        self.assertIn("PASS", result.stdout)

    def test_store(self):
        cc, _ = compilers()
        if not shutil.which(cc):
            self.skipTest(f"needs {cc}")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "store-check"
            sources = [ROOT / "tests/store_check.c", ROOT / "src/catalog.c", ROOT / "src/apps.c",
                       ROOT / "src/resume.c", ROOT / "src/util.c", *[ROOT / "kit" / name for name in KIT]]
            subprocess.run([cc, "-std=c11", "-O1", *SANITIZE, "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                            "-UNDEBUG", "-DUPDATE_CHECK_NO_NETWORK=1", "-I" + str(ROOT / "kit"),
                            "-I" + str(ROOT / "src"), *map(str, sources), "-pthread", "-o", str(executable)],
                           check=True)
            scratch = Path(directory) / "scratch"
            scratch.mkdir()
            self.run_check([str(executable), str(ROOT / "tests/fixtures"), str(scratch)])

    def test_install_helper(self):
        cc, cxx = compilers()
        if not shutil.which(cc) or not shutil.which(cxx):
            self.skipTest(f"needs {cc} and {cxx}")
        helper = ROOT / "helper/self_update_helper"
        with tempfile.TemporaryDirectory() as directory:
            objects = []
            for name in ("update_check.c", "self_update_sha256.c"):
                out = Path(directory) / (name + ".o")
                subprocess.run([cc, "-std=c11", "-O1", *SANITIZE, "-DUPDATE_CHECK_NO_NETWORK=1",
                                "-I" + str(ROOT / "kit"), "-c", str(ROOT / "kit" / name), "-o", str(out)],
                               check=True)
                objects.append(str(out))
            for name in MINIZ:
                out = Path(directory) / (name + ".o")
                subprocess.run([cc, "-O1", "-w", *SANITIZE, "-I" + str(ROOT / "third_party/miniz"), "-c",
                                str(ROOT / "third_party/miniz" / name), "-o", str(out)], check=True)
                objects.append(str(out))
            executable = Path(directory) / "helper-check"
            subprocess.run([cxx, "-std=c++20", "-O1", *SANITIZE, "-Wall", "-Wextra", "-Werror",
                            "-I" + str(helper), "-I" + str(ROOT / "kit"), "-I" + str(ROOT / "third_party"),
                            str(ROOT / "tests/helper_check.cpp"), str(helper / "updater.cpp"),
                            str(helper / "files.cpp"), str(helper / "archive.cpp"), *objects, "-pthread", "-o",
                            str(executable)], check=True)
            self.run_check([str(executable)])

    def test_payloads_write_to_solanums_folder(self):
        main = (ROOT / "helper/self_update_helper/main.cpp").read_text(encoding="utf-8")
        self.assertIn('"/data/solanum/logs/updater.log"', main)
        self.assertIn('"/data/solanum/previous-version"', main)
        self.assertNotIn("prosperoeden", main)
        launch = (ROOT / "helper/app_launch/main.c").read_text(encoding="utf-8")
        self.assertIn('#define LAUNCH_LOG "/data/solanum/logs/app-launch.log"', launch)

    def test_the_store_asks_only_the_catalog_and_the_apps_releases(self):
        catalog = (ROOT / "src/catalog.h").read_text(encoding="utf-8")
        self.assertIn('"https://raw.githubusercontent.com/Oliv3rmoon/Solanum-Catalog/main/catalog.json"', catalog)
        nightshade = (ROOT / "src/nightshade.h").read_text(encoding="utf-8")
        self.assertIn('#define NP_TITLE_ID "PPSA76526"', nightshade)
        self.assertIn('#define NP_DATA "/data/solanum"', nightshade)
        # Every address the store's own sources name: the catalog, GitHub's API and its release downloads. No
        # address comes from anywhere else: there is no "add any URL".
        allowed = ("https://raw.githubusercontent.com/Oliv3rmoon/Solanum-Catalog/", "https://api.github.com/",
                   "https://github.com/")
        for name in ("store.c", "store_console.c", "catalog.h", "catalog.c", "apps.c", "resume.c", "ui/store_ui.c",
                     "ui/page.c", "ui/keyboard.c", "main.c"):
            text = (ROOT / "src" / name).read_text(encoding="utf-8")
            for url in re.findall(r'"(https?://[^"]*)', text):
                self.assertTrue(url.startswith(allowed), f"{name}: {url}")


if __name__ == "__main__":
    unittest.main()
