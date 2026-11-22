// Text/progress screens, rendered one panel row at a time (no framebuffer needed).
#pragma once
#include <stdint.h>

#define UI_MAX_LINES 6

typedef struct {
    const char *title;
    const char *lines[UI_MAX_LINES];   // NULL-terminated or full
    int progress;                      // 0..100 draws a progress bar, -1 = none
    const char *footer;
    uint8_t accent;                    // color of the top stripe and bar
    uint16_t rotate;                   // same meaning as convert_opts_t.rotate
} ui_screen_t;

// Renders the screen and passes each packed 400-byte panel row to sink().
void ui_render(const ui_screen_t *s, void (*sink)(const uint8_t *row, int len));
