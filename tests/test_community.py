#!/usr/bin/env python3
# Solanum - the community's host check.
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and runs tests/community_check.c under the address and undefined-behaviour sanitizers with warnings as
errors, then reads the QR code About and the card draw back as a phone does, with a reader of its own written from
the QR code standard (ISO/IEC 18004): its format, the mask, the codewords in their zigzag, the blocks checked by their
Reed-Solomon codes, and the segments. It must open exactly https://discord.gg/pSqgRU3YP9. Also: About names nightly and
its address in every language, the captions say both, the card comes once and its closing is kept in settings.ini,
and the screens draw what community.h makes.

CC names another compiler (default: clang)."""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SANITIZE = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
INVITE = "https://discord.gg/pSqgRU3YP9"
LANGUAGES = 10  # enum Lang

# ISO/IEC 18004, versions 1-12: codewords in all, and each level's error correction codewords a block and blocks
CODEWORDS = [26, 44, 70, 100, 134, 172, 196, 242, 292, 346, 404, 466]
ECC_PER_BLOCK = {"L": [7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24],
                 "M": [10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22],
                 "Q": [13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26],
                 "H": [17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28]}
BLOCKS = {"L": [1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4],
          "M": [1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8],
          "Q": [1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10],
          "H": [1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11]}
ALIGNMENT = {2: [6, 18], 3: [6, 22], 4: [6, 26], 5: [6, 30], 6: [6, 34], 7: [6, 22, 38], 8: [6, 24, 42],
             9: [6, 26, 46], 10: [6, 28, 50], 11: [6, 30, 54], 12: [6, 32, 58]}
LEVEL_OF_BITS = {1: "L", 0: "M", 3: "Q", 2: "H"}
MASKS = [lambda i, j: (i + j) % 2 == 0, lambda i, j: i % 2 == 0, lambda i, j: j % 3 == 0,
         lambda i, j: (i + j) % 3 == 0, lambda i, j: (i // 2 + j // 3) % 2 == 0,
         lambda i, j: (i * j) % 2 + (i * j) % 3 == 0, lambda i, j: ((i * j) % 2 + (i * j) % 3) % 2 == 0,
         lambda i, j: ((i + j) % 2 + (i * j) % 3) % 2 == 0]
ALPHANUMERIC = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:"


def format_codeword(data):
    """The 15 format bits of 5 data bits: BCH(15,5) with generator 0x537, masked with 0x5412."""
    bits = data << 10
    for shift in range(4, -1, -1):
        if bits & (1 << (shift + 10)):
            bits ^= 0x537 << shift
    return ((data << 10) | bits) ^ 0x5412


def gf_tables():
    exp, log = [0] * 512, [0] * 256
    x = 1
    for i in range(255):
        exp[i], log[x] = x, i
        x <<= 1
        if x & 0x100:
            x ^= 0x11D
    for i in range(255, 512):
        exp[i] = exp[i - 255]
    return exp, log


EXP, LOG = gf_tables()


def syndromes_zero(block, ecc):
    """Whether the block (data then error correction codewords) is a Reed-Solomon codeword: r(a^i) = 0, i < ecc."""
    for i in range(ecc):
        value = 0
        for byte in block:
            value = (EXP[LOG[value] + i] if value else 0) ^ byte
        if value:
            return False
    return True


def read_qr(rows):
    """The text of a QR code given as rows of '#' and '.', read from its modules alone."""
    n = len(rows)
    m = [[c == "#" for c in row] for row in rows]
    if any(len(row) != n for row in rows) or (n - 17) % 4 or not 21 <= n <= 65:
        raise ValueError(f"not a QR code of version 1-12: {n} modules a side")
    version = (n - 17) // 4
    # the format: both copies, the nearest of the 32 codewords
    first = [m[i][8] for i in range(6)] + [m[7][8], m[8][8], m[8][7]] + [m[8][14 - i] for i in range(9, 15)]
    second = [m[8][n - 1 - i] for i in range(8)] + [m[n - 15 + i][8] for i in range(8, 15)]
    found = None
    for bits in (first, second):
        word = sum(1 << i for i, on in enumerate(bits) if on)
        best = min(range(32), key=lambda d: bin(format_codeword(d) ^ word).count("1"))
        if bin(format_codeword(best) ^ word).count("1") <= 3:
            found = best
            break
    if found is None:
        raise ValueError("no format information")
    level, mask = LEVEL_OF_BITS[found >> 3], MASKS[found & 7]
    # the function patterns: finders with their separators and the format, timing, alignment, version, dark module
    function = [[False] * n for _ in range(n)]

    def mark(r0, c0, h, w):
        for r in range(max(r0, 0), min(r0 + h, n)):
            for c in range(max(c0, 0), min(c0 + w, n)):
                function[r][c] = True

    mark(0, 0, 9, 9)
    mark(0, n - 8, 9, 8)
    mark(n - 8, 0, 8, 9)
    mark(6, 0, 1, n)
    mark(0, 6, n, 1)
    centres = ALIGNMENT.get(version, [])
    for r in centres:
        for c in centres:
            if not ((r == 6 and c == 6) or (r == 6 and c == centres[-1]) or (r == centres[-1] and c == 6)):
                mark(r - 2, c - 2, 5, 5)
    if version >= 7:
        mark(0, n - 11, 6, 3)
        mark(n - 11, 0, 3, 6)
    function[n - 8][8] = True
    # the data, two columns at a time from the right, up then down, column 6 (the timing) skipped
    bits = []
    upward, col = True, n - 1
    while col > 0:
        if col == 6:
            col -= 1
        for r in (range(n - 1, -1, -1) if upward else range(n)):
            for c in (col, col - 1):
                if not function[r][c]:
                    bits.append(m[r][c] ^ mask(r, c))
        upward = not upward
        col -= 2
    total = CODEWORDS[version - 1]
    codewords = [int("".join("1" if b else "0" for b in bits[8 * i:8 * i + 8]), 2) for i in range(total)]
    # the blocks: data codewords interleaved, then the error correction codewords
    count, ecc = BLOCKS[level][version - 1], ECC_PER_BLOCK[level][version - 1]
    short = count - total % count
    data_lengths = [total // count - ecc + (0 if b < short else 1) for b in range(count)]
    blocks = [[] for _ in range(count)]
    at = 0
    for i in range(max(data_lengths)):
        for b in range(count):
            if i < data_lengths[b]:
                blocks[b].append(codewords[at])
                at += 1
    for _ in range(ecc):
        for b in range(count):
            blocks[b].append(codewords[at])
            at += 1
    if not all(syndromes_zero(block, ecc) for block in blocks):
        raise ValueError("a block fails its Reed-Solomon check")
    data = [byte for b in range(count) for byte in blocks[b][:data_lengths[b]]]
    stream = "".join(f"{byte:08b}" for byte in data)
    # the segments, to the terminator
    out, at = bytearray(), 0

    def take(k):
        nonlocal at
        value = int(stream[at:at + k] or "0", 2)
        at += k
        return value

    big = version >= 10
    while at + 4 <= len(stream):
        mode = take(4)
        if mode == 0:
            break
        if mode == 4:  # bytes
            out += bytes(take(8) for _ in range(take(16 if big else 8)))
        elif mode == 2:  # alphanumeric
            length = take(11 if big else 9)
            for _ in range(length // 2):
                pair = take(11)
                out += (ALPHANUMERIC[pair // 45] + ALPHANUMERIC[pair % 45]).encode()
            if length % 2:
                out += ALPHANUMERIC[take(6)].encode()
        elif mode == 1:  # numeric
            length = take(12 if big else 10)
            for _ in range(length // 3):
                out += f"{take(10):03d}".encode()
            if length % 3 == 2:
                out += f"{take(7):02d}".encode()
            elif length % 3 == 1:
                out += f"{take(4):01d}".encode()
        else:
            raise ValueError(f"unexpected mode {mode}")
    return out.decode("utf-8"), version, level


class CommunityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = os.environ.get("CC", "clang")
        if not shutil.which(cc):
            raise unittest.SkipTest(f"needs {cc}")
        cls.directory = tempfile.TemporaryDirectory()
        cls.executable = Path(cls.directory.name) / "community-check"
        sources = [ROOT / "tests/community_check.c", ROOT / "src/community.c", ROOT / "src/i18n.c",
                   ROOT / "src/config.c", ROOT / "src/util.c"]
        # the library as vendor.c builds it, its own code: without Solanum's warnings
        library = Path(cls.directory.name) / "qrcodegen.o"
        subprocess.run([cc, "-std=c11", "-O1", "-w", *SANITIZE, "-c", str(ROOT / "third_party/qrcodegen/qrcodegen.c"),
                        "-o", str(library)], check=True)
        subprocess.run([cc, "-std=c11", "-O1", *SANITIZE, "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                        *map(str, sources), str(library), "-o", str(cls.executable)], check=True)
        scratch = Path(cls.directory.name) / "data"
        scratch.mkdir()
        result = subprocess.run([str(cls.executable), str(scratch)], capture_output=True, text=True, timeout=120,
                                check=False)
        cls.output = result.stdout
        cls.returncode = result.returncode
        cls.stderr = result.stderr

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def lines(self, kind):
        return {int(m.group(1)): m.group(2) for m in re.finditer(rf"^{kind} (\d+) (.*)$", self.output, re.M)}

    def code(self, output):
        return [line[3:] for line in output.splitlines() if line.startswith("qr ")]

    def test_the_checks_pass(self):
        print(self.output.strip().splitlines()[-2])
        self.assertEqual(self.returncode, 0, self.output[-3000:] + self.stderr[-3000:])
        self.assertIn("PASS", self.output)

    def test_the_qr_code_opens_the_invitation(self):
        text, version, level = read_qr(self.code(self.output))
        print(f"About and the card: a version {version}-{level} QR code, opens {text}")
        self.assertEqual(text, INVITE)

    def test_the_reader_reads_other_codes_and_refuses_a_damaged_one(self):
        """The reader reads what it is given, at every level and over several blocks, and refuses a code with a
        module changed: a reader that answered the invitation whatever it saw would fail here."""
        for text in ("https://github.com/Oliv3rmoon/Solanum", "SOLANUM 0.4.0", "0123456789012",
                     "Solanum: a store for PS5 homebrew from an open catalog, a Nightshade app."):
            for level in "LMQH":
                run = subprocess.run([str(self.executable), "--encode", text, level], capture_output=True, text=True,
                                     check=True)
                self.assertEqual(read_qr(self.code(run.stdout))[0], text, f"{text!r} at {level}")
        rows = self.code(self.output)
        n = len(rows)
        damaged = [list(row) for row in rows]
        r, c = n - 1, n - 1  # the first data module
        damaged[r][c] = "." if damaged[r][c] == "#" else "#"
        with self.assertRaises(ValueError):
            read_qr(["".join(row) for row in damaged])

    def test_about_names_the_server_in_every_language(self):
        about = self.lines("about")
        self.assertEqual(sorted(about), list(range(LANGUAGES)))
        english = about[0].split("|")[0]
        for lang, line in about.items():
            label, name, link = line.split("|")
            self.assertEqual((name, link), ("nightly", "discord.gg/pSqgRU3YP9"), lang)
            if lang:
                self.assertNotEqual(label, english, f"language {lang}: the label isn't translated")
        for kind in ("caption", "card"):
            for lang, line in self.lines(kind).items():
                self.assertIn("nightly", line, f"{kind} {lang}")
                self.assertIn("discord.gg/pSqgRU3YP9", line, f"{kind} {lang}")

    def test_the_screens_draw_what_community_makes(self):
        """About draws community_about's texts and community_qr's code, Triangle shows that code larger, and the card
        shows it once from the main loop and keeps its closing."""
        store_ui = (ROOT / "src/ui/store_ui.c").read_text(encoding="utf-8")
        about = store_ui[store_ui.index("static void about("):]
        about = about[:about.index("\n}\n")]
        for needed in ("community_qr(community)", "community_about(community_texts)", "draw_code(community,",
                       "community_texts[0]", "community_texts[1]", "community_texts[2]", "qr_show(community, caption)",
                       "BIT(BTN_TRIANGLE)", '"Discord QR code"'):
            self.assertIn(needed, about)
        main = (ROOT / "src/main.c").read_text(encoding="utf-8")
        card = main[main.index("static void community_card("):]
        card = card[:card.index("\n}\n")]
        for needed in ("community_card_due(&app.settings", "qr_show(code, caption)", "community_caption(caption",
                       "community_card_closed(&app.settings)", "app_save_settings()"):
            self.assertIn(needed, card)
        self.assertIn("community_card(now - opened);", main)
        overlay = (ROOT / "src/ui/overlay.c").read_text(encoding="utf-8")
        shows = r"void qr_show\(const uint8_t \*code, const char \*caption\)\s*\{[^}]*memcpy\(qr, code"
        self.assertRegex(overlay, shows)


if __name__ == "__main__":
    unittest.main()
