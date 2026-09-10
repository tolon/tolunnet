#!/bin/bash
# Stage the TNET-139 repro HDF — param $1: binary to stage (default build/tolunnet)
set -e
cd /mnt/d/Projeler/tolunnet || exit 1
BIN="${1:-build/tolunnet}"

HDF_UX=/mnt/e/amiga/Amigatolon/bench/tolunnet/wb30-repro139.hdf
PRISTINE="/mnt/e/amiga/Amigatolon/hdf/Workbench v3.0 (1992)(Commodore).hdf"

# strip CR from AmigaDOS scripts (CR silently breaks If-blocks)
for f in ci/.repro139-startup ci/.repro139-script; do
  tr -d '\r' < "$f" > /tmp/$(basename "$f").lf
done

rm -f "$HDF_UX"
cp "$PRISTINE" "$HDF_UX"

xd() { "$HOME/.local/bin/xdftool" "$HDF_UX" "$@"; }

xd delete C/tolunnet          >/dev/null 2>&1 || true
xd delete S/User-Startup      >/dev/null 2>&1 || true
xd delete S/Repro139-Script   >/dev/null 2>&1 || true
xd delete Devs/tolunnet.config >/dev/null 2>&1 || true
xd write "$BIN" C/tolunnet
xd write /tmp/.repro139-startup.lf S/User-Startup
xd write /tmp/.repro139-script.lf S/Repro139-Script
xd write ci/.repro139-config Devs/tolunnet.config
echo "STAGED repro139 OK with $BIN"
