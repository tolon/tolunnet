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
TIMEOUT_SECS="${TIMEOUT_SECS:-1500}"
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
        say "building (make release-stage)"
        wsl -d Ubuntu-24.04 -e bash -c "export PATH=/usr/bin:/bin:/usr/local/bin:/home/tolon/opt/m68k-amigaos/bin:\$PATH && cd /mnt/d/Projeler/tolunnet && make release-stage CROSS=$CROSS" >/dev/null \
            || die "make release-stage failed"
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

    # z.ai step 9a: the pristine image is 99.9% full - reclaim space from
    # stock/user content the headless bench never runs (ephemeral copy):
    # Storage driver variants and the Storage drawer itself, plus C:
    # user tools. Roughly 200 KB freed; the wizard's runtime writes
    # (IFF captures, config rewrites) need the headroom.
    # 10b item 1: the generated installer script is ~27 KB (was ~11 KB);
    # reclaim the stale typo'd tolunnet binaries, the packers and the
    # unused Installer (no installer row since 10a-3) to keep the HDF
    # writable for the wizard and undo rows.
    for junk in arc cdboot All2Lha butcher Amigatool AllowBad bigcli ArchEdge BootPic Bounce tolunet TolunetGet TolunetPing TolunetStatus DiskSalv3 viewtek vt Installer LZX DMS SnoopDos3 UnZip PPShow Zip Prod_Prep PKAzip LhA gzip ConfigOpus zoo specconvert DOpus_Disk ZShell KEY-MAPPER Lhwarp DOpus_Print DiskExpander MousoMeter flick lharc id Ed MagicWB-Demon keymapper2 UnDeletev2 PicBoot SystemTakeover Edit MuchMore UnARJ UUxT PlaySample intuitracker NUKE1.5 simdisk Lhunarc Startup-Menu DOpus_Icon lister TUDE Splice; do
        xd delete C/$junk >/dev/null 2>&1
    done
    # 10f item 2: stock Tools-drawer binaries and the Utilities
    # packers - heavy, and the headless bench never launches them.
