/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 */

/*
 * bsdsocket_lib.c --- the loadable LIBS:bsdsocket.library skeleton.
 *
 * PORT (AmiTCP_NG): this turns the AmiTCP_NG stack into a self-starting Amiga
 * shared library, the way Roadshow's bsdsocket.library works -- so it can be
 * installed as a genuine drop-in replacement (back up theirs, drop ours in).
 *
 * Structure of a loadable library:
 *   - a RomTag (`struct Resident`) that exec's InitResident() finds when the
 *     file is LoadSeg()'d by OpenLibrary(). RTF_AUTOINIT means rt_Init points at
 *     an InitTable; exec then allocates the base, MakeLibrary()s it from the
 *     function table, applies the dataInit initialisers, calls our LibInit(),
 *     and AddLibrary()s the result -- i.e. it does what api_init()+api_show()
 *     did in the old program build, for free.
 *   - LibInit() does only the internal bookkeeping (api_libinit(), in
 *     api/amiga_api.c): record the base + seglist, init the syscall semaphore,
 *     select data and per-opener base lists.
 *   - the stack subsystems (timer/SANA/protocols/netdb) and the service loop are
 *     NOT started here; they come up on the FIRST OpenLibrary(), spawned by
 *     ELL_Open(). That keeps a freshly-loaded-but-unopened library cheap.
 *
 * The function tables (ExecLibraryList_funcTable, the per-opener vectors) and the
 * dataInit table (Library_initTable) are reused verbatim from api/amiga_api.c /
 * api/amiga_libtables.c -- the ABI is identical whether the base is made by a
 * program (old amitcp) or by the loader (this library).
 */

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <sys/cdefs.h>				/* SAVEDS */

#include "bsdsocket.library_rev.h"

#define NG_XSTR(x) #x
#define NG_STR(x)  NG_XSTR(x)

/* Reused from the existing stack sources (address references only). */
extern APTR   ExecLibraryList_funcTable[];	/* master lib Open/Close/Expunge */
extern UWORD  Library_initTable[];		/* dataInit (name/version/id) */
extern VOID   api_libinit(struct Library *base, BPTR seglist);

/*
 * Library runtime. In the old `amitcp` PROGRAM build, libnix's C startup (crt0)
 * defined SysBase and exit(); a -nostartfiles LIBRARY has no crt0, so we own
 * them. SysBase is the one global every stack file references; LibInit sets it
 * from exec's a6. A library can never "exit" -- stub it so subr_prf's fatal
 * path links (it should never actually run in the library).
 */
struct ExecBase *SysBase = NULL;
void exit(int code) { (void)code; }

/*
 * Entry stub. A library must never be run as a program; if someone does, return
 * failure (moveq #-1,d0 / rts). OpenLibrary() ignores this and uses the RomTag.
 */
asm("  .text                \n"
    "  .even                \n"
    "  .globl _start        \n"
    "_start:                \n"
    "  moveq #-1,d0         \n"
    "  rts                  \n");

/*
 * RTF_AUTOINIT init function. On entry (exec calling convention): d0 = the freshly
 * MakeLibrary()'d base, a0 = the load seglist, a6 = SysBase. Do the internal
 * setup and hand the base back; exec AddLibrary()s it on return.
 */
struct Library * SAVEDS LibInit(VOID)
{
  register struct Library  *_base    __asm("d0");
  register BPTR             _seglist __asm("a0");
  register struct ExecBase *_sysbase __asm("a6");
  struct Library  *base    = _base;		/* capture args before anything */
  BPTR             seglist = _seglist;		/* clobbers the registers */
  struct ExecBase *sysbase = _sysbase;

  SysBase = sysbase;
  api_libinit(base, seglist);
  return base;
}

/* AUTOINIT table: { dataSize, funcTable, dataInit, initFunc }. */
static const ULONG ng_InitTable[4] = {
  (ULONG) sizeof(struct Library),
  (ULONG) ExecLibraryList_funcTable,
  (ULONG) Library_initTable,
  (ULONG) LibInit
};

static const char ng_LibName[] = "bsdsocket.library";
static const char ng_LibID[]   = "bsdsocket.library " NG_STR(VERSION) "." NG_STR(REVISION)
				 " (AmiTCP_NG " AMITCP_NG_VER ")\r\n";

/*
 * The RomTag. exec scans a LoadSeg()'d segment for RTC_MATCHWORD (the 68000
 * ILLEGAL opcode 0x4AFC); rt_MatchTag pointing back at this structure confirms
 * the find. rt_EndSkip bounds the scan.
 */
extern const struct Resident ng_RomTag;
const struct Resident ng_RomTag = {
  RTC_MATCHWORD,			/* rt_MatchWord */
  (struct Resident *)&ng_RomTag,	/* rt_MatchTag  */
  (APTR)(&ng_RomTag + 1),		/* rt_EndSkip   */
  RTF_AUTOINIT,				/* rt_Flags     */
  VERSION,				/* rt_Version   */
  NT_LIBRARY,				/* rt_Type      */
  0,					/* rt_Pri       */
  (char *)ng_LibName,			/* rt_Name      */
  (char *)ng_LibID,			/* rt_IdString  */
  (APTR)ng_InitTable			/* rt_Init      */
};
