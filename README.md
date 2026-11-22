# PhotoPainter custom firmware

Replacement firmware for the Waveshare PhotoPainter (RP2040, 7.3" 7-color e-paper).

- Put JPG or BMP pictures in `pic/` on the SD card. Any size, any orientation (EXIF is honored).
- New pictures are converted once on the frame (resize, crop/fit, dither) and cached in `.cache/`.
  A progress screen is shown when two or more pictures need converting.
- USB to a computer: the SD card shows up as a drive. Copy pictures, eject -> the frame updates.
- NEXT: next picture. Hold NEXT 2 s (on USB): drive mode again.
- `config.txt` (created on first start): interval, order (shuffle/sequential), fill/fit, saturation,
  orientation (landscape, landscape-flipped, portrait, portrait-flipped), collage (percent).
- Collages: `collage = 33` means about 1 in 3 changes shows 4 random photos with black lines between them.
  Layout follows the photos: 2x2 grid (landscape), 4 strips (portrait) or 2 strips + 2 tiles (mixed).
  Tiles are made at conversion time (one decode -> full picture + both tile shapes), so a collage shows instantly.
- Only files directly in `pic/` are used: subfolders (e.g. `pic/inactive/`) are ignored, so pictures can be parked there.
- Problems with files are listed in `photopainter-errors.txt` on the card.

Build: `./build.sh` -> `build/photopainter.uf2`
Flash (no buttons needed while this or the factory firmware runs): `picotool load -f -x build/photopainter.uf2`
Restore factory firmware: `picotool load -f -x ../backup/factory_program.uf2`

Host tests for the converter and screens: see `test/host/` (device output is byte-identical).

Serial control (no buttons needed): `tools/ppctl.py log|next|drive|status` (needs pyserial).

Measured on the frame (200 MHz): 12 MP JPEG ~10 s, 800x480 BMP ~3.6 s, panel refresh ~30 s.
Not supported on the frame: PNG, HEIC, progressive JPEG (listed in photopainter-errors.txt).
