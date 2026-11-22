// Host test: convert_test <in> <out.epd> [fit 0|1] [saturation 0-100] [rotate 0|90|180|270]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "ff.h"
#include "convert.h"

void keepalive(void) {}

FRESULT f_open(FIL *f, const char *path, int mode) {
    const char *m = (mode & FA_CREATE_ALWAYS) ? ((mode & FA_READ) ? "w+b" : "wb") : "rb";
    f->fp = fopen(path, m);
    return f->fp ? FR_OK : FR_NO_FILE;
}
FRESULT f_close(FIL *f) { if (f->fp) fclose(f->fp); f->fp = NULL; return FR_OK; }
FRESULT f_read(FIL *f, void *b, UINT n, UINT *br) { *br = (UINT)fread(b, 1, n, f->fp); return FR_OK; }
FRESULT f_write(FIL *f, const void *b, UINT n, UINT *bw) { *bw = (UINT)fwrite(b, 1, n, f->fp); return *bw == n ? FR_OK : FR_DISK_ERR; }
FRESULT f_lseek(FIL *f, FSIZE_t o) {
    // FatFs extends files when seeking past the end in write mode; emulate that.
    fseek(f->fp, 0, SEEK_END);
    long end = ftell(f->fp);
    if ((long)o > end) { static const char z[4096]; long need = (long)o - end; while (need > 0) { long k = need > 4096 ? 4096 : need; fwrite(z, 1, k, f->fp); need -= k; } }
    return fseek(f->fp, (long)o, SEEK_SET) == 0 ? FR_OK : FR_DISK_ERR;
}
FSIZE_t f_tell(FIL *f) { return (FSIZE_t)ftell(f->fp); }
FRESULT f_unlink(const char *p) { remove(p); return FR_OK; }
FRESULT f_stat(const char *p, FILINFO *fi) { struct stat st; if (stat(p, &st)) return FR_NO_FILE; fi->fsize = st.st_size; return FR_OK; }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s in out.epd [fit] [sat] [rot]\n", argv[0]); return 2; }
    convert_opts_t o = {.fit = argc > 3 ? atoi(argv[3]) : 0, .saturation = argc > 4 ? atoi(argv[4]) : 50,
                        .rotate = argc > 5 ? atoi(argv[5]) : 0, .background = 1};
    // Same arena size the firmware uses, to catch memory problems on the PC too.
    size_t sz = 150 * 1024;
    uint8_t *arena = malloc(sz);
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", argv[2]);
    clock_t t0 = clock();
    cv_result_t r = convert_image(argv[1], argv[2], tmp, &o, arena, sz);
    printf("%s: %s (%.0f ms)\n", argv[1], convert_strerror(r), (clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    return r;
}
