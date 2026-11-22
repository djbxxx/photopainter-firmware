// 4-photo collages with black separator lines. Pure C + FatFs (host-testable).
//
// Two tile shapes in panel coordinates, cached per photo at conversion time:
//   Q (quarter)  396 x 236   - 2x2 grid
//   S (strip)    194 x 480   - four side-by-side strips
// Layouts (8 px black lines, exactly 800 x 480):
//   GRID   = 4 x Q            STRIPS = 4 x S            MIXED = 2 x S (left) + 2 x Q (right)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TILE_Q_W 396
#define TILE_Q_H 236
#define TILE_S_W 194
#define TILE_S_H 480

enum { TILE_Q = 0, TILE_S = 1 };
enum { LAYOUT_GRID = 0, LAYOUT_STRIPS, LAYOUT_MIXED };

typedef struct { uint8_t tile; uint16_t x, y; } collage_slot_t;

const collage_slot_t *collage_slots(int layout);   // 4 slots
const char *collage_layout_name(int layout);

// Pick 4 photos and a layout. pref[i] is the tile shape photo i prefers (TILE_Q/TILE_S),
// eligible[i] says whether it can be used. rnd() supplies random numbers.
// On success fills pick[4] (photo index per slot, in slot order) and returns the layout; -1 if < 4 photos.
int collage_choose(const uint8_t *pref, const uint8_t *eligible, int n, uint32_t (*rnd)(void), int pick[4]);

// Stream the collage: tile_path[i] is the cached tile file for slot i (shape per collage_slots).
// sink receives 480 rows of 400 packed bytes. Returns false if a tile could not be read.
bool collage_compose(int layout, const char *const tile_path[4], void (*sink)(const uint8_t *row, int len));
