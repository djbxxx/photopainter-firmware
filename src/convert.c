#include "convert.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "tjpgd.h"
#include "epd.h"

void keepalive(void);   // provided by the firmware (watchdog + USB) or a host stub

// ------------------------------------------------------------------ helpers

typedef struct { uint8_t *base; size_t size, used; } arena_t;

static void *aalloc(arena_t *a, size_t n) {
    n = (n + 3) & ~(size_t)3;
    if (a->used + n > a->size) return NULL;
    void *p = a->base + a->used;
    a->used += n;
    memset(p, 0, n);
    return p;
}

const char *convert_strerror(cv_result_t r) {
    switch (r) {
        case CV_OK: return "ok";
        case CV_ERR_OPEN: return "could not open file";
        case CV_ERR_UNKNOWN: return "not a JPG or BMP picture";
        case CV_ERR_PNG: return "PNG is not supported - save as JPG";
        case CV_ERR_HEIC: return "HEIC (iPhone) is not supported - export as JPG";
        case CV_ERR_PROGRESSIVE: return "progressive JPG is not supported - re-save as baseline JPG";
        case CV_ERR_UNSUPPORTED: return "unsupported BMP variant";
        case CV_ERR_CORRUPT: return "file is damaged or incomplete";
        case CV_ERR_TOOBIG: return "picture is too large";
        case CV_ERR_WRITE: return "could not write to SD card";
    }
    return "?";
}

// Panel colors in panel index order. PURE is what we ask for, REAL is roughly what
// the ACeP panel actually shows (measured values from Pimoroni's Inky driver).
// Dithering against a blend of the two gives livelier pictures than the pure palette.
static const uint8_t PAL_PURE[C_NUM][3] = {
    {0, 0, 0}, {255, 255, 255}, {0, 255, 0}, {0, 0, 255}, {255, 0, 0}, {255, 255, 0}, {255, 128, 0}};
static const uint8_t PAL_REAL[C_NUM][3] = {
    {57, 48, 57}, {255, 255, 255}, {58, 91, 70}, {61, 59, 94}, {156, 72, 75}, {208, 190, 71}, {177, 106, 73}};

// ------------------------------------------------------------------ pipeline
//
// Stage A works in *source orientation*: the decoder delivers source rows top to
// bottom, they are box-averaged into Aw x Ah output rows (Aw x Ah is the output size,
// swapped when the picture must be turned 90/270), and each output row is dithered
// immediately. Rotation is applied when the dithered rows are written out.
// Several outputs (full screen + collage tiles) are fed from one decode.

typedef struct {
    int Aw, Ah;                 // output size in source orientation
    int ox, oy, iw, ih;         // where the picture lands inside Aw x Ah
    uint16_t *cs, *ce;          // source column range for each picture column
    uint16_t *rs, *re;          // source row range for each picture row
    uint32_t *acc;              // running sums for the current picture row
    int acc_rows;
    int cur;                    // next picture row to finish
    int out_row;                // next output row (0..Ah)
    int32_t *e0, *e1;           // dither error rows (x16), with one guard pixel each side
    int pal[C_NUM][3];
    uint8_t bg;
    uint8_t *rgb;               // one output row, RGB
    uint8_t *pk;                // one output row, packed 4bpp
    uint8_t *pk2;               // scratch for 180 degree rows
    int rot;
    FIL *out;
    cv_result_t err;
} pipe_t;

