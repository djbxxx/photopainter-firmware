// Picture library: scans 0:/pic, keeps converted frames in 0:/.cache, remembers
// what was shown last, and picks the next picture.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

typedef struct {
    int total;        // picture files found in pic/
    int ok;           // ready to show
    int failed;       // could not be converted (see photopainter-errors.txt)
    int converted;    // converted during this sync
    int first_new;    // index of the first picture converted in this sync, -1 if none
    bool changed;     // pictures added/removed or settings changed
    char first_error[96];
} lib_summary_t;

// Called before each conversion (done < total) and once at the end (done == total).
typedef void (*lib_progress_cb)(int done, int total, const char *name);

bool lib_mount(void);
void lib_unmount(void);
bool lib_sync(const config_t *cfg, uint8_t *arena, size_t arena_size, lib_progress_cb cb, lib_summary_t *out);
int  lib_pick_next(const config_t *cfg, const lib_summary_t *s);   // -1 if nothing to show
bool lib_show(int idx);          // upload the cached frame and refresh the panel (blocking)
const char *lib_last_name(void); // file name of the picture last passed to lib_show

// Small persistent flags (stored in 0:/.cache/state.txt)
int  lib_screen_get(void);
void lib_screen_set(int screen);
