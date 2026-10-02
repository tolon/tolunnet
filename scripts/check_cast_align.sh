#!/bin/sh
# TNET-39 alignment gate (9.1): three passes over src/, all must be clean.
#
# 1. host gcc -Wcast-align=strict (project include path incl. lwipopts).
# 2. every file the host cannot parse (NDK headers) is re-checked with the
#    m68k cross compiler and the exact build flags (-m68000 -Wcast-align
#    -Werror=cast-align + NDK). A file neither pass can parse FAILS the
#    gate; ALIGN_CHECK_NO_NDK=1 downgrades that to a loud SKIPPED list
#    (host-only machines without the cross toolchain).
# 3. source lint (both compilers are blind to it): a `)(void *)` hop and a
#    typed `*(T *)` dereference of imsg->ptrs / optval / argp (client
#    memory, any alignment) are banned unless listed, by exact source
#    line, in scripts/check-cast-known.txt. Byte-typed derefs are exempt.
#
# Usage: sh scripts/check_cast_align.sh [--lint-only]
#   --lint-only   run pass 3 only (no compiler needed; used by CI)
# Env: CC (host compiler), CROSS (m68k prefix, default m68k-amigaos-),
#      NDK_INC (-I<ndk-include>, auto-detected like the Makefile),
#      ALIGN_CHECK_NO_NDK=1 (see 2.)
set -u
cd "$(dirname "$0")/.."

FAIL=0

# ---- pass 3: cast-hop / client-pointer deref lint ------------------------
KNOWN=scripts/check-cast-known.txt
HOP_RE=')[[:space:]]*\([[:space:]]*(const[[:space:]]+)?void[[:space:]]*\*[[:space:]]*\)'
DEREF_RE='\*[[:space:]]*\([[:space:]]*(const[[:space:]]+)?[A-Za-z_][A-Za-z0-9_ ]*\*+[[:space:]]*\)[[:space:]]*\(?[[:space:]]*(imsg->ptrs|optval|argp)'
BYTE_RE='\([[:space:]]*(const[[:space:]]+)?(u8_t|uint8_t|UBYTE|BYTE|char|unsigned char|signed char)[[:space:]]*\*[[:space:]]*\)'