static uint8_t nearest(const pipe_t *p, int r, int g, int b) {
    int best = 0, bd = 0x7fffffff;
    for (int i = 0; i < C_NUM; i++) {
        int dr = r - p->pal[i][0], dg = g - p->pal[i][1], db = b - p->pal[i][2];
        int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return (uint8_t)best;
}

static inline void put_nib(uint8_t *row, int x, uint8_t c) {
    if (x & 1) row[x >> 1] = (row[x >> 1] & 0xF0) | c;
    else       row[x >> 1] = (row[x >> 1] & 0x0F) | (uint8_t)(c << 4);
}
static inline uint8_t get_nib(const uint8_t *row, int x) {
    return (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
}

static void write_row(pipe_t *p, int y) {
    UINT bw;
    int rb = p->Aw / 2;
    if (p->err) return;
    if (p->rot == 180) {
        for (int x = 0; x < p->Aw; x++) put_nib(p->pk2, p->Aw - 1 - x, get_nib(p->pk, x));
        if (f_lseek(p->out, (FSIZE_t)(p->Ah - 1 - y) * rb) != FR_OK ||
            f_write(p->out, p->pk2, rb, &bw) != FR_OK || bw != (UINT)rb) p->err = CV_ERR_WRITE;
    } else {   // 0: final file, 90/270: temp file in source orientation
        if (f_write(p->out, p->pk, rb, &bw) != FR_OK || bw != (UINT)rb) p->err = CV_ERR_WRITE;
    }
}

static void emit_pad_row(pipe_t *p) {
    uint8_t b = (uint8_t)(p->bg << 4 | p->bg);
    memset(p->pk, b, p->Aw / 2);
    memset(p->e0, 0, (p->Aw + 2) * 3 * sizeof(int32_t));
    write_row(p, p->out_row++);
}

// Dither p->rgb (picture columns ox..ox+iw) into p->pk and write it.
static void emit_image_row(pipe_t *p) {
    int y = p->out_row;
    uint8_t b = (uint8_t)(p->bg << 4 | p->bg);
    memset(p->pk, b, p->Aw / 2);
    int32_t *cur = p->e0, *nxt = p->e1;
    memset(nxt, 0, (p->Aw + 2) * 3 * sizeof(int32_t));
    int dir = (y & 1) ? -1 : 1;
    for (int k = 0; k < p->iw; k++) {
        int x = dir > 0 ? p->ox + k : p->ox + p->iw - 1 - k;
        int e = (x + 1) * 3;
        int v[3];
        for (int c = 0; c < 3; c++) {
            int t = p->rgb[x * 3 + c] + (cur[e + c] / 16);
            v[c] = t < 0 ? 0 : t > 255 ? 255 : t;
        }
        uint8_t ci = nearest(p, v[0], v[1], v[2]);
        put_nib(p->pk, x, ci);
        for (int c = 0; c < 3; c++) {
            int er = v[c] - p->pal[ci][c];
            cur[e + dir * 3 + c] += er * 7;
            nxt[e - dir * 3 + c] += er * 3;
            nxt[e + c]           += er * 5;
            nxt[e + dir * 3 + c] += er * 1;
        }
    }
    p->e0 = nxt; p->e1 = cur;
    write_row(p, p->out_row++);
}

static bool pipe_done(const pipe_t *p) { return p->cur >= p->ih || p->err; }

// Feed one decoded source row (index i, increasing) of the cropped/scaled source.
static void feed_row(pipe_t *p, const uint8_t *src, int i) {
    while (p->cur < p->ih && p->rs[p->cur] <= i) {
        int y = p->cur;
        if (i < p->re[y]) {
            uint32_t *a = p->acc;
            for (int x = 0; x < p->iw; x++, a += 3) {
                const uint8_t *s = src + p->cs[x] * 3;
                for (int k = p->cs[x]; k < p->ce[x]; k++, s += 3) { a[0] += s[0]; a[1] += s[1]; a[2] += s[2]; }
            }
            p->acc_rows++;
        }
        if (i >= p->re[y] - 1) {
            // finish picture row y
            while (p->out_row < p->oy) emit_pad_row(p);
            uint32_t *a = p->acc;
            uint8_t *o = p->rgb + p->ox * 3;
            for (int x = 0; x < p->iw; x++, a += 3, o += 3) {
                uint32_t n = (uint32_t)(p->ce[x] - p->cs[x]) * (uint32_t)p->acc_rows;
                if (!n) n = 1;
                o[0] = (uint8_t)(a[0] / n); o[1] = (uint8_t)(a[1] / n); o[2] = (uint8_t)(a[2] / n);
            }
            emit_image_row(p);
            memset(p->acc, 0, p->iw * 3 * sizeof(uint32_t));
            p->acc_rows = 0;
            p->cur++;
            continue;
        }
        break;
    }
}

// Work out crop/placement for a source of sw x sh pixels and allocate buffers.
static cv_result_t pipe_setup(pipe_t *p, arena_t *ar, int sw, int sh, int fit, const convert_opts_t *o) {
    int Aw = p->Aw, Ah = p->Ah;
    int cx = 0, cy = 0, cw = sw, ch = sh;
    if (fit == 0) {                          // crop to fill
        p->ox = p->oy = 0; p->iw = Aw; p->ih = Ah;
        if ((int64_t)sw * Ah > (int64_t)sh * Aw) { cw = (int)((int64_t)sh * Aw / Ah); cx = (sw - cw) / 2; }
        else                                    { ch = (int)((int64_t)sw * Ah / Aw); cy = (sh - ch) / 2; }
        if (cw < 1) cw = 1;
        if (ch < 1) ch = 1;
    } else {                                 // fit whole picture
        if ((int64_t)sw * Ah > (int64_t)sh * Aw) { p->iw = Aw; p->ih = (int)((int64_t)sh * Aw / sw); }
        else                                    { p->ih = Ah; p->iw = (int)((int64_t)sw * Ah / sh); }
        if (p->iw < 1) p->iw = 1;
        if (p->ih < 1) p->ih = 1;
        p->ox = (Aw - p->iw) / 2; p->oy = (Ah - p->ih) / 2;
    }
    p->cs = aalloc(ar, p->iw * 2); p->ce = aalloc(ar, p->iw * 2);
    p->rs = aalloc(ar, p->ih * 2); p->re = aalloc(ar, p->ih * 2);
    p->acc = aalloc(ar, p->iw * 3 * 4);
    p->e0 = aalloc(ar, (Aw + 2) * 3 * 4); p->e1 = aalloc(ar, (Aw + 2) * 3 * 4);
    p->rgb = aalloc(ar, Aw * 3);
    p->pk = aalloc(ar, Aw / 2); p->pk2 = aalloc(ar, Aw / 2);
    if (!p->cs || !p->ce || !p->rs || !p->re || !p->acc || !p->e0 || !p->e1 || !p->rgb || !p->pk || !p->pk2)
        return CV_ERR_TOOBIG;
    for (int x = 0; x < p->iw; x++) {
        int a = cx + (int)((int64_t)x * cw / p->iw), b = cx + (int)((int64_t)(x + 1) * cw / p->iw);
        if (b <= a) b = a + 1;
        if (b > sw) b = sw;
        if (a >= b) a = b - 1;
        p->cs[x] = (uint16_t)a; p->ce[x] = (uint16_t)b;
    }
    for (int y = 0; y < p->ih; y++) {
        int a = cy + (int)((int64_t)y * ch / p->ih), b = cy + (int)((int64_t)(y + 1) * ch / p->ih);
        if (b <= a) b = a + 1;
        if (b > sh) b = sh;
        if (a >= b) a = b - 1;
        p->rs[y] = (uint16_t)a; p->re[y] = (uint16_t)b;
    }
    int s = o->saturation > 100 ? 100 : o->saturation;
    for (int i = 0; i < C_NUM; i++)
        for (int c = 0; c < 3; c++)
            p->pal[i][c] = (PAL_REAL[i][c] * s + PAL_PURE[i][c] * (100 - s)) / 100;
    return CV_OK;
}

// ------------------------------------------------------------------ multiple outputs

#define MAX_OUT 3
typedef struct { pipe_t *p[MAX_OUT]; int n; } multi_t;

static bool all_done(const multi_t *m) {
    for (int k = 0; k < m->n; k++) if (!pipe_done(m->p[k])) return false;
    return true;
}
static void feed_all(multi_t *m, const uint8_t *row, int i) {
    for (int k = 0; k < m->n; k++) if (!pipe_done(m->p[k])) feed_row(m->p[k], row, i);
}

// Decode-scale ratio an output needs: how many source pixels per output pixel.
static double need_ratio(int W, int H, int Aw, int Ah, int fit) {
    double rw = (double)W / Aw, rh = (double)H / Ah;
    return fit == 0 ? (rw < rh ? rw : rh) : (rw > rh ? rw : rh);
}

// ------------------------------------------------------------------ JPEG

typedef struct {
    FIL *f;
    multi_t *m;
    uint8_t *band;
    int sw;
    unsigned blocks;
} jctx_t;

static size_t jpg_in(JDEC *jd, uint8_t *buf, size_t n) {
    jctx_t *c = jd->device;
    if (buf) {
        UINT br = 0;
        if (f_read(c->f, buf, (UINT)n, &br) != FR_OK) return 0;
        return br;
    }
    return f_lseek(c->f, f_tell(c->f) + n) == FR_OK ? n : 0;
}

static int jpg_out(JDEC *jd, void *bitmap, JRECT *r) {
    jctx_t *c = jd->device;
    int w = r->right - r->left + 1, h = r->bottom - r->top + 1;
    const uint8_t *s = bitmap;
    for (int y = 0; y < h; y++)
        memcpy(c->band + ((size_t)y * c->sw + r->left) * 3, s + (size_t)y * w * 3, (size_t)w * 3);
    if ((++c->blocks & 63) == 0) keepalive();
    if (r->right == c->sw - 1) {             // band of rows complete
        for (int y = 0; y < h; y++) feed_all(c->m, c->band + (size_t)y * c->sw * 3, r->top + y);
        keepalive();
        if (all_done(c->m)) return 0;        // stop decoding: everything we need is done
    }
    return 1;
}

// EXIF orientation -> clockwise degrees needed to show the photo upright.
static int jpeg_exif_rotation(FIL *f) {
    uint8_t b[12];
    UINT br;
    f_lseek(f, 2);
    for (int guard = 0; guard < 32; guard++) {
        if (f_read(f, b, 4, &br) != FR_OK || br != 4 || b[0] != 0xFF) break;
        int marker = b[1], len = (b[2] << 8) | b[3];
        if (marker == 0xDA || marker == 0xD9) break;           // start of scan / end
        FSIZE_t next = f_tell(f) + len - 2;
        if (marker == 0xE1 && len > 16) {
            uint8_t hdr[6];
            f_read(f, hdr, 6, &br);
            if (br == 6 && !memcmp(hdr, "Exif\0\0", 6)) {
                FSIZE_t tiff = f_tell(f);
                uint8_t t[8];
                f_read(f, t, 8, &br);
                bool le = t[0] == 'I';
                #define U16(q) (le ? (q)[0] | (q)[1] << 8 : (q)[0] << 8 | (q)[1])
                #define U32(q) (le ? (uint32_t)((q)[0] | (q)[1] << 8 | (q)[2] << 16 | (uint32_t)(q)[3] << 24) \
                                   : (uint32_t)((uint32_t)(q)[0] << 24 | (q)[1] << 16 | (q)[2] << 8 | (q)[3]))
                f_lseek(f, tiff + U32(t + 4));
                f_read(f, t, 2, &br);
                int n = U16(t);
                for (int i = 0; i < n && i < 64; i++) {
                    if (f_read(f, b, 12, &br) != FR_OK || br != 12) break;
                    if (U16(b) == 0x0112) {
                        int v = U16(b + 8);
                        switch (v) { case 3: case 4: return 180; case 5: case 6: return 90; case 7: case 8: return 270; }
                        return 0;
                    }
                }
                #undef U16
                #undef U32
                return 0;
            }
        }
        if (f_lseek(f, next) != FR_OK) break;
    }
    return 0;
}

// Frame size from the SOF marker. Returns 1 = baseline, 2 = progressive, 0 = not found.
static int jpeg_size(FIL *f, int *W, int *H) {
    uint8_t b[9];
    UINT br;
    f_lseek(f, 2);
    for (int guard = 0; guard < 64; guard++) {
        if (f_read(f, b, 4, &br) != FR_OK || br != 4 || b[0] != 0xFF) return 0;
        int marker = b[1], len = (b[2] << 8) | b[3];
        if (marker == 0xDA || marker == 0xD9) return 0;
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (f_read(f, b, 5, &br) != FR_OK || br != 5) return 0;
            *H = (b[1] << 8) | b[2];
            *W = (b[3] << 8) | b[4];
            return (marker == 0xC0 || marker == 0xC1) ? 1 : 2;
        }
        if (f_lseek(f, f_tell(f) + len - 2) != FR_OK) return 0;
    }
    return 0;
}

