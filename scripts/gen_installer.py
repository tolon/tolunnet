#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the backup/undo logic of Install_Tolunnet.script from
docs/commands.md (z.ai step 10b item 1, bugtrack 2026-10-02 1.x).

The command list is the docs/commands.md C: table plus the
script's own copyfiles sources, so the installer cannot drift from
the documentation. Per file, the install keeps a backup
(SYS:Storage/tolunnet-backup/C/<x>), a "tolunnet created it" marker
(.../new/<x>) and checksum markers of what it installed
(.../ours/<x>/<getsum>; FFS names stop at 30 chars). `make installer` regenerates the marked
sections in place; `gen_installer.py --check` (python-checks) fails
when the file is stale.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from installer_lint import doc_c_commands, parse, Str, clause  # noqa: E402

SCRIPT = "Install_Tolunnet.script"
DOC = "docs/commands.md"

# bugtrack 2026-10-02: the whole backup/undo logic is generated. The
# PREPARE section (undo first, then backups) runs before the copies,
# the FINISH section (checksum markers, boot line) after them.
P_BEGIN = ";BEGIN GENERATED-PREPARE (scripts/gen_installer.py - do not edit)"
P_END = ";END GENERATED-PREPARE"
F_BEGIN = ";BEGIN GENERATED-FINISH (scripts/gen_installer.py - do not edit)"
F_END = ";END GENERATED-FINISH"
# The undo lives inside one Installer string literal built with
# (cat ...): every line is its own quoted token on its own source line.
U_INDENT = "          "
U_BEGIN_INNER = ";BEGIN GENERATED-UNDO"
U_END_INNER = ";END GENERATED-UNDO"
BK = "SYS:Storage/tolunnet-backup"
UG = "usergroup.library"
LIB = "LIBS:bsdsocket.library"
UNDO = "S:tolunnet-undo"
# 1.1: the setup wizard writes its own restore script here (it never
# touches S:tolunnet-undo); the undo runs it first.
STACKS = "S:tolunnet-undo-stacks"
# 1.2: the second park name for a newer foreign library. FFS/OFS names
# stop at 30 characters: bsdsocket.library.pre-tolunnet is exactly 30.
P2 = ".pre-tn-newer"
# 11ab item 2c: the welcome block is generated too, so the version the
# user sees comes from include/version.h - never a hand-edited number.
W_BEGIN = ";BEGIN GENERATED-WELCOME (scripts/gen_installer.py - do not edit;"
W_BEGIN2 = "; the version comes from include/version.h)"
W_END = ";END GENERATED-WELCOME"
BSN = chr(92) + "n"
NL = chr(10)
Q = chr(34)


def version_from_header():
    import re
    vpath = os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), "include", "version.h")
    txt = open(vpath, encoding="utf-8").read()
    return re.search(r'#define TOLUNNET_VERSION "(.*?)"', txt).group(1)


def welcome_section(version):
    out = [W_BEGIN, W_BEGIN2]
    out.append('(welcome')
    out.append('  "Welcome to the tolunnet %s Installation.%s%sThe tolunnet '
               'package installs a TCP/IP stack and bsdsocket.library v4.1 '
               'runtime for AmigaOS 3.0 or newer on 68000-class Amigas.'
               '%s%sAuthor: Ismail Ozturk (tolon)%s"'
               % (version, BSN, BSN, BSN, BSN, BSN))
    out.append(')')
    # 11ab item 2b / 11ac item 1: the OS gate is generated too -
    # and uses (shiftleft 39 16): the Installer has no infix ops.
    out.append('; 11ab item 2b: refuse to run on anything older '
               'than OS 3.0.')
    out.append('; 11ac item 1: Installer has no infix operators - '
               'shiftleft, not <<.')
    out.append('(if (< (getversion "exec.library" (resident)) '
               '(shiftleft 39 16))')
    out.append('  (abort "tolunnet needs AmigaOS 3.0 (exec.library '
               'V39) or newer. Installation aborted.")')
    out.append(')')
    out.append(W_END)
    return NL.join(out)


class GenError(Exception):
    pass


