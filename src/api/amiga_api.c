/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: amiga_api.c,v 3.7 1994/04/02 11:12:59 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created: Tue Jan 26 20:44:28 1993 too
 * Last modified: Mon Feb 14 16:48:27 1994 jraja
 *
 * HISTORY
 * $Log: amiga_api.c,v $
 * Revision 3.7  1994/04/02  11:12:59  jraja
 * Added initialization of the resolver variables.
 *
 * Revision 3.6  1994/03/22  07:36:28  jraja
 * Added initialization of the fdCallback to NULL. Also clearing the
 * fdCallback pointer before the final CloseSocket sweep on the library
 * close.
 * Removed f_void definition, already defined in apicalls.h.
 *
 * Revision 3.5  1994/02/14  14:49:20  jraja
 * Changed the default log tag to be NULL (no tag).
 *
 * Revision 3.4  1994/01/20  02:23:44  jraja
 * Changed writeErrnoValue() to use only errno sizes of 1, 2 or 4 bytes.
 *
 * Revision 3.3  1994/01/12  07:17:58  jraja
 * Added initialization of the Syslog() related variables of the library base.
 *
 * Revision 3.2  1994/01/07  15:40:29  too
 * Bug fixes after revision 3.1. Now tested.
 *
 * Revision 3.1  1994/01/04  14:24:05  too
 * Addeed compile time fd_mask and long sizeof checks.
 * Added allocation of socket usage bitmask (at then end of dTable)
 *
 * Revision 1.50  1993/11/26  16:23:42  too
 * Added sendbreaktotasks() function
 *
 * Revision 1.49  1993/08/09  21:28:22  ppessi
 * Added revision headers and release string.
 *
 * Revision 1.48  1993/08/06  08:42:44  jraja
 * Removed the version check, since it does not work.
 *
 * Revision 1.47  1993/08/05  10:35:52  jraja
 * Added version check to the ELL_Open(). Now all Open requests with
 * version less than 2 will be rejected.
 * No requester or alert is set up yet (Add later).
 *
 * Revision 1.46  1993/07/15  20:17:25  too
 * Changed library revision to VERSION 2 REVISION 1
 *
 * Revision 1.45  1993/06/12  22:56:08  too
 * Added freeDataBuffer() calls to UL_Close so now selitem and netdb
 * buffers are freed at CloseLIbrary()
 *
 * Revision 1.44  1993/06/07  12:37:20  too
 * Changed inet_ntoa, netdatabase functions and WaitSelect() use
 * separate buffers for their dynamic buffers
 *
 */

/*
 * amiga_api.c --- construct and manage bsdsocket.library itself.
 *
 * This is the file that turns a running program into a shared library. Read it
 * second, after amiga_main.c (docs/ARCHITECTURE.md section 5).
 *
 * An AmigaOS library is a `struct Library` (a node + housekeeping) immediately
 * preceded in memory by its VECTOR TABLE -- an array of function pointers that
 * callers reach at negative offsets from the base (jsr a6@(-30) etc.). Here:
 *
 *   Library_initTable  a hand-built description of the library's fixed fields
 *                      (name, version, flags) for MakeLibrary(). It is written by
 *                      hand rather than with exec/initializers.h because the name
 *                      and id strings are addresses not known until link time.
 *   LibVectors[]       the actual vector table -- lives in api/amiga_libtables.c;
 *                      its ORDER is the ABI (socket, bind, listen, ... ). Do not
 *                      reorder it.
 *   ELL_Open / UL_Close / ELL_Expunge / Null
 *                      the four mandatory library entry points. api_init() (called
 *                      from init_all) passes these + LibVectors to MakeLibrary()
 *                      to create the master base; api_show() AddLibrary()s it.
 *
 * PER-OPENER STATE. Every application that OpenLibrary()s bsdsocket.library gets
 * its OWN extended base, a `SocketBase`, holding that program's socket fd table,
 * errno pointer, signal masks and resolver state. ELL_Open builds a fresh
 * SocketBase per opener (that is what "ELL" = Extended-Library-Library denotes);
 * UL_Close tears one down, closing any sockets the program leaked. Two programs
 * therefore never see each other's sockets. The API functions elsewhere in api/
 * receive the caller's SocketBase in register a6 and operate on it.
 *
 * CONTEXT WARNING (see the NOTE below): Open/Close/Expunge run under Exec
 * Forbid()/Permit(), so task switching is disabled while they execute -- they
 * must be quick and must not Wait().
 */

#include <bsdsocket.library_rev.h>
#define RELEASESTRING "AmiTCP/IP release 3 "

/*
 * NOTE: Exec has turned off task switching while in Open, Close, Expunge and
 *	 Reserved functions (via Forbid()/Permit()) so we should not take
 *	 too long in them.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>

#include <kern/amiga_includes.h>

#include <api/amiga_api.h>
#include <api/allocdatabuffer.h>
#include <api/amiga_libcallentry.h>
#include <api/apicalls.h>

#include <kern/amiga_subr.h>
#include <amitcp/socketbasetags.h>	/* PORT (AmiTCP_NG): Log/Error hook messages */
#include <sys/synch.h>		/* PORT (AmiTCP_NG): SPL0, for the error-hook spl guard */
/* PORT (AmiTCP_NG): CallHookPkt() for SBTC_ERROR_HOOK. Uses the GLOBAL
 * UtilityBase (opened in kern/amiga_main.c), unlike kern/amiga_log.c which
 * renames it to a private base. */
#if __SASC
#include <proto/utility.h>
#elif __GNUC__
#include <inline/utility.h>
#endif
#include <kern/amiga_log.h>