typedef struct {
    const cv_output_t *outs;
    int nout;
    const convert_opts_t *o;
    multi_t m;
    int rot;
    uint8_t portrait;
} job_t;

static cv_result_t setup_all(job_t *j, arena_t *ar, int sw, int sh) {
    for (int k = 0; k < j->nout; k++) {
        cv_result_t r = pipe_setup(j->m.p[k], ar, sw, sh, j->outs[k].fit, j->o);
        if (r) return r;
    }
    return CV_OK;
}

static cv_result_t do_jpeg(FIL *f, job_t *j, arena_t *ar) {
    jctx_t c = {.f = f, .m = &j->m};
    JDEC jd;
    size_t pool_sz = 10 * 1024;
    void *pool = aalloc(ar, pool_sz);
    if (!pool) return CV_ERR_TOOBIG;
    f_lseek(f, 0);
    JRESULT jr = jd_prepare(&jd, jpg_in, pool, pool_sz, &c);
    if (jr == JDR_FMT3) return CV_ERR_PROGRESSIVE;
    if (jr == JDR_MEM1 || jr == JDR_MEM2) return CV_ERR_TOOBIG;
    if (jr != JDR_OK) return CV_ERR_CORRUPT;

    int W = jd.width, H = jd.height;
    double r = 1e9;   // the most demanding output decides the decode scale
    for (int k = 0; k < j->nout; k++) {
        pipe_t *p = j->m.p[k];
        double rk = need_ratio(W, H, p->Aw, p->Ah, j->outs[k].fit);
        if (rk < r) r = rk;
    }
    int s = 0;
    while (s < 3 && r / (1 << (s + 1)) >= 1.0) s++;
    int mx = 8 * jd.msx, my = 8 * jd.msy;
    int sw = ((W - W % mx) >> s) + ((W % mx) >> s);
    int sh = ((H - H % my) >> s) + ((H % my) >> s);
    int bandh = my >> s;
    if (bandh < 1) bandh = 1;
    printf("jpeg %dx%d -> scale 1/%d = %dx%d, rot %d, %d outputs\n", W, H, 1 << s, sw, sh, j->rot, j->nout);
    bool turned_src = j->rot == 90 || j->rot == 270;   // (includes frame rotation; only used for log)
    (void)turned_src;

    cv_result_t cr = setup_all(j, ar, sw, sh);
    if (cr) return cr;
    c.sw = sw;
    c.band = aalloc(ar, (size_t)sw * bandh * 3);
    if (!c.band) return CV_ERR_TOOBIG;
    jr = jd_decomp(&jd, jpg_out, (uint8_t)s);
    for (int k = 0; k < j->nout; k++) {
        pipe_t *p = j->m.p[k];
        if (p->err) return p->err;
        if (p->cur < p->ih) return CV_ERR_CORRUPT;
    }
    if (jr != JDR_OK && jr != JDR_INTR) return CV_ERR_CORRUPT;
    return CV_OK;
}

