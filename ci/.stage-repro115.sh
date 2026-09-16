#!/bin/bash
# Stage the TNET-115 live-capture HDF (bench set + DIAG=YES config)
set -e
cd /mnt/d/Projeler/tolunnet || exit 1

HDF_UX=/mnt/e/amiga/Amigatolon/bench/tolunnet/wb30-repro115.hdf
PRISTINE="/mnt/e/amiga/Amigatolon/hdf/Workbench v3.0 (1992)(Commodore).hdf"

for f in ci/User-Startup-Boot ci/User-Startup-Conformance; do
  tr -d '\r' < "$f" > /tmp/$(basename "$f").lf
done

rm -f "$HDF_UX"
cp "$PRISTINE" "$HDF_UX"

xd() { "$HOME/.local/bin/xdftool" "$HDF_UX" "$@"; }

xd delete C/tolunnet            >/dev/null 2>&1 || true
xd delete C/SocketConformance   >/dev/null 2>&1 || true
xd delete C/TolunnetSetup       >/dev/null 2>&1 || true
xd delete C/bsdsocktest         >/dev/null 2>&1 || true
xd delete S/User-Startup        >/dev/null 2>&1 || true
xd delete S/Conformance-Script  >/dev/null 2>&1 || true
xd delete Devs/tolunnet.config  >/dev/null 2>&1 || true
xd write build/tolunnet C/tolunnet
xd write build/SocketConformance C/SocketConformance
xd write build/TolunnetSetup C/TolunnetSetup
xd write build/bsdsocktest C/bsdsocktest
xd write build/FreezeWatch C/FreezeWatch
cat > /tmp/user-startup115 <<'US'
;BEGIN tolunnet TNET-115 capture
Echo >WORK:fw-boot.log "startup reached"
Run >WORK:fw-out.log <>NIL: C:FreezeWatch
Run <NIL: >NIL: Execute S:Conformance-Script
;END
US
tr -d '\r' < /tmp/user-startup115 > /tmp/user-startup115.lf
xd write /tmp/user-startup115.lf S/User-Startup
xd write /tmp/User-Startup-Conformance.lf S/Conformance-Script

cat > /tmp/repro115.config <<'CFG'
DEVICE=ethernet.device
UNIT=0
DHCP=YES
DNS=127.0.0.1
DNS_PORT=15353
LOG=WORK:tolunnet-task.log
DIAG=YES
CFG
xd write /tmp/repro115.config Devs/tolunnet.config
echo "STAGED repro115 OK (DIAG on)"
