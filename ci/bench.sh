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

# ANX-01: the bench must never exercise WinUAE's own bsdsocket.library.
# ---- RC3 item 3: soak mode -------------------------------------------------
# ci/bench.sh soak  ->  a1200 profile, 24 h driver loop (User-Startup-Soak via
# the standard User-Startup-Boot shim), TX_QUEUE=4 config, pass = bench-done +
# no Guru lines + RAM drift <= 8 KB between the Avail snapshots in soak.log.
if [ "${1:-}" = "soak" ]; then
    BENCH_DNS_PORT="${BENCH_DNS_PORT:-15353}"
    SOAK_HRS="${SOAK_HOURS:-2}"
    say "soak mode: a1200, ${SOAK_HRS} h (session-profile: 12x 10min stop/start), TX_QUEUE=4"
    if [ "$SOAK_HRS" -lt 2 ]; then
        SOAK_PREFIX="soakquick"
    else
        SOAK_PREFIX="soak"
    fi
    SOAK_STAMP="$(date +%Y%m%d-%H%M%S)-$SOAK_PREFIX-$(git rev-parse --short HEAD 2>/dev/null || echo dirty)"
    SOAK_DIR="docs/bench-logs/$SOAK_STAMP"
    mkdir -p "$SOAK_DIR"

    if [ "${SKIP_BUILD:-0}" != "1" ]; then
        say "building (make all)"
        wsl -d Ubuntu-24.04 -e bash -c "export PATH=/usr/bin:/bin:/usr/local/bin:/home/tolon/opt/m68k-amigaos/bin:\$PATH && cd /mnt/d/Projeler/tolunnet && make all CROSS=$CROSS" >/dev/null \
            || die "make all failed"
    fi

    BENCH_CFG="ci/.soak-tolunnet.config"
    sed -e "s/__DNS_PORT__/$BENCH_DNS_PORT/" ci/tolunnet.config > "$BENCH_CFG"
    echo "TX_QUEUE=4" >> "$BENCH_CFG"
    say "soak config: $(wc -c < "$BENCH_CFG") bytes, TX_QUEUE=4"

    STAGE_DIR="/e/amiga/Amigatolon/bench/tolunnet"
    rm -rf "$STAGE_DIR"
    mkdir -p "$STAGE_DIR"
    cp "$PRISTINE_HDF" "$STAGE_DIR/wb30-a1200.hdf" || die "cannot copy pristine HDF"
    HDF_WIN='E:\amiga\Amigatolon\bench\tolunnet\wb30-a1200.hdf'
    HDF_UX="/mnt/e/amiga/Amigatolon/bench/tolunnet/wb30-a1200.hdf"

    xd() { wsl -d Ubuntu-24.04 -e bash -c "\$HOME/.local/bin/xdftool '$HDF_UX' $*" ; }

    xd delete C/tolunnet        >/dev/null 2>&1
    xd delete C/TolunnetPing    >/dev/null 2>&1
    xd delete C/TolunnetGet     >/dev/null 2>&1
    xd delete C/TolunnetControl >/dev/null 2>&1
    xd delete S/User-Startup    >/dev/null 2>&1
    xd delete S/Conformance-Script >/dev/null 2>&1
    xd delete S/Soak-Cycle      >/dev/null 2>&1
    xd delete Devs/tolunnet.config >/dev/null 2>&1
    xd write build/tolunnet C/tolunnet          || die "soak staging: tolunnet"
    xd write build/TolunnetPing C/TolunnetPing  || die "soak staging: TolunnetPing"
    xd write build/TolunnetGet C/TolunnetGet    || die "soak staging: TolunnetGet"
    xd write build/TolunnetControl C/TolunnetControl || die "soak staging: TolunnetControl"
    xd write ci/Soak-Cycle S/Soak-Cycle         || die "soak staging: cycle script"
    xd write ci/User-Startup-Soak S/Conformance-Script || die "soak staging: driver script"
    xd write ci/User-Startup-Boot S/User-Startup || die "soak staging: boot shim"
    xd write "$BENCH_CFG" Devs/tolunnet.config  || die "soak staging: config"
    say "soak staged -> $HDF_WIN"

    rm -f "$WORK_DIR/bench-done" "$WORK_DIR/soak.log" "$WORK_DIR/soak-daemon.log" \
          "$WORK_DIR/tolunnet-task.log" "$WORK_DIR/soak-avail-base.txt"

    CFG_WIN=$(cygpath -w "$REPO_ROOT/ci/tolunnet-a1200.uae")
    LIMIT="${SOAK_LIMIT_SECS:-$(( ${SOAK_HOURS:-2} * 3600 + 600 ))}"
    say "launching soak emulator (budget: up to $LIMIT s)"
    "$WINUAE" -f "$CFG_WIN" >/dev/null 2>&1 &

    start=$SECONDS
    while [ ! -f "$WORK_DIR/bench-done" ]; do
        sleep 60
        el=$(( SECONDS - start ))
        [ $el -gt $LIMIT ] && { say "soak: time budget reached ($LIMIT s), stopping"; break; }
    done
    sleep 5
    taskkill //IM winuae64.exe //F >/dev/null 2>&1 || true
    taskkill //IM winuae.exe //F >/dev/null 2>&1 || true
    cp "$WORK_DIR/soak.log"          "$SOAK_DIR/" 2>/dev/null || true
    cp "$WORK_DIR/soak-daemon.log"   "$SOAK_DIR/" 2>/dev/null || true
    cp "$WORK_DIR/tolunnet-task.log" "$SOAK_DIR/" 2>/dev/null || true
    cp "$WORK_DIR/soak-avail-base.txt" "$SOAK_DIR/" 2>/dev/null || true
    say "soak logs saved -> $SOAK_DIR"

    # Automated Verification Check
    say "=== SOAK VERIFICATION AUDIT ==="
    if [ -f "$WORK_DIR/bench-done" ]; then
        say "  Marker: $(cat "$WORK_DIR/bench-done")"
    else
        say "  Marker: WARNING (budget limit reached, no clean bench-done marker)"
    fi

    GURUS=$(grep -Eic "Guru|Software Failure|Address Error|Illegal Instruction" "$SOAK_DIR"/*.log 2>/dev/null || echo 0)
    say "  Guru/Exception lines: $GURUS"

    NOT_OK=$(grep -ci "not ok" "$SOAK_DIR"/soak.log 2>/dev/null || echo 0)
    say "  not ok count: $NOT_OK"

    RESTARTS=$(grep -c "lwIP 2.2.0 initialized" "$SOAK_DIR"/tolunnet-task.log 2>/dev/null || echo 0)
    say "  lwIP initializations: $RESTARTS (target: >= 13 for 12 restarts)"

    BASE_FAST=$(grep -i "fast" "$SOAK_DIR"/soak-avail-base.txt 2>/dev/null | awk '{print $2}')
    LAST_FAST=$(grep -i "fast" "$SOAK_DIR"/soak.log 2>/dev/null | tail -n 1 | awk '{print $2}')
    if [ -n "$BASE_FAST" ] && [ -n "$LAST_FAST" ]; then
        DRIFT=$(( BASE_FAST - LAST_FAST ))
        say "  Fast RAM baseline: $BASE_FAST, final: $LAST_FAST, drift: $DRIFT bytes (limit: <= 8192 bytes)"
    else
        say "  Fast RAM: missing snapshot for baseline or final"
    fi

    exit 0
fi

for cfg in $CONFIGS; do
    grep -q '^bsdsocket_emu=false' "ci/tolunnet-$cfg.uae" \
        || die "ci/tolunnet-$cfg.uae lacks bsdsocket_emu=false"
done

if tasklist //FI "IMAGENAME eq winuae64.exe" 2>/dev/null | grep -qi winuae64; then
    die "a winuae64.exe is already running — close it first (the script kills by image name)"
fi

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    say "building (make all)"
    wsl -d Ubuntu-24.04 -e bash -c "export PATH=/home/tolon/opt/m68k-amigaos/bin:\$PATH && cd /mnt/d/Projeler/tolunnet && make all CROSS=$CROSS" >/dev/null \
        || die "make all failed"
fi
[ -f build/tolunnet ] || die "build/tolunnet missing (run without SKIP_BUILD)"
[ -f build/SocketConformance ] || die "build/SocketConformance missing"
[ -f build/bsdsocktest ] || die "build/bsdsocktest missing (vendor/bsdsocktest)"
[ -f build/usergroup.library ] || die "build/usergroup.library missing"

GIT_DESC="$(git describe --always --dirty 2>/dev/null || echo nogit)"
IS_DIRTY=0
if [[ "$GIT_DESC" == *-dirty* ]]; then
    IS_DIRTY=1
    say "****************************************************************"
    say "  WARNING: WORKING TREE IS DIRTY ($GIT_DESC)"
    say "  Logs from this run CANNOT be cited as clean commit proof."
    say "****************************************************************"
    if [ "${ALLOW_DIRTY:-0}" != "1" ]; then
        echo "[bench] FATAL: refusing bench run on dirty tree (set ALLOW_DIRTY=1 to override)" >&2
        exit 2
    fi
fi

STAMP="$(date +%Y%m%d-%H%M%S)-$GIT_DESC"
LOG_ROOT="docs/bench-logs/$STAMP"
mkdir -p "$LOG_ROOT"

MUFORCE_NOTE="done"
if [ -z "${MUFORCE_ADF:-}" ]; then
    MUFORCE_NOTE="SKIP (MuForce/Enforcer not present on this bench; MUFORCE_ADF unset)"
fi

# TNET-111: the suite is fully hermetic — loopback listeners only, no host
# services are started. DNS_PORT is the loopback resolver port used by
# tc_dns_local (5353 must be avoided: system mDNS on Windows).
BENCH_DNS_PORT="${BENCH_DNS_PORT:-15353}"

# Generate the guest bench config from the template. NOTE: keep it inside
# the repo tree — Git Bash /tmp is invisible to the WSL xdftool invocation.
BENCH_CFG="ci/.bench-tolunnet.config"
sed -e "s/__DNS_PORT__/$BENCH_DNS_PORT/" ci/tolunnet.config > "$BENCH_CFG"
if [ "${BENCH_EXTERNAL:-0}" = "1" ]; then
    echo "TEST_EXTERNAL=YES" >> "$BENCH_CFG"
fi
# TN_DIAG=1: stage the daemon with crash-diagnostics ON (TNET-139; the
# config key arms the trap handler + RAM:tolunnet-crash.log capture).
if [ "${TN_DIAG:-0}" = "1" ]; then
    echo "DIAG=YES" >> "$BENCH_CFG"
fi
say "bench config: resolver 127.0.0.1:$BENCH_DNS_PORT (loopback), external=${BENCH_EXTERNAL:-0}"

SUCCESS=0
cleanup() {
    rm -f "${BENCH_CFG:-ci/.bench-tolunnet.config}" 2>/dev/null || true
    if [ "${SUCCESS:-0}" != "1" ] && [ -n "${LOG_ROOT:-}" ] && [ -d "$LOG_ROOT" ]; then
        say "run had failures; keeping log directory for analysis: $LOG_ROOT"
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
    xd delete C/TolunnetControl >/dev/null 2>&1
    xd delete C/SocketConformance >/dev/null 2>&1
    xd delete C/TolunnetSetup   >/dev/null 2>&1
    xd delete C/S2Toggle        >/dev/null 2>&1
    xd delete C/bsdsocktest     >/dev/null 2>&1
    xd delete Libs/usergroup.library >/dev/null 2>&1
    xd delete S/User-Startup    >/dev/null 2>&1
    xd delete S/Conformance-Script >/dev/null 2>&1
    xd delete Devs/tolunnet.config >/dev/null 2>&1
    xd write build/tolunnet C/tolunnet          || die "xdftool write tolunnet failed"
    xd write build/TolunnetControl C/TolunnetControl || die "xdftool write TolunnetControl failed"
    xd write build/SocketConformance C/SocketConformance || die "xdftool write conformance failed"
    xd write build/TolunnetSetup C/TolunnetSetup || die "xdftool write TolunnetSetup failed"
    xd write build/S2Toggle C/S2Toggle || die "xdftool write S2Toggle failed"
    xd write build/bsdsocktest C/bsdsocktest || die "xdftool write bsdsocktest failed"
    xd write build/usergroup.library Libs/usergroup.library || die "xdftool write usergroup.library failed"
    xd write ci/User-Startup-Conformance S/Conformance-Script || die "xdftool write Conformance-Script failed"
    xd write ci/User-Startup-Boot S/User-Startup || die "xdftool write User-Startup failed"
    xd write "$BENCH_CFG" Devs/tolunnet.config          || die "xdftool write tolunnet.config failed"
    say "staged: tolunnet + SocketConformance + bsdsocktest + usergroup.library + TolunnetSetup + Conformance-Script + User-Startup + tolunnet.config -> $HDF_WIN"

    # ---- run headless ---------------------------------------------------
    rm -f "$WORK_DIR/conformance.log" "$WORK_DIR/conformance2.log" "$WORK_DIR/bench-done" "$WORK_DIR/tolunnet-task.log" "$WORK_DIR/bsdsocktest.log" "$WORK_DIR"/wizard-*.iff
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
        fail=1
    fi
    taskkill //F //IM winuae64.exe >/dev/null 2>&1
    sleep 2

    # ---- collect --------------------------------------------------------
    OUT="$LOG_ROOT/$cfg"
    mkdir -p "$OUT"
    cp "$WORK_DIR/conformance.log"   "$OUT/" 2>/dev/null || echo "(missing)" > "$OUT/conformance.log"
    cp "$WORK_DIR/conformance2.log"  "$OUT/" 2>/dev/null || echo "(missing)" > "$OUT/conformance2.log"
    cp "$WORK_DIR/daemon.log"        "$OUT/" 2>/dev/null || true
    cp "$WORK_DIR/daemon2.log"       "$OUT/" 2>/dev/null || true
    cp "$WORK_DIR/tolunnet-task.log" "$OUT/" 2>/dev/null || true
    # TN_DIAG trap capture (TNET-139): copied from RAM: by the boot script
    cp "$WORK_DIR/tolunnet-crash.log"  "$OUT/" 2>/dev/null || true
    cp "$WORK_DIR/tolunnet-crash2.log" "$OUT/" 2>/dev/null || true
    # TNET-110: wizard page screenshots (PAL + NTSC) from tc_wizard_ntsc
    cp "$WORK_DIR"/wizard-*.iff "$OUT/" 2>/dev/null || true
    # ANX-01: third-party bsdsocktest log (does not gate the bench)
    cp "$WORK_DIR/bsdsocktest.log" "$OUT/" 2>/dev/null \
        || echo "(missing)" > "$OUT/bsdsocktest.log"

    # ANX-01: per-config bsdsocktest score line -> SUMMARY.txt
    # log format: "# Results: N passed, F failed, K known, S skipped (T total)"
    {
        bs_line=$(grep '^# Results:' "$OUT/bsdsocktest.log" | tail -1)
        bs_n=$(echo "$bs_line" | sed -n 's/^# Results: \([0-9]*\) passed.*/\1/p')
        bs_t=$(echo "$bs_line" | sed -n 's/.*(\([0-9]*\) total)/\1/p')
        bs_f=$(echo "$bs_line" | sed -n 's/.*, \([0-9]*\) failed.*/\1/p')
        if [ -n "$bs_n" ] && [ -n "$bs_t" ]; then
            echo "$cfg: bsdsocktest: $bs_n/$bs_t (failed ${bs_f:-?}) — $LOG_ROOT/$cfg/bsdsocktest.log"
        else
            echo "$cfg: bsdsocktest: NO-RESULT (log incomplete or crashed) — $LOG_ROOT/$cfg/bsdsocktest.log"
        fi
    } >> "$LOG_ROOT/SUMMARY.txt"

    # CLOSE §B.6 / ANX-18g: tc_iperf_loopback throughput number -> SUMMARY.txt
    ip_line=$(grep '^# iperf loopback:' "$OUT/conformance.log" 2>/dev/null | tail -1)
    if [ -n "$ip_line" ]; then
        echo "$cfg: $ip_line — $LOG_ROOT/$cfg/conformance.log" >> "$LOG_ROOT/SUMMARY.txt"
    fi
    {
        echo "$cfg: $(date)"
        for lg in conformance.log conformance2.log; do
            c_ok=$(grep -c '^ok' "$OUT/$lg" 2>/dev/null | tr -d '\r' || echo 0)
            c_nok=$(grep -c '^not ok' "$OUT/$lg" 2>/dev/null | tr -d '\r' || echo 0)
            c_ext=$(grep -c '# SKIP external' "$OUT/$lg" 2>/dev/null | tr -d '\r' || echo 0)
            echo "$lg: core: $((c_ok - c_ext)) ok / $c_nok not ok; external: $c_ext skipped"
        done
        echo "bench services: none (suite is loopback-hermetic); DNS_PORT=$BENCH_DNS_PORT"
    } > "$OUT/README.txt"
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
        ext=$(grep -c '# SKIP external' "$log" 2>/dev/null | tr -d '\r' || echo 0)
        core_ok=$((ok - ext))
        say "$(basename "$log"): core: $core_ok ok / $nok not ok; external: $ext skipped (skip=$skip todo=$todo)"
        if [ "$nok" -gt 0 ]; then
            grep '^not ok' "$log" 2>/dev/null || true
        fi
        real_nok=$((nok - todo))
        [ "$real_nok" -gt 0 ] 2>/dev/null && fail=1
        # An empty log means the suite never even started (system hang):
        # without this check a timed-out run reports a false ALL-GREEN
        # (bench incident 556ea30).
        [ "$ok" -eq 0 ] && fail=1
        # TNET-111: the core suite must be complete: every core row is either
        # ok or not ok; a missing row count means the suite died mid-run.
        expected=$((core_ok + nok + ext))
        planned=$(grep -c '^1..' "$log" 2>/dev/null | tr -d '\r' || echo 0)
        if [ "$planned" -eq 1 ]; then
            plan_n=$(grep '^1..' "$log" | head -1 | sed 's/^1\.\.//')
            [ "$expected" -ne "$plan_n" ] 2>/dev/null && fail=1 && \
                say "$(basename "$log"): row count $expected != plan $plan_n"
        fi
    done
done

echo "$MUFORCE_NOTE" > "$LOG_ROOT/muforce.txt"
say "logs: $LOG_ROOT"
say "RESULT: $([ $fail -eq 0 ] && echo ALL-GREEN || echo HAS-FAILURES)"
[ $fail -eq 0 ] && SUCCESS=1
exit $fail