// ------------------------------------------------------------------ BMP

static uint32_t le32(const uint8_t *q) { return q[0] | q[1] << 8 | q[2] << 16 | (uint32_t)q[3] << 24; }
static uint16_t le16(const uint8_t *q) { return (uint16_t)(q[0] | q[1] << 8); }

typedef struct { int32_t W, H; int bpp; uint32_t off, dib, comp, ncol; bool bottom_up; } bmp_t;

static cv_result_t bmp_header(FIL *f, bmp_t *b) {
    uint8_t h[54];
    UINT br;
    f_lseek(f, 0);
    if (f_read(f, h, 54, &br) != FR_OK || br != 54) return CV_ERR_CORRUPT;
    b->off = le32(h + 10); b->dib = le32(h + 14);
    b->W = (int32_t)le32(h + 18); b->H = (int32_t)le32(h + 22);
    b->bpp = le16(h + 28);
    b->comp = le32(h + 30); b->ncol = le32(h + 46);
    b->bottom_up = b->H > 0;
    if (b->H < 0) b->H = -b->H;
    if (b->W <= 0 || b->H <= 0) return CV_ERR_CORRUPT;
    if (b->W > 12000 || b->H > 12000) return CV_ERR_TOOBIG;
    if (!(b->comp == 0 || (b->comp == 3 && b->bpp == 32))) return CV_ERR_UNSUPPORTED;
    if (b->bpp != 1 && b->bpp != 4 && b->bpp != 8 && b->bpp != 24 && b->bpp != 32) return CV_ERR_UNSUPPORTED;
    return CV_OK;
}

