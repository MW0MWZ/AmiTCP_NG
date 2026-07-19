/*
 * amitcp_ng_bases.h --- PORT shim (bebbo m68k-amigaos-gcc 6.5)
 *
 * AmiTCP was built with a 1990s amiga-gcc whose inline/ headers implied
 * the library base symbol. Bebbo's inline/ headers emit only the call stubs and
 * expect the base variable (DOSBase, TimerBase, ...) to already be in scope --
 * exactly what the proto/ headers would declare. AmiTCP's kern/amiga_includes.h
 * declares SysBase and TimerBase but never DOSBase, and several TUs pull an
 * inline/ header without a matching base decl, so they fail to compile.
 *
 * This header declares the standard bases as externs, with types matching
 * AmiTCP's own conventions (see kern/amiga_includes.h). It is force-included
 * (-include) into every TU, so base visibility no longer depends on per-file
 * include order. Duplicate compatible `extern` decls elsewhere are harmless.
 *
 * The definitions of these symbols live in the library init, one each.
 */
#ifndef AMITCP_NG_BASES_H
#define AMITCP_NG_BASES_H

struct ExecBase;
struct DosLibrary;
struct Library;

extern struct ExecBase   *SysBase;
extern struct DosLibrary *DOSBase;
extern struct Library    *TimerBase;   /* AmiTCP treats timer.device base as struct Library * */

#endif /* AMITCP_NG_BASES_H */
