#!/usr/bin/env python3
"""
gen_lvo_table.py — Complete SFD Vector Table, Assembly Stubs, and Honest Stubs Generator (Round 3 §D).

Parses authentic Commodore/Roadshow SFD (Simple Function Definition), computes
exact LVO offsets based on ==bias, tracks ==varargs twins sharing LVO slots,
and generates:
  1. src/lib/lib_table.gen.c (vector table array g_lib_vectors)
  2. src/lib/lib_stubs.gen.s (68k assembly dispatch stubs)
  3. src/lib/lib_unimpl.c   (honest C stubs with exact signatures & return codes)
  4. src/lib/lib_compat_table.gen.md — the single-source compatibility table
     (ANX-02): BUILT/BROKEN/STUB derived from lib_vectors.c + ipc_dispatch.c,
     PASS column from the latest bench TAP; updates the README lvo-stats block
"""

import os, sys, re

IMPLEMENTED_FUNCS = {
    # Core socket calls
    "socket", "bind", "listen", "accept", "connect",
    "sendto", "send", "recvfrom", "recv", "shutdown",
    "setsockopt", "getsockopt", "getsockname", "getpeername",
    "ioctlsocket", "closesocket", "waitselect", "setsocketsignals",
    "getdtablesize", "obtainsocket", "releasesocket", "releasecopyofsocket",
    "errno", "seterrnoptr",
    "dup2socket", "sendmsg", "recvmsg", "gethostname", "gethostid", "socketbasetaglist",
    "getsocketevents",
    # Resolver and db
    "gethostbyname", "gethostbyaddr",
    "getservbyname", "getservbyport",
    "getprotobyname", "getprotobynumber",
    # Address conversions
    "inet_ntoa", "inet_addr", "inet_lnaof", "inet_netof", "inet_makeaddr", "inet_network",
    # §D.5 implementations
    "inet_aton", "inet_ntop", "inet_pton",
    "in_localaddr", "in_canforward",
    "getdefaultdomainname", "setdefaultdomainname",
    "getnetbyname", "getnetbyaddr",
    "getnetent", "setnetent", "endnetent",
    "getprotoent", "setprotoent", "endprotoent",
    "getservent", "setservent", "endservent",
    "gethostbyname_r", "gethostbyaddr_r",
    "vsyslog",
    # Socket parking & Server API (Round 4 §C6)
    "processisserver", "obtainserversocket"
}

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

# --------------------------------------------------------------------------
# ANX-02: source- and bench-derived compatibility status.
#
# The compat table is generated from three sources:
#   (a) sfd/bsdsocket_lib.sfd        — LVO offset + exact prototype
#   (b) src/lib/lib_vectors.c +
#       src/task/ipc_dispatch.c      — BUILT / STUB / BROKEN:
#               no tn_lvo_<name> body          -> STUB (honest stub)
#               body marshals TN_IPC_CMD_x and the daemon dispatch table
#               has no handler for x           -> BROKEN (ENOSYS at runtime)
#               otherwise                      -> BUILT
#   (c) latest docs/bench-logs/*/68000/conformance.log TAP — PASS column:
#               which green tc_* tests exercise the function
# --------------------------------------------------------------------------

def parse_lib_vectors(path):
    """Map each tn_lvo_<name> definition in lib_vectors.c to the set of
    TN_IPC_CMD_* constants referenced inside its body."""
    per_func = {}
    cur = None
    # A definition line starts with the return type immediately followed by
    # the function name; intra-body calls ("x = tn_lvo_foo(...)") never match.
    pat_def = re.compile(r"^(?:struct\s+[A-Za-z_]\w*|[A-Za-z_]\w*)\s*\*?\s*tn_lvo_(\w+)\s*\(")
    pat_cmd = re.compile(r"TN_IPC_CMD_(\w+)")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat_def.match(line.lstrip())
            if m:
                cur = m.group(1)
                per_func.setdefault(cur, set())
                continue
            if cur is not None:
                for c in pat_cmd.findall(line):
                    per_func[cur].add(c)
    return per_func

