#!/usr/bin/env bash
# ci/bench.sh — headless WinUAE bench for the SocketConformance suite (Round 3 §B.3).
#
# Per run: build (unless SKIP_BUILD=1) -> stage a COPY of the pristine WB 3.0
# HDF (xdftool: daemon + conformance + User-Startup) -> run WinUAE headless
# -> wait for WORK:bench-done -> collect logs into docs/bench-logs/<stamp>/.
# Two configurations are mandatory: A1200/68020 and A600/68000-NTSC.
#
# Usage (from the repo root, Git Bash on Windows):
#   ./ci/bench.sh                  # both configs
#   CONFIGS="a1200" ./ci/bench.sh  # one config
#   SKIP_BUILD=1 ./ci/bench.sh     # reuse build/ as-is
#
# MuForce/Enforcer pass: not automated yet — the tool is not present on this
# bench (searched 2026-09-05). Set MUFORCE_ADF=<path> to enable the hook when
# it is supplied; until then the second pass prints an explicit SKIP note.
#
# Quit mechanism: the bench HDF has no ARexx (C:RX), so WinUAE is terminated
# from the host after the bench-done marker + grace period. This is safe:
# the bench writes nothing to DH0 at runtime (all logs go to WORK:, a host
# directory). The script REFUSES to start if a winuae64.exe is already
# running (it kills by image name at the end).

set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

CROSS="${CROSS:-/home/tolon/opt/m68k-amigaos/bin/m68k-amigaos-}"
WINUAE="/c/Program Files/WinUAE/winuae64.exe"
WORK_DIR="/e/amiga/Amigatolon/work"
PRISTINE_HDF="/e/amiga/Amigatolon/hdf/Workbench v3.0 (1992)(Commodore).hdf"
STAGE_DIR="/e/amiga/Amigatolon/bench/tolunnet"
XDF="wsl -e bash -c '\$HOME/.local/bin/xdftool'"
TIMEOUT_SECS="${TIMEOUT_SECS:-600}"
GRACE_SECS="${GRACE_SECS:-8}"
CONFIGS="${CONFIGS:-a1200 68000}"

say() { echo "[bench] $*"; }

die() { echo "[bench] FATAL: $*" >&2; exit 1; }

[ -x "$WINUAE" ] || die "winuae64.exe not found at $WINUAE"
[ -f "$PRISTINE_HDF" ] || die "pristine bench HDF not found"
command -v wsl >/dev/null || die "wsl not available (xdftool runs in WSL)"

if tasklist //FI "IMAGENAME eq winuae64.exe" 2>/dev/null | grep -qi winuae64; then
    die "a winuae64.exe is already running — close it first (the script kills by image name)"
fi

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    say "building (make all)"
    wsl -d Ubuntu-24.04 -e bash -c "cd /mnt/d/Projeler/tolunnet && make all CROSS=$CROSS" >/dev/null \
        || die "make all failed"
fi
[ -f build/tolunnet ] || die "build/tolunnet missing (run without SKIP_BUILD)"
[ -f build/SocketConformance ] || die "build/SocketConformance missing"

GIT_DESC="$(git describe --always --dirty 2>/dev/null || echo nogit)"
IS_DIRTY=0
if [[ "$GIT_DESC" == *-dirty* ]]; then
    IS_DIRTY=1
    say "****************************************************************"
    say "  WARNING: WORKING TREE IS DIRTY ($GIT_DESC)"
    say "  Logs from this run CANNOT be cited as clean commit proof."
    say "****************************************************************"
fi

STAMP="$(date +%Y%m%d-%H%M%S)-$GIT_DESC"
LOG_ROOT="docs/bench-logs/$STAMP"
mkdir -p "$LOG_ROOT"

MUFORCE_NOTE="done"
if [ -z "${MUFORCE_ADF:-}" ]; then
    MUFORCE_NOTE="SKIP (MuForce/Enforcer not present on this bench; MUFORCE_ADF unset)"
fi

# Start background host HTTP server for live TCP test (Round 4 §C8)
say "starting host HTTP server on port 8000"
python -m http.server 8000 --bind 0.0.0.0 >/dev/null 2>&1 &
HTTP_PID=$!
SUCCESS=0
cleanup() {
    if [ -n "${HTTP_PID:-}" ]; then
        kill "$HTTP_PID" 2>/dev/null || true
    fi
    if [ "${SUCCESS:-0}" != "1" ] && [ -n "${LOG_ROOT:-}" ] && [ -d "$LOG_ROOT" ]; then
        say "run failed; removing incomplete log directory: $LOG_ROOT"
        rm -rf "$LOG_ROOT"
    fi
}
trap cleanup EXIT INT TERM