for junk in Tools/BRU Tools/HDToolBox Tools/HDBackup Tools/HDBackup.help Tools/MEmacs Tools/IconEdit Tools/PrepCard Utilities/Packers/BlitzDms Utilities/Packers/CrM Utilities/Packers/CrMData Utilities/Packers/dirii 'Utilities/Packers/DiskMasher(GUI)'; do
        xd delete "$junk" >/dev/null 2>&1
    done
    xd list Storage 2>/dev/null | tail -n +2 | sed 's/^[[:space:]]*//;s/ .*//' | while read -r entry; do
        [ -z "$entry" ] && continue
        xd delete "Storage/$entry" >/dev/null 2>&1
    done
    xd delete Storage >/dev/null 2>&1

    xd delete C/tolunnet        >/dev/null 2>&1
    xd delete C/TolunnetPing    >/dev/null 2>&1
    xd delete C/ping            >/dev/null 2>&1
    xd delete C/TolunnetGet     >/dev/null 2>&1
    xd delete C/TolunnetControl >/dev/null 2>&1
    xd delete S/User-Startup    >/dev/null 2>&1
    xd delete S/Conformance-Script >/dev/null 2>&1
    xd delete S/Soak-Cycle      >/dev/null 2>&1
    xd delete Devs/tolunnet.config >/dev/null 2>&1
    # 11y item 2: soak stages the shipped binaries from the release
    # tree too (same rule as the conformance bench).
    SREL=build/release/tolunnet
    xd write "$SREL/C/tolunnet" C/tolunnet          || die "soak staging: tolunnet"
    xd write "$SREL/C/TolunnetPing" C/TolunnetPing  || die "soak staging: TolunnetPing"
    xd write "$SREL/C/ping" C/ping                  || die "soak staging: ping"
    xd write "$SREL/C/TolunnetGet" C/TolunnetGet    || die "soak staging: TolunnetGet"
    xd write "$SREL/C/TolunnetControl" C/TolunnetControl || die "soak staging: TolunnetControl"
    xd write ci/Soak-Cycle S/Soak-Cycle         || die "soak staging: cycle script"
    xd write ci/User-Startup-Soak S/Conformance-Script || die "soak staging: driver script"
    xd write ci/User-Startup-Boot S/User-Startup || die "soak staging: boot shim"
    xd write "$BENCH_CFG" Devs/tolunnet.config  || die "soak staging: config"
    say "soak staged -> $HDF_WIN"

    rm -f "$WORK_DIR/bench-done" "$WORK_DIR/soak.log" "$WORK_DIR/soak-daemon.log" \
          "$WORK_DIR/tolunnet-task.log" "$WORK_DIR/soak-avail-base.txt"

    CFG_WIN=$(cygpath -w "$REPO_ROOT/ci/tolunnet-a1200.uae")
    LIMIT="${SOAK_LIMIT_SECS:-$(( ${SOAK_HOURS:-4} * 3600 + 1800 ))}"
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
    SOAK_STATUS="PASS"

    if [ -f "$WORK_DIR/bench-done" ]; then
        MARKER="$(cat "$WORK_DIR/bench-done")"
        say "  Marker: $MARKER"
    else
        MARKER="WARNING (budget limit reached, no clean bench-done marker)"
        say "  Marker: $MARKER"
        SOAK_STATUS="FAIL"
    fi

    GURUS=$(grep -Eih "Guru|Software Failure|Address Error|Illegal Instruction" "$SOAK_DIR"/*.log 2>/dev/null | wc -l)
    say "  Guru/Exception lines: $GURUS"
    [ "$GURUS" -ne 0 ] && SOAK_STATUS="FAIL"

    NOT_OK=$(grep -F "not ok" "$SOAK_DIR"/soak.log 2>/dev/null | wc -l)
    say "  not ok count: $NOT_OK"
    [ "$NOT_OK" -ne 0 ] && SOAK_STATUS="FAIL"

    CYCLES=$(grep -F "=== CYCLE " "$SOAK_DIR"/soak.log 2>/dev/null | wc -l)
    EXPECTED_CYCLES=12
    say "  Cycles executed: $CYCLES (expected: $EXPECTED_CYCLES)"
    [ "$CYCLES" -ne "$EXPECTED_CYCLES" ] && SOAK_STATUS="FAIL"

    RESTARTS=$(grep -F "lwIP 2.2.0 initialized" "$SOAK_DIR"/tolunnet-task.log 2>/dev/null | wc -l)
    say "  lwIP initializations: $RESTARTS (target: >= 12)"
    [ "$RESTARTS" -lt 12 ] && SOAK_STATUS="FAIL"

    BASE_CHIP=$(grep -i "chip" "$SOAK_DIR"/soak-avail-base.txt 2>/dev/null | awk '{print $2}')
    LAST_CHIP=$(grep -i "chip" "$SOAK_DIR"/soak.log 2>/dev/null | tail -n 1 | awk '{print $2}')
    CHIP_DRIFT=0
    if [ -n "$BASE_CHIP" ] && [ -n "$LAST_CHIP" ]; then
        CHIP_DRIFT=$(( BASE_CHIP - LAST_CHIP ))
    fi

    BASE_FAST=$(grep -i "fast" "$SOAK_DIR"/soak-avail-base.txt 2>/dev/null | awk '{print $2}')
    CYCLE1_FAST=$(grep -i "fast" "$SOAK_DIR"/soak.log 2>/dev/null | sed -n '3p' | awk '{print $2}')
    LAST_FAST=$(grep -i "fast" "$SOAK_DIR"/soak.log 2>/dev/null | tail -n 1 | awk '{print $2}')
    DRIFT=0
    REF_FAST="${CYCLE1_FAST:-$BASE_FAST}"
    if [ -n "$REF_FAST" ] && [ -n "$LAST_FAST" ]; then
        DRIFT=$(( REF_FAST - LAST_FAST ))
        say "  Fast RAM reference: $REF_FAST, final: $LAST_FAST, drift: $DRIFT bytes (limit: <= 8192 bytes)"
        ABS_DRIFT=${DRIFT#-}
        if [ "$ABS_DRIFT" -gt 8192 ]; then
            say "  Fast RAM drift exceeds 8192 bytes limit!"
            SOAK_STATUS="FAIL"
        fi
    else
        say "  Fast RAM: missing snapshot for baseline or final"
        SOAK_STATUS="FAIL"
    fi

    # Emit SUMMARY.txt in $SOAK_DIR
    cat <<EOF > "$SOAK_DIR/SUMMARY.txt"
bench: session-profile soak (${SOAK_HRS}h, ${EXPECTED_CYCLES} sessions)
profile: a1200 / 68EC020 (TX_QUEUE=4)
duration_secs: ${el:-0}
cycles_executed: $CYCLES
lwip_initializations: $RESTARTS
guru_count: $GURUS
not_ok_count: $NOT_OK
chip_ram_drift_bytes: $CHIP_DRIFT
fast_ram_baseline: ${BASE_FAST:-0}
fast_ram_cycle1: ${CYCLE1_FAST:-0}
fast_ram_final: ${LAST_FAST:-0}
fast_ram_drift_cycle1_to_final: $DRIFT
status: $SOAK_STATUS
EOF

    say "  Final status: $SOAK_STATUS"
    if [ "$SOAK_STATUS" = "PASS" ]; then
        say "=== SOAK AUDIT: PASSED ==="
        exit 0
    else
        say "=== SOAK AUDIT: FAILED ==="
        exit 1
    fi
fi

for cfg in $CONFIGS; do
    grep -q '^bsdsocket_emu=false' "ci/tolunnet-$cfg.uae" \
        || die "ci/tolunnet-$cfg.uae lacks bsdsocket_emu=false"
done

if tasklist //FI "IMAGENAME eq winuae64.exe" 2>/dev/null | grep -qi winuae64; then
    die "a winuae64.exe is already running — close it first (the script kills by image name)"
fi

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    say "building (make release-stage)"
    wsl -d Ubuntu-24.04 -e bash -c "export PATH=/usr/bin:/bin:/usr/local/bin:/home/tolon/opt/m68k-amigaos/bin:\$PATH && cd /mnt/d/Projeler/tolunnet && make release-stage CROSS=$CROSS" >/dev/null \
        || die "make release-stage failed"
fi
[ -f build/tolunnet ] || die "build/tolunnet missing (run without SKIP_BUILD)"
[ -f build/SocketConformance ] || die "build/SocketConformance missing"
[ -f build/bsdsocktest ] || die "build/bsdsocktest missing (vendor/bsdsocktest)"
[ -f build/usergroup.library ] || die "build/usergroup.library missing"
[ -f build/nc ] || die "build/nc missing"
# 11y item 2: the shipped files must exist - the bench stages them
# from the release tree so it tests exactly what the archive ships.
[ -f build/release/tolunnet/C/tolunnet ] || die "release tree missing (make release-stage)"
[ -f build/release/tolunnet/C/nc ] || die "release C/nc missing"
[ -f build/release/tolunnet/Libs/usergroup.library ] || die "release Libs/usergroup.library missing"
[ -f build/release/tolunnet/Install_Tolunnet ] || die "release Install_Tolunnet missing"
[ -f build/whois ] || die "build/whois missing"
[ -f build/TolunnetGet ] || die "build/TolunnetGet missing"
[ -f build/ftp ] || die "build/ftp missing"
[ -f build/sntp ] || die "build/sntp missing"
[ -f build/traceroute ] || die "build/traceroute missing"
[ -f build/tftp ] || die "build/tftp missing"

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

BENCH_SUFFIX="${BENCH_SUFFIX:-}"
if [ -n "$BENCH_SUFFIX" ]; then
    STAMP="$(date +%Y%m%d-%H%M%S)-$GIT_DESC-$BENCH_SUFFIX"
else
    STAMP="$(date +%Y%m%d-%H%M%S)-$GIT_DESC"
fi
LOG_ROOT="docs/bench-logs/$STAMP"
mkdir -p "$LOG_ROOT"
# 11s item 1: the bench's own stdout lands in the log directory too -
# a timeout or a port failure must never be silent again.
exec > >(tee "$LOG_ROOT/bench-stdout.txt") 2>&1

MUFORCE_NOTE="done"
if [ -z "${MUFORCE_ADF:-}" ]; then
    MUFORCE_NOTE="SKIP (MuForce/Enforcer not present on this bench; MUFORCE_ADF unset)"
fi

# TNET-111: DNS_PORT is the loopback resolver port used by
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
    if [ -n "${NETSVC_PID:-}" ]; then
        say "stopping hermetic slirp host services (PID $NETSVC_PID)"
        kill -9 "$NETSVC_PID" 2>/dev/null || true
        wait "$NETSVC_PID" 2>/dev/null || true
        sleep 1
        python.exe ci/netsvc.py --check-free || say "warning: netsvc ports not free after shutdown"
    fi
    if [ "${SUCCESS:-0}" != "1" ] && [ -n "${LOG_ROOT:-}" ] && [ -d "$LOG_ROOT" ]; then
        say "run had failures; keeping log directory for analysis: $LOG_ROOT"
    fi
}
trap cleanup EXIT INT TERM

# ---- start hermetic slirp host mock services (ci/netsvc.py) -----------------
say "verifying netsvc ports are free"
python.exe ci/netsvc.py --check-free || python.exe ci/netsvc.py --kill-stale
python.exe ci/netsvc.py --check-free || die "netsvc ports not free before bench run (see HOLDER lines above)"
say "starting hermetic slirp host services (ci/netsvc.py)"
python.exe ci/netsvc.py --log "$LOG_ROOT/netsvc.log" &
NETSVC_PID=$!
say "probing netsvc readiness (5s limit)..."
python.exe ci/netsvc.py --probe || die "netsvc readiness probe failed"
say "netsvc ready (PID $NETSVC_PID)"

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

    # z.ai step 9a: the pristine image is 99.9% full - reclaim space from
    # stock/user content the headless bench never runs (ephemeral copy):
    # Storage driver variants and the Storage drawer itself, plus C:
    # user tools. Roughly 200 KB freed; the wizard's runtime writes
    # (IFF captures, config rewrites) need the headroom.
    # 10b item 1: the generated installer script is ~27 KB (was ~11 KB);
    # reclaim the stale typo'd tolunnet binaries, the packers and the
    # unused Installer (no installer row since 10a-3) to keep the HDF
    # writable for the wizard and undo rows.
    for junk in arc cdboot All2Lha butcher Amigatool AllowBad bigcli ArchEdge BootPic Bounce tolunet TolunetGet TolunetPing TolunetStatus DiskSalv3 viewtek vt Installer LZX DMS SnoopDos3 UnZip PPShow Zip Prod_Prep PKAzip LhA gzip ConfigOpus zoo specconvert DOpus_Disk ZShell KEY-MAPPER Lhwarp DOpus_Print DiskExpander MousoMeter flick lharc id Ed MagicWB-Demon keymapper2 UnDeletev2 PicBoot SystemTakeover Edit MuchMore UnARJ UUxT PlaySample intuitracker NUKE1.5 simdisk Lhunarc Startup-Menu DOpus_Icon lister TUDE Splice; do
        xd delete C/$junk >/dev/null 2>&1
    done
    # 10f item 2: stock Tools-drawer binaries and the Utilities
    # packers - heavy, and the headless bench never launches them.
for junk in Tools/BRU Tools/HDToolBox Tools/HDBackup Tools/HDBackup.help Tools/MEmacs Tools/IconEdit Tools/PrepCard Utilities/Packers/BlitzDms Utilities/Packers/CrM Utilities/Packers/CrMData Utilities/Packers/dirii 'Utilities/Packers/DiskMasher(GUI)'; do
        xd delete "$junk" >/dev/null 2>&1
    done
    xd list Storage 2>/dev/null | tail -n +2 | sed 's/^[[:space:]]*//;s/ .*//' | while read -r entry; do
        [ -z "$entry" ] && continue
        xd delete "Storage/$entry" >/dev/null 2>&1
    done
    xd delete Storage >/dev/null 2>&1

    xd delete C/tolunnet        >/dev/null 2>&1
    xd delete C/TolunnetControl >/dev/null 2>&1
    xd delete C/SocketConformance >/dev/null 2>&1
    xd delete C/TolunnetSetup   >/dev/null 2>&1
    xd delete C/TolunnetPrefs   >/dev/null 2>&1
    xd delete C/S2Toggle        >/dev/null 2>&1
    xd delete C/bsdsocktest     >/dev/null 2>&1
    xd delete C/nc              >/dev/null 2>&1
    xd delete C/telnet          >/dev/null 2>&1
    xd delete C/nslookup        >/dev/null 2>&1
    xd delete C/hostname        >/dev/null 2>&1
    xd delete C/TolunnetPing   >/dev/null 2>&1
    xd delete C/ping           >/dev/null 2>&1
    xd delete C/GetNetStatus    >/dev/null 2>&1
    xd delete C/ShowNetStatus   >/dev/null 2>&1
    xd delete C/route           >/dev/null 2>&1
    xd delete C/Online          >/dev/null 2>&1
    xd delete C/Offline         >/dev/null 2>&1
    xd delete C/NetShutdown     >/dev/null 2>&1
    xd delete C/whois           >/dev/null 2>&1
    xd delete C/TolunnetGet     >/dev/null 2>&1
    xd delete C/ftp             >/dev/null 2>&1
    xd delete C/sntp            >/dev/null 2>&1
    xd delete C/traceroute      >/dev/null 2>&1
    xd delete C/tftp            >/dev/null 2>&1
    xd delete Libs/usergroup.library >/dev/null 2>&1
    xd delete S/User-Startup    >/dev/null 2>&1
    xd delete S/Conformance-Script >/dev/null 2>&1
    xd delete S/Install_Tolunnet.script >/dev/null 2>&1
    xd delete Devs/tolunnet.config >/dev/null 2>&1
    # 11y item 2: SHIPPED files are staged from build/release/tolunnet
    # (stripped release tree, exactly what the .lha carries); test-only
    # tools (SocketConformance, S2Toggle, bsdsocktest) still come from
    # build/. REL= prefix for the release tree.
    REL=build/release/tolunnet
    xd write "$REL/C/tolunnet" C/tolunnet          || die "xdftool write tolunnet failed"
    xd write "$REL/C/TolunnetControl" C/TolunnetControl || die "xdftool write TolunnetControl failed"
    xd write build/SocketConformance C/SocketConformance || die "xdftool write conformance failed"
    xd write "$REL/C/TolunnetSetup" C/TolunnetSetup || die "xdftool write TolunnetSetup failed"
    xd write "$REL/TolunnetPrefs" C/TolunnetPrefs || die "xdftool write TolunnetPrefs failed"
    xd write build/S2Toggle C/S2Toggle || die "xdftool write S2Toggle failed"
    xd write build/bsdsocktest C/bsdsocktest || die "xdftool write bsdsocktest failed"
    xd write "$REL/C/nc" C/nc                     || die "xdftool write nc failed"
    xd write "$REL/C/telnet" C/telnet             || die "xdftool write telnet failed"
    xd write "$REL/C/nslookup" C/nslookup         || die "xdftool write nslookup failed"
    xd write "$REL/C/hostname" C/hostname         || die "xdftool write hostname failed"
    xd write "$REL/C/TolunnetPing" C/TolunnetPing || die "xdftool write TolunnetPing failed"
    xd write "$REL/C/ping" C/ping                 || die "xdftool write ping failed"
    xd write "$REL/C/GetNetStatus" C/GetNetStatus || die "xdftool write GetNetStatus failed"
    xd write "$REL/C/ShowNetStatus" C/ShowNetStatus || die "xdftool write ShowNetStatus failed"
    xd write "$REL/C/route" C/route               || die "xdftool write route failed"
    xd write "$REL/C/Online" C/Online             || die "xdftool write Online failed"
    xd write "$REL/C/Offline" C/Offline           || die "xdftool write Offline failed"
    xd write "$REL/C/NetShutdown" C/NetShutdown   || die "xdftool write NetShutdown failed"
    xd write "$REL/C/whois" C/whois               || die "xdftool write whois failed"
    xd write "$REL/C/TolunnetGet" C/TolunnetGet   || die "xdftool write TolunnetGet failed"
    xd write "$REL/C/ftp" C/ftp                   || die "xdftool write ftp failed"
    xd write "$REL/C/sntp" C/sntp                 || die "xdftool write sntp failed"
    xd write "$REL/C/traceroute" C/traceroute     || die "xdftool write traceroute failed"
    xd write "$REL/C/tftp" C/tftp                 || die "xdftool write tftp failed"
    xd write "$REL/Libs/usergroup.library" Libs/usergroup.library || die "xdftool write usergroup.library failed"
    xd write ci/User-Startup-Conformance S/Conformance-Script || die "xdftool write Conformance-Script failed"
    xd write ci/User-Startup-Boot S/User-Startup || die "xdftool write User-Startup failed"
    xd write "$REL/Install_Tolunnet" S/Install_Tolunnet.script || die "xdftool write Install_Tolunnet.script failed"
    # 10c item 2: the undo, proven in a T: sandbox by tc_undo_sandbox -
    # same script with every SYS:/S:/LIBS:/DEVS: path rewritten under T:tnsbx/
    python3 scripts/gen_installer.py --undo-root="T:tnsbx/" --undo-out="build/tolunnet-undo-sandbox" || die "undo sandbox generation failed"
    xd write build/tolunnet-undo-sandbox S/tolunnet-undo-sandbox || die "xdftool write tolunnet-undo-sandbox failed"
    # 10e item 3: the wizard/undo rows need writable headroom - the
    # 10b/10d HDF-full incidents must not come back silently.
    HDF_FREE_BYTES=$(xd info 2>/dev/null | awk '/^free:/ {print $4}')
    case "$HDF_FREE_BYTES" in (*[0-9]*) ;; (*) die "cannot read HDF free space" ;; esac
    HDF_FREE_KB=$((HDF_FREE_BYTES / 1024))
    say "hdf free: ${HDF_FREE_KB} KB"
    [ "$HDF_FREE_KB" -ge 1024 ] || die "HDF headroom ${HDF_FREE_KB} KB < 1024 KB after staging"
    xd write "$BENCH_CFG" Devs/tolunnet.config          || die "xdftool write tolunnet.config failed"
    say "staged: tolunnet + SocketConformance + bsdsocktest + cmds + usergroup.library + TolunnetSetup + TolunnetPrefs + Conformance-Script + User-Startup + tolunnet.config -> $HDF_WIN"

    # ---- run headless ---------------------------------------------------
    rm -f "$WORK_DIR/conformance.log" "$WORK_DIR/conformance2.log" "$WORK_DIR/bench-done" "$WORK_DIR/tolunnet-task.log" "$WORK_DIR/bsdsocktest.log" "$WORK_DIR"/wizard-*.iff "$WORK_DIR"/prefs-*.iff "$WORK_DIR"/crash-*.iff "$WORK_DIR"/prefs-crash.log "$WORK_DIR"/TolunnetPrefs.map
    # 11aa item 3: attach the Gotek disk set as DF0/DF1 when it is
    # built, and stage the expected list for tc_floppy_install.
    DISK1=$(ls "$REPO_ROOT"/build/tolunnet-*-disk1.adf 2>/dev/null | head -1)
    if [ -n "$DISK1" ] && [ -f "$DISK1" ]; then
        DISK2=$(ls "$REPO_ROOT"/build/tolunnet-*-disk2.adf 2>/dev/null | head -1)
        RUN_CFG="$REPO_ROOT/ci/.bench-floppy.uae"
        cp "$REPO_ROOT/ci/tolunnet-$cfg.uae" "$RUN_CFG"
        # the base configs disable the drives (floppy0type=-1);
        # re-enable DD drives and insert the images
        echo "floppy0type=0" >> "$RUN_CFG"
        echo "floppy1type=0" >> "$RUN_CFG"
        echo "floppy0=$(cygpath -w "$DISK1")" >> "$RUN_CFG"
        echo "floppy1=$(cygpath -w "$DISK2")" >> "$RUN_CFG"
        CFG_WIN=$(cygpath -w "$RUN_CFG")
        say "floppies attached: $(basename "$DISK1") DF0, $(basename "$DISK2") DF1"
        EXP="ci/.adf-expected.txt"
        : > "$EXP"
        while read -r mpath mdisk; do
            [ -z "$mpath" ] && continue
            [ "$mpath" = "Disk.info" ] && continue
            if [ -f "$REPO_ROOT/$mpath" ]; then mfile="$REPO_ROOT/$mpath"; else mfile="$REPO_ROOT/build/release/tolunnet/$mpath"; fi
            [ -f "$mfile" ] || continue
            echo "$mpath $(wc -c < "$mfile")" >> "$EXP"
        done < <(awk 'NF==2 && ($2=="1" || $2=="2")' "$REPO_ROOT/scripts/adf_manifest.txt")
        xd delete S/adf-expected.txt >/dev/null 2>&1
        xd write "$EXP" S/adf-expected.txt || die "staging adf-expected failed"
    fi
    CFG_WIN=${CFG_WIN:-$(cygpath -w "$REPO_ROOT/ci/tolunnet-$cfg.uae")}
    say "launching WinUAE headless ($CFG_WIN), timeout ${TIMEOUT_SECS}s"
    "$WINUAE" -f "$CFG_WIN" >/dev/null 2>&1 &

    waited=0
    leg_t0=$(date +%s)
    while [ ! -f "$WORK_DIR/bench-done" ]; do
        sleep 2
        waited=$((waited + 2))
        if [ $waited -ge "$TIMEOUT_SECS" ]; then
            say "TIMEOUT waiting for bench-done after ${TIMEOUT_SECS}s"
            break
        fi
    done
    leg_elapsed=$(( $(date +%s) - leg_t0 ))

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
    cp "$WORK_DIR"/prefs-*.iff  "$OUT/" 2>/dev/null || true
    cp "$WORK_DIR"/crash-*.iff "$OUT/" 2>/dev/null || true
    # 11a item 2: Prefs trap capture + linker map for PC->symbol
    cp "$WORK_DIR"/prefs-crash.log "$OUT/" 2>/dev/null || true
    cp build/TolunnetPrefs.map "$OUT/" 2>/dev/null || true
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
        echo "bench services: hermetic slirp host services (ci/netsvc.py); DNS_PORT=$BENCH_DNS_PORT"
    } > "$OUT/README.txt"
    {
        echo "elapsed: ${leg_elapsed}s"
        if [ ! -f "$WORK_DIR/bench-done" ]; then
            echo "TIMEOUT: leg killed after ${leg_elapsed}s"
        fi
        echo "config: ci/tolunnet-$cfg.uae (HDF copy staged from the pristine WB3.0 image)"
        echo "hdf free: ${HDF_FREE_KB} KB"
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

NET_TODO=$(grep -c 'net_.*# TODO' "$LOG_ROOT/a1200/conformance.log" 2>/dev/null || echo 0)
echo "net TODO remaining: $NET_TODO" >> "$LOG_ROOT/SUMMARY.txt"
say "net TODO remaining: $NET_TODO"

echo "$MUFORCE_NOTE" > "$LOG_ROOT/muforce.txt"
say "logs: $LOG_ROOT"
say "RESULT: $([ $fail -eq 0 ] && echo ALL-GREEN || echo HAS-FAILURES)"
[ $fail -eq 0 ] && SUCCESS=1
exit $fail
