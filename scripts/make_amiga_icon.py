#!/usr/bin/env python3
"""
make_amiga_icon.py — Convert a logo image into authentic AmigaOS .info icons
and IFF-ILBM graphic files.

Supports:
- Standard 4-color (2 bitplanes) AmigaOS 2.x/3.x DiskObject format (magic 0xE310)
- Fully compatible with standard 4-colour Workbench screens (PlanePick=3, Depth=2)
- Tool, Drawer, and Project icon types with 16 KB stack size
- IFF-ILBM image export for Amiga MultiView
"""

import sys
import struct
from PIL import Image

# Standard Workbench 3.x 4-colour palette (Opal / Topaz standard screen)
AMIGA_WB3_4COLOR_PALETTE = [
    (170, 170, 170),  # 0: Grey (Workbench background)
    (0,   0,   0),    # 1: Black (Outline / Shadow)
    (255, 255, 255),  # 2: White (Highlights)
    (0,   85,  170),  # 3: Blue (Amiga Blue Accent)
]

# Amiga DiskObject types (per <workbench/workbench.h>)
WBDISK    = 1
WBDRAWER  = 2
WBTOOL    = 3
WBPROJECT = 4
WBGARBAGE = 5
WBDEVICE  = 6
WBKICK    = 7
WBAPPICON = 8

def quantize_image(img, palette):
    """Map RGB image to palette indices."""
    img = img.convert("RGBA")
    w, h = img.size
    px = img.load()
    indices = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 64:
                indices.append(0) # background
                continue
            # Euclidean color distance
            best_idx = 0
            best_dist = 100000000
            for i, (pr, pg, pb) in enumerate(palette):
                dist = (r - pr)**2 + (g - pg)**2 + (b - pb)**2
                if dist < best_dist:
                    best_dist = dist
                    best_idx = i
            indices.append(best_idx)
    return indices

def planar_encode(indices, w, h, nplanes):
    """Convert chunky pixel indices to planar bitplane data."""
    row_bytes = (w + 15) // 16 * 2  # 16-bit word aligned
    planes = [bytearray(row_bytes * h) for _ in range(nplanes)]
    
    for y in range(h):
        for x in range(w):
            idx = indices[y * w + x]
            byte_offset = y * row_bytes + (x >> 3)
            bit = 7 - (x & 7)
            for p in range(nplanes):
                if idx & (1 << p):
                    planes[p][byte_offset] |= (1 << bit)
    
    # Concatenate bitplane data (plane 0, plane 1, ...)
    res = bytearray()
    for p in range(nplanes):
        res += planes[p]
    return bytes(res)

