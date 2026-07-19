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
NG_DEF="-DAMITCP -DKERNEL -DSOCKBUF_DEBUG -DTCPDEBUG -DDIRECTED_BROADCAST -DICMPPRINTFS"
# -Wall -Werror: the whole stack core compiles warning-clean; keep it that way --
# a new warning fails the build instead of being silently swallowed by build.sh.
NG_CFLAGS="-noixemul -std=gnu89 -fno-builtin -O1 -fomit-frame-pointer -Wall -Werror"
NG_FORCEINC="-include src/conf/rcs.h -include src/conf/amitcp_ng_bases.h"