def parse_ipc_dispatch(path):
    """Map TN_IPC_CMD_<name> -> handler function name, or None when the
    daemon's dispatch table holds a NULL handler (ENOSYS path)."""
    handlers = {}
    pat = re.compile(r"\[\s*TN_IPC_CMD_(\w+)\s*\]\s*=\s*\{\s*TN_IPC_CMD_\w+\s*,\s*(tn_ipc_cmd_\w+|NULL)")
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m:
                handlers[m.group(1)] = None if m.group(2) == "NULL" else m.group(2)
    return handlers

# tc_* bench test -> API functions it exercises (PASS column evidence).
# Only tests that are GREEN in the latest bench earn the label.
TC_COVERS = {
    "tc_socket_types":            ["socket"],
    "tc_bind_udp":                ["bind"],
    "tc_bind_reuse":              ["bind", "setsockopt"],
    "tc_sockopt_matrix":          ["setsockopt", "getsockopt"],
    "tc_multicast_join":          ["setsockopt"],
    "tc_ioctl_ifconf":            ["ioctlsocket"],
    "tc_ioctl_fionread":          ["ioctlsocket"],
    "tc_listen_accept_loopback":  ["listen", "accept", "connect", "send", "recv"],
    "tc_connect_refused":         ["connect", "closesocket"],
    "tc_nonblock_connect":        ["connect", "ioctlsocket", "getsockopt"],
    "tc_shutdown_wr":             ["shutdown", "send", "recv"],
    "tc_getpeername":             ["getpeername"],
    "tc_dns_local":               ["gethostbyname"],
    "tc_dns_fail":                ["gethostbyname"],
    "tc_errno_ptr":               ["seterrnoptr", "errno"],
    "tc_dup2":                    ["dup2socket"],
    "tc_waitselect_timeout":      ["waitselect"],
    "tc_waitselect_eintr":        ["waitselect", "setsocketsignals"],
    "tc_waitselect_badf":         ["waitselect"],
    "tc_waitselect_no_sigio":     ["waitselect"],
    "tc_sigio":                   ["setsocketsignals"],
    "tc_icmp_raw":                ["socket"],
    "tc_sendmsg_iov":             ["sendmsg", "recvmsg"],
    "tc_recv_peek":               ["recv", "recvfrom"],
    "tc_socket_events":           ["getsocketevents", "setsockopt"],
    "tc_sbtc_full":               ["socketbasetaglist"],
    "tc_release_obtain":          ["obtainsocket", "releasesocket", "releasecopyofsocket"],
    "tc_stats_counters":          ["sendto", "socket"],
}

