#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Build bsdsocket.library with a MIXTURE of CPU targets, to find which translation
# unit's -m68040 codegen breaks the stack.
#
# When a fault appears in one CPU build and not another, it is usually neither
# "the code" nor "the arch" alone -- it is one of our files miscompiling, or
# relying on something that only holds at the safer target. This finds which, by
# compiling a NAMED SUBSET at -m68000 and everything else at -m68040, then
# linking exactly as the failing build does.
#
# THIS IS THE RIGHT TOOL FOR AN ARCH-SPECIFIC FAULT -- reach for it early. The
# 4.1.6 DHCP bug (lease failed on 68040 only) was found exactly this way and took
# four emulator runs once the rig matched the machine; weeks went into theorising
# first. Narrow to ONE file, then narrow again WITHIN it by compiling that file
# at -O1, and then per-function with __attribute__((optimize("O1"))). When the
# file is identified, disassemble the build that FAILS, not the one that works.
#
# (The header here used to describe a 4.1.6 "dies before log_init at -m68040"
# symptom. That turned out to be batched m68k-amigaos-strip truncating binaries,
# not codegen at all -- so do not treat that as this script's reason to exist.)
#
#   usage:  docker/bisect-arch.sh  "amiga_main uipc_mbuf if_sana"
#           (basenames, no path, no .c -- these get the SAFE -m68000 target)
#           empty list = everything -m68040 = the known-bad build
#
# Output: build/bsdsocket.library
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG=amigadev/crosstools:m68k-amigaos
SAFE="${1:-}"

"$ROOT/docker/gen_config_var.sh" >/dev/null

docker run --rm -e SAFE="$SAFE" -v "$ROOT":/work -w /work "$IMG" bash -c '
  set -e
  BAD_ARCH=-m68040
  SAFE_ARCH=-m68000
  source docker/ccflags.sh          # NG_ARCH default is -m68000; we override per file
  mkdir -p build/obj && rm -f build/obj/*.o

  # strip the arch out of NG_CFLAGS so we can append our own per file
  BASE_CFLAGS="${NG_CFLAGS%% -m68000}"
  case "$NG_CFLAGS" in *-m68000*) BASE_CFLAGS="$(echo "$NG_CFLAGS" | sed "s/ -m68000//")";; esac

  srcs="$(ls src/api/*.c src/kern/*.c src/net/*.c src/netinet/*.c) src/amitcp_ng_glue.c src/lib/bsdsocket_lib.c"
  nsafe=0; nbad=0
  for s in $srcs; do
    b="$(basename "${s%.c}")"
    arch="$BAD_ARCH"
    for k in $SAFE; do [ "$b" = "$k" ] && arch="$SAFE_ARCH"; done
    [ "$arch" = "$SAFE_ARCH" ] && nsafe=$((nsafe+1)) || nbad=$((nbad+1))
    m68k-amigaos-gcc -c "$s" -o "build/obj/$b.o" $NG_INC -Isrc $NG_DEF $BASE_CFLAGS $arch ${NG_FORCEINC:-}
  done

  # asm: assemble at the BAD arch (matches the failing build)
  m68k-amigaos-gcc -c src/kern/ng_move16.S -o build/obj/ng_move16.o $BAD_ARCH
  [ "${NG_CKSUM_ASM:-1}" = 1 ] && m68k-amigaos-gcc -c src/netinet/in_cksum_asm.S -o build/obj/in_cksum_asm.o $BAD_ARCH

  cd build/obj
  m68k-amigaos-gcc -noixemul $BAD_ARCH -nostartfiles -e _start \
      -o /work/build/bsdsocket.library *.o \
      -Wl,--allow-multiple-definition \
      /opt/m68k-amigaos/m68k-amigaos/lib/libamiga.a
  echo "linked: $nsafe file(s) at 68000, $nbad at 68040 -> $(wc -c < /work/build/bsdsocket.library) bytes"
'