lint_hits() {
    grep -rnE -e "$HOP_RE" -e "$DEREF_RE" --include='*.c' --include='*.h' src |
    while IFS= read -r hit; do
        file=${hit%%:*}
        rest=${hit#*:}
        text=${rest#*:}
        # byte-typed deref of client memory is alignment-safe
        if printf '%s' "$text" | grep -qE "$DEREF_RE" &&
           ! printf '%s' "$text" | grep -qE "$HOP_RE" &&
           printf '%s' "$text" | grep -qE "$BYTE_RE"; then
            continue
        fi
        key="$file|$(printf '%s' "$text" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
        printf '%s\t%s\n' "$key" "$hit"
    done
}

LINT_NEW=0
LINT_WAIVED=0
TMP_HITS=$(mktemp)
lint_hits > "$TMP_HITS"
while IFS="$(printf '\t')" read -r key hit; do
    [ -z "$key" ] && continue
    if [ -f "$KNOWN" ] && grep -qxF -- "$key" "$KNOWN"; then
        LINT_WAIVED=$((LINT_WAIVED + 1))
    else
        echo "CAST-LINT $hit"
        LINT_NEW=$((LINT_NEW + 1))
    fi
done < "$TMP_HITS"
# waivers whose source line no longer exists are stale - report them
if [ -f "$KNOWN" ]; then
    grep -v '^#' "$KNOWN" | grep -v '^[[:space:]]*$' | while IFS= read -r w; do
        cut -f1 "$TMP_HITS" | grep -qxF -- "$w" ||
            echo "CAST-LINT stale waiver (line gone or fixed): $w"
    done
fi
rm -f "$TMP_HITS"
echo "cast-lint: $LINT_NEW unwaived, $LINT_WAIVED waived ($KNOWN)"
[ "$LINT_NEW" -eq 0 ] || FAIL=$((FAIL + 1))

if [ "${1:-}" = "--lint-only" ]; then
    [ "$FAIL" -eq 0 ] || exit 1
    exit 0
fi

# ---- pass 1: host strict ---------------------------------------------------
CC=${CC:-cc}
CROSS=${CROSS:-m68k-amigaos-}
M68K_CC="${CROSS}gcc"
if [ -z "${NDK_INC+x}" ]; then
    NDK_INC=""
    m68k_path=$(command -v "$M68K_CC" 2>/dev/null || true)
    if [ -n "$m68k_path" ] && [ -d "$(dirname "$m68k_path")/../m68k-amigaos/ndk-include" ]; then
        NDK_INC="-I$(dirname "$m68k_path")/../m68k-amigaos/ndk-include"
    fi
fi
HAVE_M68K=0
command -v "$M68K_CC" >/dev/null 2>&1 && HAVE_M68K=1
# a missing host compiler must not read as "every file clean"
command -v "$CC" >/dev/null 2>&1 || { echo "align-check: host compiler $CC not found"; exit 1; }

HOST_FLAGS="-Ilwipopts -Iinclude -Iinclude/netinclude -Ivendor/lwip/src/include -Isrc"
M68K_FLAGS="-O2 -m68000 -msoft-float -noixemul -Wall -Wextra -Wcast-align \
-Werror=cast-align -Ilwipopts -Iinclude -Iinclude/netinclude \
-Ivendor/lwip/src/include -Isrc $NDK_INC -std=c11"

HOST_CHECKED=0
M68K_CHECKED=0
SKIPPED=0
SKIP_LIST=""
ALIGN_FAIL=0

# not referenced by any build rule (see scripts/libnix_bases_lint.py)
NOT_BUILT="src/cmds/Install_Tolunnet_Launcher.c"

for f in $(find src -name '*.c' | sort); do
    case " $NOT_BUILT " in (*" $f "*) continue ;; esac
    # shellcheck disable=SC2086
    OUT=$($CC -fsyntax-only -std=gnu11 -Wall -Wextra -Wcast-align=strict \
        $HOST_FLAGS "$f" 2>&1)
    HITS=$(printf '%s' "$OUT" | grep -c "Wcast-align" || true)
    INCERR=$(printf '%s' "$OUT" | grep -c "fatal error" || true)
    if [ "$HITS" -gt 0 ]; then
        printf '%s\n' "$OUT" | grep -- "-Wcast-align"
        ALIGN_FAIL=$((ALIGN_FAIL + 1))
        HOST_CHECKED=$((HOST_CHECKED + 1))
        continue
    fi
    if [ "$INCERR" -eq 0 ]; then
        HOST_CHECKED=$((HOST_CHECKED + 1))
        continue
    fi
    # ---- pass 2: the host cannot see this file (NDK) -> m68k compiler
    if [ "$HAVE_M68K" -eq 1 ] && [ -n "$NDK_INC" ]; then
        # shellcheck disable=SC2086
        OUT=$($M68K_CC -fsyntax-only $M68K_FLAGS "$f" 2>&1)
        RC=$?
        if printf '%s' "$OUT" | grep -q "cast-align"; then
            printf '%s\n' "$OUT" | grep -- "cast-align"
            ALIGN_FAIL=$((ALIGN_FAIL + 1))
        elif [ "$RC" -ne 0 ]; then
            echo "align-check: m68k compile of $f FAILED (not checked):"
            printf '%s\n' "$OUT" | grep -E "error" | head -3
            ALIGN_FAIL=$((ALIGN_FAIL + 1))
        fi
        M68K_CHECKED=$((M68K_CHECKED + 1))
        continue
    fi
    SKIPPED=$((SKIPPED + 1))
    SKIP_LIST="$SKIP_LIST $f"
done

echo "align-check: $HOST_CHECKED host-strict, $M68K_CHECKED m68k (-Werror=cast-align), $SKIPPED unchecked, $ALIGN_FAIL failing"
if [ "$SKIPPED" -gt 0 ]; then
    echo "align-check: NOT CHECKED (no ${M68K_CC} / NDK on this host):"
    for f in $SKIP_LIST; do echo "    $f"; done
    if [ "${ALIGN_CHECK_NO_NDK:-0}" = "1" ]; then
        echo "align-check: ALIGN_CHECK_NO_NDK=1 - unchecked files tolerated"
    else
        echo "align-check: FAIL - install the cross toolchain (CROSS=...) or set ALIGN_CHECK_NO_NDK=1"
        FAIL=$((FAIL + 1))
    fi
fi
[ "$ALIGN_FAIL" -eq 0 ] || FAIL=$((FAIL + 1))
[ "$FAIL" -eq 0 ] || exit 1
exit 0
