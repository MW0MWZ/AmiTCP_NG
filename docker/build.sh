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
  # PER-ARCH OBJECT DIRECTORY. All three arches used to share build/obj, so the
  # smoke suite -m68000 -> -m68020 -> -m68040 sweep recompiled all 79 units from
  # scratch three times, and every re-run started from nothing again. Keyed on the
  # arch, each tier keeps its own objects and a repeat run is incremental.
  OBJ="build/obj${NG_ARCH:--m68000}"; OBJ="${OBJ// /}"
  mkdir -p "$OBJ"
  # --- compile every translation unit ---
  srcs="$(ls src/api/*.c src/kern/*.c src/net/*.c src/netinet/*.c) src/amitcp_ng_glue.c"
  fail=0
  # Compile in parallel. This was a serial loop over 79 units; nproc jobs is a
  # straight win and the failure reporting still works because each job writes its
  # own error file and we collect them afterwards.
  jobs=$(nproc 2>/dev/null || echo 2)
  rm -f /tmp/cfail.*
  run_one() {
    local s="$1" o="$OBJ/$(basename "${1%.c}").o"
    # NO INCREMENTAL SKIP. There was one here, comparing the object against its
    # own .c and against ccflags.sh -- and it was HEADER-BLIND. Grow a struct in
    # a header and every .c that includes it but was not itself edited keeps its
    # stale object, so the link mixes translation units that disagree about the
    # size and offsets of that struct. With no MMU that is not a crash you get to debug:
    # it corrupts whatever sits next door and surfaces later as something else.
    # A cold build of all 83 objects takes about five seconds -- the skip saved
    # four and a half of them. Correctness is worth more than that.
    if ! m68k-amigaos-gcc -c "$s" -o "$o" $NG_INC $NG_DEF $NG_CFLAGS $NG_FORCEINC 2>"/tmp/e.$$.$(basename "$s")"; then
      { echo "COMPILE FAIL: $s"; grep -m1 -iE "error:|fatal" "/tmp/e.$$.$(basename "$s")"; } > "/tmp/cfail.$(basename "$s")"
    fi
    rm -f "/tmp/e.$$.$(basename "$s")"
  }
  n=0
  for s in $srcs; do
    run_one "$s" &
    n=$((n+1))
    [ $((n % jobs)) -eq 0 ] && wait
  done
  wait
  if ls /tmp/cfail.* >/dev/null 2>&1; then cat /tmp/cfail.*; rm -f /tmp/cfail.*; fail=1; fi
  [ "$fail" = 0 ] || { echo "compile errors -> abort"; exit 1; }
  # --- assemble the hand-tuned 68k routines (.S). gcc drives cpp+as; no C headers,
  #     no -std/-force-include -- the .S carries its own #defines. NG_ARCH selects CPU.
  #     Gated on NG_CKSUM_ASM (same switch that -D-empties in_cksum.c): when the asm is
  #     OFF the C in_cksum() is compiled instead, and assembling the .S too would give a
  #     duplicate _in_cksum -- so this MUST stay in lock-step with ccflags.sh.
  # ng_bcopy.S holds ng_bcopy(), the universal copy used on EVERY CPU, so it is
  # always assembled and -- unlike before -- at the target arch. The asm checksum is
  # gated on NG_CKSUM_ASM (mutually exclusive with the C in_cksum.c).
  asm_srcs="src/kern/ng_bcopy.S"
  # The fused RX copy+checksum. Self-gating is not possible in a .S, so only assemble
  # it when the C side is compiled in (NG_RX_CSUM is off for 68040/060).
  case "$NG_ARCH" in *68040*|*68060*) ;; *) asm_srcs="$asm_srcs src/netinet/in_cksum_copy_asm.S" ;; esac
  [ "$NG_CKSUM_ASM" = 1 ] && asm_srcs="$asm_srcs src/netinet/in_cksum_asm.S"
  for a in $asm_srcs; do
    o="$OBJ/$(basename "${a%.S}").o"
    # This file used to be ng_move16.S, forced to -m68040 because it held MOVE16 behind
    # a run-time AFF_68040 test. It holds no MOVE16 now: it is ng_bcopy(), which runs on
    # every CPU. Assembling it at the target arch is now the SAFETY GATE -- gas will
    # reject a 68020+ encoding in the 68000 build instead of silently emitting one into
    # a library that a plain 68000 will execute.
    aarch="$NG_ARCH"
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
  for o in "$OBJ"/*.o; do
    b="$(basename "$o")"
    case "$keep" in
      *" $b "*) ;;
      *) echo "removing stray object (not from this build): $b"; rm -f "$o" ;;
    esac
  done

  # Belt-and-braces: exactly one _in_cksum must survive into the object set, whichever
  # path built it -- a duplicate would otherwise be silently arbitrated by the link-time
  # --allow-multiple-definition (there for an unrelated libnix symbol).
  n=$(m68k-amigaos-nm "$OBJ"/*.o 2>/dev/null | grep -cE " T _in_cksum$")
  [ "$n" = 1 ] || { echo "!!! expected exactly one _in_cksum definition, found $n"; exit 1; }
  echo "compiled $(ls "$OBJ"/*.o | wc -l) objects into $OBJ"
  # --- link the stack program (installs bsdsocket.library at runtime) ---
  #  -noixemul                  : link libnix (light runtime).
  #  $NG_ARCH                   : CPU multilib (-m68000 default; -m68020/-m68040 variants).
  #  --allow-multiple-definition: AmiTCP ships its own ultoa (also in libnix)
  #  libamiga.a (explicit path) : ROM-call stubs (Amiga2Date, ...)
  #  NOTE: on -m68000 __mulsi3/__divsi3 call utility.library (SMult32/UMult32) via
  #        UtilityBase, which amiga_main.c opens up front (see the PORT note there);
  #        an 020+ build emits native muls.l/divs.l instead, so those stubs go unused.
  cd "$OBJ" && m68k-amigaos-gcc -noixemul $NG_ARCH -o ../amitcp *.o \
      -Wl,--allow-multiple-definition \
      /opt/m68k-amigaos/m68k-amigaos/lib/libamiga.a && cd ../..
  echo "linked: build/amitcp"; file build/amitcp
'
