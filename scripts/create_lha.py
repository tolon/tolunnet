#!/usr/bin/env python3
"""
create_lha.py — Create authentic Amiga LhA archives (Level 0 / Level 1).
"""

import os
import sys
import struct
import time

def crc16_lha(data: bytes, poly=0xA001) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
    return crc & 0xFFFF

def dos_datetime():
    """Returns MS-DOS format 32-bit date/time integer."""
    t = time.localtime()
    d = ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    s = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec >> 1)
    return (d << 16) | s

def build_lha_header_level0(rel_path: str, data: bytes) -> bytes:
    """
    Standard Level 0 LhA header (-lh0- method: stored).
    Universally supported by all Amiga LhA versions.
    """
    rel_path_amiga = rel_path.replace("\\", "/")
    filename_bytes = rel_path_amiga.encode("latin1")
    filename_len = len(filename_bytes)
    
    orig_size = len(data)
    comp_size = len(data)
    data_crc = crc16_lha(data)
    dt = dos_datetime()
    
    # Body of header (without size and checksum bytes)
    # size = 5 (method) + 4 (comp) + 4 (orig) + 4 (time) + 1 (attr) + 1 (lvl) + 1 (fn_len) + fn_len + 2 (crc) = 22 + fn_len
    hdr = bytearray()
    hdr += b"-lh0-"
    hdr += struct.pack("<I", comp_size)
    hdr += struct.pack("<I", orig_size)
    hdr += struct.pack("<I", dt)
    hdr += b"\x20"  # Attribute: file
    hdr += b"\x00"  # Level: 0
    hdr += struct.pack("<B", filename_len)
    hdr += filename_bytes
    hdr += struct.pack("<H", data_crc)
    
    hdr_size = len(hdr)
    hdr_checksum = sum(hdr) & 0xFF
    
    return bytes([hdr_size, hdr_checksum]) + bytes(hdr)

def pack_directory_to_lha(root_dir: str, lha_output_path: str):
    """Recursively pack a directory into an Amiga LhA archive."""
    with open(lha_output_path, "wb") as out_f:
        for root, dirs, files in os.walk(root_dir):
            for file in files:
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, os.path.dirname(root_dir))
                with open(full_path, "rb") as in_f:
                    file_data = in_f.read()
                
                header = build_lha_header_level0(rel_path, file_data)
                out_f.write(header)
                out_f.write(file_data)
        # End of archive
        out_f.write(b"\x00")
    print(f"LhA archive created: {lha_output_path} ({os.path.getsize(lha_output_path)} bytes)")

def main():
    if len(sys.argv) < 3:
        print("Usage: create_lha.py <source_dir> <output_archive.lha>")
        sys.exit(1)
    
    src_dir = sys.argv[1]
    out_archive = sys.argv[2]
    pack_directory_to_lha(src_dir, out_archive)

if __name__ == "__main__":
    main()
