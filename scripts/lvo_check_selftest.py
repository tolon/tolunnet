#!/usr/bin/env python3
"""lvo_check self-test (z.ai step 9d item 2).

Builds a tiny fixture tree and asserts lvo_check's decisions:
  - setsockopt -> -96 (getsockopt's offset)  must FAIL  (name mismatch)
  - getpwnam   -> -114 in a usergroup wrapper must PASS
  - errno      -> -83                        must FAIL  (not a multiple of 6)
Runs the real scripts/lvo_check.py over the fixtures.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = os.path.join(HERE, "lvo_check.py")


def write_tables(root):
    lib = os.path.join(root, "src", "lib")
    ug = os.path.join(root, "src", "usergroup")
    os.makedirs(lib, exist_ok=True)
    os.makedirs(ug, exist_ok=True)
    # bsdsocket table: setsockopt -90, getsockopt -96, Errno -162, socket -30
    with open(os.path.join(lib, "lib_table.gen.c"), "w") as fh:
        fh.write("/* -30 socket */\n/* -90 setsockopt */\n"
                 "/* -96 getsockopt */\n/* -162 Errno */\n")
    # usergroup table: getpwnam -114 (same offset as bsdsocket IoctlSocket!)
    with open(os.path.join(ug, "ug_table.gen.c"), "w") as fh:
        fh.write("/* -114 getpwnam */\n")


def write_wrapper(root, relsrc, func, offset, comment=None):
    path = os.path.join(root, relsrc)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tail = (" /* lvo:%s */" % comment) if comment else ""
    with open(path, "a") as fh:
        fh.write("static LONG %s(LONG fd)\n{\n"
                 '    __asm__ __volatile__("jsr -%d(%%%%a6)" : "+r"(fd)%s);\n'
                 "    return fd;\n}\n" % (func, offset, tail))
        fh.write("\n")


def run(root):
    env = dict(os.environ)
    env["LVO_CHECK_REPO"] = root
    r = subprocess.run([sys.executable, CHECK], env=env,
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    failures = 0

    # 1. setsockopt -> -96: getsockopt's offset, must FAIL
    root = tempfile.mkdtemp(prefix="lvo1-")
    write_tables(root)
    write_wrapper(root, "src/cmds/wrong.c", "call_setsockopt", 96)
    rc, out = run(root)
    ok = rc != 0 and "setsockopt" in out and "96" in out
    print(("PASS" if ok else "FAIL") + ": setsockopt->-96 rejected")
    failures += 0 if ok else 1
    shutil.rmtree(root, ignore_errors=True)

    # 2. getpwnam -> -114 in a usergroup wrapper: must PASS
    root = tempfile.mkdtemp(prefix="lvo2-")
    write_tables(root)
    write_wrapper(root, "src/usergroup/wrap.c", "ug_call_getpwnam", 114)
    rc, out = run(root)
    ok = rc == 0
    print(("PASS" if ok else "FAIL") + ": getpwnam->-114 usergroup accepted")
    failures += 0 if ok else 1
    shutil.rmtree(root, ignore_errors=True)

    # 3. -83: not a multiple of 6, must FAIL
    root = tempfile.mkdtemp(prefix="lvo3-")
    write_tables(root)
    write_wrapper(root, "src/cmds/bad.c", "call_errno", 83)
    rc, out = run(root)
    ok = rc != 0 and "83" in out
    print(("PASS" if ok else "FAIL") + ": -83 rejected")
    failures += 0 if ok else 1
    shutil.rmtree(root, ignore_errors=True)

    print("lvo_check_selftest: %d failure(s)" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
