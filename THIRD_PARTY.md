# Third-party code

| Path | Project | License |
|---|---|---|
| `lib/FatFs_SPI/ff14a/` | [FatFs](http://elm-chan.org/fsw/ff/) R0.14a by ChaN | FatFs license (BSD-style, see file headers) |
| `lib/FatFs_SPI/` (other files) | [no-OS-FatFS-SD-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico) by Carl John Kugler III, as shipped in Waveshare's PhotoPainter example | Apache License 2.0 |
| `lib/tjpgd/` | [TJpgDec](http://elm-chan.org/fsw/tjpgd/) R0.03 by ChaN (`JD_FASTDECODE` set to 2) | TJpgDec license (no restriction on use, see file header) |
| `lib/fonts/` | Bitmap fonts from STMicroelectronics, as shipped in Waveshare's e-Paper examples | BSD 3-Clause (see file headers) |

Not bundled but referenced:

- Panel initialization sequence and pin assignments: Waveshare PhotoPainter example code
  (https://github.com/waveshareteam/PhotoPainter).
- Measured panel colors used for dithering: Pimoroni Inky library (https://github.com/pimoroni/inky).
- Pico SDK and TinyUSB (BSD-3 / MIT) are used at build time and not included here.