def _q(s):
    return Q + s + Q


def undo_lines(names):
    """The S:tolunnet-undo script, one AmigaDOS line per entry.

    bugtrack 2026-10-02: 1.1 the wizard's own S:tolunnet-undo-stacks
    runs first; 1.4 a backup is deleted only after its Copy reported
    no error (If WARN keeps it); 1.7 a SYS:C file is deleted only when
    the new/ marker says tolunnet created it - an unknown file is left
    alone; 1.3 S:User-Startup is never overwritten; 1.2 the stack
    library comes back by Rename, never over a live one; 1.11
    usergroup.library is restored from its backup."""
    out = ["; tolunnet-undo - remove tolunnet and restore the previous stack",
           U_BEGIN_INNER,
           # 10d item 1: a missing file must not abort the undo
           "FailAt 21",
           "; the setup wizard keeps its own restore script - it runs first",
           "If EXISTS " + STACKS,
           "  Execute " + STACKS,
           "  If EXISTS " + STACKS,
           "    Delete >NIL: " + STACKS + " QUIET",
           "  EndIf",
           "EndIf",
           "; restore what we replaced, delete only what we created"]
    for n in names:
        b = BK + "/C/" + n
        out += ["If EXISTS " + b,
                "  Copy >NIL: " + b + " SYS:C/" + n + " CLONE",
                "  If WARN",
                "    Echo tolunnet-undo: SYS:C/" + n + " not restored, the"
                " original stays in " + BK + "/C",
                "  Else",
                "    Delete >NIL: " + b + " QUIET",
                "  EndIf",
                "Else",
                "  If EXISTS " + BK + "/new/" + n,
                "    If EXISTS SYS:C/" + n,
                "      Delete >NIL: SYS:C/" + n + " QUIET",
                "    EndIf",
                "  EndIf",
                "EndIf"]
    # 1.11: usergroup.library comes back from its backup; it is never
    # deleted (other software may use it).
    b = BK + "/Libs/" + UG
    out += ["If EXISTS " + b,
            "  Copy >NIL: " + b + " SYS:Libs/" + UG + " CLONE",
            "  If WARN",
            "    Echo tolunnet-undo: SYS:Libs/" + UG + " not restored, the"
            " original stays in " + BK + "/Libs",
            "  Else",
            "    Delete >NIL: " + b + " QUIET",
            "  EndIf",
            "EndIf"]
    # 11ab item 2d: the preference tools are always tolunnet's own
    for n in ("Prefs/TolunnetPrefs", "Prefs/TolunnetPrefs.info",
              "Prefs/TolunnetSetup", "Prefs/TolunnetSetup.info"):
        out += ["If EXISTS SYS:" + n,
                "  Delete >NIL: SYS:" + n + " QUIET",
                "EndIf"]
    out += ["If EXISTS DEVS:tolunnet.config",
            "  Delete >NIL: DEVS:tolunnet.config QUIET",
            "EndIf",
            # 1.3: AmigaDOS cannot cut the tolunnet block out of the
            # live file, and copying the backup over it would lose every
            # later edit. The block is guarded by If EXISTS C:tolunnet,
            # so it is inert from now on. The backup is moved aside so a
            # reinstall takes a fresh one.
            "If EXISTS S:User-Startup.tolunnet-bak",
            "  If EXISTS S:User-Startup",
            "    Echo tolunnet-undo: S:User-Startup left as it is. Its"
            " tolunnet block does nothing without C:tolunnet and can be"
            " removed by hand. The pre-install copy is"
            " S:User-Startup.tolunnet-old",
            "    If EXISTS S:User-Startup.tolunnet-old",
            "      Delete >NIL: S:User-Startup.tolunnet-old QUIET",
            "    EndIf",
            "    Rename >NIL: S:User-Startup.tolunnet-bak"
            " S:User-Startup.tolunnet-old",
            "  Else",
            "    Rename >NIL: S:User-Startup.tolunnet-bak S:User-Startup",
            "  EndIf",
            "EndIf",
            # 1.2: never over a live library; the newest parked one wins
            "If NOT EXISTS " + LIB,
            "  If EXISTS " + LIB + P2,
            "    Rename >NIL: " + LIB + P2 + " " + LIB,
            "  Else",
            "    If EXISTS " + LIB + ".pre-tolunnet",
            "      Rename >NIL: " + LIB + ".pre-tolunnet " + LIB,
            "    EndIf",
            "  EndIf",
            "EndIf",
            "If EXISTS " + LIB + ".pre-tolunnet",
            "  Echo tolunnet-undo: a stack library is kept as " + LIB
            + ".pre-tolunnet",
            "EndIf",
            "If EXISTS " + LIB + P2,
            "  Echo tolunnet-undo: a stack library is kept as " + LIB
            + P2,
            "EndIf",
            # 1.4: markers go with ALL; the backup dirs only when empty -
            # an original that could not be restored keeps its dir.
            "If EXISTS " + BK + "/new",
            "  Delete >NIL: " + BK + "/new ALL QUIET",
            "EndIf",
            "If EXISTS " + BK + "/ours",
            "  Delete >NIL: " + BK + "/ours ALL QUIET",
            "EndIf",
            "If EXISTS " + BK + "/C",
            "  Delete >NIL: " + BK + "/C QUIET",
            "EndIf",
            "If EXISTS " + BK + "/Libs",
            "  Delete >NIL: " + BK + "/Libs QUIET",
            "EndIf",
            "If EXISTS " + BK,
            "  Delete >NIL: " + BK + " QUIET",
            "EndIf",
            "If EXISTS " + BK,
            "  Echo tolunnet-undo: " + BK + " still holds files that"
            " were not restored",
            "EndIf",
            U_END_INNER,
            "Run >NIL: Delete " + UNDO + " QUIET"]
    return out


