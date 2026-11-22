// Host test: collage_test <outdir> <img1> <img2> ... (>= 4 images)
// Converts every image (full + tiles), writes one collage per layout and a chooser statistic.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "convert.h"
#include "collage.h"

static FILE *out;
static void sink(const uint8_t *row, int len) { fwrite(row, 1, len, out); }
static uint32_t rnd(void) { return (uint32_t)rand(); }

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: %s outdir img1 img2 img3 img4 [...]\n", argv[0]); return 2; }
    const char *dir = argv[1];
    int n = argc - 2;
    static char full[64][512], q[64][512], s[64][512], tmp[512];
    uint8_t pref[64], elig[64];
    uint8_t *arena = malloc(150 * 1024);
    convert_opts_t o = {.saturation = 50, .rotate = 0, .background = 1};
    for (int i = 0; i < n; i++) {
        snprintf(full[i], 512, "%s/p%d.epd", dir, i);
        snprintf(q[i], 512, "%s/p%d.q", dir, i);
        snprintf(s[i], 512, "%s/p%d.s", dir, i);
        snprintf(tmp, 512, "%s/tmp", dir);
        cv_output_t outs[3] = {{full[i], 800, 480, 0}, {q[i], TILE_Q_W, TILE_Q_H, 0}, {s[i], TILE_S_W, TILE_S_H, 0}};
        uint8_t portrait = 0;
        cv_result_t r = convert_image(argv[2 + i], outs, 3, tmp, &o, arena, 150 * 1024, &portrait);
        elig[i] = r == CV_OK;
        pref[i] = portrait ? TILE_S : TILE_Q;   // landscape frame: portraits prefer strips
        printf("%d %-28s %s %s\n", i, argv[2 + i], convert_strerror(r), portrait ? "portrait" : "landscape");
    }
    // one collage per layout, chosen by the real chooser until each layout appeared
    int done[3] = {0};
    srand(1);
    for (int tries = 0; tries < 500 && !(done[0] && done[1] && done[2]); tries++) {
        int pick[4];
        int L = collage_choose(pref, elig, n, rnd, pick);
        if (L < 0 || done[L]) continue;
        done[L] = 1;
        const collage_slot_t *sl = collage_slots(L);
        const char *paths[4];
        for (int k = 0; k < 4; k++) paths[k] = sl[k].tile == TILE_Q ? q[pick[k]] : s[pick[k]];
        char path[512];
        snprintf(path, sizeof path, "%s/collage_%s.epd", dir, collage_layout_name(L));
        out = fopen(path, "wb");
        bool ok = collage_compose(L, paths, sink);
        fclose(out);
        printf("%s: photos %d %d %d %d (%s)\n", collage_layout_name(L), pick[0], pick[1], pick[2], pick[3], ok ? "ok" : "FAILED");
    }
    // statistics: how often does each layout come up, and how often is a photo in the wrong shape?
    int cnt[3] = {0}, mismatch = 0;
    for (int t = 0; t < 10000; t++) {
        int pick[4];
        int L = collage_choose(pref, elig, n, rnd, pick);
        cnt[L]++;
        const collage_slot_t *sl = collage_slots(L);
        for (int k = 0; k < 4; k++) mismatch += pref[pick[k]] != sl[k].tile;
        for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) if (pick[a] == pick[b]) { printf("DUPLICATE!\n"); return 1; }
    }
    printf("10000 collages: grid %d, strips %d, mixed %d, photos in wrong tile shape: %d\n", cnt[0], cnt[1], cnt[2], mismatch);
    return 0;
}
