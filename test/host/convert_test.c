// Host test: convert_test <in> <out.epd> [fit 0|1] [saturation 0-100] [rotate 0|90|180|270]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "ff.h"
#include "convert.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s in out.epd [fit] [sat] [rot]\n", argv[0]); return 2; }
    int fit = argc > 3 ? atoi(argv[3]) : 0;
    convert_opts_t o = {.saturation = argc > 4 ? atoi(argv[4]) : 50,
                        .rotate = argc > 5 ? atoi(argv[5]) : 0, .background = 1};
    // full screen + both collage tile shapes, like the firmware
    char q[512], st[512];
    snprintf(q, sizeof q, "%s.q", argv[2]);
    snprintf(st, sizeof st, "%s.s", argv[2]);
    cv_output_t outs[3] = {{argv[2], 800, 480, (uint8_t)fit}, {q, 396, 236, 0}, {st, 194, 480, 0}};
    uint8_t portrait = 0;
    // Same arena size the firmware uses, to catch memory problems on the PC too.
    size_t sz = 150 * 1024;
    uint8_t *arena = malloc(sz);
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", argv[2]);
    clock_t t0 = clock();
    cv_result_t r = convert_image(argv[1], outs, 3, tmp, &o, arena, sz, &portrait);
    printf("%s: %s%s (%.0f ms)\n", argv[1], convert_strerror(r), r ? "" : (portrait ? " [portrait]" : " [landscape]"),
           (clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    return r;
}