static cv_result_t do_bmp(FIL *f, const bmp_t *b, job_t *j, arena_t *ar) {
    UINT br;
    int W = b->W, H = b->H, bpp = b->bpp;
    cv_result_t cr = setup_all(j, ar, W, H);
    if (cr) return cr;

    static uint8_t pal[256][3];
    if (bpp <= 8) {
        uint32_t ncol = b->ncol ? b->ncol : 1u << bpp;
        if (ncol > 256) ncol = 256;
        f_lseek(f, 14 + b->dib);
        for (uint32_t i = 0; i < ncol; i++) {
            uint8_t q[4];
            f_read(f, q, 4, &br);
            pal[i][0] = q[2]; pal[i][1] = q[1]; pal[i][2] = q[0];
        }
    }
    uint32_t stride = ((uint32_t)W * bpp + 31) / 32 * 4;
    uint8_t *raw = aalloc(ar, stride), *row = aalloc(ar, (size_t)W * 3);
    if (!raw || !row) return CV_ERR_TOOBIG;
    printf("bmp %ldx%ld %dbpp rot %d, %d outputs\n", (long)W, (long)H, bpp, j->rot, j->nout);

    int first = H;
    for (int k = 0; k < j->nout; k++) if (j->m.p[k]->rs[0] < first) first = j->m.p[k]->rs[0];
    for (int y = first; y < H && !all_done(&j->m); y++) {
        int fy = b->bottom_up ? H - 1 - y : y;
        if (f_lseek(f, b->off + (FSIZE_t)fy * stride) != FR_OK ||
            f_read(f, raw, stride, &br) != FR_OK || br != stride) return CV_ERR_CORRUPT;
        uint8_t *d = row;
        for (int x = 0; x < W; x++, d += 3) {
            if (bpp == 24 || bpp == 32) {
                const uint8_t *q = raw + x * (bpp / 8);
                d[0] = q[2]; d[1] = q[1]; d[2] = q[0];
            } else {
                int bit = x * bpp, idx = (raw[bit >> 3] >> (8 - bpp - (bit & 7))) & ((1 << bpp) - 1);
                d[0] = pal[idx][0]; d[1] = pal[idx][1]; d[2] = pal[idx][2];
            }
        }
        feed_all(&j->m, row, y);
        if ((y & 15) == 0) keepalive();
    }
    for (int k = 0; k < j->nout; k++) {
        pipe_t *p = j->m.p[k];
        if (p->err) return p->err;
        if (p->cur < p->ih) return CV_ERR_CORRUPT;
    }
    return CV_OK;
}

