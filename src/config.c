#include "config.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"

#define CONFIG_PATH "0:/config.txt"

static const char DEFAULT_TEXT[] =
    "# PhotoPainter settings. Edit this file, save it, then eject the drive\r\n"
    "# (or press NEXT). Lines starting with # are ignored.\r\n"
    "\r\n"
    "# How often the picture changes: e.g. 30m, 2h, 12h, 1d. 0 = only when NEXT is pressed.\r\n"
    "interval = 12h\r\n"
    "\r\n"
    "# Order: shuffle (random, no repeats until all were shown) or sequential (by file name)\r\n"
    "order = shuffle\r\n"
    "\r\n"
    "# fill = crop pictures to fill the screen, fit = show the whole picture with white borders\r\n"
    "fit = fill\r\n"
    "\r\n"
    "# Color boost 0-100. Higher looks more vivid on the panel's muted colors.\r\n"
    "saturation = 50\r\n"
    "\r\n"
    "# How the frame hangs: landscape, portrait, landscape-flipped, portrait-flipped\r\n"
    "orientation = landscape\r\n"
    "\r\n"
    "# Collages: percent of picture changes that show 4 random photos with black lines\r\n"
    "# between them (0 = never, 100 = always). Needs at least 4 pictures.\r\n"
    "collage = 33\r\n";

void config_defaults(config_t *c) {
    c->interval_min = 12 * 60;
    c->shuffle = 1;
    c->fit = 0;
    c->saturation = 50;
    c->rotate = 0;
    c->collage = 33;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static uint32_t parse_interval(const char *v, uint32_t fallback) {
    char *end;
    double n = strtod(v, &end);
    if (end == v || n < 0) return fallback;
    while (*end == ' ') end++;
    switch (tolower((unsigned char)*end)) {
        case 'd': n *= 1440; break;
        case 'h': n *= 60; break;
        case 's': n /= 60; break;
        default: break;   // minutes
    }
    if (n > 0 && n < 1) n = 1;
    if (n > 28 * 1440) n = 28 * 1440;
    return (uint32_t)n;
}

void config_load(config_t *c) {
    config_defaults(c);
    FIL f;
    if (f_open(&f, CONFIG_PATH, FA_READ) != FR_OK) {
        if (f_open(&f, CONFIG_PATH, FA_CREATE_NEW | FA_WRITE) == FR_OK) {
            UINT bw;
            f_write(&f, DEFAULT_TEXT, sizeof DEFAULT_TEXT - 1, &bw);
            f_close(&f);
            printf("config: wrote default %s\n", CONFIG_PATH);
        }
        return;
    }
    char line[160];
    bool seen_collage = false;
    while (f_gets(line, sizeof line, &f)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        for (char *p = k; *p; p++) *p = (char)tolower((unsigned char)*p);
        for (char *p = v; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (!strcmp(k, "interval")) c->interval_min = parse_interval(v, c->interval_min);
        else if (!strcmp(k, "order")) c->shuffle = !strncmp(v, "shuf", 4) || !strncmp(v, "rand", 4);
        else if (!strcmp(k, "fit")) c->fit = !strcmp(v, "fit") || !strcmp(v, "contain") || !strcmp(v, "whole");
        else if (!strcmp(k, "saturation")) { int n = atoi(v); c->saturation = n < 0 ? 0 : n > 100 ? 100 : n; }
        else if (!strcmp(k, "collage")) { seen_collage = true; int n = atoi(v); c->collage = n < 0 ? 0 : n > 100 ? 100 : n; }
        else if (!strcmp(k, "orientation")) {
            if (!strcmp(v, "portrait")) c->rotate = 270;
            else if (!strcmp(v, "portrait-flipped")) c->rotate = 90;
            else if (!strcmp(v, "landscape-flipped")) c->rotate = 180;
            else c->rotate = 0;
        }
    }
    f_close(&f);
    if (!seen_collage && f_open(&f, CONFIG_PATH, FA_OPEN_APPEND | FA_WRITE) == FR_OK) {
        // Older config.txt (before collages existed): add the setting so it can be found.
        static const char add[] =
            "\r\n# Collages: percent of picture changes that show 4 random photos with black lines\r\n"
            "# between them (0 = never, 100 = always). Needs at least 4 pictures.\r\n"
            "collage = 33\r\n";
        UINT bw;
        f_write(&f, add, sizeof add - 1, &bw);
        f_close(&f);
        printf("config: added collage setting\n");
    }
    printf("config: interval %lu min, %s, %s, saturation %d, rotate %d, collage %d%%\n", (unsigned long)c->interval_min,
           c->shuffle ? "shuffle" : "sequential", c->fit ? "fit" : "fill", c->saturation, c->rotate, c->collage);
}
