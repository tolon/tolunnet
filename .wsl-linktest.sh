#!/bin/bash
export PATH=/home/tolon/opt/m68k-amigaos/bin:$PATH
cd /mnt/d/Projeler/tolunnet || exit 1
make all 2>&1 | grep "undefined reference" | head -6
