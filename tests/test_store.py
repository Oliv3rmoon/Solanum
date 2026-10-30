#!/usr/bin/env python3
# Solanum - the store's host checks.
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and runs, under the address and undefined-behaviour sanitizers with warnings as errors:

  tests/store_check.c   the catalog (bad entries left out with a log line), the release's file from the
                        catalog's pattern and the apps' real releases (tests/fixtures), checksums (a download
                        that isn't the release's is refused before anything is unpacked), resuming an
                        interrupted download, where an app is and what is offered, the launch payload's
                        conversation, the Konami code, and a random tip or egg; and 0.2.0's: the catalog's
                        pinned hashes (a release with no checksum file offered with its pin, a file that changed
                        after it was pinned refused), tags and search, custom catalogs (third-party: never
                        family, attested or pinned), holds and skips, the kept ZIPs a roll back installs from
                        without the network, a repository's list of releases, and every folder apps are found in;
  tests/helper_check.cpp the install helper's whole conversation (helper/self_update_helper): an app installed
                        beside Solanum, in a folder chosen for it on another drive, or updated in its own folder,
                        a download that isn't the one announced refused, an uninstall (its folder, its home-screen
                        entry, its data folder when asked, never the running app's), and nothing left behind.

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
                       ROOT / "src/resume.c", ROOT / "src/util.c", ROOT / "src/prefs.c", ROOT / "src/keep.c",
                       ROOT / "src/custom.c", *[ROOT / "kit" / name for name in KIT]]
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

    def test_the_store_asks_only_the_catalogs_and_the_apps_releases(self):
        catalog = (ROOT / "src/catalog.h").read_text(encoding="utf-8")
        self.assertIn('"https://raw.githubusercontent.com/Oliv3rmoon/Solanum-Catalog/main/catalog.json"', catalog)
        nightshade = (ROOT / "src/nightshade.h").read_text(encoding="utf-8")
        self.assertIn('#define NP_TITLE_ID "PPSA76526"', nightshade)
        self.assertIn('#define NP_DATA "/data/solanum"', nightshade)
        # Every address the store's own sources name: the catalogs (Solanum's, and a custom one's catalog.json on
        # raw.githubusercontent.com or the HTTPS address the player typed), GitHub's API and its release downloads.
        allowed = ("https://raw.githubusercontent.com/Oliv3rmoon/Solanum-Catalog/", "https://api.github.com/",
                   "https://github.com/")
        custom = allowed + ("https://raw.githubusercontent.com/",)
        for name in ("store.c", "store_console.c", "catalog.h", "catalog.c", "apps.c", "resume.c", "custom.c",
                     "keep.c", "prefs.c", "ui/store_ui.c", "ui/page.c", "ui/keyboard.c", "ui/settings.c",
                     "ui/overlay.c", "platform/keys.c", "main.c"):
            text = (ROOT / "src" / name).read_text(encoding="utf-8")
            for url in re.findall(r'"(https?://[^"]*)', text):
                if name == "custom.c" and url == "https://":
                    continue  # the scheme an address the player types must have
                self.assertTrue(url.startswith(custom if name in ("store.c", "custom.c") else allowed),
                                f"{name}: {url}")
        # A download is only ever an app's own GitHub release, whatever catalog listed it.
        for name in ("store.c", "apps.c"):
            text = (ROOT / "src" / name).read_text(encoding="utf-8")
            for made in re.findall(r'"https://github\.com/[^"]*"', text):
                self.assertTrue(made.startswith(('"https://github.com/%s/releases/download/',
                                                 '"https://github.com/%s/releases/tag/')), f"{name}: {made}")

    def test_the_helper_and_the_catalog_keep_the_same_folders(self):
        """An uninstall removes an app's data folder only when the catalog and the helper both allow it: the same
        folders of /data are the console's, the HEN's, ShadowMountPlus's or Solanum's in both."""
        def kept(path, start):
            text = (ROOT / path).read_text(encoding="utf-8")
            block = text[text.index(start):]
            block = block[block.index("{") + 1:block.index("}")]
            return sorted(re.findall(r'"([^"]+)"', block))
        helper = kept("helper/self_update_helper/updater.cpp", "static const char *const kept[]")
        catalog = kept("src/catalog.c", "static const char *const kept[]")
        self.assertEqual(helper, catalog)
        self.assertIn("homebrew", helper)
        self.assertIn("etahen", helper)
        self.assertIn("solanum", helper)


if __name__ == "__main__":
    unittest.main()
