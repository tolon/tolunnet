#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""make_icons.py — three distinct Workbench tool icons for tolunnet
(11ag item 3). Pure Python, stdlib only.

Writes TolunnetSetup.info and TolunnetPrefs.info as fully valid
big-endian DiskObject WBTOOL icons (magic 0xE310, version 1, 32x32,
2 planar bitplanes = the classic 4-colour Workbench icon, 630 bytes
- the same size class as the icons it replaces). ci/tolunnet.info
(the drawer/tool logo) is left untouched, so the three files end up
with three distinct md5s.

Glyphs: Setup = an "S" mark on a plate, Prefs = three slider tracks.
Geometry/palette reuse: the canonical Workbench tool-icon geometry
(32x32, 2 planes, workbench pens 1/2/3) - the same class and byte
size as the current 630-byte icons; no new colours are introduced.

--png out.png <setup|prefs|tolunnet> renders the glyph as a PNG
(zlib stdlib) so the owner can eyeball it.
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


def build_icon(glyph):
    planes = glyph_planes(glyph)
    data = bytearray(TOTAL)

    def u16(off, v): struct.pack_into(">H", data, off, v)
    def u32(off, v): struct.pack_into(">I", data, off, v)

    u16(0, 0xE310)                       # magic
    u16(2, 1)                            # version 1
    # embedded struct Gadget (42 bytes, file form), offsets 4..45
    u32(4, 0)                            # NextGadget
    u32(8, 0)                            # LeftTop (left<<16 | top)
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
    data[46] = 0                         # pad
    data[47] = 0                         # pad
    data[48] = TYPE_TOOL                 # do_Type (verify_icons reads 48)
    data[49] = 0                         # pad
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
    # plane data 374..629, row-major, plane 0 first
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


def blank():
    return [[False] * W for _ in range(H)]


def pen(bits, x0, y0, x1, y1, which):
    for y in range(max(0, y0), min(H - 1, y1) + 1):
        for x in range(max(0, x0), min(W - 1, x1) + 1):
            if which & 1:
                bits[0][y][x] = True
            if which & 2:
                bits[1][y][x] = True


def glyph_planes(glyph):
    """Return the two plane pixel maps. Pen 1 = plane0 only, pen 2 =
    plane1 only, pen 3 = both (workbench pens 1/2/3)."""
    p0, p1 = blank(), blank()
    bits = (p0, p1)

    if glyph == "setup":
        pen(bits, 4, 4, 27, 27, 1)              # white plate
        pen(bits, 4, 4, 27, 4, 2)               # black border
        pen(bits, 4, 27, 27, 27, 2)
        pen(bits, 4, 4, 4, 27, 2)
        pen(bits, 27, 4, 27, 27, 2)
        pen(bits, 21, 8, 24, 11, 2)             # S: top bar
        pen(bits, 18, 14, 24, 17, 2)            # S: middle bar
        pen(bits, 18, 20, 21, 23, 2)            # S: bottom bar
        pen(bits, 8, 8, 12, 11, 2)              # wrench-like stems
        pen(bits, 8, 20, 12, 23, 2)
        pen(bits, 8, 11, 12, 20, 1)             # stem highlight
    elif glyph == "prefs":
        for y in (8, 15, 22):                   # slider tracks
            pen(bits, 5, y, 26, y + 2, 2)
            pen(bits, 6, y + 1, 25, y + 1, 1)   # groove
        pen(bits, 10, 7, 13, 11, 3)             # knob 1
        pen(bits, 17, 14, 20, 18, 3)            # knob 2
        pen(bits, 9, 21, 12, 25, 3)             # knob 3
    elif glyph == "tolunnet":
        # the logo mark (kept simple; the shipped ci/tolunnet.info
        # itself stays untouched)
        pen(bits, 6, 6, 25, 25, 2)
        pen(bits, 8, 8, 23, 23, 1)
        pen(bits, 13, 13, 18, 18, 2)
    else:
        raise SystemExit("unknown glyph %r" % glyph)
    return [p0, p1]


def write_png(path, glyph):
    """Render the glyph (pen indices 0..3) as a tiny indexed PNG."""
    p0, p1 = glyph_planes(glyph)
    raw = b""
    for y in range(H):
        row = bytes((2 if p1[y][x] else 1) if (p0[y][x] or p1[y][x]) else 0
                    for x in range(W))
        raw += b"\x00" + row          # filter 0 per scanline
    pal = bytes((0x66, 0x88, 0xBB,   # 0: workbench blue
                 0xFF, 0xFF, 0xFF,   # 1: white
                 0x00, 0x00, 0x00,   # 2: black
                 0x66, 0x88, 0xBB))  # 3: blue

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + \
            struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", pal)
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main(argv):
    if argv[1:2] == ["--png"]:
        write_png(argv[2], argv[3])
        print("make_icons: wrote %s (%s)" % (argv[2], argv[3]))
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
