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

## Asset bundles

Several raw assets can be packed into one binary file with a generated C++
manifest containing stable IDs, offsets, sizes and image metadata:

```powershell
python tools/assets/bundle_assets.py `
  --output assets/generated/assets.bin `
  --header assets/generated/assets.h `
  --manifest assets/generated/assets.tsv `
  --asset joy_tears=assets/generated/joy_tears.bin,480x480,rgb565 `
  --asset red_background=assets/generated/red.bin,480x480,rgb565
```

The bundle is deliberately just concatenated payloads. The generated manifest
is the catalog; the optional tab-separated file is useful for inspection and
upload tooling. There is no per-asset header in the binary: every offset points
directly at pixel data, relative to the beginning of the bundle. The generator
rejects duplicate names, invalid dimensions, wrong payload sizes and bundles
that cannot be addressed with 32-bit offsets.

The storage backend can be an embedded binary, a flash partition, external
flash, filesystem or another memory device. `IAssetSource` receives the
descriptor and a relative payload offset, so the streamer does not need to
know where the bytes physically live. A platform adapter is responsible for
mapping the descriptor offset to its storage medium and checking the storage
boundaries.

For Arduino targets that keep the bundle in program memory, the generator can
also emit a source file containing one `PROGMEM` byte array:

```powershell
python tools/assets/bundle_assets.py `
  --output generated/assets.bin `
  --header generated/assets.h `
  --c-source generated/assets.cpp `
  --c-symbol brick_asset_bundle `
  --asset joy_tears=joy.bin,240x240,rgb565
```
