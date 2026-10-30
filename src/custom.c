/*
 * Solanum - custom catalogs (custom.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "custom.h"

#include "github_release.h"
#include "nightshade.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char texts[CUSTOM_MAX][CUSTOM_TEXT];
static int count;
static char file[NP_PATH_MAX];

int custom_count(void)
{
    return count;
}

const char *custom_text(int index)
{
    return index >= 0 && index < count ? texts[index] : "";
}

/* A host as an address may have it: letters, digits, '-' and '.', a dot inside, at most 253 long. */
static bool host_valid(const char *host, size_t length)
{
    bool dot = false;
    if (length == 0 || length > 253 || host[0] == '.' || host[0] == '-' || host[length - 1] == '.')
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        const char c = host[i];
        if (c == '.')
            dot = true;
        else if (!isalnum((unsigned char)c) && c != '-')
            return false;
    }
    return dot;
}

bool custom_normalise(const char *text, char *out, size_t size)
{
    char trimmed[CUSTOM_TEXT];
    size_t length;
    out[0] = '\0';
    if (!text)
        return false;
    while (*text == ' ')
        ++text;
    str_copy(trimmed, sizeof(trimmed), text);
    length = strlen(trimmed);
    while (length > 0 && (trimmed[length - 1] == ' ' || trimmed[length - 1] == '/'))
        trimmed[--length] = '\0';
    if (length == 0 || length >= CUSTOM_TEXT - 1)
        return false;
    /* https://github.com/owner/name is the repository */
    static const char github[] = "https://github.com/";
    const char *repo = strncmp(trimmed, github, sizeof(github) - 1) == 0 ? trimmed + sizeof(github) - 1 : trimmed;
    if (github_release_repository_valid(repo))
    {
        if (strlen(repo) >= size)
            return false;
        str_copy(out, size, repo);
        return true;
    }
    if (strncmp(trimmed, "https://", 8) != 0 || repo != trimmed)
        return false;
    /* Any other HTTPS address: a host, then a path of printable characters that need no quoting. */
    const char *host = trimmed + 8;
    const char *slash = strchr(host, '/');
    const size_t host_length = slash ? (size_t)(slash - host) : strlen(host);
    if (!host_valid(host, host_length))
        return false;
    for (const char *c = trimmed; *c; ++c)
        if ((unsigned char)*c <= 0x20 || (unsigned char)*c >= 0x7f || *c == '"' || *c == '\'' || *c == '\\' ||
            *c == '<' || *c == '>' || *c == '`' || *c == '{' || *c == '}' || *c == '|' || *c == '^' || *c == '@')
            return false;
    if (length >= size)
        return false;
    str_copy(out, size, trimmed);
    return true;
}

void custom_url(int index, char *out, size_t size)
{
    const char *text = custom_text(index);
    if (strncmp(text, "https://", 8) == 0)
        snprintf(out, size, "%s", text);
    else
        snprintf(out, size, "https://raw.githubusercontent.com/%s/HEAD/catalog.json", text);
}

void custom_name(int index, char *out, size_t size)
{
    const char *text = custom_text(index);
    if (strncmp(text, "https://", 8) != 0)
    {
        snprintf(out, size, "%s", text);
        return;
    }
    const char *host = text + 8;
    const char *slash = strchr(host, '/');
    snprintf(out, size, "%.*s", (int)(slash ? (size_t)(slash - host) : strlen(host)), host);
}

bool custom_add(const char *text, char *why, size_t size)
{
    char normal[CUSTOM_TEXT];
    if (!custom_normalise(text, normal, sizeof(normal)))
    {
        snprintf(why, size, "%s", "That isn't a GitHub repository (owner/name) or an HTTPS address.");
        return false;
    }
    if (strcmp(normal, "Oliv3rmoon/Solanum-Catalog") == 0)
    {
        snprintf(why, size, "%s", "That's Solanum's own catalog: it is always there.");
        return false;
    }
    for (int i = 0; i < count; ++i)
        if (strcmp(texts[i], normal) == 0)
        {
            snprintf(why, size, "%s", "That catalog is listed already.");
            return false;
        }
    if (count == CUSTOM_MAX)
    {
        snprintf(why, size, "%s", "Eight catalogs are listed already. Remove one first.");
        return false;
    }
    str_copy(texts[count++], CUSTOM_TEXT, normal);
    why[0] = '\0';
    return true;
}

void custom_remove(int index)
{
    if (index < 0 || index >= count)
        return;
    memmove(texts[index], texts[index + 1], sizeof(texts[0]) * (size_t)(count - index - 1));
    --count;
}

void custom_load(const char *path)
{
    char line[CUSTOM_TEXT + 32];
    count = 0;
    str_copy(file, sizeof(file), path);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f) && count < CUSTOM_MAX)
    {
        char value[CUSTOM_TEXT + 16], normal[CUSTOM_TEXT];
        if (line[0] == '#' || sscanf(line, " catalog = %315[^\r\n]", value) != 1 ||
            !custom_normalise(value, normal, sizeof(normal)))
            continue;
        bool known = false;
        for (int i = 0; i < count; ++i)
            known = known || strcmp(texts[i], normal) == 0;
        if (!known)
            str_copy(texts[count++], CUSTOM_TEXT, normal);
    }
    fclose(f);
}

bool custom_save(void)
{
    char temp[NP_PATH_MAX + 8];
    if (!file[0])
        return false;
    snprintf(temp, sizeof(temp), "%s.new", file);
    FILE *f = fopen(temp, "w");
    if (!f)
        return false;
    fprintf(f, "# Solanum %s: custom catalogs (third-party), in the order they were added\n", NP_VERSION);
    for (int i = 0; i < count; ++i)
        fprintf(f, "catalog = %s\n", texts[i]);
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

void custom_copy_path(const char *cache, const char *url, char *out, size_t size)
{
    /* FNV-1a, 64 bits: a name for the address, not a secret */
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const char *c = url; *c; ++c)
    {
        hash ^= (unsigned char)*c;
        hash *= 0x100000001b3ull;
    }
    snprintf(out, size, "%s/catalog-%016llx.json", cache, (unsigned long long)hash);
}
