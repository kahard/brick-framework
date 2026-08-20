# BRICK asset tools

## LVGL fonts

`generate_lvgl_font.py` converts a TrueType, OpenType or WOFF font to an LVGL
C asset. By default it includes ASCII, Latin-1 and Latin Extended-A, which is
enough for Polish text. The generated font is uncompressed and unprefiltered,
matching the format used by LVGL's built-in fonts.

The tool requires Node.js/npm because it invokes `lv_font_conv` through `npx`:

```powershell
python tools/assets/generate_lvgl_font.py `
  --font C:\path\to\OpenSans-Regular.ttf `
  --output assets\fonts\open_sans_24.c `
  --name brick_open_sans_24 `
  --size 24 `
  --bpp 4
```

For a smaller embedded asset, pass only the required ranges:

```powershell
python tools/assets/generate_lvgl_font.py `
  --font C:\path\to\OpenSans-Regular.ttf `
  --output assets\fonts\open_sans_pl_20.c `
  --name brick_open_sans_pl_20 `
  --size 20 `
  --range 0x20-0x7F `
  --range 0x0100-0x017F
```

Use a redistributable font for committed project assets. System fonts such as
Arial are useful for local experiments, but should not be copied into a public
repository without checking their license.

## Images

`generate_image_asset.py` converts PNG, BMP and JPEG files to a C asset with
width, height, pixel format, bytes per pixel and data size metadata:

```powershell
python tools/assets/generate_image_asset.py `
  --input assets\logo.png `
  --output assets\generated\logo_rgb565.c `
  --symbol brick_logo `
  --format rgb565 `
  --resize 100x100 `
  --binary assets\generated\logo_rgb565.bin
```

Supported formats are `rgb565` (little-endian, intended for direct display
transfers) and `rgba8888` (intended for compositing or LVGL-side conversion).
The generated `.h` and `.c` files are self-contained and C-compatible.
The optional `.bin` output contains only packed pixel data, which is useful for
streaming from flash or an SD card without keeping a complete C array in RAM.
