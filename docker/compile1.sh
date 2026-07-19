#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Compile a single AmiTCP_NG source file with the standard flag set. Usage: docker/compile1.sh src/kern/foo.c
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$1"; obj="build/obj/$(basename "${src%.c}").o"
mkdir -p "$ROOT/build/obj"
docker run --rm -v "$ROOT":/work -w /work amigadev/crosstools:m68k-amigaos bash -c '
  source docker/ccflags.sh
  m68k-amigaos-gcc -c "'"$src"'" -o "'"$obj"'" $NG_INC $NG_DEF $NG_CFLAGS \
    -include src/conf/rcs.h'
