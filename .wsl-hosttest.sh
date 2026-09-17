#!/bin/bash
cd /mnt/d/Projeler/tolunnet || exit 1
make test-host >/tmp/hosttest.log 2>&1
RC=$?
echo "HOSTTEST_RC=$RC"
grep -E "^(not ok|# FAILED|FAIL)" /tmp/hosttest.log | head -10
grep -E "sockaddr" /tmp/hosttest.log | head
echo ====ALIGN====
make align-check 2>&1 | tail -5
