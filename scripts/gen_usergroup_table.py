#!/usr/bin/env python3
"""
gen_usergroup_table.py — Generate LVO Vector Table, Assembly Stubs, and Compat Table
for usergroup.library from sfd/usergroup_lib.sfd.
"""

import os, sys, re

SFD_PATH = os.path.join(os.path.dirname(__file__), "..", "sfd", "usergroup_lib.sfd")
OUT_TABLE = os.path.join(os.path.dirname(__file__), "..", "src", "usergroup", "ug_table.gen.c")
OUT_STUBS = os.path.join(os.path.dirname(__file__), "..", "src", "usergroup", "ug_stubs.gen.s")
OUT_COMPAT = os.path.join(os.path.dirname(__file__), "..", "src", "usergroup", "ug_compat_table.gen.md")

def parse_sfd(sfd_path):
    with open(sfd_path, "r", encoding="latin1") as f:
        lines = f.readlines()

    vectors = []
    current_offset = -30
    is_varargs = False

    for line in lines:
        line = line.strip()
        if not line or line.startswith("*"):
            continue
        if line.startswith("=="):
            if line.startswith("==varargs"):
                is_varargs = True
            elif line.startswith("==reserve"):
                count = int(line.split()[1])
                for _ in range(count):
                    vectors.append({
                        "offset": current_offset,
                        "name": f"RESERVED_{abs(current_offset)}",
                        "ret": "RESERVED",
                        "args": "",
                        "raw_args": [],
                        "regs": [],
                        "reserved": True,
                        "varargs": False
                    })
                    current_offset -= 6
            continue

        match = re.match(r"^(.*?)\s*(\w+)\s*\((.*?)\)\s*\((.*?)\)$", line)
        if match:
            ret_type, func_name, args_str, regs_str = match.groups()
            if is_varargs:
                is_varargs = False
                continue

            regs = [r.strip() for r in regs_str.split(",") if r.strip()]
            raw_args = [a.strip() for a in args_str.split(",") if a.strip() and a.strip() != "VOID"]

            vectors.append({
                "offset": current_offset,
                "name": func_name,
                "ret": ret_type.strip(),
                "args": args_str.strip(),
                "raw_args": raw_args,
                "regs": regs,
                "reserved": False,
                "varargs": False
            })
            current_offset -= 6

    return vectors

def generate_ug_table(vectors, out_path):
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/*",
        " * ug_table.gen.c — Generated usergroup.library jump table.",
        " * Generated automatically by scripts/gen_usergroup_table.py from sfd/usergroup_lib.sfd.",
        " * DO NOT EDIT MANUALLY.",
        " */",
        "",
        "#include <exec/types.h>",
        "",
        "/* Standard Library Management Stubs */",
        "extern void ug_stub_open(void);",
        "extern void ug_stub_close(void);",
        "extern void ug_stub_expunge(void);",
        "extern void ug_stub_reserved(void);",
        ""
    ]

    for v in vectors:
        if not v["reserved"]:
            lines.append(f"extern void ug_stub_{v['name'].lower()}(void);")

    lines.extend([
        "",
        "const APTR g_ug_vectors[] = {",
        "    (APTR)ug_stub_open,                  /* -6   LIB_OPEN */",
        "    (APTR)ug_stub_close,                 /* -12  LIB_CLOSE */",
        "    (APTR)ug_stub_expunge,               /* -18  LIB_EXPUNGE */",
        "    (APTR)ug_stub_reserved,              /* -24  LIB_RESERVED */",
        ""
    ])

    for v in vectors:
        off = v["offset"]
        stub = f"ug_stub_{v['name'].lower()}"
        comment = f"/* {off} {v['name']} */"
        lines.append(f"    (APTR){stub:<32}, {comment}")

    lines.extend([
        "    (APTR)-1                             /* End of table marker */",
        "};",
        ""
    ])

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[gen_usergroup_table] Emitted {out_path} ({len(vectors) + 4} vectors)")