def undo_textfile(names):
    lines = undo_lines(names)
    out = ["(textfile",
           "  (dest " + _q(UNDO) + ")",
           "    (append (cat " + _q(lines[0] + BSN)]
    for line in lines[1:]:
        out.append(U_INDENT + _q(line + BSN))
    out.append("    )")
    out.append("  )")
    out.append(")")
    return out


def _abort_unless(cond, msg):
    # never in PRETEND mode: nothing was written there
    return ["(if (= @pretend 0)",
            "  (if " + cond,
            "    (abort " + _q(msg) + ")",
            "  )",
            ")"]


def _backup_decision(live, backup, tag, mark_new):
    """Sets #tn-act: 1 = back the live file up, 2 = mark it as created
    by tolunnet (new/<tag>), 0 = nothing. A live file whose checksum
    matches an ours/<tag>/<sum> marker is the one tolunnet installed
    earlier and is never backed up as an original (1.7)."""
    ours = "(cat " + _q(BK + "/ours/" + tag + "/") + " (getsum " + \
        _q(live) + "))"
    leg = "(set #tn-act 2)" if mark_new else "(set #tn-act 0)"
    out = ["(set #tn-act 0)",
           "(if (exists " + _q(live) + " (noreq))",
           "  (if (not (exists " + ours + " (noreq)))",
           "    (if (not (exists " + _q(backup) + " (noreq)))",
           "      (if #tn-legacy " + leg + " (set #tn-act 1))",
           "    )",
           "  )"]
    if mark_new:
        out.append("  (set #tn-act 2)")
    out.append(")")
    return out


