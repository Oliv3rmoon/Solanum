/*
 * SwanStationPS5 - interface languages.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NP_I18N_H
#define NP_I18N_H

enum Lang
{
    LANG_EN,
    LANG_FR,
    LANG_PT, /* Portuguese (the console's Portugal and Brazil alike) */
    LANG_ES, /* Spanish (Spain and Latin America alike) */
    LANG_JA,
    LANG_DE,
    LANG_NL,
    LANG_TR,
    LANG_IT,
    LANG_PL,
    LANG_COUNT
};

void i18n_set(int lang);
int i18n_get(void);
/* The text in the current language; the English text itself when there is no
 * translation. Formats keep their printf conversions in the same order. */
const char *tr(const char *english) __attribute__((format_arg(1)));
/* Each language's own name, for the selector ("Français", "日本語"...). */
const char *i18n_name(int lang);
/* Every string of a language, for the font atlas (NULL-terminated walk). */
const char *i18n_string(int lang, int index);

#endif
