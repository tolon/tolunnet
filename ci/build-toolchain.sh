#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# ci/build-toolchain.sh - build the amiga-gcc 6.5 toolchain tolunnet
# releases are made with (m68k-amigaos-gcc 6.5.0b), every project pinned to
# the commit of the release toolchain, so CI compiles with the same code
# generator as the published binaries. Only `min` (binutils, gcc, libnix,
# libgcc) and the NDK 3.2 headers are built.
#
#   PREFIX=/opt/amiga ci/build-toolchain.sh
#
# The CI cache key is the sha256 of this file: changing a pin rebuilds.
set -euo pipefail

PREFIX=${PREFIX:-/opt/amiga}
WORK=${TOOLCHAIN_WORK:-$HOME/amiga-gcc}

# repository                                      commit
PINS="
m68k-amigaos-gcc  8f3d94d0ca0971a72e808bf0f0b9ddfde317258a
binutils-gdb      e9ab8a4a62fc6fc6b486ff77040cafe00229c847
gcc               f8686cd7fb6c8ae1f136f165c4ea61fa7c4bc97a
libnix            193840dae1ad4a2d4dea1583339e4222bfd41465
newlib-cygwin     f137924c20bb5941464d84bea73414ddebfb7b12
fd2sfd            403714de37f1b9851475ab455964af355bcd6523
fd2pragma         e98f0901340b8989308ca31e53c02a6e61f8339e
"

pin() { # <AmigaPorts repo> <dir> -> shallow checkout of the pinned commit
    local sha
    sha=$(echo "$PINS" | awk -v r="$1" '$1 == r { print $2 }')
    [ -n "$sha" ] || { echo "no pin for $1" >&2; exit 1; }
    mkdir -p "$2"
    git -C "$2" init -q
    git -C "$2" remote add origin "https://github.com/AmigaPorts/$1" 2>/dev/null || true
    git -C "$2" fetch -q --depth 1 origin "$sha"
    git -C "$2" checkout -q FETCH_HEAD
    echo "pinned $1 @ ${sha:0:10}"
}

pin m68k-amigaos-gcc "$WORK"
# the amiga-gcc Makefile clones a project only when its directory is
# missing - pre-placed pinned checkouts are used as they are
pin binutils-gdb  "$WORK/projects/binutils"
pin gcc           "$WORK/projects/gcc"
pin libnix        "$WORK/projects/libnix"
pin newlib-cygwin "$WORK/projects/newlib-cygwin"
pin fd2sfd        "$WORK/projects/fd2sfd"
pin fd2pragma     "$WORK/projects/fd2pragma"

make -C "$WORK" min ndk PREFIX="$PREFIX" -j"$(nproc)"
"$PREFIX/bin/m68k-amigaos-gcc" --version | head -1
