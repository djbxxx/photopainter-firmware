#include "library.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"
#include "pico/rand.h"
#include "convert.h"
#include "epd.h"
#include "board.h"

#define PIC_DIR    "0:/pic"
#define CACHE_DIR  "0:/.cache"
#define INDEX_PATH "0:/.cache/index.txt"
#define INDEX_TMP  "0:/.cache/index.new"
#define STATE_PATH "0:/.cache/state.txt"
#define TMP_PATH   "0:/.cache/tmp.bin"
#define ERR_PATH   "0:/photopainter-errors.txt"
#define MAX_PICS   1000
#define ST_TODO    255

typedef struct {
    uint32_t hash, size;
    uint16_t fdate, ftime;
    uint8_t status;    // cv_result_t, or ST_TODO
    uint8_t fresh;     // converted during this sync
} entry_t;

static entry_t ents[MAX_PICS];
static int nents;
static uint16_t perm[MAX_PICS];
static char last_name[128];

static struct {
    uint32_t shown;    // hash of the picture on screen
    uint32_t seed;
    int pos, count;
    int screen;        // which status screen is on the panel (0 = a picture)
} st;

// ------------------------------------------------------------------ utils

static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static void cache_path(char *buf, size_t n, uint32_t hash) {
    snprintf(buf, n, CACHE_DIR "/%08lx.epd", (unsigned long)hash);
}

static bool is_picture_name(const char *n) {
    const char *dot = strrchr(n, '.');
    if (!dot || n[0] == '.') return false;   // skip ._ files and other hidden files from macOS
    char e[8] = {0};
    for (int i = 0; i < 7 && dot[1 + i]; i++) e[i] = (char)tolower((unsigned char)dot[1 + i]);
    return !strcmp(e, "jpg") || !strcmp(e, "jpeg") || !strcmp(e, "bmp") || !strcmp(e, "png") ||
           !strcmp(e, "heic") || !strcmp(e, "heif");
}

static int settings_sig(const config_t *c) { return c->fit * 1000000 + c->saturation * 1000 + c->rotate; }

// ------------------------------------------------------------------ state

static void state_load(void) {
    memset(&st, 0, sizeof st);
    st.pos = -1;
    FIL f;
    if (f_open(&f, STATE_PATH, FA_READ) != FR_OK) return;
    char line[64];
    while (f_gets(line, sizeof line, &f)) {
        unsigned long v;
        char k[16];
        if (sscanf(line, "%15s %lu", k, &v) != 2) continue;
        if (!strcmp(k, "shown")) st.shown = v;
        else if (!strcmp(k, "seed")) st.seed = v;
        else if (!strcmp(k, "pos")) st.pos = (int)(long)v;
        else if (!strcmp(k, "count")) st.count = (int)v;
        else if (!strcmp(k, "screen")) st.screen = (int)v;
    }
    f_close(&f);
}

