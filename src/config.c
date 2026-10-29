/*
 * Solanum - the settings file (config.h): one "key = value" a line (Dulcamara's).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"

#include "ui/sfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->mc_w = s->mc_h = 100;
    s->upscale = 1;
    s->upscale_filter = UPSCALE_SHARP;
    s->ui_sound = SFX_STYLE_SOFT;
    s->ui_volume = 2;
    s->update_check = true;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void config_load(Settings *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char key[64], value[128];
        if (sscanf(line, " %63[^= ] = %127[^\r\n]", key, value) != 2)
            continue;
        const int v = atoi(value);
        if (!strcmp(key, "ui_sound"))
            s->ui_sound = clamp(v, 0, SFX_STYLE_OFF);
        else if (!strcmp(key, "ui_volume"))
            s->ui_volume = clamp(v, 0, 3);
        else if (!strcmp(key, "update_check"))
            s->update_check = v != 0;
    }
    fclose(f);
}

bool config_save(const Settings *s, const char *path)
{
    char temp[NP_PATH_MAX];
    snprintf(temp, sizeof(temp), "%s.new", path);
    FILE *f = fopen(temp, "w");
    if (!f)
        return false;
    fprintf(f, "# Solanum %s settings\n", NP_VERSION);
    fprintf(f, "version = %s\n", NP_VERSION);
    fprintf(f, "ui_sound = %d\nui_volume = %d\nupdate_check = %d\n", s->ui_sound, s->ui_volume, s->update_check);
    const bool ok = fclose(f) == 0;
    if (ok)
    {
        remove(path);
        rename(temp, path);
    }
    return ok;
}

void config_paths(Paths *p, const char *root)
{
    memset(p, 0, sizeof(*p));
    str_copy(p->root, sizeof(p->root), root);
    path_join(p->logs, sizeof(p->logs), root, "logs");
    path_join(p->cache, sizeof(p->cache), root, "cache");
    path_join(p->config, sizeof(p->config), root, "settings.ini");
}
