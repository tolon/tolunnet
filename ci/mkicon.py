#!/usr/bin/env python3
"""
mkicon.py — render a PNG logo into an AmigaOS 1.x/2.x/3.x classic icon (.info).

Produces a 32x32 (default) 4-colour (2 bitplanes) tool icon in the Amiga IFF
FORM format that Workbench 1.3+ understands. This is the same icon format
Workbench itself uses for drawers and tools — no Glow/NewIcon extension, so it
renders on the broadest range of OS versions (Tier-1..3).

The palette is a fixed 4-colour scheme (Amiga-1,2,3,4 register colours by
default) that gives a clean, recognisable icon on stock Workbench.

Usage:
    python mkicon.py <logo.png> <out.info> [--size 32] [--type tool|drawer|project]

The script:
  1. Loads the PNG, centre-crops to a square, scales to NxN.
  2. Quantises to 4 colours via an Amiga-friendly palette.
  3. Builds the planar bitmaps (2 bitplanes, MSB-first per the Amiga).
  4. Writes an IFF FORM icon: FORM + FACE? no — the Workbench icon is:
        FORM....ICN#
          FACE   (icon header: type, max-drawers, current-drawer, drawer-type)
          BMHD   (bitmap header: w,h,x,y,nPlanes,masking,compression,pad,transp)
          CMAP   (4 RGB triples)
          BODY   (planar data, interleaved)
          tooltypes (optional, empty)
"""
import struct
import sys
from PIL import Image

# Default 4-colour palette: classic Amiga Workbench-ish.
# (R,G,B) each 0..255; we write them to CMAP as 0..15 nibbles (>>4).
DEFAULT_PALETTE = [
    (170, 173, 170),  # colour 0: grey (window/background)
    (0,   0,   0),    # colour 1: black (outlines)
    (255, 255, 255),  # colour 2: white (highlights)
    (115, 95,  185),  # colour 3: bluish-purple (accent)
]

# WB icon types
WBTOOL    = 259   # executable tool
WBDRAWER  = 1
WBPROJECT = 3

TYPE_MAP = {"tool": WBTOOL, "drawer": WBDRAWER, "project": WBPROJECT}


def quantize_to_palette(img, palette):
    """Snap each pixel to the nearest palette colour; return list of palette idx."""
    # Force RGBA so we can drop alpha via simple threshold.
    img = img.convert("RGBA")
    px = img.load()
    w, h = img.size
    out = [0] * (w * h)
    transparent_idx = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                out[y * w + x] = transparent_idx
                continue
            # nearest palette colour
            best = 0
            bestd = 1 << 30
            for i, (pr, pg, pb) in enumerate(palette):
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if d < bestd:
                    bestd = d
                    best = i
            out[y * w + x] = best
    return out


def build_planar(idx, w, h, nplanes):
    """Convert a palette-indexed image to Amiga interleaved planar bytes.
    Bitplane p holds bit p of each pixel; rows are interleaved plane0-row,
    plane1-row, ... (Amiga's INTERLEAVED / contig format). MSB first per byte."""
    rowsize = (w + 15) // 16 * 2  # bytes per row per plane (word-aligned)
    planes = [[0] * (rowsize * h) for _ in range(nplanes)]
    for y in range(h):
        for x in range(w):
            v = idx[y * w + x]
            byte_x = x >> 3
            bit = 7 - (x & 7)
            for p in range(nplanes):
                if v & (1 << p):
                    planes[p][y * rowsize + byte_x] |= (1 << bit)
    # Interleave: for each row, emit plane0 row, plane1 row, ...
    out = bytearray()
    for y in range(h):
        for p in range(nplanes):
            row = planes[p][y * rowsize:(y + 1) * rowsize]
            out += bytes(row)
    return bytes(out)


def iff_chunk(tag, data):
    tag = tag.encode("ascii") if isinstance(tag, str) else tag
    chunk = tag + struct.pack(">I", len(data)) + data
    if len(data) & 1:
        chunk += b"\x00"  # IFF chunks are even-aligned
    return chunk


def write_icon(path, idx, w, h, nplanes, palette, icon_type):
    face = struct.pack(">HHHBB",
                       icon_type,            # ic_Type (WBTOOL etc.)
                       1,                    # ic_MaxDrawer (unused for tool)
                       0,                    # ic_CurrentDrawer
                       1,                    # drawer-type flag (viewbyIcon)
                       0)                    # padding
    # BMHD (20 bytes): UWORD w,h; WORD x,y; UWORD nPlanes; UBYTE masking,
    # compression, pad; WORD transparentColor; UBYTE xAspect,yAspect;
    # WORD pageWidth,pageHeight.
    bmhd = struct.pack(">HHhhHBBBhBBhh",
                      w, h,
                      0, 0,                  # x, y
                      nplanes,
                      0,                     # masking: mskNone
                      0,                     # compression: cmpNone
                      0,                     # pad
                      0,                     # transparent colour
                      1, 1,                  # xAspect, yAspect
                      320, 200)              # pageWidth, pageHeight (screen)
    # CMAP: nplanes colours as 3-byte RGB (0..255)
    cmap = bytearray()
    for (r, g, b) in palette[: (1 << nplanes)]:
        cmap += bytes((r & 0xF0, g & 0xF0, b & 0xF0))
    body = build_planar(idx, w, h, nplanes)

    form = b"ICN#"
    form += iff_chunk("FACE", face)
    form += iff_chunk("BMHD", bmhd)
    form += iff_chunk("CMAP", bytes(cmap))
    form += iff_chunk("BODY", body)

    with open(path, "wb") as f:
        f.write(b"FORM")
        f.write(struct.pack(">I", len(form)))
        f.write(form)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src = sys.argv[1]
    dst = sys.argv[2]
    size = 32
    itype = "tool"
    for i in range(3, len(sys.argv)):
        a = sys.argv[i]
        if a == "--size" and i + 1 < len(sys.argv):
            size = int(sys.argv[i + 1])
        elif a == "--type" and i + 1 < len(sys.argv):
            itype = sys.argv[i + 1]
    if itype not in TYPE_MAP:
        sys.exit("unknown type: " + itype)

    img = Image.open(src)
    # Centre-crop to square.
    w, h = img.size
    s = min(w, h)
    img = img.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))
    img = img.resize((size, size), Image.LANCZOS)

    idx = quantize_to_palette(img, DEFAULT_PALETTE)
    write_icon(dst, idx, size, size, 2, DEFAULT_PALETTE, TYPE_MAP[itype])
    print("wrote", dst, size + "x" + str(size), "4-colour", itype, "icon")


if __name__ == "__main__":
    main()