def create_amiga_diskobject(indices, w, h, nplanes=2, icon_type=WBTOOL, default_tool=None, tooltypes=None, pos_x=None, pos_y=None, stack_size=16384):
    """
    Construct a standard Commodore AmigaOS DiskObject (.info file).
    """
    row_words = (w + 15) // 16
    plane_size_words = row_words * h
    bitplane_data = planar_encode(indices, w, h, nplanes)
    
    # Complement state for selected image
    sel_indices = [(3 - idx) if idx < 4 else 0 for idx in indices]
    sel_bitplane_data = planar_encode(sel_indices, w, h, nplanes)
    
    buf = bytearray()
    
    # Header
    buf += struct.pack(">HH", 0xE310, 1) # do_Magic, do_Version
    
    # struct Gadget (44 bytes)
    # NextGadget(4), LeftEdge(2), TopEdge(2), Width(2), Height(2), Flags(2), Activation(2), GadgetType(2), GadgetRender(4), SelectRender(4), GadgetText(4), MutualExclude(4), SpecialInfo(4), GadgetID(2), UserData(4)
    buf += struct.pack(">IhhhhHHHIIIIIHI",
                       0,          # NextGadget = NULL
                       0, 0,       # LeftEdge, TopEdge
                       w, h,       # Width, Height
                       0x0006,     # GFLG_GADGIMAGE | GFLG_GADGHIMAGE
                       0x0001,     # GACT_RELVERIFY
                       0x0001,     # GTYP_BOOLGADGET
                       1,          # GadgetRender (pointer flag = present)
                       1,          # SelectRender (pointer flag = present)
                       0,          # GadgetText = NULL
                       0,          # MutualExclude = 0
                       0,          # SpecialInfo = NULL
                       0,          # GadgetID = 0
                       0)          # UserData = NULL
    
    # DiskObject fields
    has_drawer = 1 if (icon_type in (WBDISK, WBDRAWER)) else 0
    cx = pos_x if pos_x is not None else -2147483648
    cy = pos_y if pos_y is not None else -2147483648
    buf += struct.pack(">BB", icon_type, 0) # do_Type, pad
    buf += struct.pack(">I", 1 if default_tool else 0) # do_DefaultTool present flag
    buf += struct.pack(">I", 1 if tooltypes else 0)    # do_ToolTypes present flag
    buf += struct.pack(">ii", cx, cy) # do_CurrentX, do_CurrentY
    buf += struct.pack(">II", has_drawer, 0) # do_DrawerData, do_ToolWindow
    buf += struct.pack(">I", stack_size)  # do_StackSize = 16 KB
    
    # struct DrawerData (56 bytes) for WBDISK and WBDRAWER
    if has_drawer:
        nw = struct.pack(">hhhhBBIIIIIIIhhhhH",
                         50, 40, 360, 160, # LeftEdge, TopEdge, Width, Height
                         0, 1,             # DetailPen, BlockPen
                         0,                # IDCMPFlags
                         0x000F,           # Flags: WFLG_SIZEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET
                         0, 0, 0, 0, 0,    # Pointers
                         80, 50, -1, -1,   # MinWidth, MinHeight, MaxWidth, MaxHeight
                         1)                # WTYPE_WORKBENCH
        dd = nw + struct.pack(">ii", -2147483648, -2147483648)
        buf += dd
    
    # Image 1 (Normal)
    # LeftEdge(2), TopEdge(2), Width(2), Height(2), Depth(2), ImageData(4), PlanePick(1), PlaneOnOff(1), NextImage(4)
    buf += struct.pack(">hhhhhIBBI",
                       0, 0,
                       w, h,
                       nplanes,
                       1, # ImageData present
                       (1 << nplanes) - 1, # PlanePick = 0x3 for 2 planes
                       0, # PlaneOnOff
                       0) # NextImage
    
    # Bitplane data for Image 1
    buf += bitplane_data
    
    # Image 2 (Selected)
    buf += struct.pack(">hhhhhIBBI",
                       0, 0,
                       w, h,
                       nplanes,
                       1, # ImageData present
                       (1 << nplanes) - 1, # PlanePick = 0x3 for 2 planes
                       0, # PlaneOnOff
                       0) # NextImage
    
    # Bitplane data for Image 2
    buf += sel_bitplane_data
    
    # Optional DefaultTool string
    if default_tool:
        encoded = default_tool.encode("latin1") + b"\x00"
        buf += struct.pack(">I", len(encoded))
        buf += encoded
    
    # Optional ToolTypes array
    if tooltypes:
        buf += struct.pack(">I", (len(tooltypes) + 1) * 4)
        for tt in tooltypes:
            encoded = tt.encode("latin1") + b"\x00"
            buf += struct.pack(">I", len(encoded))
            buf += encoded
        buf += struct.pack(">I", 0)
    
    return bytes(buf)

def export_iff_ilbm(indices, w, h, nplanes, palette, out_path):
    """Write an authentic IFF-ILBM image readable by Amiga MultiView / DPaint."""
    row_bytes = (w + 15) // 16 * 2
    body = bytearray()
    for y in range(h):
        for p in range(nplanes):
            row_plane = bytearray(row_bytes)
            for x in range(w):
                idx = indices[y * w + x]
                if idx & (1 << p):
                    row_plane[x >> 3] |= (1 << (7 - (x & 7)))
            body += row_plane
    
    # BMHD chunk (20 bytes)
    bmhd = struct.pack(">HHhhBBBBHBBhh",
                       w, h, 0, 0,
                       nplanes, 0, 0, 0,
                       0, 10, 11, w, h)
    
    # CMAP chunk
    cmap = bytearray()
    for i in range(1 << nplanes):
        if i < len(palette):
            r, g, b = palette[i]
        else:
            r, g, b = 0, 0, 0
        cmap += struct.pack(">BBB", r, g, b)
    
    form_length = 4 + (8 + len(bmhd)) + (8 + len(cmap)) + (8 + len(body))
    with open(out_path, "wb") as f:
        f.write(b"FORM")
        f.write(struct.pack(">I", form_length))
        f.write(b"ILBM")
        f.write(b"BMHD")
        f.write(struct.pack(">I", len(bmhd)))
        f.write(bmhd)
        f.write(b"CMAP")
        f.write(struct.pack(">I", len(cmap)))
        f.write(cmap)
        f.write(b"BODY")
        f.write(struct.pack(">I", len(body)))
        f.write(body)

