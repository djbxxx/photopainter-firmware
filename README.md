# PhotoPainter custom firmware

Alternative firmware for the **Waveshare PhotoPainter** (RP2040 + 7.3" 7-color e-paper, 800×480).
It replaces the factory firmware, which needs pictures pre-converted to BMP on a computer and, depending
on the build, a hand-written `fileList.txt`.

- **Just copy photos.** Put JPG or BMP files of any size into `pic/` on the SD card. Phone photos are
  turned upright automatically (EXIF orientation).
- **Converted on the frame.** New pictures are resized, cropped (or fitted) and dithered to the 7 panel
  colors once, then cached in a hidden `.cache` folder, so showing a picture afterwards needs no decoding.
  When two or more pictures need converting, a progress screen is shown.
- **USB drive mode.** Plugged into a computer, the SD card shows up as a USB drive. Copy pictures, eject,
  and the frame converts them and shows the first new one. No need to take the card out.
- **4-photo collages.** Some picture changes show 4 random photos with black lines between them. The layout
  adapts to the photos: a 2×2 grid for landscape photos, 4 strips for portrait photos, or 2 strips + 2 tiles for a mix.
- **Settings in `config.txt`** on the card: change interval, shuffle or by-name order, crop or fit,
  color boost, frame orientation, collage frequency.
- **Readable errors.** Files that can't be used are listed with the reason in `photopainter-errors.txt`.
- **Flashing without buttons** once this firmware (or the factory firmware) is running: `picotool load -f`.



## Which frame

Tested on the original **PhotoPainter** with the 7.3" 7-color ACeP panel (Waveshare driver `EPD_7in3f`).
The **PhotoPainter (B)** uses a different 6-color panel (`EPD_7in3e`) and needs a different driver and palette;
it is not supported as-is.

## Install

1. **Back up the factory firmware** (recommended), with [picotool](https://github.com/raspberrypi/picotool)
   and the frame plugged in over USB:
   ```sh
   picotool save -f factory_program.uf2
   ```
2. Download `photopainter.uf2` from the [Releases](../../releases) page.
3. Flash it, either
   - `picotool load -f -x photopainter.uf2`, or
   - press RUN, press BOOT, release RUN, release BOOT. A drive called `RPI-RP2` appears; copy the `.uf2` onto it
     (on macOS: `cp -X photopainter.uf2 /Volumes/RPI-RP2/`).

To go back: `picotool load -f -x factory_program.uf2`.

## Use

| | |
|---|---|
| Add pictures | Copy JPG/BMP into `pic/` (via USB drive mode or the card reader). Subfolders such as `pic/inactive/` are ignored, so you can park pictures there. |
| Plugged into a computer | The card appears as a drive (the green LED blinks slowly). **Eject** it and the frame updates. |
| NEXT | Next picture |
| Hold NEXT 2 s (on USB) | Drive mode again |
| On battery | Wakes on NEXT or the timer, shows the next picture, switches off |

`config.txt` (created on first start; edit it, then eject):

| Setting | Default | |
|---|---|---|
| `interval` | `12h` | How often the picture changes: `30m`, `2h`, `1d`… `0` = only on NEXT |
| `order` | `shuffle` | `shuffle` (no repeats until all were shown) or `sequential` (by file name) |
| `fit` | `fill` | `fill` crops to the screen, `fit` shows the whole picture with white borders |
| `saturation` | `50` | 0–100: how strongly to compensate for the panel's muted colors |
| `orientation` | `landscape` | `landscape`, `landscape-flipped`, `portrait`, `portrait-flipped` |
| `collage` | `33` | Percent of picture changes that show a 4-photo collage (0 = off) |

Changing `fit`, `saturation` or `orientation` reconverts all pictures once.

**Not supported on the frame:** PNG, HEIC (iPhone default), progressive JPEG. They are listed in
`photopainter-errors.txt`; export them as normal JPG.

**Speed** (RP2040 at 200 MHz): a 12-megapixel JPEG converts in about 12 s, an 800×480 BMP in about 5 s.
Every screen change takes about 30 s, which is the panel's own refresh time.

### Battery

- The frame only runs on battery if the 3.7 V Li-ion battery (MX1.25 2-pin plug) is connected and the power
  switch is ON. If the frame dies the moment USB is unplugged, check those two things first.
- The timed picture changes on battery need a CR1220 coin cell in the RTC holder (or USB power).
- Below 3.1 V the frame shows a "Battery low" screen and stays off until charged.
- On battery the log is appended to `.cache/boot.log` on the card for troubleshooting.

## Build from source

Needs the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.1.x (with the `tinyusb` submodule), the
[Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (`arm-none-eabi`),
CMake and Ninja.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
export PICO_TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain   # optional if arm-none-eabi-gcc is on PATH
./build.sh                                               # -> build/photopainter.uf2
picotool load -f -x build/photopainter.uf2
```

With CMake 4.x the build passes `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (done by `build.sh`).

### Host tests

The image converter, the collage code and the text screens are plain C and also build on a desktop. The tests
produce the same bytes as the frame:

```sh
cd test
cc -O2 -Ihost -I../src -I../lib/tjpgd -o convert_test host/convert_test.c host/ff_shim.c ../src/convert.c ../lib/tjpgd/tjpgd.c
./convert_test photo.jpg out.epd            # writes out.epd (+ .q/.s collage tiles)
python3 host/epd2png.py out.epd out.png     # needs Pillow
```

### Serial log and remote control

The firmware logs to USB serial. `tools/ppctl.py` (needs `pyserial`) reads the log and can trigger actions:

```sh
python3 tools/ppctl.py log            # follow the log
python3 tools/ppctl.py next --wait    # next picture
python3 tools/ppctl.py drive          # back to USB drive mode
python3 tools/ppctl.py status
```

## How it works

| File | |
|---|---|
| `src/main.c` | Battery cycle, USB loop (drive mode, NEXT, timer), status screens |
| `src/library.c` | Scans `pic/`, keeps `.cache/index.txt`, converts new pictures, picks the next one, shows it |
| `src/convert.c` | JPEG (TJpgDec) / BMP decode, box-filter resize, crop or fit, EXIF and frame rotation, Floyd–Steinberg dithering. One decode can write up to 3 outputs (full screen + both collage tile shapes) |
| `src/collage.c` | Collage layouts, choosing a group of 4 photos, assembling the tiles row by row |
| `src/epd.c` | Panel driver (streams rows; refresh can run while the next picture converts) |
| `src/usb.c` | TinyUSB composite device: CDC serial + mass storage (raw SD access) + Raspberry Pi reset interface |
| `src/ui.c` | Text/progress screens rendered row by row (no framebuffer) |
| `src/pcf85063.c` | RTC alarm used as the wake-up timer |

Dithering compensates for the panel's muted colors by matching against a blend of the ideal palette and
measured panel colors (values from Pimoroni's Inky driver), controlled by `saturation`.

## Credits and licenses

This project's own code is MIT licensed (see `LICENSE`). Bundled third-party code keeps its own license;
see `THIRD_PARTY.md`. Panel init sequence and pin map are from Waveshare's PhotoPainter example code.
