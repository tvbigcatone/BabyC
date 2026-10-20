#!/bin/bash
# Build the complete BabyC source set with ASan/UBSan, reusing the authoritative
# Makefile source list (including IR/VM, POSIX, extended libc and SQLite).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-/tmp/babyc_asan}"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$HERE/../src" "$OUT/src"
cd "$OUT/src"
make clean
make -j4 all CFLAGS='-Wall -O0 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer -DENABLE_WTHREADS=1 -DENABLE_WSHMEM=1 -DUNIX_HOST -DWITH_CSTDLIB_EXT -DWITH_SQLITE -DVER=\"asan\" -Wno-incompatible-pointer-types -Wno-pointer-sign -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -I. -Iwinapi/include -Ithird_party/sqlite'
echo "built $OUT/src/babyc (ASan/UBSan)"
