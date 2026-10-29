/*
 * Solanum - the catalog (catalog.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "catalog.h"

#include "github_release.h"
#include "update_check.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const CATEGORY_NAMES[CATEGORY_COUNT] = {"emulator", "media", "streaming", "game", "tool",
                                                           "store"};
static const char *const CATEGORY_TITLES[CATEGORY_COUNT] = {"Emulators", "Media", "Streaming", "Games", "Tools",
                                                            "Stores"};

int catalog_category(const char *text)
{
    for (int i = 0; i < CATEGORY_COUNT; ++i)
        if (text && strcmp(text, CATEGORY_NAMES[i]) == 0)
            return i;
    return -1;
}

const char *catalog_category_title(int category)
{
    return category >= 0 && category < CATEGORY_COUNT ? CATEGORY_TITLES[category] : "Other";
}

int catalog_title_id_valid(const char *text)
{
    if (text == NULL || strlen(text) != 9)
        return 0;
    for (int i = 0; i < 9; ++i)
        if (i < 4 ? !(text[i] >= 'A' && text[i] <= 'Z') : !(text[i] >= '0' && text[i] <= '9'))
            return 0;
    return 1;
}

static void say(catalog_log_fn log, void *user, const char *format, ...) __attribute__((format(printf, 3, 4)));
static void say(catalog_log_fn log, void *user, const char *format, ...)
{
    char line[512];
    va_list args;
    if (log == NULL)
        return;
    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log(user, line);
}

/* An id: lowercase letters, digits and '-', not empty. */
static int id_valid(const char *text)
{
    if (text[0] == '\0')
        return 0;
    for (const char *c = text; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-'))
            return 0;
    return 1;
}

/* A file or folder name: letters, digits, '.', '_' and '-', not starting with '.'. */
static int name_valid(const char *text)
{
    if (text[0] == '\0' || text[0] == '.')
        return 0;
    for (const char *c = text; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' ||
              *c == '_' || *c == '-'))
            return 0;
    return 1;
}

/* Text worth showing: something besides spaces. */
static int has_text(const char *text)
{
    for (const char *c = text; *c; ++c)
        if (*c != ' ' && *c != '\t')
            return 1;
    return 0;
}

/* A string member of an app's object: 1 copied; otherwise *why says what is wrong with it. */
static int read_text(const char *json, size_t length, const char *key, char *out, size_t size, const char **why)
{
    const int found = update_check_json_string(json, length, key, out, size);
    if (found == 1)
        return 1;
    *why = found == 0 ? "missing" : found == 2 ? "null" : "not text, or too long";
    return 0;
}

/* A boolean member: 1 true, 0 false, -1 neither (absent counts as `absent`). */
static int read_bool(const char *json, size_t length, const char *key, int absent)
{
    const char *value = NULL;
    size_t size = 0;
    const int found = update_check_json_value(json, length, key, &value, &size);
    if (found == 0)
        return absent;
    if (found != 1)
        return -1;
    if (size == 4 && memcmp(value, "true", 4) == 0)
        return 1;
    if (size == 5 && memcmp(value, "false", 5) == 0)
        return 0;
    return -1;
}

/* One element of an array, when it is a string: the element alone read as "[element]". 1 copied. */
static int element_text(const char *value, size_t length, char *out, size_t size)
{
    int found;
    char *one;
    if (length == 0 || value[0] != '"')
        return 0;
    one = (char *)malloc(length + 3);
    if (one == NULL)
        return 0;
    one[0] = '[';
    memcpy(one + 1, value, length);
    one[length + 1] = ']';
    one[length + 2] = '\0';
    found = update_check_json_array_string(one, length + 2, 0, out, size);
    free(one);
    return found == 1;
}

/* tips, eggs: the strings of an array; any other element is left out with a log line. */
static int read_lines(const char *json, size_t length, const char *key, char lines[][CATALOG_LINE], int most,
                      catalog_log_fn log, void *user)
{
    const char *array = NULL;
    size_t array_length = 0;
    size_t cursor = 0;
    int count = 0;
    int index = 0;
    const int found = update_check_json_value(json, length, key, &array, &array_length);
    if (found == 0)
        return 0;
    if (found != 1 || array_length == 0 || array[0] != '[')
    {
        say(log, user, "catalog: %s is not a list; none are shown", key);
        return 0;
    }
    for (;; ++index)
    {
        const char *item = NULL;
        size_t item_length = 0;
        const int next = update_check_json_array_next(array, array_length, &cursor, &item, &item_length);
        if (next == 0)
            break;
        if (next < 0)
        {
            say(log, user, "catalog: %s could not be read past entry %d", key, index);
            break;
        }
        if (count == most)
        {
            say(log, user, "catalog: %s has more than %d entries; the rest are left out", key, most);
            break;
        }
        if (!element_text(item, item_length, lines[count], CATALOG_LINE) || !has_text(lines[count]))
        {
            say(log, user, "catalog: %s entry %d left out: not text, empty or too long", key, index);
            continue;
        }
        ++count;
    }
    return count;
}