// ------------------------------------------------------------------ rotate 90/270

// tmp holds SW x SH packed rows (source orientation); write the turned SH x SW result.
static cv_result_t transpose(FIL *tmp, FIL *dst, int rot, int SW, int SH, arena_t *ar) {
    const int srb = SW / 2, orb = SH / 2, OH = SW;
    uint8_t *srow = aalloc(ar, srb);
    int K;
    uint8_t *slice = NULL;
    for (K = OH; K >= 8; K /= 2) {
        size_t mark = ar->used;
        slice = aalloc(ar, (size_t)K * orb);
        if (slice) break;
        ar->used = mark;
    }
    if (!srow || !slice) return CV_ERR_TOOBIG;
    UINT br, bw;
    for (int Y0 = 0; Y0 < OH; Y0 += K) {
        int n = OH - Y0 < K ? OH - Y0 : K;
        f_lseek(tmp, 0);
        for (int sy = 0; sy < SH; sy++) {
            if (f_read(tmp, srow, srb, &br) != FR_OK || br != (UINT)srb) return CV_ERR_CORRUPT;
            int X = rot == 90 ? SH - 1 - sy : sy;
            for (int jj = 0; jj < n; jj++) {
                int Y = Y0 + jj;
                int sx = rot == 90 ? Y : SW - 1 - Y;
                put_nib(slice + (size_t)jj * orb, X, get_nib(srow, sx));
            }
            if ((sy & 63) == 0) keepalive();
        }
        if (f_write(dst, slice, (UINT)n * orb, &bw) != FR_OK || bw != (UINT)n * orb) return CV_ERR_WRITE;
    }
    return CV_OK;
}

// ------------------------------------------------------------------ entry point

