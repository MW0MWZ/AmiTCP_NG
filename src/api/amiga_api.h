/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

/*
 * $Id: amiga_api.h,v 3.7 1994/04/02 11:12:59 jraja Exp $
 * 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created: Tue Jan 26 20:35:28 1993 too
 * Last modified: Sat Apr  2 14:12:06 1994 jraja
 * 
 * $Log: amiga_api.h,v $
 * Revision 3.7  1994/04/02  11:12:59  jraja
 * Added resolver variables, minor cleanup, some older fields has been
 * changed, too.
 *
 * Revision 3.6  1994/03/29  12:56:35  ppessi
 * Added SBTC_COMPAT43 tag
 *
 * Revision 3.5  1994/03/22  07:34:47  jraja
 * Added fdCallback (function pointer for fd coordination with a link library)
 * to the SocketBase.
 *
 * Revision 3.4  1994/01/20  02:12:49  jraja
 * Changed baseErrno() to readErrnoValue().
 *
 * Revision 3.3  1994/01/18  19:18:52  jraja
 * Added baseErrno(base) macro for errno reading/writing.
 *
 * Revision 3.2  1994/01/12  07:16:39  jraja
 * Added Syslog() related variables to the library base.
 *
 * Revision 3.1  1994/01/04  14:20:11  too
 * Added hErrno to SocketBase. Marker nextDToSearch to obsolete (not
 * removed since it uses no extra space)
 *
 * Revision 1.28  1993/11/26  16:23:42  too
 * added prototype for sendbreaktotasks()
 *
 * Revision 1.27  1993/06/07  12:37:20  too
 * Changed inet_ntoa, netdatabase functions and WaitSelect() use
 * separate buffers for their dynamic buffers
 *
 */

#ifndef AMIGA_API_H
#define AMIGA_API_H

#ifndef EXEC_TYPES_H
#error <exec/types.h> not included.
#endif

#ifndef EXEC_LIBRARIES_H
#error <exec/libraries.h> not included.
#endif

#ifndef EXEC_SEMAPHORES_H
#error <exec/semaphores.h> not included.
#endif

#ifndef EXEC_TASKS_H
#include <exec/tasks.h>
#endif

#ifndef SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif

#ifndef SYS_QUEUE_H
#include <sys/queue.h>
#endif

#ifndef API_RESOLV_H
#include <api/resolv.h>
#endif

struct newselbuf;

/*
 * structure for holding size and address of some dynamically allocated buffers
 * such as selitems for WaitSelect() and netdatabase entry structures
 */
struct DataBuffer {
  int		db_Size;
  void *	db_Addr;
};

typedef int (* ASM fdCallback_t)(REG(d0) int fd, REG(d1) int action);

/*
 * PORT (AmiTCP_NG): per-CALLING-TASK state.
 *
 * struct SocketBase conflated two different things: state that is genuinely
 * shared by everyone using a base (the descriptor table -- the entire point of
 * sharing), and scratch that is only per-caller, and which was safe as a single
 * instance purely because, until SBTC_CAN_SHARE_LIBRARY_BASES, exactly one task
 * could ever be inside a base. Everything below is the second kind: two tasks
 * sharing one base would otherwise stomp on each other's gethostbyname()
 * result, resolver socket or getservent() cursor.
 *
 * These all have to OUTLIVE the call that filled them -- gethostbyname()
 * returns a pointer into hostents -- so they cannot live on the caller's stack.
 * But they are plain memory, so any task can free them and teardown is trivial.
 * (The sleep machinery has the opposite shape: it must not outlive the call,
 * and its MsgPort can only ever be freed by the task that made it, because
 * FreeSignal() always acts on SysBase->ThisTask. That is handled separately.)
 *
 * The OPENER's context is embedded in the base, so the overwhelmingly common
 * unshared case allocates nothing extra and behaves exactly as before.
 */
