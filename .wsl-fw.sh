#!/bin/bash
export PATH=/home/tolon/opt/m68k-amigaos/bin:$PATH
cd /mnt/d/Projeler/tolunnet || exit 1
make build/FreezeWatch 2>&1 | tail -4
ls -la build/FreezeWatch 2>/dev/null