def main():
    src_path = sys.argv[1] if len(sys.argv) > 1 else "assets/logo.png"
    img = Image.open(src_path)
    
    # Save standard PNG
    img.save("assets/logo.png")
    print("Exported assets/logo.png")
    
    # 1. Generate 32x32 Tool Icon for tolunnet (SYS:C/tolunnet.info) - 4 color standard
    img32 = img.resize((32, 32), Image.LANCZOS)
    idx32 = quantize_image(img32, AMIGA_WB3_4COLOR_PALETTE)
    icon_tool = create_amiga_diskobject(idx32, 32, 32, nplanes=2, icon_type=WBTOOL)
    with open("ci/tolunnet.info", "wb") as f:
        f.write(icon_tool)
    with open("assets/tolunnet.info", "wb") as f:
        f.write(icon_tool)
    print("Generated ci/tolunnet.info and assets/tolunnet.info (4-color Workbench Icon)")
    
    # 2. Generate 32x32 Installer Project Icon (Install_Tolunnet.info)
    icon_inst = create_amiga_diskobject(idx32, 32, 32, nplanes=2, icon_type=WBPROJECT, default_tool="Installer")
    with open("Install_Tolunnet.info", "wb") as f:
        f.write(icon_inst)
    print("Generated Install_Tolunnet.info (Installer Project Icon)")
    
    # 3. Generate Project/Doc Icon for README.guide (README.guide.info)
    icon_doc = create_amiga_diskobject(idx32, 32, 32, nplanes=2, icon_type=WBPROJECT, default_tool="SYS:Utilities/MultiView")
    with open("README.guide.info", "wb") as f:
        f.write(icon_doc)
    print("Generated README.guide.info (AmigaGuide Project Icon)")
    
    # 4. Generate Disk.info (Volume/Floppy Disk Icon)
    icon_disk = create_amiga_diskobject(idx32, 32, 32, nplanes=2, icon_type=WBDISK)
    with open("Disk.info", "wb") as f:
        f.write(icon_disk)
    print("Generated Disk.info (Floppy Volume Icon)")
    
    # 5. Generate TolunnetPrefs.info (Workbench Preferences Tool Icon, positioned in Prefs grid)
    icon_prefs = create_amiga_diskobject(idx32, 32, 32, nplanes=2, icon_type=WBTOOL, pos_x=4, pos_y=48, stack_size=16384)
    with open("TolunnetPrefs.info", "wb") as f:
        f.write(icon_prefs)
    print("Generated TolunnetPrefs.info (Preferences Tool Icon at grid 4,48)")
    
    # 6. Generate Drawer Icon for LhA releases (tolunnet.info)
    icon_drawer = create_amiga_diskobject(idx32, 32, 32, nplanes=2, icon_type=WBDRAWER)
    with open("assets/tolunnet_drawer.info", "wb") as f:
        f.write(icon_drawer)
    print("Generated assets/tolunnet_drawer.info (Release Drawer Icon)")
    
    # 7. Generate high-resolution IFF-ILBM Logo for Amiga screens (64x64)
    img64 = img.resize((64, 64), Image.LANCZOS)
    idx64 = quantize_image(img64, AMIGA_WB3_4COLOR_PALETTE)
    export_iff_ilbm(idx64, 64, 64, 2, AMIGA_WB3_4COLOR_PALETTE, "assets/tolunnet_logo.iff")
    print("Generated assets/tolunnet_logo.iff (64x64 IFF-ILBM for Amiga MultiView)")

if __name__ == "__main__":
    main()
