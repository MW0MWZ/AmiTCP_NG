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
docker run --rm -e NG_ARCH -e NG_CKSUM_ASM -e NG_SOCKBUF_DEBUG -e NG_DEF_EXTRA \
  -v "$ROOT":/work -w /work "$IMG" bash -c '
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
  # The fused RX copy+checksum. Self-gating is not possible in a .S, so only assemble
  # it when the C side is compiled in (NG_RX_CSUM is off for 68040/060).
  case "$NG_ARCH" in *68040*|*68060*) ;; *) asm_srcs="$asm_srcs src/netinet/in_cksum_copy_asm.S" ;; esac
  [ "$NG_CKSUM_ASM" = 1 ] && asm_srcs="$asm_srcs src/netinet/in_cksum_asm.S"
  for a in $asm_srcs; do
    o="build/obj/$(basename "${a%.S}").o"
    # ng_move16.S is the one file assembled for 68040 whatever the target: it holds
    # MOVE16, which the assembler will not encode at a lower -march. It is reached
    # only behind a run-time AFF_68040 test, and an instruction that is never executed
    # cannot fault -- so one binary can carry it and still run on a 68000.
    aarch="$NG_ARCH"
    case "$a" in *ng_move16.S) aarch="-m68040" ;; esac
    if ! m68k-amigaos-gcc -c "$a" -o "$o" $aarch 2>/tmp/e; then
      echo "ASSEMBLE FAIL: $a"; grep -m1 -iE "error:|fatal" /tmp/e; exit 1
    fi
  done
  # --- drop any object this build did not produce ---------------------------------
  # build/obj is linked with a bare *.o glob (below, and again in build-lib.sh) and the
  # link carries --allow-multiple-definition, so a stray object is not a link error --
  # it is silently included, and can REPLACE a real translation unit. That is not
  # hypothetical: a throwaway if_sana.o from a compiler-warning check, built from
  # older source, was linked in preference to the real one, and the resulting library
  # behaved exactly like the code before the change under test. Objects are kept
  # between builds on purpose (incremental rebuilds), so the fix is to keep only what
  # belongs, not to wipe the directory.
  keep=" bsdsocket_lib.o "
  for s in $srcs;     do keep="$keep$(basename "${s%.c}").o "; done
  for a in $asm_srcs; do keep="$keep$(basename "${a%.S}").o "; done
  for o in build/obj/*.o; do
    b="$(basename "$o")"
    case "$keep" in
      *" $b "*) ;;
      *) echo "removing stray object (not from this build): $b"; rm -f "$o" ;;
    esac
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
