/*
 * Solanum - host check of the community (src/community.h), run by tests/test_community.py.
 * SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * usage: community-check <a scratch folder>        the community's code, About's and the card's words, the card's flag
 *        community-check --encode <text> <L|M|Q|H>  another code, for the reader's own check
 *
 * It prints the QR code About and the card draw as rows of '#' (dark) and '.', "qr " before each, for the test to read
 * back as a phone does; each language's About texts and captions; and checks that the card is due once, not when the
 * settings can't be kept, and that its closing survives settings.ini.
 */
#include "../src/community.h"
#include "../src/config.h"
#include "../src/i18n.h"
#include "../src/nightshade.h"

#include <stdio.h>
#include <string.h>

static int passed, failed;

#define EXPECT(condition, what)                                                                                  \
    do                                                                                                           \
    {                                                                                                            \
        if (condition)                                                                                           \
            ++passed;                                                                                            \
        else                                                                                                     \
        {                                                                                                        \
            ++failed;                                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                                \
        }                                                                                                        \
    } while (0)

static void print_code(const uint8_t *qr)
{
    const int size = qrcodegen_getSize(qr);
    char row[200];
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
            row[x] = qrcodegen_getModule(qr, x, y) ? '#' : '.';
        row[size] = '\0';
        printf("qr %s\n", row);
    }
}

/* One line, its line breaks written as \n. */
static void print_text(const char *what, int lang, const char *text)
{
    printf("%s %d ", what, lang);
    for (; *text; ++text)
        if (*text == '\n')
            fputs("\\n", stdout);
        else
            putchar(*text);
    putchar('\n');
}

static int encode(const char *text, const char *level)
{
    static const enum qrcodegen_Ecc LEVELS[] = {qrcodegen_Ecc_LOW, qrcodegen_Ecc_MEDIUM, qrcodegen_Ecc_QUARTILE,
                                                qrcodegen_Ecc_HIGH};
    static const char LETTERS[] = "LMQH";
    const char *at = level[0] ? strchr(LETTERS, level[0]) : NULL;
    if (!at)
        return 2;
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)], temp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    if (!qrcodegen_encodeText(text, temp, qr, LEVELS[at - LETTERS], qrcodegen_VERSION_MIN, 12, qrcodegen_Mask_AUTO,
                              false))
        return 1;
    print_code(qr);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--encode"))
        return encode(argv[2], argv[3]);
    if (argc != 2)
    {
        fprintf(stderr, "usage: community-check <scratch folder> | --encode <text> <L|M|Q|H>\n");
        return 2;
    }

    /* the code About and the card draw */
    uint8_t qr[COMMUNITY_QR_BYTES];
    EXPECT(community_qr(qr), "the community's QR code is made");
    print_code(qr);
    EXPECT(!strcmp(COMMUNITY_URL, "https://discord.gg/pSqgRU3YP9"), "the address is nightly's invitation");

    /* About's words and the captions, in every language */
    for (int lang = 0; lang < LANG_COUNT; ++lang)
    {
        i18n_set(lang);
        const char *about[COMMUNITY_ABOUT_TEXTS];
        community_about(about);
        char line[600];
        snprintf(line, sizeof(line), "%s|%s|%s", about[0], about[1], about[2]);
        print_text("about", lang, line);
        EXPECT(!strcmp(about[1], COMMUNITY_NAME) && !strcmp(about[2], COMMUNITY_LINK), "About names the server");
        char caption[400];
        community_caption(caption, sizeof(caption), false);
        print_text("caption", lang, caption);
        EXPECT(strstr(caption, COMMUNITY_NAME) && strstr(caption, COMMUNITY_LINK) && !strchr(caption, '\n'),
               "About's larger code says the server and its address");
        community_caption(caption, sizeof(caption), true);
        print_text("card", lang, caption);
        EXPECT(strstr(caption, COMMUNITY_NAME) && strstr(caption, COMMUNITY_LINK) && strchr(caption, '\n'),
               "the card says the server, its address, and where to find it again");
    }
    i18n_set(LANG_EN);

    /* the card: once */
    Settings s;
    config_defaults(&s);
    EXPECT(community_card_due(&s, true), "a first start shows the card");
    EXPECT(!community_card_due(&s, false), "not when closing it can't be kept (it would come back at every start)");
    community_card_closed(&s);
    EXPECT(!community_card_due(&s, true), "once closed, it isn't shown again");

    char path[NP_PATH_MAX];
    path_join(path, sizeof(path), argv[1], "settings.ini");
    EXPECT(config_save(&s, path), "settings.ini is written");
    Settings again;
    config_defaults(&again);
    config_load(&again, path);
    EXPECT(again.community_seen && !community_card_due(&again, true), "closing the card is kept in settings.ini");
    FILE *f = fopen(path, "r");
    char text[2048] = {0};
    if (f)
    {
        const size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
    }
    EXPECT(strstr(text, "community_seen = 1\n") != NULL, "as community_seen = 1");

    /* a settings.ini from before 0.4.0 has no community_seen: the card is shown once then */
    f = fopen(path, "w");
    if (f)
    {
        fputs("# Solanum 0.3.0 settings\nversion = 0.3.0\nui_sound = 0\nmusic = 1\n", f);
        fclose(f);
    }
    Settings old;
    config_defaults(&old);
    config_load(&old, path);
    EXPECT(community_card_due(&old, true), "an update from 0.3.0 shows the card once");

    printf("%d passed, %d failed\n%s\n", passed, failed, failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
