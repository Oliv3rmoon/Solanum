/*
 * Solanum - the catalog's app icons (appicon.h).
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _DEFAULT_SOURCE 1 /* glibc: DIR and friends under -std=c11 */
#include "appicon.h"

#include "self_update.h"
#include "self_update_sha256.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const unsigned char PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

size_t appicon_url(const CatalogIcon *icon, char *out, size_t size)
{
    if (!icon || !catalog_icon_path_valid(icon->path))
        return 0;
    const int length = snprintf(out, size, "%s%s", CATALOG_ICON_BASE, icon->path);
    return length > 0 && (size_t)length < size ? (size_t)length : 0;
}

static int sha_valid(const char *sha256)
{
    if (!sha256 || strlen(sha256) != 64)
        return 0;
    for (const char *c = sha256; *c; ++c)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f')))
            return 0;
    return 1;
}

size_t appicon_path(const char *cache, const char *sha256, char *out, size_t size)
{
    if (!cache || !sha_valid(sha256))
        return 0;
    const int length = snprintf(out, size, "%s/icons/%s.png", cache, sha256);
    return length > 0 && (size_t)length < size ? (size_t)length : 0;
}

int appicon_check(const CatalogIcon *icon, const void *data, size_t length)
{
    self_update_sha256 hash;
    unsigned char digest[32];
    if (!icon || !data || !sha_valid(icon->sha256) || length != icon->size || length < sizeof(PNG_SIGNATURE) ||
        length >= CATALOG_ICON_BYTES || memcmp(data, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) != 0)
        return 0;
    self_update_sha256_start(&hash);
    self_update_sha256_add(&hash, data, length);
    self_update_sha256_finish(&hash, digest);
    return self_update_sha256_matches(digest, icon->sha256);
}

int appicon_read(const char *cache, const CatalogIcon *icon, unsigned char *body, size_t capacity, size_t *length)
{
    char path[1024];
    *length = 0;
    if (!appicon_path(cache, icon->sha256, path, sizeof(path)))
        return 0;
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    const size_t got = fread(body, 1, capacity, file);
    const int whole = feof(file) && !ferror(file);
    fclose(file);
    if (whole && appicon_check(icon, body, got))
    {
        *length = got;
        return 1;
    }
    remove(path); /* not the icon its name says: fetched again */
    return -1;
}

int appicon_keep(const char *cache, const CatalogIcon *icon, const void *data, size_t length)
{
    char path[1024], temp[1040], folder[1024];
    if (!appicon_check(icon, data, length) || !appicon_path(cache, icon->sha256, path, sizeof(path)))
        return 0;
    snprintf(folder, sizeof(folder), "%s/icons", cache);
    mkdir(cache, 0777);
    mkdir(folder, 0777);
    snprintf(temp, sizeof(temp), "%s.new", path);
    FILE *file = fopen(temp, "wb");
    if (!file)
        return 0;
    const int written = fwrite(data, 1, length, file) == length;
    if (fclose(file) != 0 || !written)
    {
        remove(temp);
        return 0;
    }
    remove(path);
    if (rename(temp, path) != 0)
    {
        remove(temp);
        return 0;
    }
    return 1;
}

AppIconFrom appicon_load(const char *cache, const CatalogIcon *icon, appicon_fetch_fn fetch, void *user,
                         unsigned char *body, size_t capacity, size_t *length, char *why, size_t why_size)
{
    char url[256];
    int status = 0;
    if (why_size)
        why[0] = '\0';
    *length = 0;
    if (!icon || !icon->path[0] || capacity < CATALOG_ICON_BYTES)
    {
        snprintf(why, why_size, "no icon");
        return APPICON_NONE;
    }
    const int kept = appicon_read(cache, icon, body, capacity, length);
    if (kept == 1)
        return APPICON_KEPT;
    if (!fetch)
    {
        snprintf(why, why_size, "not kept, and offline");
        return APPICON_NONE;
    }
    if (!appicon_url(icon, url, sizeof(url)))
    {
        snprintf(why, why_size, "its path can't be fetched");
        return APPICON_NONE;
    }
    const int code = fetch(user, url, (char *)body, capacity, length, &status);
    if (code != 0 || status != 200)
    {
        if (code == SELF_UPDATE_FETCH_TOO_LARGE)
            snprintf(why, why_size, "%s is larger than an icon may be", url);
        else if (code != 0)
            snprintf(why, why_size, "%s couldn't be downloaded (%d)", url, code);
        else
            snprintf(why, why_size, "%s answered %d", url, status);
        *length = 0;
        return APPICON_NONE;
    }
    if (!appicon_check(icon, body, *length))
    {
        snprintf(why, why_size, "%s isn't the icon the catalog names (%zu bytes, not %u, or another SHA-256)", url,
                 *length, (unsigned)icon->size);
        *length = 0;
        return APPICON_NONE;
    }
    if (!appicon_keep(cache, icon, body, *length))
        snprintf(why, why_size, "fetched, but it couldn't be kept in %s/icons", cache);
    else if (kept < 0)
        snprintf(why, why_size, "its kept copy had changed; fetched again");
    return APPICON_FETCHED;
}

