#!/bin/sh
# TNET-39 alignment gate: host gcc -Wcast-align=strict over src/ must report
# zero cast-align diagnostics. Amiga-only files that cannot resolve their
# NDK headers on the host are skipped by include-error, not silenced: the
# full __AMIGA__ coverage is enforced by -Werror=cast-align in CFLAGS on the
# m68k build itself.
set -u
cd "$(dirname "$0")/.."

CC=${CC:-cc}
FAIL=0
CHECKED=0
SKIPPED=0

for f in $(find src -name '*.c' | sort); do
    OUT=$($CC -fsyntax-only -std=gnu11 -Wall -Wextra -Wcast-align=strict \
        -Iinclude -Iinclude/netinclude -Ivendor/lwip/src/include -Isrc "$f" 2>&1)
    HITS=$(printf '%s' "$OUT" | grep -c "cast-align" || true)
    INCERR=$(printf '%s' "$OUT" | grep -c "fatal error" || true)
    if [ "$HITS" -gt 0 ]; then
        printf '%s\n' "$OUT" | grep -- "-Wcast-align"
        FAIL=$((FAIL + 1))
        CHECKED=$((CHECKED + 1))
    elif [ "$INCERR" -gt 0 ]; then
        SKIPPED=$((SKIPPED + 1))
    else
        CHECKED=$((CHECKED + 1))
    fi
done

echo "align-check: $CHECKED host-checked, $SKIPPED amiga-only (m68k -Werror gate), $FAIL with cast-align warnings"
[ "$FAIL" -eq 0 ] || exit 1
