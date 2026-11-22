// Streaming driver for the 7.3" 7-color (ACeP, "F") panel, 800x480, 4 bits per pixel.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EPD_W 800
#define EPD_H 480
#define EPD_ROW_BYTES (EPD_W / 2)
#define EPD_FRAME_BYTES (EPD_ROW_BYTES * EPD_H)

enum { C_BLACK = 0, C_WHITE, C_GREEN, C_BLUE, C_RED, C_YELLOW, C_ORANGE, C_NUM };

void epd_begin(void);                          // (waits for a running refresh) power up, reset, init, start upload
void epd_write(const uint8_t *data, size_t n); // packed pixel bytes, row-major, 2 px per byte (left = high nibble)
// Start the refresh (~30 s). wait=true blocks and powers the panel down afterwards;
// wait=false returns immediately so other work (SD, converting) can run meanwhile.
void epd_end(bool wait);
bool epd_refreshing(void);                     // an async refresh is still running
void epd_finish(void);                         // wait for a running refresh, then power the panel down
