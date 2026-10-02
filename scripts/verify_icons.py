import hashlib
import os, struct

def verify_info_file(path, expected_type, expected_tool=None):
    with open(path, "rb") as f:
        data = f.read()
    magic, ver = struct.unpack(">HH", data[:4])
    assert magic == 0xe310 and ver == 1, f"{path}: Invalid Magic 0x{magic:04x}"
    do_Type = data[48]
    def_tool_flag = struct.unpack(">I", data[50:54])[0]
    
    type_names = {1: "WBDISK", 2: "WBDRAWER", 3: "WBTOOL", 4: "WBPROJECT", 5: "WBGARBAGE", 6: "WBDEVICE", 7: "WBKICK", 8: "WBAPPICON"}
    print(f"[{path}] Type: {type_names.get(do_Type, do_Type)} (raw {do_Type}), DefTool Flag: {def_tool_flag}")
    
    assert do_Type == expected_type, f"{path}: Expected type {expected_type} ({type_names.get(expected_type)}), got {do_Type} ({type_names.get(do_Type)})"
    
    if expected_tool:
        assert def_tool_flag != 0, f"{path}: Expected default tool {expected_tool}, but flag is 0"
        # Check trailing bytes
        str_len = struct.unpack(">I", data[-len(expected_tool)-5:-len(expected_tool)-1])[0]
        tool_str = data[-len(expected_tool)-1:-1].decode("latin1")
        print(f"  -> DefaultTool: '{tool_str}' (length {str_len})")
        assert tool_str == expected_tool, f"{path}: Expected tool '{expected_tool}', got '{tool_str}'"
    print(f"  -> PASS")

def verify_tool_icon_image(path):
    """11ag item 3: parse the embedded Gadget + Image of a generated
    tool icon: magic 0xE310, version 1, WBTOOL type, 32x32 image,
    depth 2, plane data inside the file."""
    import hashlib
    with open(path, "rb") as f:
        data = f.read()
    magic, ver = struct.unpack(">HH", data[:4])
    assert magic == 0xE310, f"{path}: magic 0x{magic:04x} != 0xE310"
    assert ver == 1, f"{path}: version {ver} != 1"
    assert data[48] == 3, f"{path}: do_Type at 48 is {data[48]}, want 3 (WBTOOL)"
    wh = struct.unpack_from(">I", data, 350)[0]
    w, h = wh >> 16, wh & 0xFFFF
    depth = struct.unpack_from(">H", data, 354)[0]
    idata = struct.unpack_from(">I", data, 358)[0]
    assert (w, h, depth) == (32, 32, 2), \
        f"{path}: image {w}x{h}x{depth} != 32x32x2"
    need = ((w + 15) // 16) * 2 * h * depth
    assert len(data) - idata == need, \
        f"{path}: plane data {len(data) - idata} bytes != {need}"
    md5 = hashlib.md5(data).hexdigest()
    print(f"  -> {path}: 32x32x2 WBTOOL icon, md5 {md5}")
    return md5


if __name__ == "__main__":
    verify_info_file("Install_Tolunnet.info", 4, "Installer") # WBPROJECT = 4
    verify_info_file("README.guide.info", 4, "SYS:Utilities/MultiView") # WBPROJECT = 4
    verify_info_file("TolunnetPrefs.info", 3) # WBTOOL = 3
    verify_info_file("TolunnetSetup.info", 3) # WBTOOL = 3
    verify_info_file("ci/tolunnet.info", 3) # WBTOOL = 3
    verify_info_file("Disk.info", 1) # WBDISK = 1
    verify_info_file("assets/tolunnet_drawer.info", 2) # WBDRAWER = 2
    # 11ag item 3: the three tool icons must parse AND be distinct.
    # The strict 32x32x2 dims check applies to the two GENERATED
    # icons; ci/tolunnet.info is the existing logo (own layout).
    md5s = (verify_tool_icon_image("TolunnetSetup.info"),
            verify_tool_icon_image("TolunnetPrefs.info"),
            hashlib.md5(open("ci/tolunnet.info", "rb").read()).hexdigest())
    assert len(set(md5s)) == 3, \
        f"the three tool icons are not distinct: {md5s}"
    print("  -> three distinct tool icons confirmed")
    print("\nALL INFO FILES VALIDATED SUCCESSFULLY AGAINST WORKBENCH.H!")