/* strncmp() is not declared by any reachable header in the GNUC/-noixemul
 * build (see the strcmp() note in amiga_roadshow_compat.c for why plain
 * <string.h> cannot be included here). Still provided by libnix at link
 * time, so just declare it. */
extern int strncmp(const char *, const char *, size_t);

/* PORT (AmiTCP_NG): the C preprocessor cannot evaluate sizeof (SAS/C allowed it
 * as an extension; ISO cpp does not -> "missing binary operator"). Enforce the
 * same 32-bit invariant at C level: this typedef fails to compile if it breaks. */
typedef char amitcp_ng_assert_32bit[(sizeof(fd_mask) == 4 && sizeof(long) == 4) ? 1 : -1];

#define SOCLIBNAME "bsdsocket.library\0\0" /* space for ".n" at the end */

/*
 *  Semaphore to prevent simultaneous access to library functions.
 */
struct SignalSemaphore syscall_semaphore = { 0 };

/*
 *  some globals.
 */
struct Library *MasterSocketBase = NULL;
struct List	socketBaseList;	     /* list of opened socket library bases */
struct List	garbageSocketBaseList; /* list of libray bases not active
				      anymore (NOT YET IMPLEMENTED) */
struct List	releasedSocketList; /* List for sockets that are in no-one's
				       context, waiting for Obtain */

extern struct Task * AmiTCP_Task; /* reference to AmiTCP/IP task information */

/*
 * Declaration of variable to hold message format string when one
 * task tries to use other tasks' library base pointer. moved here
 * from amiga_libcallentry.h so it doens't generate code.
 */
const char wrongTaskErrorFmt[] =
  "Task %ld (%s) attempted to use library base of Task %ld (%s).";

/*
 * PORT (AmiTCP_NG): find the calling task's scratch context -- see amiga_api.h.
 *
 * The opener's is embedded in the base, so that case is a compare. A task that
 * shares the base gets one of its own, created by ng_ctx_ensure() when it first
 * enters the library; this only walks the list to find it.
 *
 * On a miss it deliberately returns the OPENER's context rather than NULL. The
 * callers reach fields straight through this pointer and have no error path --
 * that is what keeps the resolver and netdb sources untouched -- so NULL would
 * be an immediate crash on a machine with no MMU to catch it. Falling back
 * means the two tasks share one set of scratch buffers, which is wrong but is
 * exactly the behaviour that shipped before any of this existed. It happens
 * only if the allocation failed, and is warned about once per base rather than
 * on every call.
 */
/*
 * PORT (AmiTCP_NG): find the base this task opened OR is sharing.
 * See the note at the declaration in api/amiga_api.h for why this is separate
 * from FindSocketBase().
 */
struct SocketBase *FindCallerBase(struct Task *task)
{
  struct Node *libNode;
  struct SocketBase *p;
  struct ng_taskctx *c;

  Forbid();
  for (libNode = socketBaseList.lh_Head; libNode->ln_Succ;
       libNode = libNode->ln_Succ) {
    p = (struct SocketBase *)libNode;
    if (p->thisTask == task) {
      Permit();
      return p;
    }
    /*
     * Walk unconditionally rather than gating on SBFF_CAN_SHARE. The flag is
     * an ordinary settable tag -- clearing it does not retract the contexts
     * already created -- so gating on it would make a sharer that had already
     * called in invisible again the moment the opener turned sharing off,
     * which is the very bug this function exists to fix. The walk costs one
     * pointer read on a base that has no sharers.
     */
    for (c = (struct ng_taskctx *)p->ctxList.mlh_Head;
         c->tc_node.mln_Succ != NULL;
         c = (struct ng_taskctx *)c->tc_node.mln_Succ)
      if (c->tc_task == task) {
        Permit();
        return p;
      }
  }
  Permit();
  return NULL;
}

/*
 * PORT (AmiTCP_NG): make sure the CALLING task has a scratch context of its own.
 *
 * Called at vector entry, from CHECK_TASK(), and only when the base is actually
 * shared -- an unshared base never gets here, so nothing changes for the vast
 * majority of programs. Vector entry is the right place: we are at spl0 with no
 * Forbid() held, so allocating is safe. Doing it lazily further in is not --
 * tsleep() and much of the socket layer run under splnet(), which IS Forbid().
 *
 * Without this a sharer silently used the OPENER's buffers, so two tasks
 * resolving names at once would overwrite each other's hostent.
 *
 * The list is walked and extended under Forbid(), which is enough: the walk is
 * short and bounded, nothing here blocks, and the alternative -- syscall_
 * semaphore -- is not held by the netdb and resolver vectors that need this.
 * Allocation happens OUTSIDE the Forbid().
 */
struct ng_taskctx *ng_ctx_ensure(struct SocketBase *p)
{
  struct Task *t = SysBase->ThisTask;
  struct ng_taskctx *c;

  if (p->ctx.tc_task == t)
    return &p->ctx;

  Forbid();
  for (c = (struct ng_taskctx *)p->ctxList.mlh_Head;
       c->tc_node.mln_Succ != NULL;
       c = (struct ng_taskctx *)c->tc_node.mln_Succ)
    if (c->tc_task == t) {
      Permit();
      return c;
    }
  Permit();

  c = (struct ng_taskctx *)AllocVec(sizeof(*c), MEMF_CLEAR|MEMF_PUBLIC);
  if (c == NULL)
    return NULL;			/* caller falls back to the opener's */

  c->tc_task    = t;
  c->res_socket = -1;
  res_init(&c->res_state);

  Forbid();
  AddTail((struct List *)&p->ctxList, (struct Node *)&c->tc_node);
  Permit();
  return c;
}

