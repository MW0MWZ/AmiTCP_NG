#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Build the self-starting LIBS:bsdsocket.library (the drop-in library form of the
# stack). Reuses the object files from ./docker/build.sh, adds the RomTag/AUTOINIT
# skeleton (src/lib/bsdsocket_lib.c), and links -nostartfiles into a loadable
# Amiga library. Output: build/bsdsocket.library.
#
#   -nostartfiles : no C crt0 -- a library has no main(); it self-starts via its
#                   RomTag. We provide SysBase/exit ourselves (src/lib/).
#   -e _start     : the "run me as a program -> return -1" safety stub.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG=amigadev/crosstools:m68k-amigaos
# (re)build the object files + the amitcp program. build.sh is chatty on stdout AND
# reports compile errors there, so capture it and only echo it back if it fails --
# never silently swallow it (a hidden compile error once shipped a stale library).
# The capture file lives under build/ (repo-local, gitignored) -- never in /tmp.
mkdir -p "$ROOT/build"
BUILD_LOG="$ROOT/build/.build-lib.log"
if ! "$ROOT/docker/build.sh" >"$BUILD_LOG" 2>&1; then
  echo "docker/build.sh FAILED:" >&2
  cat "$BUILD_LOG" >&2
  rm -f "$BUILD_LOG"
  exit 1
fi
rm -f "$BUILD_LOG"
docker run --rm -v "$ROOT":/work -w /work "$IMG" bash -c '
  source docker/ccflags.sh
  m68k-amigaos-gcc -c src/lib/bsdsocket_lib.c -o build/obj/bsdsocket_lib.o \
      $NG_INC -Isrc $NG_DEF $NG_CFLAGS
  cd build/obj
  m68k-amigaos-gcc -noixemul -m68000 -nostartfiles -e _start \
      -o /work/build/bsdsocket.library *.o \
      -Wl,--allow-multiple-definition \
      /opt/m68k-amigaos/m68k-amigaos/lib/libamiga.a
  echo "linked: build/bsdsocket.library ($(wc -c < /work/build/bsdsocket.library) bytes)"
'
