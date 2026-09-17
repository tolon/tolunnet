#!/bin/bash
# On-freeze auto-capture: watch bsdsocktest for a stall, break into the
# WinUAE debugger, collect r/Tt/ExecBase/ROM-disasm, save everything.
cd /d/Projeler/tolunnet || exit 1
CAP="docs/bench-logs/tnet115-live-capture-$(date +%H%M%S)"
mkdir -p "$CAP"

last=0; stall=0
for i in $(seq 1 36); do
    sleep 10
    b=$(wc -l < /e/amiga/Amigatolon/work/bsdsocktest.log 2>/dev/null)
    b=${b:-0}
    if [ "$b" -ge 260 ] 2>/dev/null; then echo "GREEN"; exit 1; fi
    if [ "$b" -gt 0 ] 2>/dev/null && [ "$b" -eq "$last" ]; then
        stall=$((stall + 10))
    else
        stall=0
        last=$b
    fi
    if [ "$stall" -ge 60 ] 2>/dev/null; then
        echo "FROZEN b=$b - capturing"
        PID=$(powershell.exe -NoProfile -Command "(Get-Process winuae* | Select-Object -First 1).Id" | tr -d '\r\n ')
        [ -z "$PID" ] && { echo "no-instance"; exit 3; }
        powershell.exe -NoProfile -File "D:/Projeler/tolunnet/ci/debugger/hotkey-hard.ps1" "$PID" 6 2>&1 | tail -1
        sleep 3
        for cmd in "r" "Tt" "m 00c00b28 48" "d 00f85540 28"; do
            powershell.exe -NoProfile -File "D:/Projeler/tolunnet/ci/debugger/type-console.ps1" "$PID" "$cmd" >/dev/null 2>&1
            sleep 4
        done
        sleep 2
        powershell.exe -NoProfile -File "D:/Projeler/tolunnet/ci/debugger/dump-console.ps1" "$PID" "$CAP/console.txt" >/dev/null 2>&1
        cp /d/Projeler/tolunnet/.dbg-con.log "$CAP/conlog.txt" 2>/dev/null
        cp /e/amiga/Amigatolon/work/bsdsocktest.log /e/amiga/Amigatolon/work/freezewatch.log "$CAP/" 2>/dev/null
        echo "CAPTURED pid=$PID dir=$CAP (machine left frozen)"
        exit 0
    fi
done
echo "WINDOW-END"
exit 2