struct ng_taskctx *ng_ctx(struct SocketBase *p)
{
  struct Task *t = SysBase->ThisTask;
  struct ng_taskctx *c;

  if (p->ctx.tc_task == t)		/* the opener: the common case */
    return &p->ctx;

  for (c = (struct ng_taskctx *)p->ctxList.mlh_Head;
       c->tc_node.mln_Succ != NULL;
       c = (struct ng_taskctx *)c->tc_node.mln_Succ)
    if (c->tc_task == t)
      return c;

  if (!p->ctxWarned) {
    p->ctxWarned = TRUE;
    log(LOG_WARNING, "task %s shares a library base but has no scratch context "
	"of its own yet -- using the opener's",
	t->tc_Node.ln_Name ? (STRPTR)t->tc_Node.ln_Name : (STRPTR)"?");
  }
  return &p->ctx;
}

/*
 * Instead of using exec/initializers.h we looked it as a reference
 * and wrote InitTable by hand
 */

/*
 * OFFSET needed to be casted LONG so compiler doesn't give warning
 * about casting pointer to UWORD
 */
#define OFFSET(structName, structEntry) \
  ((LONG)(&(((struct structName *) 0)->structEntry)))

/*
 * original initTable of only UWORD items doesn't work, since compiler
 * doesn't know address of SOCNAME and VSTRING at compile time, and
 * those are broken to 2 WORDS. therefore initTable is a structure
 * constructed by hand, and those (LONG) values are set longword aligned.
 */
#define id_byte 0xe000
#define id_word 0xd000
#define id_long 0xc000

struct {
  UWORD byte1; UWORD offset1; UWORD ln_type;
  UWORD byte2; UWORD offset2; UWORD lib_flags;
  UWORD long3; UWORD offset3; ULONG ln_Name;
  UWORD word4; UWORD offset4; UWORD lib_Version;
  UWORD word5; UWORD offset5; UWORD lib_Revision;
  UWORD long6; UWORD offset6; ULONG lib_IdString;
  UWORD end7; 
  } Library_initTable = {
    id_byte, OFFSET(Node, ln_Type), NT_LIBRARY << 8,
    id_byte, OFFSET(Library, lib_Flags), (LIBF_SUMUSED|LIBF_CHANGED) << 8,
    id_long, OFFSET(Node, ln_Name), (ULONG)SOCLIBNAME,
    id_word, OFFSET(Library, lib_Version), VERSION,
    id_word, OFFSET(Library, lib_Revision), REVISION,
    id_long, OFFSET(Library, lib_IdString), (ULONG)RELEASESTRING VSTRING,
    0x0000
    };
#undef id_byte
#undef id_word
#undef id_long

/*
 * Api show and hide functions.. during these calls system is not
 * inside FOrbid()/Permit() pair
 */

enum {	API_SCRATCH,		/* api not initialized */
	API_INITIALIZED,	/* librarybase created */
	API_SHOWN,		/* librarybase made visible */
	API_HIDDEN,		/* librarybase hidden */
	API_FUNCTIONPATCHED	/* Api functions set to return -1 */
} api_state = API_SCRATCH;

  /*
   * Setting the following variable to FALSE just before making
   * new socket Library base prevents ELL_Expunge, the final
   * expunging function to remove library base from memory
   */
BOOL AmiTCP_lets_you_do_expunge = FALSE;

BOOL SB_Expunged = FALSE; /* boolean value set by ELL_Expunge */


struct Library * SAVEDS RAF2(ELL_Open,
			     struct Library *,	libPtr,		a6,
			     ULONG,		version,	d0)