def prepare_section(names):
    out = [P_BEGIN,
           "; bugtrack 2026-10-02 1.5: S:tolunnet-undo is written FIRST,",
           "; before any backup, copy, rename or delete, so an aborted",
           "; install can always be undone. The undo only deletes a",
           "; file a new/ marker says tolunnet created (1.7).",
           "; Installer (if) takes ONE then and ONE else statement - no",
           "; multi-statement bodies (the old S:tolunnet-undo gate had 35).",
           "(set #tn-had-undo (exists " + _q(UNDO) + " (noreq)))",
           "; an install from before the markers (undo, but no new/ dir):",
           "; every un-backed-up SYS:C file is tolunnet's own",
           "(set #tn-legacy 0)",
           "(if #tn-had-undo",
           "  (if (not (exists " + _q(BK + "/new") + " (noreq)))",
           "    (set #tn-legacy 1)",
           "  )",
           ")"]
    out += undo_textfile(names)
    out.append("; ---- backups: per file, no S:tolunnet-undo gate (1.7) "
               "------------")
    # ours/<x>/<getsum>: one dir per file keeps every name within the
    # 30-character FFS limit (ConfigureNetInterface.<sum> would not)
    for d in ["SYS:Storage", BK, BK + "/C", BK + "/Libs", BK + "/new",
              BK + "/ours"] + [BK + "/ours/" + n for n in names + [UG]]:
        out += ["(if (not (exists " + _q(d) + " (noreq)))",
                "  (makedir " + _q(d) + ")",
                ")"]
    for n in names:
        live = "SYS:C/" + n
        bak = BK + "/C/" + n
        out.append("; " + live)
        out += _backup_decision(live, bak, n, True)
        out += ["(if (= #tn-act 1)",
                "  (copyfiles (source " + _q(live) + ") (dest "
                + _q(BK + "/C") + ") (newname " + _q(n) + "))",
                ")",
                "(if (= #tn-act 1)"]
        out += ["  " + x for x in _abort_unless(
            "(<> (getsize " + _q(live) + ") (getsize " + _q(bak) + "))",
            "Backup of %s failed - nothing was replaced yet. Free some "
            "space and run the installer again." % live)]
        out += [")",
                "(if (= #tn-act 2)",
                "  (textfile (dest " + _q(BK + "/new/" + n)
                + ") (append \"\"))",
                ")"]
    # 1.11: an existing usergroup.library is backed up before copylib
    live = "SYS:Libs/" + UG
    bak = BK + "/Libs/" + UG
    out.append("; " + live + " (1.11)")
    out += _backup_decision(live, bak, UG, False)
    out += ["(if (= #tn-act 1)",
            "  (copyfiles (source " + _q(live) + ") (dest "
            + _q(BK + "/Libs") + ") (newname " + _q(UG) + "))",
            ")",
            "(if (= #tn-act 1)"]
    out += ["  " + x for x in _abort_unless(
        "(<> (getsize " + _q(live) + ") (getsize " + _q(bak) + "))",
        "Backup of %s failed - nothing was replaced yet." % live)]
    out.append(")")
    # 1.2: park the live library by rename; an identical copy may go,
    # a different one gets its own name - never a blind Delete
    p1 = LIB + ".pre-tolunnet"
    p2 = LIB + P2

    def same(a, b):
        return ("(and (= (getsum " + _q(a) + ") (getsum " + _q(b) + ")) "
                "(= (getsize " + _q(a) + ") (getsize " + _q(b) + ")))")
    out += ["; ---- " + LIB + " (1.2): #tn-lib 1 park as .pre-tolunnet,",
            "; 2 park as " + P2 + ", 3/5 an identical copy is parked",
            "; already (.pre-tolunnet / " + P2 + "), 4 differs from both",
            "(set #tn-lib 0)",
            "(if (exists " + _q(LIB) + " (noreq))",
            "  (if (exists " + _q(p1) + " (noreq))",
            "    (if " + same(LIB, p1),
            "      (set #tn-lib 3)",
            "      (if (exists " + _q(p2) + " (noreq))",
            "        (if " + same(LIB, p2) + " (set #tn-lib 5)"
            " (set #tn-lib 4))",
            "        (set #tn-lib 2)",
            "      )",
            "    )",
            "    (set #tn-lib 1)",
            "  )",
            ")",
            "(if (= #tn-lib 4)",
            "  (abort " + _q("LIBS:bsdsocket.library differs from both "
                             "saved copies (.pre-tolunnet and "
                             "" + P2 + "). Move one of them away and "
                             "run the installer again. Nothing was "
                             "replaced.") + ")",
            ")"]
    for code, dst in (("1", p1), ("2", p2)):
        out += ["(if (= #tn-lib " + code + ")",
                "  (if (= (rename " + _q(LIB) + " " + _q(dst) + ") 0)",
                "    (if (= @pretend 0)",
                "      (abort " + _q("Could not rename " + LIB + " to "
                                     + dst + ". Nothing was replaced.")
                + ")",
                "    )",
                "  )",
                ")"]
    # the compare is repeated right at the delete
    for code, dst in (("3", p1), ("5", p2)):
        out += ["(if (= #tn-lib " + code + ")",
                "  (if " + same(LIB, dst),
                "    (delete " + _q(LIB) + ")",
                "  )",
                ")"]
    # 1.3: the User-Startup backup. One left over from an install that
    # was undone (or lost its undo) is stale: move it aside, take a
    # fresh one.
    us = "S:User-Startup"
    out += ["; ---- " + us + " backup (1.3): a backup with no undo beside",
            "; it is stale - keep it as .tolunnet-old, take a fresh one",
            "(if (not (exists " + _q(us) + " (noreq)))",
            "  (textfile (dest " + _q(us) + ") (append \"\"))",
            ")",
            "(set #tn-stale 0)",
            "(if (= #tn-had-undo 0)",
            "  (if (exists " + _q(us + ".tolunnet-bak") + " (noreq))",
            "    (set #tn-stale 1)",
            "  )",
            ")",
            "(if (= #tn-stale 1)",
            "  (if (exists " + _q(us + ".tolunnet-old") + " (noreq))",
            "    (delete " + _q(us + ".tolunnet-old") + ")",
            "  )",
            ")",
            "(if (= #tn-stale 1)",
            "  (rename " + _q(us + ".tolunnet-bak") + " "
            + _q(us + ".tolunnet-old") + ")",
            ")",
            "(set #tn-usbak 0)",
            "(if (not (exists " + _q(us + ".tolunnet-bak") + " (noreq)))",
            "  (set #tn-usbak 1)",
            ")",
            "(if (= #tn-usbak 1)",
            "  (copyfiles (source " + _q(us) + ") (dest \"S:\") (newname "
            + _q("User-Startup.tolunnet-bak") + "))",
            ")",
            "(if (= #tn-usbak 1)"]
    out += ["  " + x for x in _abort_unless(
        "(<> (getsize " + _q(us) + ") (getsize "
        + _q(us + ".tolunnet-bak") + "))",
        "Backup of S:User-Startup failed - nothing was replaced yet.")]
    out += [")", P_END]
    return NL.join(out)


