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
docker run --rm -e NG_ARCH -e NG_CKSUM_ASM -e NG_SOCKBUF_DEBUG -v "$ROOT":/work -w /work "$IMG" bash -c '
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
  # --- assemble the hand-tuned 68k routines (.S). gcc drives cpp+as; no C headers,
  #     no -std/-force-include -- the .S carries its own #defines. NG_ARCH selects CPU.
  #     Gated on NG_CKSUM_ASM (same switch that -D-empties in_cksum.c): when the asm is
  #     OFF the C in_cksum() is compiled instead, and assembling the .S too would give a
  #     duplicate _in_cksum -- so this MUST stay in lock-step with ccflags.sh.
  # ng_move16.S (68040/060 MOVE16 copy) is always assembled -- it is self-gating
  # (empty on other CPUs). The asm checksum is gated on NG_CKSUM_ASM (mutually
  # exclusive with the C in_cksum.c).
  asm_srcs="src/kern/ng_move16.S"
  [ "$NG_CKSUM_ASM" = 1 ] && asm_srcs="$asm_srcs src/netinet/in_cksum_asm.S"
  for a in $asm_srcs; do
    o="build/obj/$(basename "${a%.S}").o"
    if ! m68k-amigaos-gcc -c "$a" -o "$o" $NG_ARCH 2>/tmp/e; then
      echo "ASSEMBLE FAIL: $a"; grep -m1 -iE "error:|fatal" /tmp/e; exit 1
    fi
  done
  # Belt-and-braces: exactly one _in_cksum must survive into the object set, whichever
  # path built it -- a duplicate would otherwise be silently arbitrated by the link-time
  # --allow-multiple-definition (there for an unrelated libnix symbol).
  n=$(m68k-amigaos-nm build/obj/*.o 2>/dev/null | grep -cE " T _in_cksum$")
  [ "$n" = 1 ] || { echo "!!! expected exactly one _in_cksum definition, found $n"; exit 1; }
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