#if 0
{
#endif
  
  extern f_void UserLibrary_funcTable[];
  struct SocketBase * newBase;
  LONG error;
  WORD * i;
  (void)version;

  /*
   * One task may open socket library more than once. In that case caller
   * receives the base it has opened already.
   */
  if ((newBase = FindSocketBase(SysBase->ThisTask)) != NULL) {
    newBase->libNode.lib_OpenCnt++;
    return (struct Library *)newBase;
  }
  /*
   * PORT (AmiTCP_NG): the self-starting library must NOT start the stack here.
   * A library's Open vector runs in a restricted exec context where DOS I/O (and
   * therefore CreateNewProc/Wait, which the stack startup needs) is illegal and
   * crashes. Instead the stack is spawned lazily on the first API call that needs
   * it (socket(), the resolver, ...), which runs in the caller's normal task
   * context -- see ng_stack_ensure_running(). We only create the per-opener base
   * here. (No-op distinction in the program build, where the stack is already up.)
   */
  /*
   * Create new library base.
   * All fields in the base will first be initialized to zero and then
   * modified by initializers in initTable.
   */
  newBase = (struct SocketBase *)MakeLibrary(UserLibrary_funcTable,
					     (UWORD *)&Library_initTable,
					     NULL,
					     sizeof(struct SocketBase),
					     NULL);
  if (newBase == NULL)
    return NULL;

  /*
   * add this newly allocated library base to our list of opened
   * socket libraries
   */	
  AddTail(&socketBaseList, (struct Node *)newBase); 

  /*
   * Modify some MASTER library base fields
   */
  libPtr->lib_OpenCnt++;		/* mark us as having another opener */
  libPtr->lib_Flags&= ~LIBF_DELEXP;	/* prevent delayed expunges */

  /*
   * Initialize new library base
   */
  for (i = (WORD *)((struct Library *)newBase + 1);
       i < (WORD *)(newBase + 1);
       i++)
    *i = 0L;
  newBase->libNode.lib_OpenCnt = 1;
  newBase->errnoPtr = (VOID *)&newBase->defErrno;
  newBase->errnoSize = sizeof newBase->defErrno;
  newBase->thisTask = SysBase->ThisTask;
  /*
   * PORT (AmiTCP_NG): claim the embedded context for the opening task and start
   * the sharer list empty. ng_ctx()'s fast path compares against ctx.tc_task,
   * so without this EVERY call -- including every existing single-task program
   * -- would miss the fast path and fall into the slow-path safety net. It
   * would still work, via the fallback, which is exactly why this line is easy
   * to omit and hard to notice: nothing would fail, it would just be wrong.
   */
  newBase->ctx.tc_task = newBase->thisTask;
  NewList((struct List *)&newBase->ctxList);
  newBase->sigIntrMask = SIGBREAKF_CTRL_C;
  
  /* initialize syslog variables */
#if 0 /* initialization to zero is implicit */
  newBase->LogTag = NULL; /* no tag by default, old apps print a tag already */
#endif
  newBase->LogFacility = LOG_USER;
  newBase->LogMask = 0xff;

  /* initialize resolver variables */
  newBase->hErrnoPtr = &newBase->defHErrno;
  newBase->ctx.res_socket = -1;
  res_init(&newBase->ctx.res_state);

  /* initialize dtable variables */
#if 0 /* initialization to zero is implicit */
  newBase->fdCallback = NULL;
#endif
  /*
   * PORT (AmiTCP_NG): no descriptor tables retired yet.
   *
   * OUTSIDE the #if 0 above ON PURPOSE. The base is already zeroed (MakeLibrary
   * clears it, and the word-zero loop earlier covers the whole struct), so this
   * is redundant today exactly like fdCallback -- but UL_Close() WALKS this list
   * and frees what it finds, so a stale pointer here would hand FreeMem() memory
   * that was never ours. That is worth one instruction of belt and braces, and
   * worth being real code rather than a comment inside a disabled block claiming
   * protection it does not provide.
   */
  newBase->retiredTables = NULL;
  newBase->dTableSize = FD_SETSIZE;
  if ((newBase->dTable =
       AllocMem(newBase->dTableSize * sizeof (struct socket *) +
		((newBase->dTableSize - 1) / NFDBITS + 1) * sizeof (fd_mask),
		MEMF_CLEAR|MEMF_PUBLIC)) == NULL) {
    /* PORT (AmiTCP_NG): keep dTableSize consistent with dTable. The cleanup
     * path below calls UL_Close(), which walks dTable[0..dTableSize); leaving
     * the size set while the pointer is NULL described a 64-entry table at
     * address 0. UL_Close guards on the pointer too -- belt and braces. */
    newBase->dTableSize = 0;
  } else {
    /*	
     * allocate and initialize the timer message reply port
     */
    newBase->timerPort = CreateMsgPort();
    if (newBase->timerPort != NULL) {
      /*
       * Disable signalling for now
       */
      newBase->timerPort->mp_Flags = PA_IGNORE;
      /*
       * allocate and initialize the timerequest
       */
      newBase->tsleep_timer = (struct timerequest *)
	CreateIORequest(newBase->timerPort, sizeof(struct timerequest));
      if (newBase->tsleep_timer != NULL) {
	error = OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
			   (struct IORequest *)newBase->tsleep_timer, 0);
	if (error == 0) {
	  /*
	   * Initialize some fields of the IO request to common values
	   */
	  newBase->tsleep_timer->tr_node.io_Command = TR_ADDREQUEST;
	  newBase->tsleep_timer->tr_node.io_Message.mn_Node.ln_Type = NT_UNKNOWN;
	  return (struct Library *)newBase;
	}
      }
    }
  }
  /*
   * There was some error if we reached here. Call Close to clean up.
   */
  {
#if __GNUC__
    extern ULONG* REGARGFUN UL_Close(VOID);
    register struct SocketBase *a6 __asm("a6") = newBase;
    (void)a6;
    UL_Close();
#elif __SASC
    extern ULONG* REGARGFUN UL_Close(register __a6 struct SocketBase *);
    UL_Close(newBase);
#else 
#error Compiler not supported!
#endif
  }
  return NULL;
}


ULONG * SAVEDS RAF1(ELL_Expunge,
		    struct Library *,	libPtr, a6)
#if 0
{
#endif
  /*
   * Since every user gets her own library base, Major library base
   * can be removed immediately after 
   */ 
  if (libPtr->lib_OpenCnt == 0 && AmiTCP_lets_you_do_expunge) {
    VOID * freestart;
    ULONG  size;

#if 0	/* Currently done already  */
    /*
     * unlink SocketBase from System Library list
     */
    Remove((struct Node *)libPtr);
#endif
    
    freestart = (void *)((ULONG)libPtr - (ULONG)libPtr->lib_NegSize);
    size = libPtr->lib_NegSize + libPtr->lib_PosSize;
    FreeMem(freestart, size);
    MasterSocketBase = NULL; 

    SB_Expunged = TRUE;
    Signal(AmiTCP_Task, SIGBREAKF_CTRL_F);
    return NULL; /* no AmigaDos seglist there (for system use) */
  }
  /*
   * here if someone still have us open, or AmiTCP don't let us expunge yet
   */
  libPtr->lib_Flags |= LIBF_DELEXP;	/* set delayed expunge flag */
  SB_Expunged = FALSE;
  return NULL;
}

LONG /* SAVEDS */ Null(void)
{
  return 0L;
}


ULONG * SAVEDS RAF1(UL_Close,
	  struct SocketBase *,	libPtr, a6)