def generate_ug_stubs(vectors, out_path):
    lines = [
        "|",
        "| ug_stubs.gen.s — Generated 68k LVO Assembly Dispatch Stubs for usergroup.library",
        "| Generated automatically by scripts/gen_usergroup_table.py from sfd/usergroup_lib.sfd.",
        "|",
        "",
        "    .text",
        "    .even",
        "",
        "| --- Library Management Vectors ---",
        "    .globl _ug_stub_open",
        "_ug_stub_open:",
        "    move.l  d0,-(sp)",
        "    move.l  a6,-(sp)",
        "    jsr     _ug_lib_open",
        "    addq.l  #8,sp",
        "    move.l  d0,a0",
        "    rts",
        "",
        "    .globl _ug_stub_close",
        "_ug_stub_close:",
        "    move.l  a6,-(sp)",
        "    jsr     _ug_lib_close",
        "    addq.l  #4,sp",
        "    move.l  d0,a0",
        "    rts",
        "",
        "    .globl _ug_stub_expunge",
        "_ug_stub_expunge:",
        "    move.l  a6,-(sp)",
        "    jsr     _ug_lib_expunge",
        "    addq.l  #4,sp",
        "    move.l  d0,a0",
        "    rts",
        "",
        "    .globl _ug_stub_reserved",
        "_ug_stub_reserved:",
        "    moveq   #0,d0",
        "    suba.l  a0,a0",
        "    rts",
        ""
    ]

    for v in vectors:
        name = v["name"]
        lower = name.lower()
        regs = v["regs"]
        ret = v["ret"]
        is_ptr = ("*" in ret) or (ret in ["STRPTR", "APTR", "UBYTE *"])

        target = f"_ug_lvo_{lower}"
        stack_bytes = (len(regs) + 1) * 4

        lines.extend([
            f"| {v['offset']}: {v['name']}({v['args']})",
            f"    .globl _ug_stub_{lower}",
            f"_ug_stub_{lower}:"
        ])

        # Push a6 (library base) first, then registers in reverse order
        lines.append("    move.l  a6,-(sp)")
        for r in reversed(regs):
            lines.append(f"    move.l  {r},-(sp)")

        lines.append(f"    jsr     {target}")

        if stack_bytes <= 8:
            lines.append(f"    addq.l  #{stack_bytes},sp")
        else:
            lines.append(f"    lea     {stack_bytes}(sp),sp")

        if is_ptr:
            lines.append("    move.l  d0,a0")
        lines.append("    rts")
        lines.append("")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[gen_usergroup_table] Emitted {out_path} ({len(vectors)} stubs)")

def generate_ug_compat_table(vectors, out_path):
    lines = [
        "# usergroup.library Compatibility Matrix",
        "",
        "Generated automatically by `scripts/gen_usergroup_table.py` from `sfd/usergroup_lib.sfd`.",
        "",
        "> Note: `crypt()` uses an internal FNV hash, not Unix DES; existing AmiTCP passwd files are not compatible.",
        "",
        "| Offset | Function | Signature | Status | Implementation Details |",
        "|--------|----------|-----------|--------|------------------------|"
    ]

    for v in vectors:
        off = v["offset"]
        name = v["name"]
        sig = f"{v['ret']} {name}({v['args']})"
        if name == "crypt":
            details = "FNV-based hash (internal; existing AmiTCP passwd files are not compatible)"
        elif name == "getpass":
            details = "In-memory stub (returns empty string without prompt)"
        elif name in ("setutent", "endutent"):
            details = "No-op stub"
        elif name == "getutent":
            details = "Fixed root/console record"
        elif name in ("getlastlog", "setlastlog"):
            details = "In-memory tracking only"
        else:
            details = "In-memory DB + file reader fallback"
        lines.append(f"| `{off}` | `{name}` | `{sig}` | **BUILT** | {details} |")

    lines.append("")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[gen_usergroup_table] Emitted {out_path}")

def main():
    if not os.path.isfile(SFD_PATH):
        print(f"Error: SFD not found at {SFD_PATH}", file=sys.stderr)
        sys.exit(1)
    vectors = parse_sfd(SFD_PATH)
    generate_ug_table(vectors, OUT_TABLE)
    generate_ug_stubs(vectors, OUT_STUBS)
    generate_ug_compat_table(vectors, OUT_COMPAT)

if __name__ == "__main__":
    main()
