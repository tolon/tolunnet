#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""make_icons.py — three distinct Workbench tool icons for tolunnet
(11ah item 2: glyphs drawn from reviewable ASCII-art bitmaps).
Pure Python, stdlib only.

Each glyph is a 32x32 character map, one char per pixel:
    '.' pen 0 (transparent, workbench blue shows)
    'W' pen 1 (white)
    'K' pen 2 (black)
    'B' pen 3 (blue)
Setup = a clearly readable letter S on a white plate (black border).
Prefs = three slider tracks with knobs. tolunnet = a "T" network-node
mark (nodes on the bar ends and a stem), not a solid block.

Writes TolunnetSetup.info and TolunnetPrefs.info as valid big-endian
DiskObject WBTOOL icons (magic 0xE310, version 1, 32x32, 2 planar
bitplanes, 630 bytes - same size class as before).
ci/tolunnet.info (the existing logo file) is left untouched.

--png-scale=N --png out.png <setup|prefs|tolunnet> renders the glyph
as a nearest-neighbour scaled PNG (default scale 8 -> 256x256).
"""

import struct
import sys
import zlib

W, H, DEPTH = 32, 32, 2
ROWBYTES = (W + 15) // 16 * 2          # 4 bytes per row
DATA_OFF = 374                          # plane data starts here
DATA_BYTES = ROWBYTES * H * DEPTH       # 256
TOTAL = DATA_OFF + DATA_BYTES           # 630

TYPE_TOOL = 3

D = "." * 32                            # full transparent row
BORDER = "..." + "K" * 26 + "..."       # plate top/bottom border
PLATE = "..." + "K" + "W" * 24 + "K" + "..."  # plain plate row

# ---------------------------------------------------------------------
# The glyphs, reviewable in the source. Each row is exactly 32 chars:
# 3 dots + 26-char plate field + 3 dots (or plain dot rows).

GLYPH_SETUP_ROWS = (
    [D, D, BORDER]
    + [PLATE] * 3
    # S top bar (interior: 5W 14K 5W)
    + ["..." + "K" + "W" * 5 + "K" * 14 + "W" * 5 + "K" + "..."] * 3
    # S left stem (interior: 3K 21W)
    + ["..." + "K" + "K" * 3 + "W" * 21 + "K" + "..."] * 5
    # S middle bar
    + ["..." + "K" + "W" * 5 + "K" * 14 + "W" * 5 + "K" + "..."] * 3
    # S right stem (interior: 19W 3K 2W)
    + ["..." + "K" + "W" * 19 + "K" * 3 + "W" * 2 + "K" + "..."] * 5
    # S bottom bar
    + ["..." + "K" + "W" * 5 + "K" * 14 + "W" * 5 + "K" + "..."] * 3
    + [PLATE] * 2
    + [BORDER]
    + [D] * 4
)

GLYPH_PREFS_ROWS = (
    [D, D, BORDER]
    + [PLATE] * 5
    # slider track 1 (full interior bar)
    + ["..." + "K" + "K" * 24 + "K" + "..."]
    + ["..." + "K" + "W" * 24 + "K" + "..."]
    # knob 1 (blue block on the track)
    + ["..." + "K" + "W" * 4 + "B" * 4 + "W" * 16 + "K" + "..."] * 2
    + [PLATE] * 2
    # slider track 2
    + ["..." + "K" + "K" * 24 + "K" + "..."]
    + ["..." + "K" + "W" * 24 + "K" + "..."]
    # knob 2
    + ["..." + "K" + "W" * 12 + "B" * 4 + "W" * 8 + "K" + "..."] * 2
    + [PLATE] * 3
    # slider track 3
    + ["..." + "K" + "K" * 24 + "K" + "..."]
    + ["..." + "K" + "W" * 24 + "K" + "..."]
    # knob 3
    + ["..." + "K" + "W" * 6 + "B" * 4 + "W" * 14 + "K" + "..."] * 2
    + [PLATE] * 2
    + [BORDER]
    + [D] * 4
)

GLYPH_TOLUNNET_ROWS = (
    [D, D, BORDER]
    + [PLATE] * 2
    # bar-end nodes (blue blocks)
    + ["..." + "K" + "B" * 4 + "W" * 12 + "B" * 4 + "W" * 4 + "K" + "..."]
    # T top bar
    + ["..." + "K" + "W" * 4 + "K" * 16 + "W" * 4 + "K" + "..."] * 3
    # T stem (centre), wires to the nodes
    + ["..." + "K" + "W" * 10 + "K" * 4 + "W" * 10 + "K" + "..."] * 17
    + [PLATE]
    + [BORDER]
    + [D] * 4
)

GLYPHS = {
    "setup": "\n".join(GLYPH_SETUP_ROWS),
    "prefs": "\n".join(GLYPH_PREFS_ROWS),
    "tolunnet": "\n".join(GLYPH_TOLUNNET_ROWS),
}

# ---------------------------------------------------------------------


def art_planes(art):
    """Turn the ASCII art into two 32x32 plane maps."""
    rows = [r for r in art.split("\n") if r.strip()]
    assert len(rows) == H, "glyph art must have %d rows, got %d" % (
        H, len(rows))
    planes = [[[False] * W for _ in range(H)] for _ in range(DEPTH)]
    for y, row in enumerate(rows):
        assert len(row) == W, "glyph row %d has %d chars" % (y, len(row))
        for x, ch in enumerate(row):
            if ch == "W":
                planes[0][y][x] = True
            elif ch == "K":
                planes[1][y][x] = True
            elif ch == "B":
                planes[0][y][x] = True
                planes[1][y][x] = True
    return planes


def build_icon(glyph):
    planes = art_planes(GLYPHS[glyph])
    data = bytearray(TOTAL)

    def u16(off, v): struct.pack_into(">H", data, off, v)
    def u32(off, v): struct.pack_into(">I", data, off, v)

    u16(0, 0xE310)                       # magic
    u16(2, 1)                            # version 1
    # embedded struct Gadget (42 bytes, file form), offsets 4..45
    u32(4, 0)                            # NextGadget
    u32(8, 0)                            # LeftTop
    u32(12, (W << 16) | H)               # WidthHeight
    u16(16, 0x0001)                      # Flags: GADGIMAGE
    u16(18, 0x0001)                      # Activation: RELVERIFY
    u16(20, 0)                           # GadgetType: BOOL
    u32(22, DATA_OFF)                    # GadgetRender -> plane data
    u32(26, 0)                           # SelectRender
    u32(30, 0)                           # GadgetText
    u32(34, 0)                           # MutualExclude
    u32(38, 0)                           # SpecialInfo
    u16(42, 0)                           # GadgetID
    u16(44, 0)                           # UserData
    data[48] = TYPE_TOOL                 # do_Type (verify_icons reads 48)
    # DefaultTool 50..193: empty (zeros)
    # ToolTypes 194..337: the classic 0xFF "no tooltypes" fill
    for i in range(194, 338):
        data[i] = 0xFF
    u32(338, 0xFFFFFFFF)                 # CurrentX: NO_POSITION
    u32(342, 0xFFFFFFFF)                 # CurrentY: NO_POSITION
    # struct Image 346..367
    u32(346, 0)                          # LeftTop
    u32(350, (W << 16) | H)              # WidthHeight
    u16(354, DEPTH)                      # Depth
    u16(356, 0)                          # pad
    u32(358, DATA_OFF)                   # ImageData
    data[362] = 0x03                     # PlanePick
    data[363] = 0x00                     # PlaneOnOff
    u32(364, 0)                          # NextImage
    u32(368, 0)                          # ToolWindow
    u32(372, 0)                          # SubToolImage
    # plane data 374..629
    off = DATA_OFF
    for p in range(DEPTH):
        bits = planes[p]
        for y in range(H):
            row = 0
            for x in range(W):
                if bits[y][x]:
                    row |= 0x8000 >> x
            struct.pack_into(">H", data, off + y * ROWBYTES, row)
        off += H * ROWBYTES
    return bytes(data)


def write_png(path, glyph, scale):
    """Render the glyph as an indexed PNG, nearest-neighbour x scale."""
    planes = art_planes(GLYPHS[glyph])
    pw, ph = W * scale, H * scale
    raw = b""
    for y in range(ph):
        row = bytes(
            (2 if planes[1][y // scale][x // scale] else
             (1 if planes[0][y // scale][x // scale] else 0))
            for x in range(pw))
        raw += b"\x00" + row
    pal = bytes((0x66, 0x88, 0xBB,   # 0: workbench blue
                 0xFF, 0xFF, 0xFF,   # 1: white
                 0x00, 0x00, 0x00,   # 2: black
                 0x66, 0x88, 0xBB))  # 3: blue

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + \
            struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", pw, ph, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", pal)
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main(argv):
    args = argv[1:]
    scale = 8
    if args and args[0].startswith("--png-scale="):
        scale = int(args[0].split("=", 1)[1])
        args = args[1:]
    if args and args[0] == "--png":
        write_png(args[1], args[2], scale)
        print("make_icons: wrote %s (%s, x%d)" % (args[1], args[2], scale))
        return 0
    for path, glyph in (("TolunnetSetup.info", "setup"),
                        ("TolunnetPrefs.info", "prefs")):
        blob = build_icon(glyph)
        assert len(blob) == TOTAL
        with open(path, "wb") as f:
            f.write(blob)
        print("make_icons: wrote %s (%s glyph, %d bytes)"
              % (path, glyph, len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