#if 0
{
#endif
  
  VOID * freestart;
  ULONG  size;
  int	 i;

  /*
   * one task may have SocketLibrary opened more than once.
   */
  if (--libPtr->libNode.lib_OpenCnt > 0)
    return NULL;

  /*
   * PORT (AmiTCP_NG): do not tear down a base other tasks have been sharing.
   *
   * Everything below assumes the closer is the only user: it closes the
   * sockets, frees the descriptor table, the timer and finally the base
   * itself. If a task sharing this base is inside the library right now -- or
   * asleep in recv() on one of those sockets, which is exactly what sharing
   * was implemented for -- that is a use-after-free, and with no MMU it
   * corrupts silently rather than faulting.
   *
   * We cannot wait for them: Exec runs Close under Forbid(), so this must not
   * block. So remove the base from the list, to make sure nothing new finds
   * it, and leave it allocated. Leaking one base beats corrupting memory
   * another task is still using -- the same trade sorflush() already makes
   * with an orphaned socket.
   *
   * The test is "sharing was EVER enabled, OR some task has already called
   * in". Neither half alone is enough. A context is only created by a sharer's
   * first call, so a task handed the base but not yet using it is invisible to
   * the list -- and freeing underneath it is precisely the use-after-free this
   * guards against. And the live SBFF_CAN_SHARE flag will not do either: an
   * application may turn sharing off again, quite reasonably, after handing the
   * pointer over, which would clear the flag while that task is still about to
   * call in. Hence a latch that is set when sharing is enabled and never
   * cleared.
   *
   * The cost is that this is CONSERVATIVE in the other direction too: a
   * program that enables sharing leaks its base at close even if no other
   * task ever touched it, and a base whose sharers have all long since
   * finished still looks shared. Telling those apart needs an in-flight
   * count, and incrementing one at vector entry is easy while decrementing it
   * on the way out is not -- there is no single exit point in the ~140
   * vectors, and a sharer asleep in tsleep() has released syscall_semaphore,
   * so it would be invisible to any count based on that. Until that exists
   * this errs toward the leak, on the side that cannot corrupt memory.
   */
  if (libPtr->everShared ||
      libPtr->ctxList.mlh_Head != (struct MinNode *)&libPtr->ctxList.mlh_Tail) {
    log(LOG_WARNING,
	"bsdsocket: base closed after being shared -- not freeing it, its "
	"sockets and memory are left allocated in case a task is still inside");
    Remove((struct Node *)libPtr);
    /*
     * Still balance the master's open count: it was incremented by ELL_Open()
     * for this base, and leaving it inflated would permanently prevent the
     * count reaching zero, so a delayed expunge could never complete and
     * api_deinit()'s wait for SB_Expunged would block forever.
     */
    MasterSocketBase->lib_OpenCnt--;
    /*
     * The base deliberately stays allocated so a sharer still inside the
     * library does not have it pulled out from under it -- but that means
     * nothing on it may keep pointing INTO the closing program, whose code and
     * data AmigaOS unloads when it exits. Point every application-supplied
     * pointer back at the base's own storage.
     *
     * thisTask is the sharpest of these: it is the OPENER's Task, and
     * sowakeup()/sohasoutofband() Signal() through it. Signal() on a recycled
     * Task structure does not merely write a stale flag word -- under Disable()
     * it can relink the target through Exec's own queues, so the blast radius
     * is the scheduler rather than this library. NULL here, and both Signal
     * sites test for it (kern/uipc_socket2.c, kern/uipc_socket.c). NULL also
     * stops CHECK_TASK's `thisTask != ThisTask` test from spuriously matching a
     * task that merely happens to be allocated at the recycled address and
     * granting it owner access -- so the three CHECK_TASK* macros guard their
     * log against it too.
     */
    libPtr->fdCallback = NULL;
    libPtr->errnoPtr   = (UBYTE *)&libPtr->defErrno;
    libPtr->errnoSize  = sizeof libPtr->defErrno;
    libPtr->hErrnoPtr  = &libPtr->defHErrno;
    libPtr->LogTag     = NULL;
    libPtr->thisTask   = NULL;
    /*
     * sigIntrMask is genuinely still read: a sharer blocking on this base calls
     * tsleep(libPtr, ...) directly (api/amiga_syscalls.c), which takes its
     * interrupt mask from here. Clearing it deliberately narrows what can
     * interrupt such a sleep -- the owner those signals were meant for has
     * gone. sigIOMask, sigUrgMask and sigEventMask are unreachable once the
     * Signal sites are guarded; cleared only as defence in depth.
     */
    libPtr->sigIntrMask  = 0;
    libPtr->sigIOMask    = 0;
    libPtr->sigUrgMask   = 0;
    libPtr->sigEventMask = 0;
    libPtr->sigAddrChangeMask = 0;
    /*
     * errorHook is NOT defence in depth -- clearing it is required. The hook
     * belongs to the closing program: its code and its struct Hook may both be
     * gone the moment this returns, while the base itself deliberately survives
     * for any task still sharing it. A surviving sharer taking an error would
     * otherwise CallHookPkt() into freed memory.
     */
    libPtr->errorHook    = NULL;
    return NULL;
  }
#ifdef DEBUG
  log(LOG_DEBUG, "Closing proc 0x%lx base 0x%lx\n", 
      libPtr->thisTask, libPtr);
#endif
  /*
   * Since library base is to be closed, all sockets referenced by this
   * library base must be closed too. Next piece of code searches for open
   * sockets and calls CloseSocket() on our own library base. It is safe
   * to call since Forbid() state is broken if semaphore needs to be waited.
   *
   * Note that the close may linger. In such case the linger time will be
   * waited. The linger may be interrupted by any signal in sigIntrMask.
   */
  libPtr->fdCallback = NULL; /* don't call the callback any more */
  /*
   * PORT (AmiTCP_NG) security fix: guard the sweep on dTable being present.
   * ELL_Open() sets dTableSize BEFORE allocating dTable, and calls us to clean
   * up if that allocation fails -- so we can arrive with dTable == NULL and
   * dTableSize == FD_SETSIZE. The loop then read NULL[0..63], i.e. the CPU
   * exception-vector table, whose first entry (the initial SSP) is never zero
   * on a running machine, so CloseSocket() was called on the very first
   * iteration with a wild pointer: getSock() would hand back that garbage as a
   * struct socket * and the close path would dereference and even soclose() it.
   * (FreeMem below was already guarded the same way.)
   */
  if (libPtr->dTable != NULL)
    for (i = 0; i < libPtr->dTableSize; i++)
      if (libPtr->dTable[i] != NULL)
	CloseSocket(libPtr, i);
  
  Remove((struct Node *)libPtr); /* remove this librarybase from our list
				    of opened library bases */

  if (libPtr->tsleep_timer) {
    if (libPtr->tsleep_timer->tr_node.io_Device != NULL) {
      AbortIO((struct IORequest *)(libPtr->tsleep_timer));
      CloseDevice((struct IORequest *)libPtr->tsleep_timer);
    }
    DeleteIORequest((struct IORequest *)libPtr->tsleep_timer);
  }
  if (libPtr->timerPort)
    DeleteMsgPort(libPtr->timerPort);

  /*
   * Only the opener's own buffers are freed here. A base that was ever shared
   * never reaches this point -- the guard above returns early and leaks the
   * whole thing -- so the sharer contexts on ctxList are leaked with it. There
   * is deliberately no loop freeing them: it could only ever run on a base
   * with an empty list, and pretending otherwise would read as though the
   * sharer teardown were solved.
   */
  freeDataBuffer(&libPtr->ctx.selitems);
  freeDataBuffer(&libPtr->ctx.hostents);
  freeDataBuffer(&libPtr->ctx.netents);
  freeDataBuffer(&libPtr->ctx.protoents);
  freeDataBuffer(&libPtr->ctx.servents);
  
  if (libPtr->dTable)
    FreeMem(libPtr->dTable, libPtr->dTableSize * sizeof (struct socket *) +
	    ((libPtr->dTableSize - 1) / NFDBITS + 1) * sizeof (fd_mask));

  /*
   * PORT (AmiTCP_NG): release the descriptor tables retired by SBTC_DTABLESIZE
   * resizes. setdtablesize() deliberately does not free them at the time of the
   * swap, because selscan() may still hold a snapshot of the old pointer and
   * cannot be locked against for the duration of its scan -- see the reasoning
   * there. Here is safe: this base is being torn down, so nothing can still be
   * inside a library call using one of them.
   */
  while (libPtr->retiredTables != NULL) {
    struct ng_retired_dtable *r = libPtr->retiredTables;

    libPtr->retiredTables = r->rd_next;
    FreeMem(r->rd_mem, r->rd_size);
    FreeMem(r, sizeof (struct ng_retired_dtable));
  }

  freestart = (void *)((ULONG)libPtr - (ULONG)libPtr->libNode.lib_NegSize);
  size = libPtr->libNode.lib_NegSize + libPtr->libNode.lib_PosSize;
  bzero(freestart, size);
  FreeMem(freestart, size);

  MasterSocketBase->lib_OpenCnt--;
  /*
   * If no more libraries are open and delayed expunge is asked,
   * ELL_expunge() is called.
   */
  if (MasterSocketBase->lib_OpenCnt == 0 &&
      (MasterSocketBase->lib_Flags & LIBF_DELEXP)) {
#if __GNUC__
    register struct Library *a6 __asm("a6") = MasterSocketBase;
    (void)a6;
    return ELL_Expunge();
#elif __SASC
    return ELL_Expunge(MasterSocketBase);
#else 
#error Compiler not supported!
#endif
  }

  return NULL; /* allways return null */
}

