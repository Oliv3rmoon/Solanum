#!/usr/bin/env python3
# Solanum - its translations.
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/i18n.py check: i18n/strings.json holds every English text the interface shows, every language translates
all of them keeping their printf conversions in the same order, and src/i18n_table.inc is written from them. And the
fonts draw them: Inter carries Latin Extended-A (Turkish, Polish), the Noto Sans JP subset every character the
Japanese uses."""

import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def cmap(path):
    """A TrueType font's characters, read without a library: the format 4 and 12 subtables of its cmap."""
    import struct
    data = path.read_bytes()
    tables = {data[12 + 16 * i:16 + 16 * i].decode('latin-1'): struct.unpack('>II', data[20 + 16 * i:28 + 16 * i])
              for i in range(struct.unpack('>H', data[4:6])[0])}
    start = tables['cmap'][0]
    codes = set()
    for i in range(struct.unpack('>H', data[start + 2:start + 4])[0]):
        offset = start + struct.unpack('>I', data[start + 8 + 8 * i:start + 12 + 8 * i])[0]
        kind = struct.unpack('>H', data[offset:offset + 2])[0]
        if kind == 4:
            count = struct.unpack('>H', data[offset + 6:offset + 8])[0] // 2
            ends = struct.unpack(f'>{count}H', data[offset + 14:offset + 14 + 2 * count])
            starts = struct.unpack(f'>{count}H', data[offset + 16 + 2 * count:offset + 16 + 4 * count])
            for a, b in zip(starts, ends):
                codes.update(range(a, b + 1))
        elif kind == 12:
            groups = struct.unpack('>I', data[offset + 12:offset + 16])[0]
            for g in range(groups):
                a, b, _ = struct.unpack('>III', data[offset + 16 + 12 * g:offset + 28 + 12 * g])
                codes.update(range(a, b + 1))
    return codes


class I18nTest(unittest.TestCase):
    def test_translations_are_whole_and_up_to_date(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/i18n.py'), 'check'], capture_output=True,
                                text=True, check=False)
        print(result.stdout.strip())
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_the_fonts_draw_every_language(self):
        inter = cmap(ROOT / 'assets/fonts/Inter-400.ttf') & cmap(ROOT / 'assets/fonts/Inter-600.ttf')
        japanese = cmap(ROOT / 'assets/fonts/NotoSansJP-subset.ttf')
        for language in ('fr', 'pt', 'es', 'de', 'nl', 'tr', 'it', 'pl', 'ja'):
            words = json.loads((ROOT / 'i18n' / f'{language}.json').read_text(encoding='utf-8'))
            used = {ord(c) for text in words.values() for c in text}
            # Latin from Inter (U+0020..U+017F, the atlas's range), the rest from the Japanese face
            missing = sorted(c for c in used if c >= 0x20 and not ((c < 0x180 and c in inter) or c in japanese))
            self.assertFalse(missing, f'{language}: no glyph for {[chr(c) for c in missing][:10]}')


if __name__ == '__main__':
    unittest.main()
