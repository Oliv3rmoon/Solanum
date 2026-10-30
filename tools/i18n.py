#!/usr/bin/env python3
# Solanum - its interface's words in other languages.
# SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Solanum's own words, and their translations.

    python3 tools/i18n.py strings   writes i18n/strings.json: every English text the interface shows (tr()'s, the
                                    notices', the hints', the settings' rows', the helper's and the kit's reasons)
    python3 tools/i18n.py table     writes src/i18n_table.inc from i18n/<language>.json, which tr() looks up
    python3 tools/i18n.py check     both are up to date, and every translation keeps its text's printf conversions
                                    in the same order (exit 1 otherwise)

A translation file is one JSON object, English text -> its translation. Only Solanum's own words are translated: an
app's name, summary and notes stay as their authors wrote them.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = ['fr', 'pt', 'es', 'ja', 'de', 'nl', 'tr', 'it', 'pl']  # the order of enum Lang after English
STRING = r'"((?:[^"\\\n]|\\.)*)"'
LITERALS = rf'({STRING}(?:\s*{STRING})*)'
CONVERSION = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXcsfgeEp])')


def literal(text):
    """The C literals of `text` (adjacent ones joined) as one Python string."""
    parts = re.findall(STRING, text)
    joined = ''.join(parts)
    return bytes(joined, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')


def sources():
    for path in sorted((ROOT / 'src').rglob('*.c')):
        if 'platform' in path.parts and path.name != 'keys.c':
            continue
        yield path, path.read_text(encoding='utf-8')


def strings():
    found = set()
    calls = re.compile(r'\b(?:tr|app_toast|app_toast_for|app_message_box|app_draw_hints\([^;]*?)\s*\(\s*' + LITERALS)
    for path, text in sources():
        for match in re.finditer(r'\b(?:tr|app_toast|app_toast_for|app_message_box)\s*\(\s*' + LITERALS, text):
            found.add(literal(match.group(1)))
        # the hints' labels and right-hand text, the tabs', the settings' names and rows
        arrays = r'(?:labels|later|hints|lines|TAB_NAMES|UI_SOUNDS|PLACES|CATEGORY_TITLES)'
        if path.name in ('keyboard.c', 'settings.c'):
            arrays = arrays[:-1] + r'|names)'
        for match in re.finditer(arrays + r'\[[^\]]*\]\s*=\s*\{([^;]*)\};', text):
            for item in re.finditer(LITERALS, match.group(1)):
                found.add(literal(item.group(1)))
        for match in re.finditer(r'app_draw_hints\([^;]*?,\s*' + LITERALS + r'\s*\)', text):
            found.add(literal(match.group(1)))
        if path.name == 'settings.c':
            for match in re.finditer(r'\{\s*' + STRING + r'\s*,\s*ICON_', text):  # its tabs
                found.add(literal('"' + match.group(1) + '"'))
            for match in re.finditer(r'\{\s*' + LITERALS + r'\s*,\s*(?:' + LITERALS + r'|NULL)\s*,\s*K_', text):
                found.add(literal(match.group(1)))
                if match.group(4):
                    found.add(literal(match.group(4)))
            for match in re.finditer(r'\(Row\)\s*\{\s*' + LITERALS + r'\s*,\s*(?:' + LITERALS + r'|NULL)', text):
                found.add(literal(match.group(1)))
                if match.group(4):
                    found.add(literal(match.group(4)))
        if path.name in ('store.c',):
            body = text[text.index('const char *store_problem'):]
            body = body[:body.index('\n}\n')]
            for item in re.finditer(r'return\s+' + LITERALS, body):
                found.add(literal(item.group(1)))
            for item in re.finditer(r'str_copy\(reason, size,\s*' + LITERALS, text):
                found.add(literal(item.group(1)))
            for item in re.finditer(r'remove_status\(slot, JOB_FAILED,\s*' + LITERALS, text):
                found.add(literal(item.group(1)))
        if path.name == 'custom.c':
            for item in re.finditer(r'snprintf\(why, size, "%s",\s*' + LITERALS, text):
                found.add(literal(item.group(1)))
    # The install helper's and the kit's reasons, shown as a failed job's error.
    helper = (ROOT / 'helper/self_update_helper/updater.cpp').read_text(encoding='utf-8')
    for match in re.finditer(r'(?:refuse|fail)\(\s*"([^"\\]*)"\s*\)', helper):
        found.add(match.group(1))
    for match in re.finditer(r'say\("fail ([^"\\]*)\\n"\)', helper):
        found.add(match.group(1))
    kit = (ROOT / 'kit/self_update.c').read_text(encoding='utf-8')
    for match in re.finditer(r'stop\(job,\s*(?:[^;]*\?\s*)?"([^"\\]+)"', kit):
        found.add(match.group(1))
    found.add("The console's")  # the first of Settings' languages; the others are each language's own name
    found = {s for s in found if re.search(r'[A-Za-z]{2}', s) and not s.startswith(('http', '/', '%s/'))}
    return sorted(found)


def conversions(text):
    return [c for c in CONVERSION.findall(text) if c != '%%']


def c_string(text):
    out = []
    for ch in text:
        if ch in '\\"':
            out.append('\\' + ch)
        elif ch == '\n':
            out.append('\\n')
        elif ord(ch) < 0x20:
            out.append('\\x%02x' % ord(ch))
        else:
            out.append(ch)
    # a hex escape followed by a hex digit would run on: split the literal there
    return '"' + ''.join(out) + '"'


def table(english):
    lines = ['/* Solanum - the interface\'s translations, written by tools/i18n.py table from i18n/<language>.json. */',
             '/* SPDX-License-Identifier: GPL-3.0-or-later */',
             f'#define I18N_STRINGS {len(english)}',
             'static const char *const I18N_ENGLISH[I18N_STRINGS] = {']
    lines += [f'    {c_string(s)},' for s in english]
    lines.append('};')
    lines.append('static const char *const I18N_TRANSLATED[LANG_COUNT - 1][I18N_STRINGS] = {')
    for language in LANGUAGES:
        path = ROOT / 'i18n' / f'{language}.json'
        words = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
        lines.append(f'    /* {language} */ {{')
        for s in english:
            lines.append(f'        {c_string(words[s]) if words.get(s) else "NULL"},')
        lines.append('    },')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def check(english):
    problems = []
    stored = json.loads((ROOT / 'i18n/strings.json').read_text(encoding='utf-8'))
    if stored != english:
        problems.append('i18n/strings.json is not up to date: run tools/i18n.py strings')
    for language in LANGUAGES:
        words = json.loads((ROOT / 'i18n' / f'{language}.json').read_text(encoding='utf-8'))
        missing = [s for s in english if not words.get(s)]
        if missing:
            problems.append(f'{language}: {len(missing)} not translated, e.g. {missing[0]!r}')
        for s, t in words.items():
            if s in english and t and conversions(s) != conversions(t):
                problems.append(f'{language}: {s!r} -> {t!r}: its conversions differ')
    if (ROOT / 'src/i18n_table.inc').read_text(encoding='utf-8') != table(english):
        problems.append('src/i18n_table.inc is not up to date: run tools/i18n.py table')
    return problems


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else 'check'
    english = strings()
    if what == 'strings':
        (ROOT / 'i18n/strings.json').write_text(json.dumps(english, indent=1, ensure_ascii=False) + '\n',
                                                encoding='utf-8')
        print(f'{len(english)} strings')
    elif what == 'table':
        (ROOT / 'src/i18n_table.inc').write_text(table(english), encoding='utf-8')
        print(f'{len(english)} strings, {len(LANGUAGES)} languages')
    else:
        problems = check(english)
        for problem in problems:
            print(f'FAIL {problem}')
        print('PASS' if not problems else f'{len(problems)} problems')
        sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
