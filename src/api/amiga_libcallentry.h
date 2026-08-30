/*
 * $Id: amiga_libcallentry.h,v 3.3 1994/02/15 23:18:42 jraja Exp $
 * 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created: Mon Feb 15 13:37:42 1993 too
 * Last modified: Wed Feb 16 01:18:01 1994 jraja
 * 
 * HISTORY
 * $Log: amiga_libcallentry.h,v $
 * Revision 3.3  1994/02/15  23:18:42  jraja
 * Changed sdFind() to return the sd via LONG * instead of ULONG * to be
 * consistent with API types.
 *
 * Revision 3.2  1994/01/18  22:53:22  jraja
 * Added macros CHECK_TASK_NULL() and CHECK_TASK_VOID().
 *
 * Revision 3.1  1994/01/04  14:17:16  too
 * Removed obsolete fdAlloc. It is now replaced by sdFind which uses
 * new socket uasge bitmask to find free socket index.
 * ..prototype for sdFind added
 *
 * Revision 1.20  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.19  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.18  1993/05/15  10:06:42  too
 * Removed macros AMI_ENTRY() and API_EXIT()
 *
 * Revision 1.17  93/04/26  11:52:33  11:52:33  too (Tomi Ollila)
 * Changed include paths of amiga_api.h, amiga_libcallentry.h and amiga_raf.h
 * from kern to api
 * 
 * Revision 1.16  93/04/26  10:54:08  10:54:08  too (Tomi Ollila)
 * Added CHECK_TASK2().
 * Removed setting of errno when task checking fails
 * 
 * Revision 1.15  93/04/06  17:57:28  17:57:28  too (Tomi Ollila)
 * Changed FIndTask(NULL):s to SysBase->ThisTask
 * 
 * Revision 1.14  93/03/30  16:51:52  16:51:52  too (Tomi Ollila)
 * Fixed API_entry etc macros. added prototype for writeErrnoValue().
 * moved fdAlloc here from amiga_generic.c
 * 
 * Revision 1.13  93/03/15  14:35:08  14:35:08  jraja (Jarno Tapio Rajahalme)
 * Changed little wrongTaskErrorFmt's declaration.
 * (To make it compile with SASC).
 * 
 * Revision 1.12  93/03/14  16:06:22  16:06:22  too (Tomi Ollila)
 * Moved declaration of wrongTaskErrorFrm from here to amiga_api.c
 * 
 * Revision 1.11  93/03/13  14:01:12  14:01:12  too (Tomi Ollila)
 * Added macros CHSCK_TASK and API_STDRETURN to replace macros
 * API_ENTRY and API_EXIT, which are left untill all uses are removed.
 * 
 * Revision 1.10  93/03/12  00:59:54  00:59:54  too (Tomi Ollila)
 * Changed errno to errnoPtr, so user can set it anywhere wanted
 * 
 * Revision 1.9  93/03/04  09:43:20  09:43:20  jraja (Jarno Tapio Rajahalme)
 * Fixed includes.
 * 
 * Revision 1.8  93/03/02  16:55:16  16:55:16  ppessi (Pekka Pessi)
 * af.h
 * 
 * Revision 1.1  93/02/28  22:39:30  22:39:30  ppessi (Pekka Pessi)
 * 	Separated RAF macros to amiga_raf.h
 * 
 * Revision 1.7  93/02/26  13:22:41  13:22:41  too (Tomi Ollila)
 * code checked w/ too, ppessi and jraja
 * 
 * Revision 1.6  93/02/25  16:46:59  16:46:59  too (Tomi Ollila)
 * Added #include <sys/errno.h>
 * 
 * Revision 1.5  93/02/25  13:01:58  13:01:58  too (Tomi Ollila)
 * Added static inlines, sys/cdefs etc.
 * 
 * Revision 1.4  93/02/24  10:57:08  10:57:08  too (Tomi Ollila)
 * some getSock discussion added.
 * 
 * Revision 1.3  93/02/17  14:40:18  14:40:18  too (Tomi Ollila)
 * Added RAFs to make libcall writing easier and more readable.
 * 
 * Revision 1.2  93/02/16  16:01:12  16:01:12  too (Tomi Ollila)
 * fixed semaphore and SetTaskPri (forbid/permit removed)
 * 
 * Revision 1.1  93/02/16  15:39:57  15:39:57  too (Tomi Ollila)
 * Initial revision
 * 
 * 
 */

#ifndef AMIGA_LIBCALLENTRY_H
#define AMIGA_LIBCALLENTRY_H

#ifndef AMIGA_API_H
#error include amiga_api.h before this (libcallentry.h)
#endif

#ifndef SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

#ifndef SYS_ERRNO_H
#include <sys/errno.h>
#endif

#ifndef SYS_SYSLOG_H
#include <sys/syslog.h>
#endif

