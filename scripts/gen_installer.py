#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the repeated per-command backup/undo blocks in
Install_Tolunnet.script from docs/commands.md (z.ai step 10b item 1).

The command list is the docs/commands.md C: table, so the installer
cannot drift from the documentation. `make installer` regenerates the
marked sections in place; `gen_installer.py --check` (python-checks)
fails when the file is stale.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from installer_lint import doc_c_commands  # noqa: E402

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
    for n in names:
        out.append('  (if (not (exists "SYS:C/%s" (noreq)))' % n)
        out.append('    (textfile (dest "SYS:Storage/tolunnet-backup/'
                   'new-files") (append "%s%s"))' % (n, BSN))
        out.append("  )")
    out.append(END_B)
    return NL.join(out)


def undo_tokens(names):
    out = [U_INDENT + Q + U_BEGIN_INNER + BSN + Q]
    out.append(U_INDENT + Q + "; restore what we replaced, remove what we"
               " created" + BSN + Q)
    for n in names:
        for line in ("If EXISTS SYS:Storage/tolunnet-backup/C/" + n,
                     "  Copy >NIL: SYS:Storage/tolunnet-backup/C/" + n
                     + " SYS:C/" + n + " CLONE",
                     "Else",
                     "  Delete >NIL: SYS:C/" + n + " QUIET",
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


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    script = args[0] if args else SCRIPT
    doc = args[1] if len(args) > 1 else DOC
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
