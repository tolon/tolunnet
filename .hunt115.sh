#!/bin/bash
# TNET-115 freeze hunter v2: when bsdsocktest stalls mid-suite, WAIT for the
# FreezeWatch dump instead of killing the instance; capture all evidence.
cd /d/Projeler/tolunnet || exit 1
CAPDIR="docs/bench-logs/tnet115-capture-$(date +%H%M%S)"
for attempt in $(seq 1 10); do
    echo "ATTEMPT $attempt $(date +%H:%M:%S)"
    taskkill //IM winuae64.exe //F >/dev/null 2>&1
    sleep 2
    rm -f /e/amiga/Amigatolon/work/bsdsocktest.log /e/amiga/Amigatolon/work/tolunnet-task.log \
          /e/amiga/Amigatolon/work/conformance.log /e/amiga/Amigatolon/work/bench-done \
          /e/amiga/Amigatolon/work/freezewatch.log /e/amiga/Amigatolon/work/fw-boot.log \
          /e/amiga/Amigatolon/work/fw-out.log
    cmd //c start "" "C:\Program Files\WinUAE\winuae64.exe" -f "D:\Projeler\tolunnet\ci\.repro115.uae" -conlogfile "D:\Projeler\tolunnet\.dbg-con.log"

    last_b=0; stall=0
    for i in $(seq 1 40); do
        sleep 10
        b=$(wc -l < /e/amiga/Amigatolon/work/bsdsocktest.log 2>/dev/null)
        b=${b:-0}
        d=$(grep -c '===' /e/amiga/Amigatolon/work/freezewatch.log 2>/dev/null)
        d=${d:-0}
        if [ "$d" -gt 0 ] 2>/dev/null; then
            echo "CAPTURED attempt=$attempt"
            mkdir -p "$CAPDIR"
            cp /e/amiga/Amigatolon/work/freezewatch.log /e/amiga/Amigatolon/work/bsdsocktest.log \
               /e/amiga/Amigatolon/work/tolunnet-task.log "$CAPDIR"/ 2>/dev/null
            echo "EVIDENCE: $CAPDIR"
            exit 0
        fi
        if [ "$b" -ge 260 ] 2>/dev/null; then
            echo "GREEN attempt=$attempt"
            break
        fi
        if [ "$b" -gt 0 ] 2>/dev/null && [ "$b" -eq "$last_b" ]; then
            stall=$((stall + 10))
            if [ "$stall" -eq 60 ]; then
                # frozen 60 s: poke the in-machine dumper from the host side
                echo "STALLED b=$b - poking WORK:dumptasks"
                : > /e/amiga/Amigatolon/work/dumptasks
            fi
        else
            stall=0
        fi
        last_b=$b
    done
done
echo "NO-CAPTURE-IN-10"
exit 2