def latest_bench_green_tests(root_dir):
    """Newest GREEN bench dir's 68000 conformance TAP -> (green tc names, dir).

    "Green" = the run's own 68000 conformance.log reports no "not ok" line.
    Red/frozen runs (e.g. the TNET-115 freeze dirs) must not strip the
    coverage column from the generated table (TNET-139 regen incident)."""
    base = os.path.join(root_dir, "docs", "bench-logs")
    if not os.path.isdir(base):
        return set(), None
    newest = None
    for d in sorted(os.listdir(base)):
        log = os.path.join(base, d, "68000", "conformance.log")
        if not os.path.isfile(log):
            continue
        with open(log, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        has_plan = any(re.match(r"1\.\.[0-9]+", l) for l in lines)
        has_ok = any(l.startswith("ok ") for l in lines)
        has_fail = any(l.startswith("not ok") for l in lines)
        if not (has_plan and has_ok and not has_fail):
            continue
        newest = d
    if newest is None:
        return set(), None
    green = set()
    pat = re.compile(r"^ok \d+ - (tc_\w+)")
    with open(os.path.join(base, newest, "68000", "conformance.log"),
              "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if m:
                green.add(m.group(1))
    return green, newest

def vector_status(name, per_func_cmds, ipc_handlers):
    """(status, note) for one SFD function, derived from sources only.
    SFD names are mixed-case (Inet_NtoA) while C handlers are lowercase
    (tn_lvo_inet_ntoa) — matching is case-insensitive."""
    key = name.lower()
    if key not in per_func_cmds:
        return ("STUB", "honest stub — exact Roadshow error semantics "
                        "(ENOSYS / ENXIO / NULL / NO_RECOVERY / FALSE)")
    cmds = per_func_cmds.get(key, set())
    broken = sorted(c for c in cmds if ipc_handlers.get(c, "MISSING") is None)
    missing = sorted(c for c in cmds if c not in ipc_handlers)
    if broken:
        return ("BROKEN", f"marshals `TN_IPC_CMD_{broken[0]}` but the daemon "
                          f"dispatch table has a NULL handler → ENOSYS")
    if missing:
        return ("BROKEN", f"marshals `TN_IPC_CMD_{missing[0]}` which has no "
                          f"dispatch-table entry at all")
    if cmds:
        return ("BUILT", "vector → IPC → daemon handler")
    return ("BUILT", "handled entirely in-library (per-task socket base)")

def generate_lib_table(vectors, out_path):
    lines = [
        "/*",
        " * lib_table.gen.c — Generated bsdsocket.library jump table.",
        " * Generated automatically by scripts/gen_lvo_table.py from sfd/bsdsocket_lib.sfd.",
        " * DO NOT EDIT MANUALLY.",
        " */",
        "",
        "#include <exec/types.h>",
        "",
        "/* Forward declarations for 68k assembly dispatch stubs */",
        "extern void tn_stub_open(void);",
        "extern void tn_stub_close(void);",
        "extern void tn_stub_expunge(void);",
        "extern void tn_stub_reserved(void);",
        "extern void tn_stub_neg1(void);",
        ""
    ]
    
    for v in vectors:
        if not v["reserved"]:
            lines.append(f"extern void tn_stub_{v['name'].lower()}(void);")
            
    lines.extend([
        "",
        "const APTR g_lib_vectors[] = {",
        "    (APTR)tn_stub_open,                  /* -6   LIB_OPEN */",
        "    (APTR)tn_stub_close,                 /* -12  LIB_CLOSE */",
        "    (APTR)tn_stub_expunge,               /* -18  LIB_EXPUNGE */",
        "    (APTR)tn_stub_reserved,              /* -24  LIB_RESERVED */",
        ""
    ])
    
    for v in vectors:
        off = v["offset"]
        if v["reserved"]:
            stub = "tn_stub_neg1"
            comment = f"/* {off} RESERVED */"
        else:
            stub = f"tn_stub_{v['name'].lower()}"
            comment = f"/* {off} {v['name']} */"
        lines.append(f"    (APTR){stub:<30}, {comment}")
        
    lines.extend([
        "    (APTR)-1                             /* End of table marker */",
        "};",
        ""
    ])
    
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[gen_lvo_table] Emitted {out_path} ({len(vectors) + 4} vectors)")

def generate_lib_stubs(vectors, out_path):
    lines = [
        "|",
        "| lib_stubs.gen.s — Generated 68k LVO Assembly Dispatch Stubs",
        "| Generated automatically by scripts/gen_lvo_table.py from sfd/bsdsocket_lib.sfd.",
        "|",
        "",
        "    .text",
        "    .even",
        "",
        "| --- Library Management Vectors ---",
        "    .globl _tn_stub_open",
        "_tn_stub_open:",
        "    move.l  d0,-(sp)",
        "    move.l  a6,-(sp)",
        "    jsr     _tn_lib_open",
        "    addq.l  #8,sp",
        "    move.l  d0,a0",
        "    rts",
        "",
        "    .globl _tn_stub_close",
        "_tn_stub_close:",
        "    move.l  a6,-(sp)",
        "    jsr     _tn_lib_close",
        "    addq.l  #4,sp",
        "    move.l  d0,a0",
        "    rts",
        "",
        "    .globl _tn_stub_expunge",
        "_tn_stub_expunge:",
        "    move.l  a6,-(sp)",
        "    jsr     _tn_lib_expunge",
        "    addq.l  #4,sp",
        "    move.l  d0,a0",
        "    rts",
        "",
        "    .globl _tn_stub_reserved",
        "_tn_stub_reserved:",
        "    moveq   #0,d0",
        "    suba.l  a0,a0",
        "    rts",
        "",
        "    .globl _tn_stub_neg1",
        "_tn_stub_neg1:",
        "    moveq   #-1,d0",
        "    suba.l  a0,a0",
        "    rts",
        ""
    ]
    
    for v in vectors:
        if v["reserved"]:
            continue
            
        name = v["name"]
        lower = name.lower()
        regs = v["regs"]
        ret = v["ret"]
        is_ptr = ("*" in ret) or (ret in ["STRPTR", "APTR"])
        
        if lower in IMPLEMENTED_FUNCS:
            target = f"_tn_lvo_{lower}"
        else:
            target = f"_tn_unimpl_{lower}"
            
        stack_bytes = (len(regs) + 1) * 4
        
        lines.extend([
            f"| {v['offset']}: {name}({v['args']})",
            f"    .globl _tn_stub_{lower}",
            f"_tn_stub_{lower}:",
            "    move.l  a6,-(sp)"
        ])
        
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
        
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[gen_lvo_table] Emitted {out_path}")

def generate_lib_unimpl(vectors, out_path):
    lines = [
        "/*",
        " * lib_unimpl.c — Honest C stubs for unimplemented bsdsocket.library vectors.",
        " * Generated automatically by scripts/gen_lvo_table.py from sfd/bsdsocket_lib.sfd (Round 3 §D.2).",
        " * Returns exact honest values per SFD signature: LONG -> -1 + ENOSYS/ENXIO,",
        " * pointers -> NULL (+ NO_RECOVERY for resolver), BOOL -> FALSE, in_addr_t -> INADDR_NONE.",
        " */",
        "",
        "#include \"../../include/ipc.h\"",
        "#include \"../common/log.h\"",
        "#include <proto/exec.h>",
        "#include <exec/lists.h>",
        "#include <devices/timer.h>",
        "#include <utility/tagitem.h>",
        "#include <utility/hooks.h>",
        "#include <netinet/in.h>",
        "#include <sys/socket.h>",
        "#include <sys/mbuf.h>",
        "#include <net/route.h>",
        "#include <netdb.h>",
        "#include <libraries/bsdsocket.h>",
        "#include <dos/dosextens.h>",
        "#include <sys/errno.h>",
        "",
        "static inline void tn_unimpl_set_errno(TnSocketBase *base, LONG err)",
        "{",
        "    if (base == NULL) return;",
        "    base->task_errno = err;",
        "    if (base->errno_ptr != NULL) {",
        "        if (base->errno_width == 1) {",
        "            *((UBYTE *)base->errno_ptr) = (UBYTE)err;",
        "        } else if (base->errno_width == 2) {",
        "            *((UWORD *)base->errno_ptr) = (UWORD)err;",
        "        } else {",
        "            *base->errno_ptr = err;",
        "        }",
        "    }",
        "}",
        "",
        "static inline void tn_unimpl_set_herrno(TnSocketBase *base, LONG herr)",
        "{",
        "    if (base == NULL) return;",
        "    base->task_herrno = herr;",
        "    if (base->herrno_ptr != NULL) {",
        "        *base->herrno_ptr = herr;",
        "    }",
        "}",
        ""
    ]
    
    unimpl_count = 0
    for v in vectors:
        if v["reserved"]:
            continue
            
        name = v["name"]
        lower = name.lower()
        if lower in IMPLEMENTED_FUNCS:
            continue
            
        unimpl_count += 1
        ret = v["ret"]
        raw_args = v["raw_args"]
        
        if lower.startswith("bpf_") or lower.startswith("ipf_"):
            errno_val = "ENXIO"
        else:
            errno_val = "ENOSYS"
            
        is_resolver = "host" in lower or "addr" in lower
        
        # Build parameter string
        if raw_args:
            params_decl = ", ".join(raw_args) + ", TnSocketBase *base"
        else:
            params_decl = "TnSocketBase *base"
            
        # Extract parameter names for unused casts
        unused_casts = []
        for a in raw_args:
            # find last word
            words = re.findall(r"\b\w+\b", a)
            if words:
                unused_casts.append(f"(void){words[-1]};")
        unused_casts.append("(void)base;")
        unused_casts_str = " ".join(unused_casts)
        
        lines.append(f"/* {v['offset']}: {name} */")
        
        if ret == "LONG":
            lines.extend([
                f"LONG tn_unimpl_{lower}({params_decl})",
                "{",
                f"    static int logged = 0; {unused_casts_str}",
                f"    if (!logged) {{ logged = 1; tn_logf(TN_LOG_VERBOSE, \"tolunnet: %s not implemented\\n\", \"{name}\"); }}",
                f"    tn_unimpl_set_errno(base, {errno_val});",
                "    return -1;",
                "}",
                ""
            ])
        elif ret == "BOOL":
            lines.extend([
                f"BOOL tn_unimpl_{lower}({params_decl})",
                "{",
                f"    static int logged = 0; {unused_casts_str}",
                f"    if (!logged) {{ logged = 1; tn_logf(TN_LOG_VERBOSE, \"tolunnet: %s not implemented\\n\", \"{name}\"); }}",
                f"    tn_unimpl_set_errno(base, {errno_val});",
                "    return FALSE;",
                "}",
                ""
            ])
        elif ret == "in_addr_t":
            lines.extend([
                f"in_addr_t tn_unimpl_{lower}({params_decl})",
                "{",
                f"    static int logged = 0; {unused_casts_str}",
                f"    if (!logged) {{ logged = 1; tn_logf(TN_LOG_VERBOSE, \"tolunnet: %s not implemented\\n\", \"{name}\"); }}",
                f"    tn_unimpl_set_errno(base, {errno_val});",
                "    return INADDR_NONE;",
                "}",
                ""
            ])
        elif ret == "VOID":
            lines.extend([
                f"VOID tn_unimpl_{lower}({params_decl})",
                "{",
                f"    static int logged = 0; {unused_casts_str}",
                f"    if (!logged) {{ logged = 1; tn_logf(TN_LOG_VERBOSE, \"tolunnet: %s not implemented\\n\", \"{name}\"); }}",
                "    return;",
                "}",
                ""
            ])
        else:
            # Pointer return type
            herrno_stmt = " tn_unimpl_set_herrno(base, NO_RECOVERY);" if is_resolver else ""
            lines.extend([
                f"{ret} tn_unimpl_{lower}({params_decl})",
                "{",
                f"    static int logged = 0; {unused_casts_str}",
                f"    if (!logged) {{ logged = 1; tn_logf(TN_LOG_VERBOSE, \"tolunnet: %s not implemented\\n\", \"{name}\"); }}",
                f"    tn_unimpl_set_errno(base, {errno_val});{herrno_stmt}",
                "    return NULL;",
                "}",
                ""
            ])
            
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[gen_lvo_table] Emitted {out_path} ({unimpl_count} honest stubs)")

def generate_compat_table(vectors, per_func_cmds, ipc_handlers, pass_map,
                           bench_dir, out_path):
    counts = {"BUILT": 0, "BROKEN": 0, "STUB": 0}
    lines = [
        "# bsdsocket.library LVO Compatibility Table (generated)",
        "",
        "> Generated by `scripts/gen_lvo_table.py` — DO NOT EDIT MANUALLY.",
        "> Enforced fresh by `make python-checks` (regenerate + git-diff gate).",
        "> Sources: `sfd/bsdsocket_lib.sfd` (LVO offset + prototype) ·",
        "> `src/lib/lib_vectors.c` + `src/task/ipc_dispatch.c` (BUILT/BROKEN/STUB) ·",
        f"> bench TAP `{bench_dir}` (PASS column: green `tc_*` coverage).",
        "",
        "| Offset | Function | Signature | Status | Notes | PASS (bench) |",
        "|---|---|---|---|---|---|"
    ]

    for v in vectors:
        off = v["offset"]
        if v["reserved"]:
            lines.append(f"| `{off}` | *(reserved)* | — | Reserved | Exec reserved slot | — |")
            continue
        name = v["name"]
        sig = f"`{v['ret']} {name}({v['args']})`"
        status, note = vector_status(name, per_func_cmds, ipc_handlers)
        counts[status] += 1
        tcs = pass_map.get(name.lower(), [])
        pas = ", ".join(f"`{t}`" for t in sorted(tcs)) if tcs else "—"
        lines.append(f"| `{off}` | `{name}` | {sig} | **{status}** | {note} | {pas} |")

    named = counts["BUILT"] + counts["BROKEN"] + counts["STUB"]
    lines.extend([
        "",
        f"**Summary:** {len(vectors)} LVO slots ({named} SFD functions + "
        f"{len(vectors) - named} reserved) · "
        f"{counts['BUILT']} BUILT · {counts['STUB']} STUB · {counts['BROKEN']} BROKEN.",
        ""
    ])
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"[gen_lvo_table] Emitted {out_path} "
          f"(BUILT {counts['BUILT']} / STUB {counts['STUB']} / BROKEN {counts['BROKEN']})")
    return counts, len(vectors)

README_STATS_START = "<!-- lvo-stats:start -->"
README_STATS_END = "<!-- lvo-stats:end -->"

def update_readme_stats(root_dir, counts, total_slots):
    readme = os.path.join(root_dir, "README.md")
    with open(readme, "r", encoding="utf-8") as f:
        text = f.read()
    if README_STATS_START not in text or README_STATS_END not in text:
        raise SystemExit("README.md lacks the <!-- lvo-stats --> block")
    named = counts["BUILT"] + counts["BROKEN"] + counts["STUB"]
    block = (
        f"{README_STATS_START}\n"
        f"- **`bsdsocket.library` v4.1 runtime:** {total_slots} total LVO slots "
        f"(-30 … -858; {named} SFD functions + reserved) generated from Roadshow SDK "
        f"`sfd/bsdsocket_lib.sfd` via `scripts/gen_lvo_table.py`; **{counts['BUILT']} BUILT** "
        f"(vector → IPC → daemon handler, or handled in-library), {counts['STUB']} honest stubs "
        f"returning exact Roadshow error semantics (`ENOSYS`, `ENXIO`, `NULL`, `NO_RECOVERY`, "
        f"`FALSE`), {counts['BROKEN']} BROKEN (marshaled without a daemon handler). "
        f"Vector-by-vector status and bench PASS coverage: "
        f"[`src/lib/lib_compat_table.gen.md`](src/lib/lib_compat_table.gen.md) "
        f"(single source of truth, regenerated by `make python-checks`).\n"
        f"{README_STATS_END}"
    )
    pre = text.split(README_STATS_START)[0]
    post = text.split(README_STATS_END)[1]
    with open(readme, "w", encoding="utf-8", newline="") as f:
        f.write(pre + block + post)
    print("[gen_lvo_table] README.md lvo-stats block updated")

if __name__ == "__main__":
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    sfd_file = os.path.join(root_dir, "sfd", "bsdsocket_lib.sfd")
    vectors = parse_sfd(sfd_file)

    # Source of truth for "implemented" is lib_vectors.c itself; the static
    # set below is only a drift guard (ANX-02).
    per_func_cmds = parse_lib_vectors(os.path.join(root_dir, "src", "lib", "lib_vectors.c"))
    derived = set(per_func_cmds.keys())
    if derived != IMPLEMENTED_FUNCS:
        only_static = sorted(IMPLEMENTED_FUNCS - derived)
        only_derived = sorted(derived - IMPLEMENTED_FUNCS)
        raise SystemExit(
            "IMPLEMENTED_FUNCS drifted from src/lib/lib_vectors.c — update the set: "
            f"static-only={only_static} lib_vectors-only={only_derived}")
    ipc_handlers = parse_ipc_dispatch(os.path.join(root_dir, "src", "task", "ipc_dispatch.c"))
    green, bench_dir = latest_bench_green_tests(root_dir)
    pass_map = {}
    for tc in sorted(green):
        for fn in TC_COVERS.get(tc, []):
            pass_map.setdefault(fn, []).append(tc)
    if bench_dir is None:
        print("[gen_lvo_table] WARNING: no bench TAP found — PASS column empty")

    generate_lib_table(vectors, os.path.join(root_dir, "src", "lib", "lib_table.gen.c"))
    generate_lib_stubs(vectors, os.path.join(root_dir, "src", "lib", "lib_stubs.gen.s"))
    generate_lib_unimpl(vectors, os.path.join(root_dir, "src", "lib", "lib_unimpl.c"))
    counts, total_slots = generate_compat_table(
        vectors, per_func_cmds, ipc_handlers, pass_map, bench_dir,
        os.path.join(root_dir, "src", "lib", "lib_compat_table.gen.md"))
    update_readme_stats(root_dir, counts, total_slots)
    print(f"Total SFD vectors: {len(vectors)} (from -30 to {vectors[-1]['offset']})")
