#!/bin/bash
cd /mnt/d/Projeler/tolunnet || exit 1
for t in build/host/test_*; do
    "./$t" > /dev/null 2>&1
    RC=$?
    if [ "$RC" -ne 0 ]; then
        echo "FAIL($RC): $t"
    fi
done
echo "CHECK_DONE"