/* One entry of apps into *app. 1 kept; 0 left out (with the reason in the log). */
static int read_app(const char *json, size_t length, int index, const Catalog *kept, CatalogApp *app,
                    catalog_log_fn log, void *user)
{
    static const struct
    {
        const char *key;
        size_t offset, size;
    } TEXTS[] = {
        {"id", offsetof(CatalogApp, id), sizeof(((CatalogApp *)0)->id)},
        {"name", offsetof(CatalogApp, name), sizeof(((CatalogApp *)0)->name)},
        {"summary", offsetof(CatalogApp, summary), sizeof(((CatalogApp *)0)->summary)},
        {"title_id", offsetof(CatalogApp, title_id), sizeof(((CatalogApp *)0)->title_id)},
        {"repo", offsetof(CatalogApp, repo), sizeof(((CatalogApp *)0)->repo)},
        {"asset", offsetof(CatalogApp, asset), sizeof(((CatalogApp *)0)->asset)},
        {"folder", offsetof(CatalogApp, folder), sizeof(((CatalogApp *)0)->folder)},
        {"checksums", offsetof(CatalogApp, checksums), sizeof(((CatalogApp *)0)->checksums)},
        {"license", offsetof(CatalogApp, license), sizeof(((CatalogApp *)0)->license)},
    };
    char label[48];
    char category[24];
    char example[160];
    const char *why = NULL;
    const char *value = NULL;
    size_t value_length = 0;
    int flag;
    memset(app, 0, sizeof(*app));
    /* One whole object (the reader checks all of it, whichever member is asked for). */
    if (length == 0 || json[0] != '{' || update_check_json_value(json, length, "id", &value, &value_length) < 0)
    {
        say(log, user, "catalog: app %d left out: not an object that can be read", index);
        return 0;
    }
    /* Its name in the log: its id when it has one. */
    if (update_check_json_string(json, length, "id", label, sizeof(label)) != 1)
        (void)snprintf(label, sizeof(label), "#%d", index);
    for (size_t i = 0; i < sizeof(TEXTS) / sizeof(TEXTS[0]); ++i)
        if (!read_text(json, length, TEXTS[i].key, (char *)app + TEXTS[i].offset, TEXTS[i].size, &why))
        {
            say(log, user, "catalog: app %d (%s) left out: %s is %s", index, label, TEXTS[i].key, why);
            return 0;
        }
    if (!read_text(json, length, "category", category, sizeof(category), &why))
    {
        say(log, user, "catalog: app %d (%s) left out: category is %s", index, label, why);
        return 0;
    }
    if ((app->category = catalog_category(category)) < 0)
    {
        say(log, user, "catalog: app %d (%s) left out: category \"%s\" is not one Solanum knows", index, label,
            category);
        return 0;
    }
    if ((flag = read_bool(json, length, "attested", -1)) < 0)
    {
        say(log, user, "catalog: app %d (%s) left out: attested is not true or false", index, label);
        return 0;
    }
    app->attested = flag == 1;
    if ((flag = read_bool(json, length, "family", 0)) < 0)
    {
        say(log, user, "catalog: app %d (%s) left out: family is not true or false", index, label);
        return 0;
    }
    app->family = flag == 1;
    if (!id_valid(app->id))
        why = "id is not lowercase letters, digits and '-'";
    else if (!has_text(app->name))
        why = "it has no name";
    else if (!catalog_title_id_valid(app->title_id))
        why = "title_id is not four letters and five digits";
    else if (!github_release_repository_valid(app->repo))
        why = "repo is not owner/name";
    else if (!github_release_asset_name(app->asset, "v1.0.0", example, sizeof(example)))
        why = "asset can't name a release's file";
    else if (!name_valid(app->folder) || strcmp(app->folder, "..") == 0)
        why = "folder can't be a folder's name";
    else if (!name_valid(app->checksums))
        why = "checksums can't name a release's file";
    else if (!has_text(app->license))
        why = "it has no license";
    if (why == NULL)
        for (int i = 0; i < kept->app_count && why == NULL; ++i)
        {
            if (strcmp(kept->apps[i].id, app->id) == 0)
                why = "its id is listed already";
            else if (strcmp(kept->apps[i].title_id, app->title_id) == 0)
                why = "its title ID belongs to an app listed already";
        }
    if (why != NULL)
    {
        say(log, user, "catalog: app %d (%s) left out: %s", index, label, why);
        return 0;
    }
    return 1;
}

int catalog_parse(const char *json, size_t length, Catalog *out, catalog_log_fn log, void *user)
{
    const char *apps = NULL;
    size_t apps_length = 0;
    size_t cursor = 0;
    unsigned schema = 0;
    int index = 0;
    memset(out, 0, sizeof(*out));
    if (json == NULL || update_check_json_uint(json, length, "schema", &schema) != 1 || schema != CATALOG_SCHEMA)
    {
        say(log, user, "catalog: not a catalog of schema %d", CATALOG_SCHEMA);
        return -1;
    }
    if (update_check_json_value(json, length, "apps", &apps, &apps_length) != 1 || apps_length == 0 ||
        apps[0] != '[')
    {
        say(log, user, "catalog: it has no list of apps");
        return -1;
    }
    if (update_check_json_string(json, length, "name", out->name, sizeof(out->name)) != 1)
        out->name[0] = '\0';
    if (update_check_json_string(json, length, "about", out->about, sizeof(out->about)) != 1)
        out->about[0] = '\0';
    if (update_check_json_string(json, length, "about_footer", out->about_footer, sizeof(out->about_footer)) != 1)
        out->about_footer[0] = '\0';
    for (;; ++index)
    {
        const char *item = NULL;
        size_t item_length = 0;
        const int next = update_check_json_array_next(apps, apps_length, &cursor, &item, &item_length);
        if (next == 0)
            break;
        if (next < 0)
        {
            say(log, user, "catalog: the list of apps could not be read past app %d", index);
            break;
        }
        if (out->app_count == CATALOG_MAX_APPS)
        {
            say(log, user, "catalog: more than %d apps; the rest are left out", CATALOG_MAX_APPS);
            break;
        }
        if (read_app(item, item_length, index, out, &out->apps[out->app_count], log, user))
            ++out->app_count;
    }
    out->tip_count = read_lines(json, length, "tips", out->tips, CATALOG_MAX_LINES, log, user);
    out->egg_count = read_lines(json, length, "eggs", out->eggs, CATALOG_MAX_LINES, log, user);
    return out->app_count;
}
