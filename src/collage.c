#include "collage.h"
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "epd.h"

#define GAP 8

static const collage_slot_t SLOTS[3][4] = {
    [LAYOUT_GRID]   = {{TILE_Q, 0, 0}, {TILE_Q, TILE_Q_W + GAP, 0},
                       {TILE_Q, 0, TILE_Q_H + GAP}, {TILE_Q, TILE_Q_W + GAP, TILE_Q_H + GAP}},
    [LAYOUT_STRIPS] = {{TILE_S, 0, 0}, {TILE_S, (TILE_S_W + GAP), 0},
                       {TILE_S, 2 * (TILE_S_W + GAP), 0}, {TILE_S, 3 * (TILE_S_W + GAP), 0}},
    [LAYOUT_MIXED]  = {{TILE_S, 0, 0}, {TILE_S, TILE_S_W + GAP, 0},
                       {TILE_Q, 2 * (TILE_S_W + GAP), 0}, {TILE_Q, 2 * (TILE_S_W + GAP), TILE_Q_H + GAP}},
};

const collage_slot_t *collage_slots(int layout) { return SLOTS[layout]; }

const char *collage_layout_name(int layout) {
    return layout == LAYOUT_GRID ? "grid" : layout == LAYOUT_STRIPS ? "strips" : "mixed";
}

// Random eligible photo of the given shape (or any shape if shape < 0) not already used.
static int pick_one(const uint8_t *pref, const uint8_t *elig, int n, int shape, const int *used, int nused,
                    uint32_t (*rnd)(void)) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (!elig[i] || (shape >= 0 && pref[i] != shape)) continue;
        bool u = false;
        for (int k = 0; k < nused; k++) u |= used[k] == i;
        if (!u) count++;
    }
    if (!count) return -1;
    int target = (int)(rnd() % (uint32_t)count);
    for (int i = 0; i < n; i++) {
        if (!elig[i] || (shape >= 0 && pref[i] != shape)) continue;
        bool u = false;
        for (int k = 0; k < nused; k++) u |= used[k] == i;
        if (!u && target-- == 0) return i;
    }
    return -1;
}

int collage_choose(const uint8_t *pref, const uint8_t *elig, int n, uint32_t (*rnd)(void), int pick[4]) {
    int g[4];
    for (int k = 0; k < 4; k++) {
        g[k] = pick_one(pref, elig, n, -1, g, k, rnd);
        if (g[k] < 0) return -1;
    }
    int ns = 0;
    for (int k = 0; k < 4; k++) ns += pref[g[k]] == TILE_S;

    // 1 or 3 strip-shaped photos don't fit a layout: swap one photo to get 0/2/4.
    if (ns == 1 || ns == 3) {
        int odd_shape = ns == 1 ? TILE_S : TILE_Q;          // the lone one
        int maj_shape = 1 - odd_shape;
        // Prefer replacing the lone photo with one of the majority shape (all same shape),
        // otherwise turn the group into 2+2 by swapping one majority photo for the other shape.
        int lone = -1, maj = -1;
        for (int k = 0; k < 4; k++) {
            if (pref[g[k]] == odd_shape) lone = k;
            else if (maj < 0) maj = k;
        }
        int rep = (rnd() & 1) ? pick_one(pref, elig, n, maj_shape, g, 4, rnd) : -1;
        if (rep >= 0) { g[lone] = rep; ns = maj_shape == TILE_S ? 4 : 0; }
        else if ((rep = pick_one(pref, elig, n, odd_shape, g, 4, rnd)) >= 0) { g[maj] = rep; ns = 2; }
        else if ((rep = pick_one(pref, elig, n, maj_shape, g, 4, rnd)) >= 0) { g[lone] = rep; ns = maj_shape == TILE_S ? 4 : 0; }
        // else: not enough photos of either shape - keep the group, one photo gets cropped
    }
    int layout = ns <= 1 ? LAYOUT_GRID : ns >= 3 ? LAYOUT_STRIPS : LAYOUT_MIXED;

    // Assign photos to slots: matching shapes first.
    const collage_slot_t *sl = SLOTS[layout];
    bool taken[4] = {0};
    for (int s = 0; s < 4; s++) {
        pick[s] = -1;
        for (int k = 0; k < 4 && pick[s] < 0; k++)
            if (!taken[k] && pref[g[k]] == sl[s].tile) { pick[s] = g[k]; taken[k] = true; }
    }
    for (int s = 0; s < 4; s++)
        for (int k = 0; k < 4 && pick[s] < 0; k++)
            if (!taken[k]) { pick[s] = g[k]; taken[k] = true; }
    return layout;
}

bool collage_compose(int layout, const char *const tile_path[4], void (*sink)(const uint8_t *row, int len)) {
    static FIL f[4];
    const collage_slot_t *sl = SLOTS[layout];
    bool ok = true;
    int opened = 0;
    for (; opened < 4; opened++) {
        if (f_open(&f[opened], tile_path[opened], FA_READ) != FR_OK) {
            printf("collage: cannot open %s\n", tile_path[opened]);
            ok = false;
            break;
        }
    }
    static uint8_t row[EPD_ROW_BYTES];
    for (int y = 0; y < EPD_H; y++) {
        memset(row, (C_BLACK << 4) | C_BLACK, sizeof row);   // black separator lines
        for (int s = 0; s < 4 && ok; s++) {
            int tw = sl[s].tile == TILE_Q ? TILE_Q_W : TILE_S_W;
            int th = sl[s].tile == TILE_Q ? TILE_Q_H : TILE_S_H;
            if (y < sl[s].y || y >= sl[s].y + th) continue;
            UINT br;
            if (f_read(&f[s], row + sl[s].x / 2, (UINT)(tw / 2), &br) != FR_OK || br != (UINT)(tw / 2)) ok = false;
        }
        sink(row, EPD_ROW_BYTES);
    }
    for (int k = 0; k < opened; k++) f_close(&f[k]);
    return ok;
}