BOOL api_init()
{
  extern void select_init(void);
  extern f_void ExecLibraryList_funcTable[];
  
  if (api_state != API_SCRATCH)
    return TRUE;

  AmiTCP_lets_you_do_expunge = FALSE;
  
  MasterSocketBase = MakeLibrary(ExecLibraryList_funcTable,
				 (UWORD *)&Library_initTable,
				 NULL,
				 sizeof(struct Library),
				 NULL);
  if (MasterSocketBase == NULL)
    return FALSE;

  InitSemaphore(&syscall_semaphore);
  { extern struct SignalSemaphore ng_spawn_semaphore;
    InitSemaphore(&ng_spawn_semaphore); }
  select_init(); /* initializes data Select() needs */
  NewList(&socketBaseList);
  NewList(&garbageSocketBaseList);
  NewList(&releasedSocketList);

  /* PORT (AmiTCP_NG): api_init() is the PROGRAM build's library creation. Mark
   * the stack as managed here -- BEFORE api_show() makes the library visible and
   * before NETTRACE (spawned by log_init) can open it -- so ELL_Open() never
   * tries to auto-spawn a second stack Process in the program build. (Only the
   * self-starting library build, which uses api_libinit() instead of api_init(),
   * leaves this FALSE and lets the first open spawn the stack.) */
  { extern volatile BOOL ng_stack_running; ng_stack_running = TRUE; }

  api_state = API_INITIALIZED;
  return TRUE;
}

