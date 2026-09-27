#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the repeated per-command backup/undo blocks in
Install_Tolunnet.script from docs/commands.md (z.ai step 10b item 1).

The command list is the docs/commands.md C: table plus the
script's own copyfiles sources, so the installer cannot drift from
the documentation. (new-files bookkeeping was removed in 10c item 1:
undo works per file.) `make installer` regenerates the
marked sections in place; `gen_installer.py --check` (python-checks)
fails when the file is stale.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from installer_lint import doc_c_commands, parse, Str, clause  # noqa: E402

SCRIPT = "Install_Tolunnet.script"
DOC = "docs/commands.md"

BEGIN_B = ";BEGIN GENERATED-BACKUP (scripts/gen_installer.py - do not edit)"
END_B = ";END GENERATED-BACKUP"
# The undo section lives inside one Installer string literal built with
# (cat ...): every line is its own quoted token on its own source line.
U_INDENT = "          "
U_BEGIN_INNER = ";BEGIN GENERATED-UNDO (scripts/gen_installer.py - do not edit)"
U_END_INNER = ";END GENERATED-UNDO"
BSN = chr(92) + "n"
NL = chr(10)
Q = chr(34)


class GenError(Exception):
    pass


def backup_section(names):
    out = [BEGIN_B]
    out.append("; first install only: preserve whatever we are about to")
    out.append("; replace, and note what we create from scratch")
    for n in names:
        out.append('  (if (exists "SYS:C/%s" (noreq))' % n)
        out.append('    (if (not (exists "SYS:Storage/tolunnet-backup/C/%s"'
                   " (noreq)))" % n)
        out.append('      (copyfiles (source "SYS:C/%s")'
                   ' (dest "SYS:Storage/tolunnet-backup/C")'
                   ' (newname "%s"))' % (n, n))
        out.append("    )")
        out.append("  )")
    out.append(END_B)
    return NL.join(out)


def undo_tokens(names):
    # 10d item 1: FailAt 21 first (a missing file must not abort the
    # undo: Delete of a non-existent path returns 20 > default 10), and
    # every C: Delete sits inside its own If EXISTS.
    out = [U_INDENT + Q + U_BEGIN_INNER + BSN + Q]
    out.append(U_INDENT + Q + "FailAt 21" + BSN + Q)
    out.append(U_INDENT + Q + "; restore what we replaced, remove what we"
               " created" + BSN + Q)
    for n in names:
        # 10e item 2: restore from the backup, and ONLY when there is no
        # backup delete the file we created - never both (the flat
        # If/If shape deleted what the Copy had just restored).
        for line in ("If EXISTS SYS:Storage/tolunnet-backup/C/" + n,
                     "  Copy >NIL: SYS:Storage/tolunnet-backup/C/" + n
                     + " SYS:C/" + n + " CLONE",
                     "Else",
                     "  If EXISTS SYS:C/" + n,
                     "    Delete >NIL: SYS:C/" + n + " QUIET",
                     "  EndIf",
                     "EndIf"):
            out.append(U_INDENT + Q + line + BSN + Q)
    out.append(U_INDENT + Q + U_END_INNER + BSN + Q)
    return out


def replace_between(text, begin, end, replacement):
    i = text.find(begin)
    if i < 0:
        raise GenError("marker not found: %s" % begin.strip())
    j = text.find(end, i + len(begin))
    if j < 0:
        raise GenError("marker not found: %s" % end.strip())
    return text[:i] + replacement + text[j + len(end):]


def generate(text, names):
    text = replace_between(text, BEGIN_B, END_B, backup_section(names))
    u_begin = U_INDENT + Q + U_BEGIN_INNER + BSN + Q
    u_end = U_INDENT + Q + U_END_INNER + BSN + Q
    text = replace_between(text, u_begin, u_end,
                           NL.join(undo_tokens(names)))
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
