# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# AmiTCP_NG build flags — sourced by build helpers. Paths are INSIDE the toolchain image.
NDK=/opt/m68k-amigaos/m68k-amigaos/ndk-include
LIBNIX=/opt/m68k-amigaos/m68k-amigaos/libnix/include
SYSINC=/opt/m68k-amigaos/m68k-amigaos/sys-include
GCCINC=/opt/m68k-amigaos/lib/gcc/m68k-amigaos/6.5.0b/include
# -nostdinc + explicit re-adds. Order: our netinclude wins, then AmigaOS NDK, then
# LIBNIX libc headers (ctype.h -> _ctype_, matching the -noixemul/libnix runtime),
# then sys-include gap-fillers (machine/limits.h, assert.h), then gcc builtins.
# -noixemul: link against libnix (light runtime) NOT clib2/ixemul. This matters at
# RUNTIME: clib2's per-process crt state is absent in CreateNewProcTags-spawned
# tasks (e.g. NETTRACE log_task), which hung the stack; libnix has no such need.
NG_INC="-nostdinc -Isrc/netinclude -Isrc -Isrc/conf -Isrc/protos -isystem $NDK -isystem $LIBNIX -isystem $SYSINC -isystem $GCCINC"
NG_DEF="-DAMITCP -DKERNEL -DTCPDEBUG -DDIRECTED_BROADCAST -DICMPPRINTFS"
# SOCKBUF_DEBUG enables the sbcheck() socket-buffer consistency validator, now called
# from sbappend() -- it walks the whole buffer (O(n)) on every append, which would undo
# the O(1) sb_mbtail append in a shipped build. So it is OFF by default (production) and
# turned on only for validation runs:  NG_SOCKBUF_DEBUG=1 bash docker/run-bench.sh ...
NG_SOCKBUF_DEBUG="${NG_SOCKBUF_DEBUG:-0}"
[ "$NG_SOCKBUF_DEBUG" = 1 ] && NG_DEF="$NG_DEF -DSOCKBUF_DEBUG"
# NG_CKSUM_ASM=1 (default) builds the hand-tuned 68k assembly checksum (in_cksum_asm.S)
# and compiles the C in_cksum() out; =0 falls back to the portable C version. This ONE
# switch must gate BOTH the -D (which empties in_cksum.c) AND the .S assembly step in
# build.sh -- otherwise both define _in_cksum and the link silently picks one.
NG_CKSUM_ASM="${NG_CKSUM_ASM:-1}"
[ "$NG_CKSUM_ASM" = 1 ] && NG_DEF="$NG_DEF -DNG_CKSUM_ASM"
# CPU target multilib. Default -m68000 (runs on every 68k, the "standard" build).
# build-release.sh overrides NG_ARCH (-m68020 / -m68040) to ship pre-tuned variants;
# it is applied to BOTH compile (codegen) and link (libgcc multilib), so an 020/040
# build actually emits 020/040 instructions, not just links the 000 objects.
NG_ARCH="${NG_ARCH:--m68000}"
# -Wall -Werror: the whole stack core compiles warning-clean; keep it that way --
# a new warning fails the build instead of being silently swallowed by build.sh.
# -O2 for whole-stack codegen (inlining, register allocation, CSE, loop opts). PAIRED
# WITH -fno-strict-aliasing because the 4.4BSD core type-puns pervasively (mtod() over
# mbuf data, sockaddr_in/sockaddr casts, packet headers overlaid on buffers); -O2's
# default -fstrict-aliasing would silently miscompile those. This is the standard flag
# for BSD/kernel C -- NOT a workaround to "clean up" (that rewrite buys ~no speed).
NG_CFLAGS="-noixemul -std=gnu89 -fno-builtin -O2 -fno-strict-aliasing -fomit-frame-pointer -Wall -Werror $NG_ARCH"
NG_FORCEINC="-include src/conf/rcs.h -include src/conf/amitcp_ng_bases.h"
