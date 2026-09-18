#!/bin/bash
export PATH=/home/tolon/opt/m68k-amigaos/bin:$PATH
cd /mnt/d/Projeler/tolunnet || exit 1
m68k-amigaos-gcc -O2 -m68000 -msoft-float -noixemul -Ilwipopts -Iinclude -Iinclude/netinclude -Ivendor/lwip/src/include -Isrc -std=c11 build/src/cmds/hostname.o build/src/cmds/cmdlib.o -o /tmp/hostname_test -noixemul -msoft-float 2>&1 | head -12
echo "RC=$?"
