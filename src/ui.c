#include "ui.h"
#include <stdbool.h>
#include <string.h>
#include "epd.h"
#include "fonts.h"

extern sFONT Font24, Font16;

typedef struct {
    const char *text;
    const sFONT *font;
    int x, y, scale, len;
    uint8_t color;
} item_t;

typedef struct {
    int x, y, w, h;
    uint8_t color;
} box_t;

#define MAX_ITEMS (UI_MAX_LINES + 2)
#define MAX_BOXES 4

static int fit_len(const char *t, const sFONT *f, int scale, int max_w) {
    int n = (int)strlen(t);
    int max_chars = max_w / (f->Width * scale);
    return n < max_chars ? n : max_chars;
}

static void add_text(item_t *it, int *n, const char *t, const sFONT *f, int scale, int cy, int lw, uint8_t c) {
    if (!t || *n >= MAX_ITEMS) return;
    item_t *i = &it[(*n)++];
    i->text = t; i->font = f; i->scale = scale; i->color = c;
    i->len = fit_len(t, f, scale, lw - 40);
    i->x = (lw - i->len * f->Width * scale) / 2;
    i->y = cy - f->Height * scale / 2;
}

static uint8_t pixel(int x, int y, const item_t *it, int ni, const box_t *bx, int nb) {
    uint8_t c = C_WHITE;
    for (int b = 0; b < nb; b++)
        if (x >= bx[b].x && x < bx[b].x + bx[b].w && y >= bx[b].y && y < bx[b].y + bx[b].h) c = bx[b].color;
    for (int k = 0; k < ni; k++) {
        const item_t *i = &it[k];
        int fw = i->font->Width, fh = i->font->Height, s = i->scale;
        if (y < i->y || y >= i->y + fh * s || x < i->x || x >= i->x + i->len * fw * s) continue;
        int gx = (x - i->x) / s, gy = (y - i->y) / s;
        int ch = (unsigned char)i->text[gx / fw];
        if (ch < ' ' || ch > '~') ch = '?';
        int bpr = (fw + 7) / 8;
        const uint8_t *g = i->font->table + (ch - ' ') * fh * bpr + gy * bpr;
        int bit = gx % fw;
        if (g[bit / 8] & (0x80 >> (bit % 8))) return i->color;
    }
    return c;
}

void ui_render(const ui_screen_t *s, void (*sink)(const uint8_t *row, int len)) {
    int rot = s->rotate % 360;
    bool turned = rot == 90 || rot == 270;
    int lw = turned ? EPD_H : EPD_W, lh = turned ? EPD_W : EPD_H;   // logical (upright) canvas

    item_t it[MAX_ITEMS];
    box_t bx[MAX_BOXES];
    int ni = 0, nb = 0;
    int title_scale = (s->title && (int)strlen(s->title) * Font24.Width * 2 <= lw - 40) ? 2 : 1;

    bx[nb++] = (box_t){0, 0, lw, 16, s->accent};
    int y = turned ? 130 : 90;
    add_text(it, &ni, s->title, &Font24, title_scale, y, lw, C_BLACK);
    y += turned ? 110 : 80;
    for (int k = 0; k < UI_MAX_LINES && s->lines[k]; k++, y += 40)
        add_text(it, &ni, s->lines[k], &Font24, 1, y, lw, C_BLACK);
    if (s->progress >= 0) {
        int bw = lw - 160, bh = 44, x0 = 80, y0 = turned ? lh - 260 : lh - 130;
        int p = s->progress > 100 ? 100 : s->progress;
        bx[nb++] = (box_t){x0, y0, bw, bh, C_BLACK};
        bx[nb++] = (box_t){x0 + 4, y0 + 4, bw - 8, bh - 8, C_WHITE};
        bx[nb++] = (box_t){x0 + 4, y0 + 4, (bw - 8) * p / 100, bh - 8, s->accent};
    }
    add_text(it, &ni, s->footer, &Font16, 1, lh - 30, lw, C_BLACK);

    uint8_t row[EPD_ROW_BYTES];
    for (int Y = 0; Y < EPD_H; Y++) {
        for (int X = 0; X < EPD_W; X += 2) {
            uint8_t c2[2];
            for (int k = 0; k < 2; k++) {
                int px = X + k, lx, ly;
                // panel pixel -> upright canvas pixel (inverse of the clockwise rotation)
                switch (rot) {
                    case 90:  lx = Y;              ly = lh - 1 - px; break;
                    case 180: lx = lw - 1 - px;    ly = lh - 1 - Y;  break;
                    case 270: lx = lw - 1 - Y;     ly = px;          break;
                    default:  lx = px;             ly = Y;           break;
                }
                c2[k] = pixel(lx, ly, it, ni, bx, nb);
            }
            row[X / 2] = (uint8_t)(c2[0] << 4 | c2[1]);
        }
        sink(row, EPD_ROW_BYTES);
    }
}
