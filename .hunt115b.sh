#!/bin/bash
# TNET-115 proof: 4 consecutive freeze-free full runs on the freeze-prone
# chip-only pistorm-68000 rig (the profile that froze 2/2 before the fix).
cd /d/Projeler/tolunnet || exit 1
freeze=0
for run in 1 2 3 4; do
    echo "RUN $run $(date +%H:%M:%S)"
    taskkill //IM winuae64.exe //F >/dev/null 2>&1
    sleep 2
    rm -f /e/amiga/Amigatolon/work/bsdsocktest.log /e/amiga/Amigatolon/work/freezewatch.log \
          /e/amiga/Amigatolon/work/fw-boot.log /e/amiga/Amigatolon/work/fw-out.log \
          /e/amiga/Amigatolon/work/conformance.log /e/amiga/Amigatolon/work/bench-done \
          /e/amiga/Amigatolon/work/tolunnet-task.log
    cmd //c start "" "C:\\Program Files\\WinUAE\\winuae64.exe" -f "D:\\Projeler\\tolunnet\\ci\\.repro115.uae" -conlogfile "D:\\Projeler\\tolunnet\\.dbg-con.log"
    done_seen=0
    for i in $(seq 1 45); do
        sleep 10
        if [ -f /e/amiga/Amigatolon/work/bench-done ]; then
            done_seen=1
            break
        fi
        d=$(grep -c '===' /e/amiga/Amigatolon/work/freezewatch.log 2>/dev/null)
        d=${d:-0}
        if [ "$d" -gt 0 ] 2>/dev/null; then
            echo "FREEZE-DETECTED run=$run"
            freeze=$((freeze + 1))
            break
        fi
    done
    if [ "$done_seen" = "1" ]; then
        n=$(grep -cE "^ok " /e/amiga/Amigatolon/work/conformance.log 2>/dev/null)
        echo "COMPLETE run=$run conformance-ok=$n"
    elif [ "$freeze" = "0" ]; then
        echo "TIMEOUT run=$run (no bench-done, no dump)"
        freeze=$((freeze + 1))
    fi
done
echo "RESULT: freeze_events=$freeze/4"
[ "$freeze" -eq 0 ] && exit 0 || exit 2