static void state_save(void) {
    FIL f;
    if (f_open(&f, STATE_PATH, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return;
    f_printf(&f, "shown %lu\nseed %lu\npos %ld\ncount %d\nscreen %d\n", (unsigned long)st.shown,
             (unsigned long)st.seed, (long)st.pos, st.count, st.screen);
    f_close(&f);
}

int lib_screen_get(void) { return st.screen; }
void lib_screen_set(int s) { st.screen = s; state_save(); }

// ------------------------------------------------------------------ mount

bool lib_mount(void) {
    sd_card_t *sd = sd_get_by_num(0);
    FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);
    if (fr != FR_OK) {
        printf("sd: mount failed (%d)\n", fr);
        return false;
    }
    sd->mounted = true;
    f_mkdir(CACHE_DIR);
    state_load();
    return true;
}

void lib_unmount(void) {
    sd_card_t *sd = sd_get_by_num(0);
    f_unmount(sd->pcName);
    sd->mounted = false;
}

// ------------------------------------------------------------------ sync

typedef struct { uint32_t hash, size; uint16_t fdate, ftime; uint8_t status; } old_t;
static const char *g_pool;
static const uint32_t *g_name_off;

static int cmp_names(const void *a, const void *b) {
    const char *x = g_pool + g_name_off[*(const uint16_t *)a], *y = g_pool + g_name_off[*(const uint16_t *)b];
    return strcasecmp(x, y);
}

static bool write_index(const char *path, int sig, const char *pool, const uint32_t *off, const uint16_t *order,
                        FIL *names_from) {
    FIL f;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    f_printf(&f, "PPv1 %d\n", sig);
    static char line[320];
    for (int k = 0; k < nents; k++) {
        const char *name;
        if (pool) name = pool + off[order[k]];
        else {   // copy names from the previous index file, in the same order
            if (!f_gets(line, sizeof line, names_from)) break;
            char *p = line;
            for (int i = 0; i < 5 && p; i++) p = strchr(p + (i ? 1 : 0), ' ');
            name = p ? p + 1 : "";
            line[strcspn(line, "\n")] = 0;
        }
        const entry_t *e = &ents[k];
        f_printf(&f, "%08lx %lu %u %u %u %s\n", (unsigned long)e->hash, (unsigned long)e->size, e->fdate, e->ftime,
                 e->status, name);
    }
    return f_close(&f) == FR_OK;
}

// Reads the name of entry k (0-based) from an open index file positioned at the header.
static bool index_next_name(FIL *f, char *name, size_t n) {
    static char line[320];
    if (!f_gets(line, sizeof line, f)) return false;
    line[strcspn(line, "\n")] = 0;
    char *p = line;
    for (int i = 0; i < 5 && p; i++) p = strchr(p + (i ? 1 : 0), ' ');
    snprintf(name, n, "%s", p ? p + 1 : "");
    return true;
}

bool lib_sync(const config_t *cfg, uint8_t *arena, size_t arena_size, lib_progress_cb cb, lib_summary_t *out) {
    memset(out, 0, sizeof *out);
    out->first_new = -1;
    int sig = settings_sig(cfg);
    size_t used = 0;
#define ALLOC(n) (used + (((n) + 3) & ~3u) <= arena_size ? (void *)(arena + (used += (((n) + 3) & ~3u)) - (((n) + 3) & ~3u)) : NULL)

    // 1. previous index (only trusted if made with the same settings)
    old_t *olds = ALLOC(sizeof(old_t) * MAX_PICS);
    int nold = 0;
    FIL f;
    if (f_open(&f, INDEX_PATH, FA_READ) == FR_OK) {
        char line[320];
        int osig = -1;
        if (f_gets(line, sizeof line, &f) && sscanf(line, "PPv1 %d", &osig) == 1 && osig == sig) {
            while (nold < MAX_PICS && f_gets(line, sizeof line, &f)) {
                unsigned long h, sz;
                unsigned d, t, s;
                if (sscanf(line, "%lx %lu %u %u %u", &h, &sz, &d, &t, &s) == 5)
                    olds[nold++] = (old_t){(uint32_t)h, (uint32_t)sz, (uint16_t)d, (uint16_t)t, (uint8_t)s};
            }
        } else if (osig != -1) {
            printf("lib: settings changed, converting everything again\n");
            out->changed = true;
        }
        f_close(&f);
    }

    // 2. scan pic/
    size_t pool_cap = 48 * 1024;
    char *pool = ALLOC(pool_cap);
    uint32_t *off = ALLOC(sizeof(uint32_t) * MAX_PICS);
    entry_t *raw = ALLOC(sizeof(entry_t) * MAX_PICS);
    uint16_t *order = ALLOC(sizeof(uint16_t) * MAX_PICS);
    if (!olds || !pool || !off || !raw || !order) { printf("lib: arena too small\n"); return false; }
    size_t pool_used = 0;
    int n = 0;
    DIR dir;
    FILINFO fi;
    if (f_opendir(&dir, PIC_DIR) != FR_OK) {
        f_mkdir(PIC_DIR);
    } else {
        while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
            if ((fi.fattrib & (AM_DIR | AM_HID | AM_SYS)) || !is_picture_name(fi.fname)) continue;
            size_t len = strlen(fi.fname) + 1;
            if (n >= MAX_PICS || pool_used + len > pool_cap) { printf("lib: too many pictures\n"); break; }
            memcpy(pool + pool_used, fi.fname, len);
            off[n] = pool_used;
            pool_used += len;
            raw[n] = (entry_t){fnv1a(fi.fname), (uint32_t)fi.fsize, fi.fdate, fi.ftime, ST_TODO, 0};
            order[n] = n;
            n++;
            keepalive();
        }
        f_closedir(&dir);
    }
    g_pool = pool; g_name_off = off;
    qsort(order, n, sizeof(uint16_t), cmp_names);

    // 3. match against the previous index
    nents = n;
    int todo = 0;
    static char path[300];
    for (int k = 0; k < n; k++) {
        entry_t e = raw[order[k]];
        for (int j = 0; j < nold; j++) {
            if (olds[j].hash == e.hash && olds[j].size == e.size && olds[j].fdate == e.fdate && olds[j].ftime == e.ftime) {
                e.status = olds[j].status;
                if (e.status == CV_OK) {
                    cache_path(path, sizeof path, e.hash);
                    if (f_stat(path, &fi) != FR_OK || fi.fsize != EPD_FRAME_BYTES) e.status = ST_TODO;
                }
                break;
            }
        }
        if (e.status == ST_TODO) todo++;
        ents[k] = e;
    }
    if (todo || n != nold) out->changed = true;
    out->total = n;
    printf("lib: %d pictures, %d to convert\n", n, todo);
    if (!write_index(INDEX_PATH, sig, pool, off, order, NULL)) printf("lib: could not write index\n");

    // 4. remove cache files that no longer belong to a picture
    if (f_opendir(&dir, CACHE_DIR) == FR_OK) {
        while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
            unsigned long h;
            if (strlen(fi.fname) != 12 || sscanf(fi.fname, "%8lx.epd", &h) != 1) continue;
            bool used_h = false;
            for (int k = 0; k < n && !used_h; k++) used_h = ents[k].hash == (uint32_t)h;
            if (!used_h) {
                snprintf(path, sizeof path, CACHE_DIR "/%s", fi.fname);
                f_unlink(path);
            }
        }
        f_closedir(&dir);
    }

    // 5. convert what is new or changed (the arena is reused by the converter from here on)
    if (todo) {
        convert_opts_t o = {.fit = cfg->fit, .saturation = cfg->saturation, .rotate = cfg->rotate,
                            .background = C_WHITE};
        FIL idx;
        int done = 0;
        if (f_open(&idx, INDEX_PATH, FA_READ) == FR_OK) {
            static char line[320], name[260], src[300], dst[40];
            f_gets(line, sizeof line, &idx);   // header
            for (int k = 0; k < n && index_next_name(&idx, name, sizeof name); k++) {
                if (ents[k].status != ST_TODO) continue;
                if (cb) cb(done, todo, name);
                snprintf(src, sizeof src, PIC_DIR "/%s", name);
                cache_path(dst, sizeof dst, ents[k].hash);
                board_led_act(true);
                uint32_t t0 = board_millis();
                cv_result_t r = convert_image(src, dst, TMP_PATH, &o, arena, arena_size);
                board_led_act(false);
                printf("lib: %s -> %s (%lu ms)\n", name, convert_strerror(r), (unsigned long)(board_millis() - t0));
                ents[k].status = (uint8_t)r;
                if (r == CV_OK) {
                    ents[k].fresh = 1;
                    out->converted++;
                    if (out->first_new < 0) out->first_new = k;
                }
                done++;
            }
            f_close(&idx);
        }
        if (cb) cb(todo, todo, NULL);
        // rewrite the index with the results
        if (f_open(&idx, INDEX_PATH, FA_READ) == FR_OK) {
            char line[64];
            f_gets(line, sizeof line, &idx);
            bool okw = write_index(INDEX_TMP, sig, NULL, NULL, NULL, &idx);
            f_close(&idx);
            if (okw) { f_unlink(INDEX_PATH); f_rename(INDEX_TMP, INDEX_PATH); }
        }
    }

    // 6. summary + error report
    FIL ef;
    bool have_ef = false;
    FIL idx;
    bool have_idx = f_open(&idx, INDEX_PATH, FA_READ) == FR_OK;
    static char line[64], name[260];
    if (have_idx) f_gets(line, sizeof line, &idx);
    for (int k = 0; k < n; k++) {
        bool got = have_idx && index_next_name(&idx, name, sizeof name);
        if (ents[k].status == CV_OK) { out->ok++; continue; }
        out->failed++;
        const char *why = convert_strerror((cv_result_t)ents[k].status);
        if (!have_ef) have_ef = f_open(&ef, ERR_PATH, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK;
        if (have_ef) {
            if (out->failed == 1) f_printf(&ef, "These pictures in the pic folder could not be shown:\n\n");
            f_printf(&ef, "%s: %s\n", got ? name : "?", why);
        }
        if (out->failed == 1) snprintf(out->first_error, sizeof out->first_error, "%s: %s", got ? name : "?", why);
    }
    if (have_idx) f_close(&idx);
    if (have_ef) f_close(&ef);
    else f_unlink(ERR_PATH);
    printf("lib: %d ready, %d failed, %d converted now\n", out->ok, out->failed, out->converted);
    return true;
#undef ALLOC
}

