#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# AmiTCP_NG full build: AmiTCP 3.0b2 core -> `amitcp` (installs bsdsocket.library).
# Toolchain: amigadev/crosstools:m68k-amigaos (bebbo gcc 6.5). All steps run in a
# disposable (--rm) container. Output: build/amitcp (AmigaOS Hunk executable).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG=amigadev/crosstools:m68k-amigaos
"$ROOT/docker/gen_config_var.sh"          # regenerate the config-variable table
# NG_ARCH selects the CPU multilib (default -m68000); forward it so ccflags.sh inside
# the container picks up the variant build-release.sh asked for.
docker run --rm -e NG_ARCH -v "$ROOT":/work -w /work "$IMG" bash -c '
  source docker/ccflags.sh
  mkdir -p build/obj
  # --- compile every translation unit ---
  srcs="$(ls src/api/*.c src/kern/*.c src/net/*.c src/netinet/*.c) src/amitcp_ng_glue.c"
  fail=0
  for s in $srcs; do
    o="build/obj/$(basename "${s%.c}").o"
    if ! m68k-amigaos-gcc -c "$s" -o "$o" $NG_INC $NG_DEF $NG_CFLAGS $NG_FORCEINC 2>/tmp/e; then
      echo "COMPILE FAIL: $s"; grep -m1 -iE "error:|fatal" /tmp/e; fail=1
    fi
  done
  [ "$fail" = 0 ] || { echo "compile errors -> abort"; exit 1; }
  echo "compiled $(ls build/obj/*.o | wc -l) objects"
  # --- link the stack program (installs bsdsocket.library at runtime) ---
  #  -noixemul                  : link libnix (light runtime).
  #  $NG_ARCH                   : CPU multilib (-m68000 default; -m68020/-m68040 variants).
  #  --allow-multiple-definition: AmiTCP ships its own ultoa (also in libnix)
  #  libamiga.a (explicit path) : ROM-call stubs (Amiga2Date, ...)
  #  NOTE: on -m68000 __mulsi3/__divsi3 call utility.library (SMult32/UMult32) via
  #        UtilityBase, which amiga_main.c opens up front (see the PORT note there);
  #        an 020+ build emits native muls.l/divs.l instead, so those stubs go unused.
  cd build/obj && m68k-amigaos-gcc -noixemul $NG_ARCH -o ../amitcp *.o \
      -Wl,--allow-multiple-definition \
      /opt/m68k-amigaos/m68k-amigaos/lib/libamiga.a && cd ../..
  echo "linked: build/amitcp"; file build/amitcp
'
