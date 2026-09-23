#!/usr/bin/env python3
"""
scripts/convert_screenshots.py — Convert bench IFF screenshots to 2x nearest-neighbour PNGs

Locates the newest clean bench log directory under docs/bench-logs/, finds the
A1200 PAL screen captures (wizard-0..4-pal, wizard-3-pal-static, prefs-pal),
and converts them into docs/screenshots/ as PNG files via:
  ilbmtoppm | pamscale -nomix 2 | pnmtopng
"""

import os
import sys
import glob
import shutil
import subprocess

def find_repo_root():
    d = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(d, ".."))

def find_newest_clean_bench(repo_root):
    bench_root = os.path.join(repo_root, "docs", "bench-logs")
    if not os.path.isdir(bench_root):
        raise RuntimeError(f"docs/bench-logs not found at {bench_root}")

    dirs = []
    for entry in os.listdir(bench_root):
        full = os.path.join(bench_root, entry)
        if not os.path.isdir(full):
            continue
        if "-dirty" in entry or "-soakquick-" in entry:
            continue
        a1200_dir = os.path.join(full, "a1200")
        if os.path.isfile(os.path.join(a1200_dir, "wizard-0-pal.iff")):
            dirs.append(entry)

    if not dirs:
        raise RuntimeError("No clean bench directory with a1200 IFF screenshots found.")

    dirs.sort()
    return os.path.join(bench_root, dirs[-1])

def convert_iff_to_png(src_iff, dst_png, repo_root):
    # Check if native ilbmtoppm / pamscale / pnmtopng exist
    has_netpbm = (shutil.which("ilbmtoppm") and shutil.which("pamscale") and shutil.which("pnmtopng"))

    if has_netpbm:
        cmd = f'ilbmtoppm "{src_iff}" | pamscale -nomix 2 | pnmtopng > "{dst_png}"'
        res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Conversion failed for {src_iff}: {res.stderr}")
    else:
        # Fall back to WSL execution
        # Convert paths to WSL format if running on Windows
        rel_src = os.path.relpath(src_iff, repo_root).replace('\\', '/')
        rel_dst = os.path.relpath(dst_png, repo_root).replace('\\', '/')
        wsl_cmd = f'ilbmtoppm "{rel_src}" | pamscale -nomix 2 | pnmtopng > "{rel_dst}"'
        res = subprocess.run(["wsl", "-d", "Ubuntu-24.04", "-e", "bash", "-c", wsl_cmd],
                             cwd=repo_root, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"WSL conversion failed for {src_iff}: {res.stderr}")

def main():
    repo_root = find_repo_root()
    bench_dir = find_newest_clean_bench(repo_root)
    a1200_dir = os.path.join(bench_dir, "a1200")
    out_dir = os.path.join(repo_root, "docs", "screenshots")
    os.makedirs(out_dir, exist_ok=True)

    print(f"Source bench: {os.path.basename(bench_dir)}")
    print(f"Target directory: {out_dir}")

    files = [
        ("wizard-0-pal.iff", "wizard-0-pal.png"),
        ("wizard-1-pal.iff", "wizard-1-pal.png"),
        ("wizard-2-pal.iff", "wizard-2-pal.png"),
        ("wizard-3-pal.iff", "wizard-3-pal.png"),
        ("wizard-3-pal-static.iff", "wizard-3-pal-static.png"),
        ("wizard-4-pal.iff", "wizard-4-pal.png"),
        ("prefs-pal.iff", "prefs-pal.png"),
    ]

    converted = 0
    for src_name, dst_name in files:
        src_path = os.path.join(a1200_dir, src_name)
        dst_path = os.path.join(out_dir, dst_name)
        if not os.path.exists(src_path):
            print(f"  [SKIP] {src_name} not found in {a1200_dir}")
            continue
        convert_iff_to_png(src_path, dst_path, repo_root)
        size = os.path.getsize(dst_path)
        print(f"  [OK] {src_name} -> {dst_name} ({size} bytes)")
        converted += 1

    print(f"Total converted: {converted} screenshots.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