struct ng_taskctx {
  struct MinNode	tc_node;	/* link in SocketBase.ctxList     */
  struct Task *		tc_task;	/* task this context belongs to   */
/* buffer for inet_ntoa */
  char			inet_ntoa[20]; /* xxx.xxx.xxx.xxx\0 */
/* -- which select buffer this task is currently building, during WaitSelect.
 * MUST live beside selitems, not on the base: selenter() reads it to decide
 * which newselbuf to chain a socket's wait item onto, and that buffer lives in
 * selitems. Leaving it shared while selitems became per-task meant one task's
 * WaitSelect could redirect another's items into ITS buffer -- which then gets
 * freed or moved when that task's selitems is reallocated, leaving the first
 * task's chain pointing at freed memory. WaitSelect deliberately holds no
 * semaphore, so the two really do interleave. -- */
  struct newselbuf *	p_sb;
/* -- pointers for data buffers that MAY be used -- */
  struct DataBuffer	selitems;
  struct DataBuffer	hostents;
  struct DataBuffer	netents;
  struct DataBuffer	protoents;
  struct DataBuffer	servents;
/* -- resolver state (see api/resolv.h, which reaches these via _res/res_sock) -- */
  LONG			res_socket;       /* socket used for resolver comm. */
  struct state          res_state;
/* -- sequential-read cursors for the get{net,proto,serv}ent() iterators. Each
 * points at the next NDB list node this task will return, or NULL before
 * setXent()/after endXent(). -- */
  struct MinNode *	netentCursor;
  struct MinNode *	protoentCursor;
  struct MinNode *	serventCursor;
/* -- NDB generation each cursor above was stamped at (see reset_netdb()). A
 * mismatch against NDB->ndb_Generation means the cursor points into a freed
 * list and must be rewound to the current head before use. -- */
  ULONG			netentGen;
  ULONG			protoentGen;
  ULONG			serventGen;
};

/*
 * PORT (AmiTCP_NG): a descriptor table that has been replaced by a resize and
 * must NOT be freed while the base lives. setdtablesize() cannot know whether
 * another task is midway through a scan holding the old pointer -- selscan()
 * legitimately does -- so retired blocks are chained here and released together
 * in UL_Close(). See setdtablesize() (api/amiga_generic2.c) for the reasoning.
 */
struct ng_retired_dtable {
  struct ng_retired_dtable *	rd_next;
  APTR				rd_mem;
  ULONG				rd_size;
};

struct SocketBase {
  struct Library	libNode;
/* -- "Global" Errno -- */
  BYTE			flags;
  BYTE			errnoSize;                             /* 1, 2 or 4 */
 /* -- now we are longword aligned -- */
  UBYTE *		errnoPtr;                   /* this points to errno */
  LONG			defErrno;
/* Task pointer of owner task */
  struct Task *		thisTask;
/* Task that made the library call currently in progress -- the same as
 * thisTask unless SBTC_CAN_SHARE_LIBRARY_BASES let another task in. Set by
 * ObtainSyscallSemaphore() and used to restore the priority it boosted. */
  struct Task *		callerTask;
/* task priority changes (WORDS so we keep structure longword aligned) */  
  BYTE			myPri;        /* task's priority just after libcall */
  BYTE			libCallPri;  /* task's priority during library call */
/* note: not long word aligned at this point */
/* -- descriptor sets -- */
  WORD			dTableSize; /* long word aligned again */
  struct socket	**	dTable;
/* PORT (AmiTCP_NG): descriptor tables retired by a resize, freed at UL_Close. */
  struct ng_retired_dtable * retiredTables;
  fdCallback_t		fdCallback;
/* AmiTCP signal masks */
  ULONG			sigIntrMask;
  ULONG			sigIOMask;
  ULONG			sigUrgMask;
/* PORT (AmiTCP_NG): SBTC_SIGEVENTMASK (tag 4). The signal sent to thisTask when
 * a socket owned by this base records an event selected with SO_EVENTMASK.
 * Zero -- the default -- means the application never asked for events, and no
 * signal is sent however many events are recorded. */
  ULONG			sigEventMask;
/* PORT (AmiTCP_NG): SBTC_SIG_ADDRESS_CHANGE_MASK (tag 57). Signalled to
 * thisTask whenever an interface address is added, changed or removed. Every
 * base that asked is signalled -- the walk is over socketBaseList -- because an
 * address change is a fact about the machine, not about one application. */
  ULONG			sigAddrChangeMask;
/* PORT (AmiTCP_NG): SBTC_ERROR_HOOK (tag 68). When installed, errno and
 * h_errno are delivered by CALLING this hook instead of being written through
 * errnoPtr/hErrnoPtr. That is the point of it: with SBTC_CAN_SHARE_LIBRARY_BASES
 * those pointers belong to whichever task opened the base, so writing them
 * reports one task's error into another task's variable. NULL = write directly,
 * the default and the behaviour every existing program expects. */
  struct Hook *		errorHook;
/* -- timer.device, opened once when the base is opened. The per-call sleep
 * context (struct ng_sleepctx, sys/synch.h) clones io_Device/io_Unit from this
 * request rather than opening the device itself, because tsleep() runs with
 * Forbid() held by its caller and OpenDevice() there can hang the machine. The
 * request and port are NOT used to block on any more -- each blocking call
 * builds its own on the stack of the task that blocks. -- */
  struct timerequest *	tsleep_timer;
  struct MsgPort *	timerPort;
/* -- variables for the syslog (see netinclude:sys/syslog.h) -- */
  UBYTE			LogStat;                                  /* status */
  UBYTE			LogMask;                     /* mask for log events */
  UWORD			LogFacility;                       /* facility code */
  const char *		LogTag;	           /* tag string for the log events */
/* -- resolver variables -- */
  LONG *		hErrnoPtr;
  LONG			defHErrno;
/* -- PORT (AmiTCP_NG): per-calling-task scratch. ctx is the OPENER's, embedded
 * so the unshared case costs nothing; ctxList holds one for each additional
 * task once the base is shared. Reach these through NG_CTX(), never directly,
 * so the right task's copy is used. -- */
  struct ng_taskctx	ctx;
  struct MinList	ctxList;
  BOOL			ctxWarned;	/* warned once about a context-less sharer */
/* Sharing was enabled on this base AT SOME POINT. A latch, never cleared --
 * unlike SBFF_CAN_SHARE, which an application may turn off again. UL_Close
 * needs the latch: the base pointer may already be in another task's hands
 * before that task has made its first call, so neither the live flag nor the
 * context list proves it is safe to free. */
  BOOL			everShared;
/* PORT (AmiTCP_NG): this base belonged to a stack that has since been shut down.
 *
 * A NetShutdown proceeds even when a program is still holding a base open -- the
 * breaks are a warning, not a veto. That program keeps its pointer, so the base
 * cannot be freed; but everything it refers to (sockets, mbufs, the descriptor
 * table's contents) is torn down with the stack. Marked dead, the base stays
 * allocated and every vector entered through it fails cleanly with ENETDOWN
 * instead of walking into freed memory, and its eventual CloseLibrary() frees
 * only the base's own storage. */
  BOOL			sbDead;
};

