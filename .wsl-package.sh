#!/bin/bash
export PATH=/home/tolon/opt/m68k-amigaos/bin:$PATH
cd /mnt/d/Projeler/tolunnet || exit 1
make package > /tmp/package.log 2>&1
echo "PACKAGE_RC=$?"
tail -5 /tmp/package.log
ls -la build/*.lha build/*.adf 2>/dev/null
echo "--- stripped vs unstripped ---"
ls -la build/tolunnet build/release/*/C/tolunnet 2>/dev/null
sha256sum build/tolunnet build/release/*/C/tolunnet 2>/dev/null
