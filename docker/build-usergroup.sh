#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Build LIBS:usergroup.library -- the user/group/account library Roadshow ships
# and AmiTCP_NG previously did not (we shipped only its headers). Output:
# build/usergroup.library.
#
# Self-contained: it shares NOTHING with the stack objects except
# src/api/amiga_errlists.c, the errno string tables that ug_StrError() hands out
# (the same text bsdsocket.library returns for the same code -- worth having one
# copy of rather than two that can drift).
#
#   -nostartfiles : a library has no main(); it self-starts via its RomTag.
#   -e _start     : the "run me as a program -> return -1" safety stub.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG=amigadev/crosstools:m68k-amigaos
mkdir -p "$ROOT/build/obj-ug"
docker run --rm -e NG_ARCH -v "$ROOT":/work -w /work "$IMG" bash -c '
  source docker/ccflags.sh
  rm -f build/obj-ug/*.o
  fail=0
  for s in src/usergroup/ug_lib.c src/usergroup/ug_funcs.c src/usergroup/ug_db.c \
           src/api/amiga_errlists.c; do
    o="build/obj-ug/$(basename "${s%.c}").o"
    # rcs.h only: it defines the RCS_ID_C macro the borrowed amiga_errlists.c opens
    # with. NOT the whole $NG_FORCEINC -- amitcp_ng_bases.h declares the stack library
    # bases, which this library neither has nor wants.
    if ! m68k-amigaos-gcc -c "$s" -o "$o" $NG_INC -Isrc/usergroup $NG_DEF $NG_CFLAGS \
         -include src/conf/rcs.h 2>/tmp/e; then
      echo "COMPILE FAIL: $s"; cat /tmp/e; fail=1
    fi
  done
  [ "$fail" = 0 ] || { echo "compile errors -> abort"; exit 1; }
  m68k-amigaos-gcc -noixemul $NG_ARCH -nostartfiles -e _start \
      -o /work/build/usergroup.library build/obj-ug/*.o \
      -Wl,--allow-multiple-definition \
      /opt/m68k-amigaos/m68k-amigaos/lib/libamiga.a
  echo "linked: build/usergroup.library ($(wc -c < /work/build/usergroup.library) bytes)"
'