/* 
 * Socket base flags 
 */
#define SBFB_COMPAT43	0L	    /* compat 43 code (without sockaddr_len) */
#define SBFB_CAN_SHARE	1L	    /* base may be used by non-opener tasks  */

#define SBFF_COMPAT43   1L
#define SBFF_CAN_SHARE  2L

/*
 * NB: the CASE_FLAG() macro in api/amiga_generic2.c tests and sets
 * `libPtr->flags & (flag)`, so it must be handed one of the SBFF_ MASKS above,
 * never an SBFB_ bit number. (The dead `#ifdef notyet` COMPAT43 entry there
 * passes SBFB_COMPAT43, which is 0 -- a permanent no-op if ever enabled.)
 */

/*
 * PORT (AmiTCP_NG): resolve the per-calling-task scratch context.
 *
 * Deliberately OUT OF LINE, and deliberately resolved on every access.
 *
 * The obvious optimisation -- stamp the caller's context on the base once per
 * call and just load it -- is wrong the moment two tasks really share a base:
 * the vectors that touch this scratch (the resolver, the netdb iterators,
 * Inet_NtoA, WaitSelect) do NOT hold syscall_semaphore, so a second task
 * entering the library mid-call would re-stamp the field and the first task
 * would start reading and writing the second one's buffers. That is precisely
 * the bug this whole partition exists to remove, so it cannot be reintroduced
 * as an optimisation.
 *
 * Inlining the resolution instead cost 8.6 KB of code and nearly doubled
 * res_send.o and getxbyy.o, because _res and res_sock expand dozens of times in
 * a single function. Out of line, each use is a call: correct, and the cost
 * lands only on the resolver and netdb paths, which are not hot -- no
 * per-packet path touches any of this.
 *
 * It must NEVER return NULL: the call sites reach fields straight through it
 * with no error path -- that is what keeps the resolver and netdb sources
 * untouched -- so a NULL would be an immediate crash at whichever field was
 * touched first. If a sharer has no context of its own it gets the opener's,
 * which is merely the behaviour that predates contexts.
 *
 * The list walk needs no Forbid(), though ng_ctx_ensure() takes one to EXTEND
 * the list: this is a single-CPU machine, nothing touches ctxList at interrupt
 * level, and AddTail()'s stores are done under Forbid(), so a walker can only
 * ever see the list fully before or fully after an insertion, never torn.
 */
extern struct ng_taskctx *ng_ctx(struct SocketBase *p);

#define NG_CTX(p)	ng_ctx(p)

/*
 * macro for getting error value pointed by the library base. All but
 * the lowest byte of the errno are assumed to stay zero. 
 */
