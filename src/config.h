// Settings from config.txt in the root of the SD card.
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t interval_min;   // 0 = only change on button press
    uint8_t  shuffle;        // 1 = random order, 0 = by file name
    uint8_t  fit;            // 0 = crop to fill, 1 = whole picture with borders
    uint8_t  saturation;     // 0..100
    uint16_t rotate;         // clockwise degrees applied to pictures and screens
} config_t;

void config_defaults(config_t *c);
// Reads 0:/config.txt; writes a commented default file if it does not exist.
void config_load(config_t *c);
