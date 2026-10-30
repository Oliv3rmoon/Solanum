/*
 * Solanum - what the player decided for each app (prefs.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "prefs.h"

#include "catalog.h"
#include "nightshade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AppPrefs entries[PREFS_MAX];
static int count;
static char file[NP_PATH_MAX];

int prefs_count(void)
{
    return count;
}

AppPrefs *prefs_get(const char *title, bool create)
{
    if (!title || !catalog_title_id_valid(title))
        return NULL;
    for (int i = 0; i < count; ++i)
        if (strcmp(entries[i].title, title) == 0)
            return &entries[i];
    if (!create || count == PREFS_MAX)
        return NULL;
    AppPrefs *made = &entries[count++];
    memset(made, 0, sizeof(*made));
    memcpy(made->title, title, 10);
    return made;
}

void prefs_forget_install(const char *title)
{
    AppPrefs *prefs = prefs_get(title, false);
    if (prefs)
        prefs->by = BY_UNKNOWN;
}

/* A tag as a release's can be: letters, digits, '.', '_' and '-'. */
static bool tag_text(const char *text)
{
    if (!text[0] || strlen(text) >= 40)
        return false;
    for (const char *c = text; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' ||
              *c == '_' || *c == '-'))
            return false;
    return true;
}

void prefs_load(const char *path)
{
    char line[256];
    count = 0;
    str_copy(file, sizeof(file), path);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f))
    {
        char title[16], key[16], value[64];
        if (line[0] == '#' || sscanf(line, " %15[A-Z0-9].%15[a-z] = %63[^\r\n]", title, key, value) != 3)
            continue;
        AppPrefs *prefs = prefs_get(title, true);
        if (!prefs)
            continue;
        if (strcmp(key, "hold") == 0)
            prefs->hold = atoi(value) != 0;
        else if (strcmp(key, "skip") == 0 && tag_text(value))
            str_copy(prefs->skip, sizeof(prefs->skip), value);
        else if (strcmp(key, "by") == 0)
            prefs->by = strcmp(value, "solanum") == 0 ? BY_SOLANUM : strcmp(value, "hand") == 0 ? BY_HAND : BY_UNKNOWN;
    }
    fclose(f);
}

bool prefs_save(void)
{
    char temp[NP_PATH_MAX + 8];
    if (!file[0])
        return false;
    snprintf(temp, sizeof(temp), "%s.new", file);
    FILE *f = fopen(temp, "w");
    if (!f)
        return false;
    fprintf(f, "# Solanum %s: what you decided for each app, and how it was installed\n", NP_VERSION);
    for (int i = 0; i < count; ++i)
    {
        const AppPrefs *p = &entries[i];
        if (p->hold)
            fprintf(f, "%s.hold = 1\n", p->title);
        if (p->skip[0])
            fprintf(f, "%s.skip = %s\n", p->title, p->skip);
        if (p->by != BY_UNKNOWN)
            fprintf(f, "%s.by = %s\n", p->title, p->by == BY_SOLANUM ? "solanum" : "hand");
    }
    const bool ok = fclose(f) == 0;
    if (ok)
    {
        remove(file);
        rename(temp, file);
    }
    else
        remove(temp);
    return ok;
}
