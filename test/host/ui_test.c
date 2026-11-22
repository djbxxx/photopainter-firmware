// Host test: render sample screens to .epd files.
#include <stdio.h>
#include "ui.h"
#include "epd.h"

static FILE *out;
static void sink(const uint8_t *row, int len) { fwrite(row, 1, len, out); }

static void render(const char *path, const ui_screen_t *s) {
    out = fopen(path, "wb");
    ui_render(s, sink);
    fclose(out);
}

int main(void) {
    ui_screen_t prog = {
        .title = "Preparing pictures",
        .lines = {"Picture 3 of 12", "IMG_20240518_183245_very_long_file_name_here.jpg"},
        .progress = 25, .footer = "Each new picture is converted once and then shows instantly.",
        .accent = C_BLUE};
    render("out/ui_progress.epd", &prog);
    prog.rotate = 270;
    render("out/ui_progress_portrait.epd", &prog);
    ui_screen_t empty = {
        .title = "No pictures yet",
        .lines = {"Copy JPG or BMP pictures into the", "\"pic\" folder on the SD card.", "",
                  "Tip: connect USB to a computer -", "the frame shows up as a drive."},
        .progress = -1, .footer = "Then eject the drive or press NEXT.", .accent = C_ORANGE};
    render("out/ui_empty.epd", &empty);
    return 0;
}
