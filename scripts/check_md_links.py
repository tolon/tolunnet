import os
import re
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BENCH_LOGS_DIR = os.path.join(REPO_ROOT, "docs", "bench-logs")

# Matches specific bench log directories like 20260905-183850-3082b59
LOG_PATTERN = re.compile(r"docs/bench-logs/([0-9]{8}-[0-9]{6}-[a-zA-Z0-9]+)")

def main():
    missing = []
    checked = 0

    for root, dirs, files in os.walk(REPO_ROOT):
        dirs[:] = [d for d in dirs if d not in (".git", "build", ".gemini", "node_modules", "history")]
        for f in files:
            if not f.endswith(".md"):
                continue
            if f.startswith("TN-") or "prompt" in f.lower():
                continue
            path = os.path.join(root, f)
            rel_path = os.path.relpath(path, REPO_ROOT)
            try:
                with open(path, "r", encoding="utf-8", errors="ignore") as fp:
                    content = fp.read()
            except Exception as e:
                print(f"Error reading {rel_path}: {e}", file=sys.stderr)
                continue

            for match in LOG_PATTERN.finditer(content):
                target_dir = match.group(1)
                full_target = os.path.join(BENCH_LOGS_DIR, target_dir)
                checked += 1
                if not os.path.isdir(full_target):
                    missing.append((rel_path, target_dir))

    print(f"[check_md_links] Checked {checked} bench log reference(s).")
    if missing:
        print(f"[check_md_links] ERROR: Found {len(missing)} broken bench log link(s):", file=sys.stderr)
        for doc, link in missing:
            print(f"  - In {doc}: docs/bench-logs/{link} (does not exist)", file=sys.stderr)
        return 1

    print("[check_md_links] OK: All referenced bench logs exist.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