/*
 * The following macros are written in each socket library functions
 * (execpt Errno()). they makes sure that the task that calls library
 * functions is the opener task of the socketbase it is using.
 */

extern const char wrongTaskErrorFmt[];

/*
 * PORT (AmiTCP_NG): first-touch lazy start of the stack. In the self-starting
 * LIBS:bsdsocket.library build the stack subsystems (SANA/timer/protocols) are not
 * brought up until the first client API call -- and ANY vector may be that first
 * call. A Roadshow config tool, for instance, calls AddInterfaceTagList() before it
 * ever calls socket(). So we trigger the one-time spawn from every CHECK_TASK* entry
 * macro (below), not just from socket(). It is a single volatile-flag test on every
 * later call; the spawn itself happens exactly once (serialised in
 * ng_stack_ensure_running()). In the PROGRAM build ng_stack_running is TRUE from
 * api_init(), so this is always a no-op there.
 */
/*
 * Start the stack if it is not running -- UNLESS this base belongs to a stack that has
 * already been shut down.
 *
 * The dead test lives HERE, and not only in CHECK_TASK*, because a good number of
 * vectors never use those macros at all: the whole mbuf_* family, RemoveInterface,
 * BeginInterfaceConfig, the RoadshowData trio, AddDomainNameServer and more call this
 * and nothing else. Without the test, a straggler calling any one of them through a
 * base left over from a NetShutdown would silently RESPAWN THE ENTIRE STACK -- new
 * task, new interfaces, a fresh DHCP round -- which is precisely what the operator
 * used a shutdown to prevent. Every vector that can start the stack must first be able
 * to refuse to.
 *
 * Vectors that touch no stack state at all (inet_addr, Inet_NtoA and friends) do not
 * call this and correctly keep working.
 *
 * What this does NOT do is make such a call FAIL -- the macro is used from vectors
 * with every return type, so it cannot return a value of its own. The vectors that go
 * through CHECK_TASK*() do fail cleanly, via NG_CHECK_DEAD. The ones that only come
 * here proceed, exactly as they would have before any of this existed; what they no
 * longer do is drag a whole new stack up behind them.
 */
#define NG_ENSURE_STACK()						\
  { extern volatile BOOL ng_stack_running;				\
    extern BOOL ng_stack_ensure_running(void);				\
    if (!ng_stack_running && !(libPtr != NULL && libPtr->sbDead))	\
      (void)ng_stack_ensure_running(); }

/* DIAGNOSTIC-only: warn when a library vector is entered with little headroom on
 * the CALLER's task stack. The deepest protocol descent from a vector is ~1.5 KB
 * and there is no guard page (no MMU), so an overrun silently corrupts whatever
 * sits below the stack -- possibly another task. Implemented as ONE function (in
 * amiga_generic2.c) rather than inlined, so the per-vector cost is a single JSR
 * instead of bloating every vector body. Compiles out when DIAGNOSTIC is off. */
#if DIAGNOSTIC
extern void ng_low_stack_check(void);
#define NG_STACK_CHECK() ng_low_stack_check()
#else
#define NG_STACK_CHECK() ((void)0)
#endif

/*
 * PORT (AmiTCP_NG): give a task sharing this base its own scratch context
 * before it can touch any. Only costs anything on a shared base -- the flag
 * test short-circuits for everyone else. Done HERE, at vector entry, because
 * this is at spl0 with no Forbid() held so allocating is safe; deeper in the
 * socket layer it would not be. A failure leaves the task on the opener's
 * context, which is what it had before contexts existed.
 */
#define NG_ENSURE_CTX()							\
  { extern struct ng_taskctx *ng_ctx_ensure(struct SocketBase *);	\
    if (libPtr->flags & SBFF_CAN_SHARE) (void)ng_ctx_ensure(libPtr); }

