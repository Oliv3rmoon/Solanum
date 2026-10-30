/*
 * Solanum - interface languages: English, French, Portuguese, Spanish, Japanese, German, Dutch, Turkish, Italian and
 * Polish. tr() looks Solanum's own words up in the table tools/i18n.py writes from i18n/<language>.json
 * (i18n_table.inc), and hands the English back for anything it has no translation of: an app's name, summary and
 * notes stay as their authors wrote them. The API is SwanStationPS5's.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "i18n.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "i18n_table.inc"

static int current = LANG_EN;

/* Each language in its own words, for Settings and for the font atlas (日本語). */
static const char *const NAMES[LANG_COUNT] = {"English", "Français", "Português", "Español", "日本語",
                                              "Deutsch", "Nederlands", "Türkçe",   "Italiano", "Polski"};

static int compare(const void *key, const void *element)
{
    return strcmp((const char *)key, *(const char *const *)element);
}

const char *tr(const char *english)
{
    if (current == LANG_EN || english == NULL || english[0] == '\0')
        return english;
    const char *const *hit =
        (const char *const *)bsearch(english, I18N_ENGLISH, I18N_STRINGS, sizeof(I18N_ENGLISH[0]), compare);
    if (hit == NULL)
        return english;
    const char *translated = I18N_TRANSLATED[current - 1][hit - I18N_ENGLISH];
    return translated ? translated : english;
}

void i18n_set(int lang)
{
    current = lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN;
}

int i18n_get(void)
{
    return current;
}

const char *i18n_name(int lang)
{
    return lang >= 0 && lang < LANG_COUNT ? NAMES[lang] : NAMES[LANG_EN];
}

const char *i18n_string(int lang, int index)
{
    /* the languages' own names first (the selector shows them all), then the language's words */
    if (index < 0)
        return NULL;
    if (index < LANG_COUNT)
        return NAMES[index];
    if (lang <= LANG_EN || lang >= LANG_COUNT)
        return NULL;
    for (int i = index - LANG_COUNT; i < I18N_STRINGS; ++i)
        if (I18N_TRANSLATED[lang - 1][i])
            return I18N_TRANSLATED[lang - 1][i];
    return NULL;
}
