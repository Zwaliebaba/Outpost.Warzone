#!/usr/bin/env python3
"""One-shot converter: 8-bit palettised PCX -> uncompressed A8R8G8B8 DDS.

Stage 4 of the palette removal (Docs/MigrationPlan.md). Bakes the expansion
the game used to do at load time into the files themselves:

- every index is looked up in GameData/palette.bin, the single shared
  palette every shipped PCX was authored against (embedded PCX palettes were
  ignored by the game's loader; this tool warns when one disagrees, for the
  record, and still converts through palette.bin so the pixels match what
  the game rendered);
- index 0 - the colour key on black - becomes a fully transparent pixel;
- rows are written top-down as little-endian A8R8G8B8, which is exactly the
  iBitmap layout the game holds in memory, so the DDS loader is a copy.

Beyond the pixel files it also retargets every reference to them:

- GameData/datasets.json: ".pcx" -> ".dds" (case-preserving);
- the .img UI image headers: the TPageFiles name table is a fixed 16x16
  byte block at offset 12, and "pcx" -> "dds" is the same length, so the
  patch is in place;
- GameData/palette.bin is deleted at the end - after this conversion,
  nothing reads it.

The .pie model files are deliberately untouched: their TEXTURE names are
normalised by the IMD loader (lowercased, truncated to "page-NN"), so the
extension they carry never reaches a file name or a resource key.

This tool is one-shot by construction - it consumes the files it converts -
and is kept as the record of how the conversion was done.
"""

import os
import struct
import sys

GAME_DATA = os.path.join(os.path.dirname(__file__), "..", "GameData")

DDS_HEADER_FLAGS = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000  # CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT
DDS_PIXELFORMAT_FLAGS = 0x40 | 0x1  # DDPF_RGB | DDPF_ALPHAPIXELS
DDS_CAPS_TEXTURE = 0x1000


def load_palette(path):
    data = open(path, "rb").read()
    if len(data) != 768:
        sys.exit(f"palette.bin is {len(data)} bytes, expected 768")
    expanded = [0x00000000]  # index 0: the colour key, fully transparent
    for i in range(1, 256):
        r, g, b = data[i * 3], data[i * 3 + 1], data[i * 3 + 2]
        expanded.append(0xFF000000 | (r << 16) | (g << 8) | b)
    return data, expanded


def decode_pcx(raw, path):
    (manufacturer, version, encoding, bits_per_pixel, xmin, ymin, xmax,
     ymax) = struct.unpack_from("<BBBBhhhh", raw, 0)
    planes = raw[65]
    if manufacturer != 10 or encoding != 1:
        sys.exit(f"{path}: not an RLE PCX")
    if bits_per_pixel != 8 or planes != 1:
        sys.exit(f"{path}: not 8 bit single plane ({bits_per_pixel} bpp, {planes} planes)")

    width = xmax - xmin + 1
    height = ymax - ymin + 1
    size = width * height

    # The same decode the game's _load_image did: a flat run of size bytes,
    # ignoring bytes_per_line padding, because that is what rendered.
    pixels = bytearray(size)
    src = 128
    dest = 0
    while dest < size:
        byte = raw[src]
        src += 1
        if (byte & 0xC0) == 0xC0:
            run = byte & 0x3F
            byte = raw[src]
            src += 1
        else:
            run = 1
        run = min(run, size - dest)
        pixels[dest:dest + run] = bytes([byte]) * run
        dest += run

    embedded = None
    if len(raw) >= 769 and raw[-769] == 0x0C:
        embedded = raw[-768:]

    return width, height, pixels, embedded


def write_dds(path, width, height, indexed, expanded):
    header = struct.pack(
        "<4s7I44x2I4x5I",
        b"DDS ",
        124, DDS_HEADER_FLAGS, height, width, width * 4, 0, 0,  # header through mip count
        32, DDS_PIXELFORMAT_FLAGS,  # pixel format size and flags (FourCC is in the pad)
        32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,  # bit count and masks
    )
    header += struct.pack("<I16x", DDS_CAPS_TEXTURE)
    assert len(header) == 128, len(header)

    out = bytearray(header)
    out += b"".join(struct.pack("<I", expanded[i]) for i in indexed)
    open(path, "wb").write(bytes(out))


def patch_img_header(path):
    raw = bytearray(open(path, "rb").read())
    # IMAGEHEADER: Type[4] Version NumImages BitDepth NumTPages, then
    # TPageFiles[16][16] at offset 12 and PalFile[16] at 268.
    table = raw[12:268]
    table = table.replace(b"pcx", b"dds").replace(b"PCX", b"DDS")
    raw[12:268] = table
    open(path, "wb").write(bytes(raw))


def main():
    palette_path = os.path.join(GAME_DATA, "palette.bin")
    palette_raw, expanded = load_palette(palette_path)

    converted = 0
    mismatches = 0
    for root, _dirs, files in os.walk(GAME_DATA):
        for name in files:
            if not name.lower().endswith(".pcx"):
                continue
            src = os.path.join(root, name)
            raw = open(src, "rb").read()
            width, height, indexed, embedded = decode_pcx(raw, src)
            if embedded is not None and embedded != palette_raw:
                print(f"note: {os.path.relpath(src, GAME_DATA)} carries its own palette; "
                      f"converted through palette.bin as the game rendered it")
                mismatches += 1
            dest = src[:-4] + (".DDS" if name.endswith("PCX") else ".dds")
            write_dds(dest, width, height, indexed, expanded)
            os.remove(src)
            converted += 1

    for img in ("Images/frend.img", "Images/intfac.img"):
        patch_img_header(os.path.join(GAME_DATA, img))

    datasets = os.path.join(GAME_DATA, "datasets.json")
    text = open(datasets, "r", newline="").read()
    replaced = text.count(".pcx") + text.count(".PCX")
    text = text.replace(".pcx", ".dds").replace(".PCX", ".DDS")
    open(datasets, "w", newline="").write(text)

    os.remove(palette_path)

    print(f"converted {converted} PCX files ({mismatches} with divergent embedded palettes), "
          f"retargeted {replaced} manifest references, patched 2 .img headers, "
          f"deleted palette.bin")


if __name__ == "__main__":
    main()