/*
 * PORT (AmiTCP_NG): SBTC_CAN_SHARE_LIBRARY_BASES (tag 51) relaxes the
 * same-task rule, matching Roadshow, which also refuses non-opener callers
 * until an application opts in.
 *
 * What sharing does NOT buy, and cannot without giving every task its own copy
 * of the per-opener state (at which point it is just reopening the library,
 * which Roadshow's own autodoc recommends):
 *   - signals (SIGIO, SIGURG, address-change, CTRL-C) still go to the OPENER,
 *     exactly as Roadshow documents;
 *   - errno and h_errno are shared. errnoPtr/hErrnoPtr are one pointer each for
 *     the whole base -- and they point at the base's own defErrno/defHErrno
 *     unless the program repoints them -- so two tasks failing concurrently
 *     overwrite each other's. There is nowhere to put a per-task errno without
 *     breaking SBTC_ERRNOPTR's whole-base contract, so Roadshow has this too;
 *   - syslog settings (mask, facility, tag) are per base, not per task;
 *   - a descriptor is released the moment close() is called, before the socket
 *     has finished lingering, so another task may be given that number while
 *     the old connection is still shutting down. That is what close() means
 *     everywhere else, but it does mean Dup2Socket() onto a descriptor whose
 *     socket lingers can find the number taken, and fails with EBADF rather
 *     than seizing it back;
 *   - a context is created for a sharing task on its first call and is NEVER
 *     reclaimed, because there is no hook for a task exiting. AmigaOS recycles
 *     Task structures, so if a sharer exits and its address is later reused for
 *     another task that also shares this base, that task inherits the dead
 *     one's scratch -- including its resolver socket. Bounded, but real: a
 *     long-lived shared base with a churn of short-lived workers is the shape
 *     to avoid;
 *   - closing a base that other tasks have shared does not free it. The closer
 *     cannot tell whether a sharer is still inside the library (or asleep in
 *     recv() on one of its sockets), and Exec runs Close under Forbid() so it
 *     cannot wait to find out -- so the base, its sockets and its memory are
 *     deliberately leaked rather than pulled out from under a live task. See
 *     UL_Close() in api/amiga_api.c.
 *
 * These are the reasons Roadshow's autodoc recommends reopening the library per
 * user rather than sharing one base; sharing is the escape hatch for an
 * application that knows the limits, which is exactly how we treat it.
 *
 * NB the reject path below must tolerate a NULL libPtr->thisTask. UL_Close()
 * clears it when it leaks a base that was shared, precisely so a recycled Task
 * address cannot match the owner test above and be granted owner access -- and
 * a sharer CAN still arrive here afterwards, if the application turned
 * SBFF_CAN_SHARE back off before closing. libPtr comes straight from the
 * vector's own a6, so the delisting that hides the base from every list walk
 * does not protect this site.
 */
/*
 * PORT (AmiTCP_NG): refuse a call through a base whose stack has been shut down.
 *
 * NetShutdown can take the network down while a program still holds a base open, so
 * that base outlives everything it refers to. It is deliberately left allocated (the
 * program still has the pointer), but it must not be USED: its sockets and the whole
 * stack behind them are gone. Fail here, at the door, rather than anywhere deeper.
 *
 * Placed before NG_ENSURE_STACK() on purpose. Otherwise the first call through a dead
 * base would lazily start a BRAND NEW stack and then run against a base belonging to
 * the old one -- which is worse than either failing or restarting cleanly.
 */
#define NG_CHECK_DEAD(retval)				\
  if (libPtr->sbDead) {					\
    writeErrnoValue(libPtr, ENETDOWN);			\
    return retval;					\
  }

#define CHECK_TASK()					\
  NG_CHECK_DEAD(-1);					\
  NG_ENSURE_STACK();					\
  NG_STACK_CHECK();					\
  if (!(libPtr->flags & SBFF_CAN_SHARE) &&		\
      libPtr->thisTask != SysBase->ThisTask) {		\
    struct Task * wTask = SysBase->ThisTask;		\
    log(LOG_CRIT, wrongTaskErrorFmt, wTask,		\
	wTask->tc_Node.ln_Name,	libPtr->thisTask,	\
	libPtr->thisTask					\
	  ? (STRPTR)libPtr->thisTask->tc_Node.ln_Name	\
	  : (STRPTR)"<closed>");			\
    return -1;						\
  }							\
  NG_ENSURE_CTX()

#define CHECK_TASK_NULL()				\
  NG_CHECK_DEAD(NULL);					\
  NG_ENSURE_STACK();					\
  NG_STACK_CHECK();					\
  if (!(libPtr->flags & SBFF_CAN_SHARE) &&		\
      libPtr->thisTask != SysBase->ThisTask) {		\
    struct Task * wTask = SysBase->ThisTask;		\
    log(LOG_CRIT, wrongTaskErrorFmt, wTask,		\
	wTask->tc_Node.ln_Name,	libPtr->thisTask,	\
	libPtr->thisTask					\
	  ? (STRPTR)libPtr->thisTask->tc_Node.ln_Name	\
	  : (STRPTR)"<closed>");			\
    return NULL;					\
  }							\
  NG_ENSURE_CTX()

#define CHECK_TASK2() CHECK_TASK_NULL()

#define CHECK_TASK_VOID()				\
  NG_CHECK_DEAD();					\
  NG_ENSURE_STACK();					\
  NG_STACK_CHECK();					\
  if (!(libPtr->flags & SBFF_CAN_SHARE) &&		\
      libPtr->thisTask != SysBase->ThisTask) {		\
    struct Task * wTask = SysBase->ThisTask;		\
    log(LOG_CRIT, wrongTaskErrorFmt, wTask,		\
	wTask->tc_Node.ln_Name,	libPtr->thisTask,	\
	libPtr->thisTask					\
	  ? (STRPTR)libPtr->thisTask->tc_Node.ln_Name	\
	  : (STRPTR)"<closed>");			\
    return;						\
  }							\
  NG_ENSURE_CTX()