def finish_section(names):
    out = [F_BEGIN,
           "; 1.7: remember each installed binary by checksum, so a",
           "; reinstall never backs tolunnet's own file up as an original"]
    for live, tag in [("SYS:C/" + n, n) for n in names] + \
            [("SYS:Libs/" + UG, UG)]:
        out += ["(if (exists " + _q(live) + " (noreq))",
                "  (textfile (dest (cat " + _q(BK + "/ours/" + tag + "/")
                + " (getsum " + _q(live) + "))) (append \"\"))",
                ")"]
    out += ["; ---- boot line: stack size + daemon. Guarded by If EXISTS so",
            "; ---- a block the undo leaves behind does nothing (1.3) -----",
            "(startup \"tolunnet\"",
            "  (prompt \"Add tolunnet to S:User-Startup (start the stack "
            "at boot)?\")",
            "  (help \"Writes Stack 32768 and Run <NIL: >NIL: C:tolunnet "
            "into S:User-Startup so the stack starts on every boot.\")",
            "  (command \"If EXISTS C:tolunnet" + BSN + "Stack 32768" + BSN
            + "Run <NIL: >NIL: C:tolunnet" + BSN + "EndIf\")",
            ")",
            F_END]
    return NL.join(out)


def replace_between(text, begin, end, replacement):
    i = text.find(begin)
    if i < 0:
        raise GenError("marker not found: %s" % begin.strip())
    j = text.find(end, i + len(begin))
    if j < 0:
        raise GenError("marker not found: %s" % end.strip())
    return text[:i] + replacement + text[j + len(end):]


