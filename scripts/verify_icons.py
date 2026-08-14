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

if __name__ == "__main__":
    verify_info_file("Install_Tolunnet.info", 4, "Installer") # WBPROJECT = 4
    verify_info_file("README.guide.info", 4, "SYS:Utilities/MultiView") # WBPROJECT = 4
    verify_info_file("TolunnetPrefs.info", 3) # WBTOOL = 3
    verify_info_file("ci/tolunnet.info", 3) # WBTOOL = 3
    verify_info_file("Disk.info", 1) # WBDISK = 1
    verify_info_file("assets/tolunnet_drawer.info", 2) # WBDRAWER = 2
    print("\nALL INFO FILES VALIDATED SUCCESSFULLY AGAINST WORKBENCH.H!")