/*
 * PORT (AmiTCP_NG) fix: wrapped in do { } while (0). This macro expands to
 * multiple statements ending in `return`; unwrapped, `if (cond) API_STD_RETURN(e,
 * r);` bound only the inner `if (e == 0) return r;` to the guard, leaving
 * `writeErrnoValue(); return -1;` to run UNCONDITIONALLY. That silently broke
 * gethostname() -- its `if (namelen <= 0) API_STD_RETURN(EINVAL, 0);` made every
 * call return -1 without touching the buffer. (It was the only if-guarded use; the
 * other ~25 are standalone statements, unaffected.) The do/while makes it a single
 * statement, correct in every context.
 */
/*
 * PORT (AmiTCP_NG): API failure tracing, for compatibility debugging.
 *
 * Every vector that returns an error logs its own name and that errno. That is
 * how you find out WHICH call a closed-source client is unhappy with instead of
 * guessing from the outside -- it is what identified Amiga Explorer's
 * setsockopt(SO_EVENTMASK). __FUNCTION__ gives the vector name for free, so this
 * covers every vector that returns through here rather than a hand-picked few.
 *
 * COMPILED INTO EVERY BUILD, with no flag to switch it off. It used to be gated
 * on NG_APITRACE and that was wrong twice over: it costs 2,184 bytes and no
 * measurable speed (this is an error return, not the data path, and a disabled
 * log() stops at two compares without evaluating its arguments), and a
 * diagnostic whose whole job is to explain a machine you cannot reach must not
 * require you to build that machine a private binary. It once shipped switched
 * off in a pre-release advertised as ready for exactly that job.
 *
 * Logged at LOG_DEBUG, so LOGGING=ON plus LOGLEVEL=7 in AmiTCP.config is all it
 * takes, on any build.
 *
 * It logs from inside API_STD_RETURN, which every vector reaches after
 * releasing the syscall semaphore.
 *
 * CORRECTION (2026-08-07): an earlier version of this comment said the
 * placement mattered because "log() can block waiting for a free log message".
 * It does not. GetLogMsg() (kern/amiga_log.c) uses GetMsg(), which never waits:
 * an empty pool simply increments GetLogMsgFail and the message is dropped. So
 * there is no blocking hazard here, and none in calling log() from a vector
 * that still holds the semaphore. The claim mattered enough to correct rather
 * than leave: it now guards every build rather than an opt-in one, and a
 * hazard nobody can find is a hazard people design around for no reason.
 *
 * What IS still true is that the tracer costs a formatted message on an error
 * return, so it is gated by LOGLEVEL (LOG_DEBUG) and skipped at the call site
 * when the level excludes it -- two compares, arguments never evaluated.
 *
 * And the placement after the release is still the right one, for a different
 * reason than the old comment gave: syscall_semaphore is ONE semaphore shared by
 * every base and every socket, and ObtainSyscallSemaphore() boosts the caller to
 * the net task's priority while it is held. Formatting a log message before
 * releasing it would lengthen a machine-wide, priority-boosted critical section
 * that every other task's library calls are queued behind. Not a deadlock -- a
 * cost. Do not move the trace earlier on the grounds that it is "safe".
 */
#define NG_TRACE_FAIL(error)						\
  log(LOG_DEBUG, "APITRACE %s failed, errno %ld",			\
      (STRPTR)__FUNCTION__, (LONG)(error))

#define API_STD_RETURN(error, ret)	\
  do {					\
    if (error == 0)			\
      return ret;			\
    NG_TRACE_FAIL(error);		\
    writeErrnoValue(libPtr, error);	\
    return -1;				\
  } while (0)
						
/*
 * getSock() gets a socket referenced by given filedescriptor if exists,
 * returns EBADF (bad file descriptor) if not. (because this now uses
 * struct socket * pointer and those are often register variables, perhaps
 * some kind of change is to be done here).
 */

static inline LONG getSock(struct SocketBase *p, int fd, struct socket **sop)
{
  register struct socket *so;
  
  if ((unsigned)fd >= p->dTableSize || (so = p->dTable[(short)fd]) == NULL)
    return (EBADF);
  *sop = so;
  return 0;
}

/*
 * Prototype for sdFind. This is located in amiga_syscalls.c and replaces
 * fdAlloc there. libPtr->nextDToSearch is dumped.
 */
LONG sdFind(struct SocketBase * libPtr, LONG *fdp);

#ifndef AMIGA_RAF_H
#include <api/amiga_raf.h>
#endif

#endif /* !AMIGA_LIBCALLENTRY_H */