def generate(text, names):
    text = replace_between(text, P_BEGIN, P_END, prepare_section(names))
    text = replace_between(text, F_BEGIN, F_END, finish_section(names))
    # 11ab item 2c: the welcome carries the version from version.h
    text = replace_between(text, W_BEGIN, W_END,
                           welcome_section(version_from_header()))
    return text


def copied_c_names(text):
    """C/<name> sources of the script's own copyfiles statements - the
    physical binaries we are about to replace (docs/commands.md names
    the aliases; these are the canonical files behind them)."""
    out = []
    for line in text.split(NL):
        line = line.strip()
        if line.startswith("(copyfiles (source \"C/") and \
                "(dest \"SYS:C\")" in line:
            name = line.split('(source "C/', 1)[1].split('"', 1)[0]
            if name not in out:
                out.append(name)
    return out


def undo_content(script_text):
    """The S:tolunnet-undo textfile content as one plain-text script."""
    top = parse(script_text)

    def find(node):
        if not (isinstance(node, list) and node):
            return None
        if node[0] == "textfile":
            dest = clause(node, "dest")
            if isinstance(dest, Str) and str(dest) == "S:tolunnet-undo":
                app = clause(node, "append")
                parts = []

                def collect(n):
                    if isinstance(n, Str):
                        parts.append(str(n))
                    elif isinstance(n, list):
                        for c in n:
                            collect(c)

                collect(app)
                return "".join(parts)
        for child in node[1:]:
            r = find(child)
            if r is not None:
                return r
        return None

    for node in top:
        r = find(node)
        if r is not None:
            return r
    raise GenError("S:tolunnet-undo textfile not found")


def sandbox_undo(script_text, root):
    """The undo script with every SYS:/LIBS:/DEVS:/S: path rewritten
    under root (trailing slash), for the bench sandbox row."""
    content = undo_content(script_text)
    content = content.replace(BSN, NL)
    if not root.endswith("/"):
        root += "/"
    # SYS: first - a plain S: replacement would also hit SYS:
    content = content.replace("SYS:", root)
    content = content.replace("LIBS:", root + "LIBS/")
    content = content.replace("DEVS:", root + "DEVS/")
    content = content.replace("S:", root + "S/")
    return content


def main(argv):
    flags = {a for a in argv if a.startswith("--")}
    pos = [a for a in argv if not a.startswith("--")]
    root = None
    for a in list(argv):
        if a.startswith("--undo-root="):
            root = a.split("=", 1)[1]
    undo_out = None
    for a in list(argv):
        if a.startswith("--undo-out="):
            undo_out = a.split("=", 1)[1]
    script = pos[0] if pos else SCRIPT
    doc = pos[1] if len(pos) > 1 else DOC
    if root is not None and undo_out is not None:
        with open(script, encoding="utf-8") as fh:
            text = fh.read()
        try:
            content = sandbox_undo(text, root)
        except GenError as exc:
            print("gen_installer: ERROR: %s" % exc)
            return 2
        with open(undo_out, "w", encoding="utf-8", newline=NL) as fh:
            fh.write(content)
        print("gen_installer: %s written (root %s)" % (undo_out, root))
        return 0
    with open(doc, encoding="utf-8") as fh:
        names = doc_c_commands(fh.read())
    with open(script, encoding="utf-8") as fh:
        text = fh.read()
    for name in copied_c_names(text):
        if name not in names:
            names.append(name)
    try:
        new_text = generate(text, names)
    except GenError as exc:
        print("gen_installer: ERROR: %s" % exc)
        return 2
    if "--check" in argv:
        if new_text != text:
            print("gen_installer: %s is stale vs %s - run 'make installer'"
                  % (script, doc))
            return 1
        print("gen_installer: %s up to date (%d commands)" %
              (script, len(names)))
        return 0
    if new_text != text:
        with open(script, "w", encoding="utf-8", newline=NL) as fh:
            fh.write(new_text)
        print("gen_installer: %s regenerated (%d commands)"
              % (script, len(names)))
    else:
        print("gen_installer: %s already up to date (%d commands)"
              % (script, len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
