// Image -> panel converter. Decodes JPEG/BMP, resizes (crop-to-fill or fit),
// rotates (EXIF + frame orientation), dithers to the 7 panel colors and writes a
// ready-to-stream 4bpp file (800x480 = 192000 bytes for full screen, same layout the panel expects).
// Pure C + FatFs, no Pico SDK dependencies, so it can also be built and tested on a PC.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CV_OK = 0,
    CV_ERR_OPEN,          // could not open the source file
    CV_ERR_UNKNOWN,       // not a JPEG or BMP
    CV_ERR_PNG,           // PNG: not supported on the frame
    CV_ERR_HEIC,          // HEIC/HEIF (iPhone): not supported on the frame
    CV_ERR_PROGRESSIVE,   // progressive JPEG: not supported by the decoder
    CV_ERR_UNSUPPORTED,   // valid file but an unsupported variant (e.g. compressed BMP)
    CV_ERR_CORRUPT,       // decode error / truncated file
    CV_ERR_TOOBIG,        // image too large for the memory available
    CV_ERR_WRITE,         // SD card write failed
} cv_result_t;

typedef struct {
    uint8_t  saturation;   // 0..100, how strongly to compensate for the panel's muted colors
    uint16_t rotate;       // extra clockwise rotation 0/90/180/270 (frame hung in portrait)
    uint8_t  background;   // palette index for borders in fit mode
} convert_opts_t;

// One output file. w x h is in panel orientation (w even); file = w*h/2 bytes, 4bpp rows.
typedef struct {
    const char *path;
    uint16_t w, h;
    uint8_t fit;           // 0 = crop to fill, 1 = whole picture with borders
} cv_output_t;

// Decodes the source once and writes up to 3 outputs (e.g. full screen + collage tiles).
// tmp_base: prefix for temp files (needed for 90/270 rotation). *portrait is set to 1 when
// the photo (after EXIF rotation) is taller than wide.
cv_result_t convert_image(const char *src_path, const cv_output_t *outs, int nout, const char *tmp_base,
                          const convert_opts_t *opts, uint8_t *arena, size_t arena_size, uint8_t *portrait);
const char *convert_strerror(cv_result_t r);