/*
 * PORT (AmiTCP_NG): api_libinit() -- the post-MakeLibrary half of api_init(),
 * for the self-starting LIBS:bsdsocket.library build. When the library is a real
 * loadable file, exec's InitResident() (RTF_AUTOINIT) has ALREADY done the
 * MakeLibrary() + will AddLibrary() the base; our RomTag init function (LibInit,
 * in lib/bsdsocket_lib.c) just needs the internal bookkeeping api_init() would
 * otherwise do. Records the master base and the load seglist (kept for expunge),
 * initialises the syscall semaphore, select data and the per-opener base lists,
 * and marks the API initialised. The stack subsystems (timer/SANA/protocols/...)
 * are brought up separately, on first OpenLibrary(), by the stack process.
 */
BPTR ng_lib_seglist = 0;			/* saved by LibInit for expunge */

VOID api_libinit(struct Library *base, BPTR seglist)
{
  extern void select_init(void);

  MasterSocketBase = base;
  ng_lib_seglist = seglist;

  InitSemaphore(&syscall_semaphore);
  { extern struct SignalSemaphore ng_spawn_semaphore;
    InitSemaphore(&ng_spawn_semaphore); }
  select_init();
  NewList(&socketBaseList);
  NewList(&garbageSocketBaseList);
  NewList(&releasedSocketList);

  AmiTCP_lets_you_do_expunge = FALSE;
  api_state = API_INITIALIZED;
}

LONG nthLibrary = 0;

BOOL api_show()
{
  struct Node * libNode;
  STRPTR libName = (STRPTR)Library_initTable.ln_Name;

  if (api_state == API_SHOWN)
    return TRUE;
  if (api_state == API_SCRATCH)
    return FALSE;

  Forbid();
  for (libNode = SysBase->LibList.lh_Head; libNode->ln_Succ;
       libNode = libNode->ln_Succ) {
    if (!strncmp(libNode->ln_Name, (const char *)libName, sizeof (SOCLIBNAME) - 3)) {
#ifdef DEBUG
      int i;
      if (libNode->ln_Name[sizeof (SOCLIBNAME) - 3] == '\0') 
	i = 1;
      else 
	i = (BYTE)(libNode->ln_Name[sizeof (SOCLIBNAME) - 2] - '0' + 1);
      if (nthLibrary < i)
	nthLibrary = i;
#else
      Permit();
      return FALSE;
#endif
    }
  }
  Permit();
#ifdef DEBUG
  if (nthLibrary > 8)
    return FALSE;
  if (nthLibrary) {
    libName[sizeof (SOCLIBNAME) - 3] = '.'; 
    libName[sizeof (SOCLIBNAME) - 2] = '0' + nthLibrary;
    libName[sizeof (SOCLIBNAME) - 1] = '\0';
    MasterSocketBase->lib_Node.ln_Name = libName;
  }
#endif
  AddLibrary(MasterSocketBase);
  api_state = API_SHOWN;
  
  return TRUE;
}

VOID api_hide()
{
  if (api_state != API_SHOWN)
    return;
  Forbid();
  /* unlink Master SocketBase from System Library list */
  Remove((struct Node*)MasterSocketBase);	
  Permit();
  api_state = API_HIDDEN;
}

VOID api_setfunctions() /* DOES NOTHING NOW */
{
/*  struct Node *node2move; */
  
  if (api_state == API_SCRATCH)
    return;
  Forbid();
  if (api_state == API_SHOWN)
    /* unlink Master SocketBase from System Library list */
    Remove((struct Node*)MasterSocketBase);	

  /* here SetFunction()s to patch libray calls (forbid()/permit()) */
  /*  while(node2move = RemHead(&socketBaseList))
      AddTail(&garbageSocketBaseList, node2move); */
  Permit();
  api_state = API_FUNCTIONPATCHED;
}

/*
 * Send CTRL_C to all tasks having socketbase open. 
 */
VOID api_sendbreaktotasks()
{
  extern struct List socketBaseList; /* :/ */
  struct Node * libNode;

  Forbid();
  for (libNode = socketBaseList.lh_Head; libNode->ln_Succ;
       libNode = libNode->ln_Succ)
    if (((struct SocketBase *)libNode)->thisTask != Nettrace_Task)
      Signal(((struct SocketBase *)libNode)->thisTask, SIGBREAKF_CTRL_C);

  Permit();
}


VOID api_deinit()
{
#if DIAGNOSTIC
  if (FindTask(NULL) != AmiTCP_Task)
    log(LOG_ERR, "The calling task of api_deinit() was not AmiTCP/IP");
#endif  
  if (api_state == API_SHOWN || api_state == API_HIDDEN)
    api_setfunctions();
  if (api_state == API_SCRATCH)
    return;
  
  Forbid();
  AmiTCP_lets_you_do_expunge = TRUE;
  {
#if __GNUC__
    register struct Library *a6 __asm("a6") = MasterSocketBase;
    (void)a6;
    ELL_Expunge();
#elif __SASC
    ELL_Expunge(MasterSocketBase);
#else 
#error Compiler not supported!
#endif
  }
  Permit();

  /*
   * if SB_Expunged == FALSE, waiting until last UL_Close() expunges
   * our library.
   */
  while(SB_Expunged == FALSE)
    Wait(SIGBREAKF_CTRL_F);

  api_state = API_SCRATCH;
}

/*
 * PORT (AmiTCP_NG): deliver an error code through the SBTC_ERROR_HOOK if one is
 * installed. Returns TRUE if the hook took it, FALSE if the caller should write
 * the variable itself.
 *
 * The hook is asked to PERFORM the assignment rather than merely being told
 * about it -- ehm_Action is documented as "indicating which variable should be
 * changed" and ehm_Code as "the error code to be set". That is also the only
 * reading that makes the feature useful: its stated purpose is to work with
 * shared library bases, where errnoPtr belongs to whichever task opened the
 * base, so writing it as well would put one task's error in another's variable,
 * which is the exact fault the hook exists to avoid.
 *
 * Called on the context of whatever task is in the library call, as documented.
 * h_Entry is invoked through utility.library's CallHookPkt() so the register
 * convention (hook in A0, object in A2, message in A1) is honoured -- calling
 * h_Entry directly from C would pass the arguments on the stack and jump into
 * the application with garbage in those registers. If utility.library is not
 * open the hook is skipped rather than called incorrectly.
 */
