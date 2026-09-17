#!/bin/bash
export PATH=/home/tolon/opt/m68k-amigaos/bin:$PATH
cd /mnt/d/Projeler/tolunnet || exit 1
make all 2>&1 | grep -E "error:|Error" | head -12
echo "RC=${PIPESTATUS[0]}"
ls -la build/tolunnet 2>/dev/null | awk '{print $5, $9}'
