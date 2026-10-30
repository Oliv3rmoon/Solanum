/*
 * Solanum - the kept ZIPs of an app's current and previous versions (keep.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _DEFAULT_SOURCE 1 /* glibc: lstat under -std=c11 */
#include "keep.h"

#include "catalog.h"
#include "nightshade.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const SLOTS[2] = {"current", "previous"};

void keep_paths(const char *cache, const char *title, int slot, char *zip, size_t zip_size, char *record,
                size_t record_size)
{
    const char *name = SLOTS[slot == KEEP_PREVIOUS ? 1 : 0];
    if (zip)
        snprintf(zip, zip_size, "%s/keep/%s/%s.zip", cache, title, name);
    if (record)
        snprintf(record, record_size, "%s/keep/%s/%s.ini", cache, title, name);
}

static uint64_t file_size(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) ? (uint64_t)info.st_size : 0;
}

static bool hex64(const char *text)
{
    if (strlen(text) != 64)
        return false;
    for (const char *c = text; *c; ++c)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f')))
            return false;
    return true;
}

bool keep_read(const char *cache, const char *title, int slot, KeptZip *out)
{
    char zip[NP_PATH_MAX], record[NP_PATH_MAX], line[640];
    memset(out, 0, sizeof(*out));
    if (!catalog_title_id_valid(title))
        return false;
    keep_paths(cache, title, slot, zip, sizeof(zip), record, sizeof(record));
    FILE *f = fopen(record, "r");
    if (!f)
        return false;
    while (fgets(line, sizeof(line), f))
    {
        char key[16], value[600];
        if (sscanf(line, " %15[a-z0-9] = %599[^\r\n]", key, value) != 2)
            continue;
        if (strcmp(key, "tag") == 0)
            str_copy(out->tag, sizeof(out->tag), value);
        else if (strcmp(key, "version") == 0)
            str_copy(out->version, sizeof(out->version), value);
        else if (strcmp(key, "sha256") == 0)
            str_copy(out->sha256, sizeof(out->sha256), value);
        else if (strcmp(key, "size") == 0)
            out->size = strtoull(value, NULL, 10);
        else if (strcmp(key, "url") == 0)
            str_copy(out->url, sizeof(out->url), value);
        else if (strcmp(key, "name") == 0)
            str_copy(out->name, sizeof(out->name), value);
    }
    fclose(f);
    /* A record is only as good as its ZIP: there, and the size it says. */
    if (!out->tag[0] || !hex64(out->sha256) || !out->size || strncmp(out->url, "https://github.com/", 19) != 0 ||
        file_size(zip) != out->size)
    {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

static bool write_record(const char *path, const KeptZip *what)
{
    char temp[NP_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.new", path);
    FILE *f = fopen(temp, "w");
    if (!f)
        return false;
    fprintf(f, "tag = %s\nversion = %s\nsha256 = %s\nsize = %llu\nurl = %s\nname = %s\n", what->tag, what->version,
            what->sha256, (unsigned long long)what->size, what->url, what->name);
    if (fclose(f) != 0)
    {
        remove(temp);
        return false;
    }
    remove(path);
    return rename(temp, path) == 0;
}

bool keep_store(const char *cache, const char *title, const char *zip, const KeptZip *what)
{
    char folder[NP_PATH_MAX], cur_zip[NP_PATH_MAX], cur_ini[NP_PATH_MAX], prev_zip[NP_PATH_MAX],
        prev_ini[NP_PATH_MAX];
    if (!catalog_title_id_valid(title) || file_size(zip) == 0)
    {
        remove(zip);
        return false;
    }
    snprintf(folder, sizeof(folder), "%s/keep/%s", cache, title);
    keep_paths(cache, title, KEEP_CURRENT, cur_zip, sizeof(cur_zip), cur_ini, sizeof(cur_ini));
    keep_paths(cache, title, KEEP_PREVIOUS, prev_zip, sizeof(prev_zip), prev_ini, sizeof(prev_ini));
    if (!make_dirs(folder))
    {
        remove(zip);
        return false;
    }
    KeptZip current;
    if (keep_read(cache, title, KEEP_CURRENT, &current) && strcmp(current.sha256, what->sha256) != 0)
    {
        /* the one installed until now becomes the one before */
        remove(prev_zip);
        remove(prev_ini);
        if (rename(cur_zip, prev_zip) != 0 || rename(cur_ini, prev_ini) != 0)
        {
            remove(prev_zip);
            remove(prev_ini);
        }
    }
    remove(cur_zip);
    remove(cur_ini);
    if (rename(zip, cur_zip) != 0)
    {
        remove(zip);
        return false;
    }
    if (!write_record(cur_ini, what))
    {
        remove(cur_zip);
        return false;
    }
    return true;
}

bool keep_swap(const char *cache, const char *title)
{
    char cur_zip[NP_PATH_MAX], cur_ini[NP_PATH_MAX], prev_zip[NP_PATH_MAX], prev_ini[NP_PATH_MAX],
        temp_zip[NP_PATH_MAX + 8], temp_ini[NP_PATH_MAX + 8];
    if (!catalog_title_id_valid(title))
        return false;
    keep_paths(cache, title, KEEP_CURRENT, cur_zip, sizeof(cur_zip), cur_ini, sizeof(cur_ini));
    keep_paths(cache, title, KEEP_PREVIOUS, prev_zip, sizeof(prev_zip), prev_ini, sizeof(prev_ini));
    snprintf(temp_zip, sizeof(temp_zip), "%s.swap", cur_zip);
    snprintf(temp_ini, sizeof(temp_ini), "%s.swap", cur_ini);
    remove(temp_zip);
    remove(temp_ini);
    const bool had_current = file_size(cur_zip) > 0;
    if (had_current && (rename(cur_zip, temp_zip) != 0 || rename(cur_ini, temp_ini) != 0))
        return false;
    if (rename(prev_zip, cur_zip) != 0 || rename(prev_ini, cur_ini) != 0)
        return false;
    if (had_current && (rename(temp_zip, prev_zip) != 0 || rename(temp_ini, prev_ini) != 0))
        return false;
    return true;
}

void keep_forget(const char *cache, const char *title)
{
    char path[NP_PATH_MAX];
    if (!catalog_title_id_valid(title))
        return;
    for (int slot = 0; slot < 2; ++slot)
    {
        char zip[NP_PATH_MAX], record[NP_PATH_MAX];
        keep_paths(cache, title, slot, zip, sizeof(zip), record, sizeof(record));
        remove(zip);
        remove(record);
    }
    snprintf(path, sizeof(path), "%s/keep/%s", cache, title);
    rmdir(path);
}

/* The folders of keep/: one an app. */
static void each_title(const char *cache, void (*visit)(const char *cache, const char *title, void *user), void *user)
{
    char path[NP_PATH_MAX];
    snprintf(path, sizeof(path), "%s/keep", cache);
    static char titles[512][10];
    int count = 0;
    DIR *directory = opendir(path);
    struct dirent *entry;
    if (!directory)
        return;
    /* read first, visited after: a visit may remove the folder */
    while ((entry = readdir(directory)) != NULL && count < 512)
        if (catalog_title_id_valid(entry->d_name))
            memcpy(titles[count++], entry->d_name, 10);
    closedir(directory);
    for (int i = 0; i < count; ++i)
        visit(cache, titles[i], user);
}

static void add_size(const char *cache, const char *title, void *user)
{
    for (int slot = 0; slot < 2; ++slot)
    {
        char zip[NP_PATH_MAX];
        keep_paths(cache, title, slot, zip, sizeof(zip), NULL, 0);
        *(uint64_t *)user += file_size(zip);
    }
}

static void forget(const char *cache, const char *title, void *user)
{
    (void)user;
    keep_forget(cache, title);
}

uint64_t keep_size(const char *cache)
{
    uint64_t bytes = 0;
    each_title(cache, add_size, &bytes);
    return bytes;
}

void keep_clear(const char *cache)
{
    each_title(cache, forget, NULL);
}