// ------------------------------------------------------------------ choosing

static uint32_t xs32(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

static void make_perm(int n, uint32_t seed) {
    for (int i = 0; i < n; i++) perm[i] = (uint16_t)i;
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(xs32(&seed) % (uint32_t)(i + 1));
        uint16_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
}

int lib_pick_next(const config_t *cfg, const lib_summary_t *s) {
    if (nents == 0 || s->ok == 0) return -1;
    if (s->first_new >= 0) return s->first_new;   // show newly added pictures right away

    if (!cfg->shuffle) {
        int start = 0;
        for (int k = 0; k < nents; k++) if (ents[k].hash == st.shown) { start = k + 1; break; }
        for (int i = 0; i < nents; i++) {
            int k = (start + i) % nents;
            if (ents[k].status == CV_OK) return k;
        }
        return -1;
    }

    if (st.count != nents || st.seed == 0) { st.seed = get_rand_32() | 1; st.pos = -1; st.count = nents; }
    make_perm(nents, st.seed);
    for (int tries = 0; tries < 2 * nents; tries++) {
        st.pos++;
        if (st.pos >= nents) {             // round complete: new random order
            st.seed = get_rand_32() | 1;
            st.pos = 0;
            make_perm(nents, st.seed);
            if (nents > 1 && ents[perm[0]].hash == st.shown) { uint16_t t = perm[0]; perm[0] = perm[1]; perm[1] = t; }
        }
        int k = perm[st.pos];
        if (ents[k].status == CV_OK && (nents == 1 || ents[k].hash != st.shown)) return k;
    }
    for (int k = 0; k < nents; k++) if (ents[k].status == CV_OK) return k;
    return -1;
}

const char *lib_last_name(void) { return last_name; }

bool lib_show(int k) {
    if (k < 0 || k >= nents) return false;
    char path[40];
    cache_path(path, sizeof path, ents[k].hash);
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) { printf("show: cannot open %s\n", path); return false; }

    // look up the file name for the log
    last_name[0] = 0;
    FIL idx;
    if (f_open(&idx, INDEX_PATH, FA_READ) == FR_OK) {
        char line[64], name[260];
        f_gets(line, sizeof line, &idx);
        for (int i = 0; i <= k && index_next_name(&idx, name, sizeof name); i++)
            if (i == k) snprintf(last_name, sizeof last_name, "%s", name);
        f_close(&idx);
    }
    printf("show: #%d %s\n", k, last_name);

    static uint8_t buf[4096];
    epd_begin();
    UINT br;
    size_t total = 0;
    while (f_read(&f, buf, sizeof buf, &br) == FR_OK && br > 0) {
        epd_write(buf, br);
        total += br;
    }
    f_close(&f);
    if (total != EPD_FRAME_BYTES) printf("show: short frame %u\n", (unsigned)total);
    epd_end(true);
    st.shown = ents[k].hash;
    st.screen = 0;
    state_save();
    return true;
}