static BOOL ng_error_hook_deliver(struct SocketBase *libPtr, ULONG action, LONG code)
{
  extern struct Library *UtilityBase;
  struct ErrorHookMsg ehm;
  struct Hook *hook = libPtr->errorHook;

  if (hook == NULL || UtilityBase == NULL)
    return FALSE;

  /*
   * Never call out to application code from inside an spl region. Unlike the
   * log hook, this one CANNOT be deferred to another task -- routing errno to
   * the calling task is the entire point of it -- so the hazard is closed by
   * declining instead, and the error is written directly. Misrouting one errno
   * in a shared-base program is a bad outcome; hanging the machine is a worse
   * one, and this codebase already prefers the lesser failure elsewhere (see
   * UL_Close(), which leaks a base rather than risk a use-after-free).
   *
   * No current caller of writeErrnoValue() runs at spl, so this costs one
   * compare today. It exists so that a future one added inside splnet() -- easy
   * to do by accident across ~140 vectors -- cannot silently reintroduce a
   * machine hang.
   *
   * The test is `> SPL0`, NOT Exec's usual `TDNestCnt >= 0`. In a shipped build
   * (DEBUG undefined, sys/synch.h) this port stores the spl LEVEL directly in
   * TDNestCnt -- SPL0 is 0, SPLNET 2, SPLIMP 3 -- and spl_0() leaves 0 behind
   * rather than Exec's idle -1. So `>= 0` would read as "forbidden" at the
   * stack's own baseline and would disable the hook almost everywhere.
   *
   * KNOWN LIMIT: this sees spl regions, not a bare Forbid() taken by somebody
   * else, which is indistinguishable from SPLSOFTCLOCK in the same counter.
   */
  if (SysBase->TDNestCnt > SPL0)
    return FALSE;

  ehm.ehm_Size   = sizeof ehm;
  ehm.ehm_Action = action;
  ehm.ehm_Code   = code;
  (void)CallHookPkt(hook, NULL, (APTR)&ehm);
  return TRUE;
}

/*
 * PORT (AmiTCP_NG): h_errno counterpart of writeErrnoValue(). Exists so that
 * SBTC_ERROR_HOOK can intercept resolver errors too -- h_errno used to be
 * assigned straight through a macro at ~17 sites, which no hook could see, and
 * an error hook that silently missed every DNS failure would be worse than none.
 */
void writeHErrnoValue(struct SocketBase * libPtr, int value)
{
  if (ng_error_hook_deliver(libPtr, EHMA_Set_h_errno, (LONG)value))
    return;
  *libPtr->hErrnoPtr = (LONG)value;
}

/*
 * PORT (AmiTCP_NG): SBTC_SIG_ADDRESS_CHANGE_MASK -- tell every base that asked
 * that an interface address was added, changed or removed.
 *
 * Broadcast rather than aimed at one base: an address change is a fact about
 * the machine, and any number of applications may be watching for it. Forbid()
 * for the walk, matching FindSocketBase(); thisTask is tested for the usual
 * reason -- a shared-then-closed base outlives its opener, whose Task Exec may
 * have recycled, and Signal() through that corrupts the scheduler.
 */
void ng_signal_address_change(void)
{
  extern struct List socketBaseList;
  struct Node *libNode;

  Forbid();
  for (libNode = socketBaseList.lh_Head; libNode->ln_Succ;
       libNode = libNode->ln_Succ) {
    struct SocketBase *b = (struct SocketBase *)libNode;

    if (b->sigAddrChangeMask != 0 && b->thisTask != NULL)
      Signal(b->thisTask, b->sigAddrChangeMask);
  }
  Permit();
}

/*
 * PORT (AmiTCP_NG): read the caller's errno through the (size, pointer) pair,
 * snapshotting both under Forbid() so a concurrent _SetErrnoPtr() on a shared
 * base cannot pair a new size with an old pointer. See the note in amiga_api.h.
 */
int readErrnoValue(struct SocketBase * base)
{
  BYTE   erri;
  UBYTE *errp;

  Forbid();
  erri = base->errnoSize;
  errp = base->errnoPtr;
  Permit();

  return (int)errp[erri - 1];
}

void writeErrnoValue(struct SocketBase * libPtr, int errno)
{
  /*
   * errnoSize is now restricted to 1, 2 or 4
   */
  /* PORT (AmiTCP_NG) fix: snapshot the (size, pointer) pair together.
   * _SetErrnoPtr() can change both from another task on a shared base; reading
   * them at different moments can pair a new size with an old pointer and write
   * past the caller's errno. Forbid() is the right tool -- these fields are only
   * ever touched at task level, and the section is two loads. */
  BYTE   erri;
  UBYTE *errp;

  Forbid();
  erri = libPtr->errnoSize;
  errp = libPtr->errnoPtr;
  Permit();

  /* PORT (AmiTCP_NG): SBTC_ERROR_HOOK takes delivery when installed. */
  if (ng_error_hook_deliver(libPtr, EHMA_Set_errno, (LONG)errno))
    return;

  if (erri == 4) {
    *(ULONG *)errp = (ULONG)errno;
    return;
  }
  if (erri == 2) {
    *(UWORD *)errp = (UWORD)errno;
    return;
  }
  /* size must be 1 */
  *(UBYTE *)errp = (UBYTE)errno;
  return;
}
