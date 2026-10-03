#!/bin/sh
# show.sh [gcc driver and extra args...]
# Prints the zero stores each function of cases.c compiles to, for
# -m68000 and -m68080. Default driver: m68k-amigaos-gcc.
#   ./show.sh                                  installed compiler
#   ./show.sh m68k-amigaos-gcc -B/path/to/dir/ a patched cc1 in that dir
cd "$(dirname "$0")" || exit 1
[ $# -eq 0 ] && set -- m68k-amigaos-gcc
for cpu in -m68000 -m68080; do
  echo "== $cpu"
  "$@" -O2 $cpu -S cases.c -o - | awk '
    /^_[a-z_0-9]+:/ { fn = $1 }
    /clr|move\.l #0/ && fn != "" { gsub(/\t/, " "); a[fn] = a[fn] " " $0 }
    END { for (f in a) print f a[f] }' | sort
done