int appicon_prune(const char *cache, const char (*keep)[65], int count)
{
    char folder[1024];
    static char names[512][80];
    int found = 0, removed = 0;
    snprintf(folder, sizeof(folder), "%s/icons", cache);
    DIR *directory = opendir(folder);
    if (!directory)
        return 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && found < 512)
    {
        const size_t length = strlen(entry->d_name);
        char sha[65];
        int wanted = 0;
        if (length == 68 && strcmp(entry->d_name + 64, ".png") == 0)
        {
            snprintf(sha, sizeof(sha), "%.64s", entry->d_name);
            for (int i = 0; i < count && !wanted; ++i)
                wanted = strcmp(keep[i], sha) == 0;
        }
        else if (!(length > 4 && strcmp(entry->d_name + length - 4, ".new") == 0))
            continue; /* not one of Solanum's icons */
        if (!wanted && length < sizeof(names[0]))
            snprintf(names[found++], sizeof(names[0]), "%s", entry->d_name);
    }
    closedir(directory);
    for (int i = 0; i < found; ++i)
    {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", folder, names[i]);
        removed += remove(path) == 0;
    }
    return removed;
}

/* ---------------------------------------------------------------- the picture */

int appicon_fit(const uint8_t *rgba, int width, int height, uint8_t *out)
{
    if (!rgba || !out || width <= 0 || height <= 0 || width > CATALOG_ICON_SIDE || height > CATALOG_ICON_SIDE)
        return 0;
    memset(out, 0, APPICON_PIXEL_BYTES);
    /* the whole picture, its longer side across the square */
    const int long_side = width > height ? width : height;
    int w = (int)((float)APPICON_SIDE * (float)width / (float)long_side + 0.5f);
    int h = (int)((float)APPICON_SIDE * (float)height / (float)long_side + 0.5f);
    w = w < 1 ? 1 : w;
    h = h < 1 ? 1 : h;
    const int left = (APPICON_SIDE - w) / 2, top = (APPICON_SIDE - h) / 2;
    const float scale = (float)long_side / (float)APPICON_SIDE; /* source pixels per output pixel */
    for (int oy = 0; oy < h; ++oy)
        for (int ox = 0; ox < w; ++ox)
        {
            float sum[4] = {0, 0, 0, 0}, weights = 0;
            if (scale >= 1.0f)
            {
                /* smaller: every source pixel the output pixel covers, by how much of it */
                const float x0 = ox * scale, x1 = (ox + 1) * scale, y0 = oy * scale, y1 = (oy + 1) * scale;
                for (int sy = (int)y0; sy < height && (float)sy < y1; ++sy)
                {
                    const float wy = fminf(y1, (float)sy + 1) - fmaxf(y0, (float)sy);
                    for (int sx = (int)x0; sx < width && (float)sx < x1; ++sx)
                    {
                        const float weight = wy * (fminf(x1, (float)sx + 1) - fmaxf(x0, (float)sx));
                        const uint8_t *p = &rgba[((size_t)sy * width + sx) * 4];
                        const float a = p[3] * weight;
                        sum[0] += p[0] * a;
                        sum[1] += p[1] * a;
                        sum[2] += p[2] * a;
                        sum[3] += a;
                        weights += weight;
                    }
                }
            }
            else
            {
                /* larger: between the four nearest source pixels */
                float fx = (ox + 0.5f) * scale - 0.5f, fy = (oy + 0.5f) * scale - 0.5f;
                fx = fx < 0 ? 0 : fx;
                fy = fy < 0 ? 0 : fy;
                const int x = (int)fx, y = (int)fy;
                const int x2 = x + 1 < width ? x + 1 : x, y2 = y + 1 < height ? y + 1 : y;
                const float tx = fx - (float)x, ty = fy - (float)y;
                const int xs[4] = {x, x2, x, x2}, ys[4] = {y, y, y2, y2};
                const float ws[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                for (int k = 0; k < 4; ++k)
                {
                    const uint8_t *p = &rgba[((size_t)ys[k] * width + xs[k]) * 4];
                    const float a = p[3] * ws[k];
                    sum[0] += p[0] * a;
                    sum[1] += p[1] * a;
                    sum[2] += p[2] * a;
                    sum[3] += a;
                    weights += ws[k];
                }
            }
            uint8_t *q = &out[((size_t)(top + oy) * APPICON_SIDE + left + ox) * 4];
            if (sum[3] <= 0.0f || weights <= 0.0f)
                continue;
            for (int c = 0; c < 3; ++c)
            {
                const float v = sum[c] / sum[3];
                q[c] = (uint8_t)(v > 255.0f ? 255 : v + 0.5f);
            }
            const float alpha = sum[3] / weights;
            q[3] = (uint8_t)(alpha > 255.0f ? 255 : alpha + 0.5f);
        }
    return 1;
}

void appicon_round(uint8_t *rgba, int side, float radius)
{
    if (!rgba || side <= 0)
        return;
    if (radius > side * 0.5f)
        radius = side * 0.5f;
    const int reach = (int)ceilf(radius);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
        {
            /* only the corners' squares: the circle's centre is `radius` in from both edges */
            const int in_x = x < reach || x >= side - reach, in_y = y < reach || y >= side - reach;
            if (!in_x || !in_y)
                continue;
            const float cx = x < side / 2 ? radius : (float)side - radius;
            const float cy = y < side / 2 ? radius : (float)side - radius;
            const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            if ((x < side / 2 ? dx > 0 : dx < 0) || (y < side / 2 ? dy > 0 : dy < 0))
                continue; /* inside the straight edges */
            float cover = radius - sqrtf(dx * dx + dy * dy) + 0.5f;
            cover = cover < 0 ? 0 : cover > 1 ? 1 : cover;
            uint8_t *p = &rgba[((size_t)y * side + x) * 4];
            p[3] = (uint8_t)(p[3] * cover + 0.5f);
        }
}

/* ---------------------------------------------------------------- the textures in memory */

void *appicon_lru_get(AppIconLru *lru, const char *sha256, uint64_t frame)
{
    for (int i = 0; i < lru->count; ++i)
        if (strcmp(lru->slot[i].sha256, sha256) == 0)
        {
            lru->slot[i].used = frame;
            return lru->slot[i].texture;
        }
    return NULL;
}

int appicon_lru_put(AppIconLru *lru, const char *sha256, void *texture, uint64_t frame, void **evicted,
                    char evicted_sha[65])
{
    int at = -1;
    *evicted = NULL;
    evicted_sha[0] = '\0';
    for (int i = 0; i < lru->count && at < 0; ++i)
        if (strcmp(lru->slot[i].sha256, sha256) == 0)
            at = i; /* the same icon again: its texture is replaced */
    if (at < 0 && lru->count < APPICON_TEXTURES)
        at = lru->count++;
    else if (at < 0)
        for (int i = 0; i < lru->count; ++i)
            if (lru->slot[i].used + 1 < frame && (at < 0 || lru->slot[i].used < lru->slot[at].used))
                at = i;
    if (at < 0)
        return 0; /* every texture was drawn in this frame or the one before */
    if (lru->slot[at].texture)
    {
        *evicted = lru->slot[at].texture;
        snprintf(evicted_sha, 65, "%s", lru->slot[at].sha256);
    }
    snprintf(lru->slot[at].sha256, sizeof(lru->slot[at].sha256), "%s", sha256);
    lru->slot[at].texture = texture;
    lru->slot[at].used = frame;
    return 1;
}