cv_result_t convert_image(const char *src_path, const cv_output_t *outs, int nout, const char *tmp_base,
                          const convert_opts_t *o, uint8_t *arena_mem, size_t arena_size, uint8_t *portrait) {
    static FIL fsrc, fdst[MAX_OUT], ftmp[MAX_OUT];
    static char tmp[MAX_OUT][300];
    if (nout < 1 || nout > MAX_OUT) return CV_ERR_UNSUPPORTED;
    arena_t ar = {arena_mem, arena_size, 0};
    job_t *j = aalloc(&ar, sizeof(job_t));
    j->outs = outs; j->nout = nout; j->o = o; j->m.n = nout;
    if (portrait) *portrait = 0;

    if (f_open(&fsrc, src_path, FA_READ) != FR_OK) return CV_ERR_OPEN;
    uint8_t m[12] = {0};
    UINT br;
    f_read(&fsrc, m, 12, &br);
    enum { T_JPG, T_BMP } type = T_JPG;
    cv_result_t r = CV_OK;
    if (m[0] == 0xFF && m[1] == 0xD8) type = T_JPG;
    else if (m[0] == 'B' && m[1] == 'M') type = T_BMP;
    else if (m[0] == 0x89 && m[1] == 'P' && m[2] == 'N' && m[3] == 'G') r = CV_ERR_PNG;
    else if (!memcmp(m + 4, "ftyp", 4) && (!memcmp(m + 8, "hei", 3) || !memcmp(m + 8, "mif1", 4) || !memcmp(m + 8, "hev", 3))) r = CV_ERR_HEIC;
    else r = CV_ERR_UNKNOWN;
    if (r) { f_close(&fsrc); return r; }

    // Size + EXIF rotation first: decides the orientation of every output.
    int exif = 0, W = 0, H = 0;
    bmp_t bh = {0};
    if (type == T_JPG) {
        exif = jpeg_exif_rotation(&fsrc);
        int sof = jpeg_size(&fsrc, &W, &H);
        if (sof == 2) { f_close(&fsrc); return CV_ERR_PROGRESSIVE; }
        if (sof != 1 || W <= 0 || H <= 0) { f_close(&fsrc); return CV_ERR_CORRUPT; }
    } else {
        r = bmp_header(&fsrc, &bh);
        if (r) { f_close(&fsrc); return r; }
        W = bh.W; H = bh.H;
    }
    bool photo_turned = exif == 90 || exif == 270;
    if (portrait) *portrait = photo_turned ? (W > H) : (H > W);
    j->rot = (exif + o->rotate) % 360;
    bool turned = j->rot == 90 || j->rot == 270;

    for (int k = 0; k < nout; k++) {
        pipe_t *p = aalloc(&ar, sizeof(pipe_t));
        j->m.p[k] = p;
        p->bg = o->background;
        p->rot = j->rot;
        p->Aw = turned ? outs[k].h : outs[k].w;
        p->Ah = turned ? outs[k].w : outs[k].h;
        if (f_open(&fdst[k], outs[k].path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) r = CV_ERR_WRITE;
        if (!r && turned) {
            snprintf(tmp[k], sizeof tmp[k], "%s%d.bin", tmp_base, k);
            if (f_open(&ftmp[k], tmp[k], FA_CREATE_ALWAYS | FA_WRITE | FA_READ) != FR_OK) r = CV_ERR_WRITE;
        }
        p->out = turned ? &ftmp[k] : &fdst[k];
        if (r) { for (int q = 0; q <= k; q++) { f_close(&fdst[q]); if (turned) f_close(&ftmp[q]); } f_close(&fsrc); return r; }
    }

    r = type == T_JPG ? do_jpeg(&fsrc, j, &ar) : do_bmp(&fsrc, &bh, j, &ar);
    for (int k = 0; k < nout && !r; k++) {
        pipe_t *p = j->m.p[k];
        while (p->out_row < p->Ah) emit_pad_row(p);       // bottom border
        if (p->err) r = p->err;
    }
    f_close(&fsrc);
    // Copy what we still need: the transposes below reuse the arena, which holds j and the pipes.
    int SW[MAX_OUT], SH[MAX_OUT], rot = j->rot;
    for (int k = 0; k < nout; k++) { SW[k] = j->m.p[k]->Aw; SH[k] = j->m.p[k]->Ah; }
    j = NULL;
    for (int k = 0; k < nout; k++) {
        if (!r && turned) {
            arena_t ar2 = {arena_mem, arena_size, 0};       // stage A buffers are free now
            r = transpose(&ftmp[k], &fdst[k], rot, SW[k], SH[k], &ar2);
        }
        if (turned) { f_close(&ftmp[k]); f_unlink(tmp[k]); }
        if (f_close(&fdst[k]) != FR_OK && !r) r = CV_ERR_WRITE;
    }
    for (int k = 0; k < nout; k++) {
        FILINFO fi;
        if (!r && (f_stat(outs[k].path, &fi) != FR_OK || fi.fsize != (FSIZE_t)outs[k].w * outs[k].h / 2)) r = CV_ERR_WRITE;
    }
    if (r) for (int k = 0; k < nout; k++) f_unlink(outs[k].path);
    return r;
}