/*
 * PORT (AmiTCP_NG) fix: was a macro expanding to
 *     ((base)->errnoPtr[(base)->errnoSize - 1])
 * i.e. two separate reads of the errnoPtr/errnoSize PAIR, inlined at a dozen
 * call sites. _SetErrnoPtr() can change both from another task on a shared base,
 * so an interleaving could index the NEW size into the OLD pointer. Out of line
 * and Forbid()-protected, exactly like writeErrnoValue(), so there is one place
 * to be correct rather than twelve. */
int readErrnoValue(struct SocketBase *base);

extern struct SignalSemaphore syscall_semaphore;
extern struct List releasedSocketList;

/*
 *  Functions to put and remove application library to/from exec library list
 */
BOOL api_init(VOID);
BOOL api_show(VOID);
VOID api_hide(VOID);
VOID api_setfunctions(VOID);
VOID api_sendbreaktotasks(VOID);
VOID api_deinit(VOID);

/* Close/free sockets released with ReleaseSocket() but never claimed by an
 * ObtainSocket(). MUST be called while the stack is still up (see the note on
 * the definition in amiga_generic.c) -- deinit_all() calls it after api_hide(). */
VOID reapReleasedSockets(VOID);

/* Function which set Errno value */

VOID writeErrnoValue(struct SocketBase *, int);
/* PORT (AmiTCP_NG): h_errno counterpart, so SBTC_ERROR_HOOK sees resolver
 * errors too. Use set_h_errno() (api/resolv.h) rather than calling directly. */
VOID writeHErrnoValue(struct SocketBase *, int);
/* PORT (AmiTCP_NG): SBTC_SIG_ADDRESS_CHANGE_MASK broadcast. */
VOID ng_signal_address_change(void);

/*
 * inline functions which changes (raises) task priority while it is
 * executing library functions
 */

/*
 * PORT (AmiTCP_NG): boost the REAL caller, not libPtr->thisTask.
 *
 * The point of the boost is to stop the net task preempting whoever is inside
 * the stack holding kernel structures. That is the task running right now. With
 * SBTC_CAN_SHARE_LIBRARY_BASES the caller need not be the opener, and boosting
 * the opener instead would leave the actual caller running unprotected while
 * perturbing the priority of a task that is not even in the library. The two
 * are identical on an unshared base, so this is behaviour-neutral there.
 * SysBase->ThisTask is read ONCE and the same task is restored, so an
 * intervening share cannot leave a boost stranded on the wrong task.
 */
static inline void ObtainSyscallSemaphore(struct SocketBase *libPtr)
{
  extern struct Task *AmiTCP_Task;

  ObtainSemaphore(&syscall_semaphore);
  libPtr->callerTask = SysBase->ThisTask;
  libPtr->myPri = SetTaskPri(libPtr->callerTask,
			     libPtr->libCallPri = AmiTCP_Task->tc_Node.ln_Pri);
}

static inline void ReleaseSyscallSemaphore(struct SocketBase *libPtr)
{
  if (libPtr->libCallPri != (libPtr->myPri = SetTaskPri(libPtr->callerTask,
							libPtr->myPri)))
    SetTaskPri(libPtr->callerTask, libPtr->myPri);
  ReleaseSemaphore(&syscall_semaphore);
}

/*
 * inline function for searching library base when taskpointer is known
 */

static inline struct SocketBase *FindSocketBase(struct Task *task)
{
  extern struct List socketBaseList;
  struct Node *libNode;

  Forbid();
  for (libNode = socketBaseList.lh_Head; libNode->ln_Succ;
       libNode = libNode->ln_Succ)
    if (((struct SocketBase *)libNode)->thisTask == task) {
      Permit();
      return (struct SocketBase *)libNode;
    }
  /* here if Task wasn't in socketBaseList */
  Permit();
  return NULL;
}

/*
 * PORT (AmiTCP_NG): like FindSocketBase(), but also finds a base this task is
 * SHARING rather than one it opened.
 *
 * The socket layer uses "no base for this task" to mean "I am the network
 * task", and acts on it: soclose() skips SO_LINGER, and sorflush() refuses to
 * tear a receive buffer down under a sleeping reader. Both are right for the
 * network task and wrong for an application task, and a task sharing a base is
 * an application task -- it is simply not in socketBaseList, because only
 * openers are. Without this it would be mistaken for the network task and
 * silently lose SO_LINGER, or have its close() quietly abandoned.
 *
 * Kept separate from FindSocketBase() rather than folded into it: ELL_Open()
 * asks "has this task already opened a base?" and must NOT be answered with
 * somebody else's base that the task merely shares.
 */
extern struct SocketBase *FindCallerBase(struct Task *task);

#endif /* !AMIGA_API_H */