fail=0
for cfg in $CONFIGS; do
    say "===== config: $cfg ====="

    # ---- stage the HDF copy -------------------------------------------
    say "staging HDF copy for $cfg"
    rm -rf "$STAGE_DIR"
    mkdir -p "$STAGE_DIR"
    cp "$PRISTINE_HDF" "$STAGE_DIR/wb30-$cfg.hdf" || die "cannot copy pristine HDF"
    HDF_WIN='E:\amiga\Amigatolon\bench\tolunnet\wb30-'"$cfg"'.hdf'
    HDF_UX="/mnt/e/amiga/Amigatolon/bench/tolunnet/wb30-$cfg.hdf"

    xd() { wsl -d Ubuntu-24.04 -e bash -c "\$HOME/.local/bin/xdftool '$HDF_UX' $*" ; }

    xd delete C/tolunnet        >/dev/null 2>&1
    xd delete C/SocketConformance >/dev/null 2>&1
    xd delete S/User-Startup    >/dev/null 2>&1
    xd delete Devs/tolunnet.config >/dev/null 2>&1
    xd write build/tolunnet C/tolunnet          || die "xdftool write tolunnet failed"
    xd write build/SocketConformance C/SocketConformance || die "xdftool write conformance failed"
    xd write ci/User-Startup-Conformance S/User-Startup  || die "xdftool write User-Startup failed"
    xd write ci/tolunnet.config Devs/tolunnet.config     || die "xdftool write tolunnet.config failed"
    say "staged: tolunnet + SocketConformance + User-Startup + tolunnet.config -> $HDF_WIN"

    # ---- run headless ---------------------------------------------------
    rm -f "$WORK_DIR/conformance.log" "$WORK_DIR/conformance2.log" "$WORK_DIR/bench-done"
    CFG_WIN=$(cygpath -w "$REPO_ROOT/ci/tolunnet-$cfg.uae")
    say "launching WinUAE headless ($CFG_WIN), timeout ${TIMEOUT_SECS}s"
    "$WINUAE" -f "$CFG_WIN" >/dev/null 2>&1 &

    waited=0
    while [ ! -f "$WORK_DIR/bench-done" ]; do
        sleep 2
        waited=$((waited + 2))
        if [ $waited -ge "$TIMEOUT_SECS" ]; then
            say "TIMEOUT waiting for bench-done after ${TIMEOUT_SECS}s"
            break
        fi
    done

    if [ -f "$WORK_DIR/bench-done" ]; then
        say "bench-done marker seen after ${waited}s; grace ${GRACE_SECS}s then quit"
        sleep "$GRACE_SECS"
    else
        say "quitting stuck emulator"
    fi
    taskkill //F //IM winuae64.exe >/dev/null 2>&1
    sleep 2

    # ---- collect --------------------------------------------------------
    OUT="$LOG_ROOT/$cfg"
    mkdir -p "$OUT"
    cp "$WORK_DIR/conformance.log"  "$OUT/" 2>/dev/null || echo "(missing)" > "$OUT/conformance.log"
    cp "$WORK_DIR/conformance2.log" "$OUT/" 2>/dev/null || echo "(missing)" > "$OUT/conformance2.log"
    cp "$WORK_DIR/daemon.log"       "$OUT/" 2>/dev/null || true
    cp "$WORK_DIR/daemon2.log"      "$OUT/" 2>/dev/null || true
    echo "$cfg: $(date)" > "$OUT/README.txt"
    {
        echo "config: ci/tolunnet-$cfg.uae (HDF copy staged from the pristine WB3.0 image)"
        echo "commit: $(git rev-parse HEAD 2>/dev/null)"
        echo "describe: $GIT_DESC"
        echo "dirty: $([ $IS_DIRTY -eq 1 ] && echo YES || echo NO)"
        echo "MuForce pass: $MUFORCE_NOTE"
        if [ $IS_DIRTY -eq 1 ]; then
            echo "--- git status --short ---"
            git status --short 2>/dev/null || true
            echo "--------------------------"
        fi
    } >> "$OUT/README.txt"

    # ---- summary --------------------------------------------------------
    for log in "$OUT/conformance.log" "$OUT/conformance2.log"; do
        ok=$(grep -c '^ok' "$log" 2>/dev/null | tr -d '\r' || echo 0)
        nok=$(grep -c '^not ok' "$log" 2>/dev/null | tr -d '\r' || echo 0)
        skip=$(grep -c '# SKIP' "$log" 2>/dev/null | tr -d '\r' || echo 0)
        todo=$(grep -c '# TODO' "$log" 2>/dev/null | tr -d '\r' || echo 0)
        say "$(basename "$log"): ok=$ok not_ok=$nok skip=$skip todo=$todo"
        real_nok=$((nok - todo))
        [ "$real_nok" -gt 0 ] 2>/dev/null && fail=1
    done
done

echo "$MUFORCE_NOTE" > "$LOG_ROOT/muforce.txt"
say "logs: $LOG_ROOT"
say "RESULT: $([ $fail -eq 0 ] && echo ALL-GREEN || echo HAS-FAILURES)"
[ $fail -eq 0 ] && SUCCESS=1
exit $fail
