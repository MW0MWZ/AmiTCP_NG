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
/* task priority changes (WORDS so we keep structure longword aligned) */  
  BYTE			myPri;        /* task's priority just after libcall */
  BYTE			libCallPri;  /* task's priority during library call */
/* note: not long word aligned at this point */
/* -- descriptor sets -- */
  WORD			dTableSize; /* long word aligned again */
  struct socket	**	dTable;
  fdCallback_t		fdCallback;
/* AmiTCP signal masks */
  ULONG			sigIntrMask;
  ULONG			sigIOMask;
  ULONG			sigUrgMask;
/* -- these are used by tsleep()/wakeup() -- */
  const char *		p_wmesg;
  queue_chain_t 	p_sleep_link;
  caddr_t		p_wchan;               /* event process is awaiting */
  struct timerequest *	tsleep_timer;
  struct MsgPort *	timerPort;
/* -- pointer to select buffer during Select() -- */
  struct newselbuf *	p_sb;
/* -- per process fields used by various 'library' functions -- */
/* buffer for inet_ntoa */
  char			inet_ntoa[20]; /* xxx.xxx.xxx.xxx\0 */
/* -- pointers for data buffers that MAY be used -- */
  struct DataBuffer	selitems;
  struct DataBuffer	hostents;
  struct DataBuffer	netents;
  struct DataBuffer	protoents;
  struct DataBuffer	servents;
/* -- variables for the syslog (see netinclude:sys/syslog.h) -- */
  UBYTE			LogStat;                                  /* status */
  UBYTE			LogMask;                     /* mask for log events */
  UWORD			LogFacility;                       /* facility code */
  const char *		LogTag;	           /* tag string for the log events */
/* -- resolver variables -- */
  LONG *		hErrnoPtr;
  LONG			defHErrno;
  LONG			res_socket;       /* socket used for resolver comm. */
  struct state          res_state;
/* -- PORT (AmiTCP_NG): sequential-read cursors for the get{net,proto,serv}ent()
 * iterators. Each points at the next NDB list node this task will return, or
 * NULL before setXent()/after endXent(). Appended at the end of the struct so
 * existing field offsets are unchanged. -- */
  struct MinNode *	netentCursor;
  struct MinNode *	protoentCursor;
  struct MinNode *	serventCursor;
};

/* 
 * Socket base flags 
 */
#define SBFB_COMPAT43	0L	    /* compat 43 code (without sockaddr_len) */

#define SBFF_COMPAT43   1L

/*
 * macro for getting error value pointed by the library base. All but
 * the lowest byte of the errno are assumed to stay zero. 
 */
#define readErrnoValue(base) ((base)->errnoPtr[(base)->errnoSize - 1])

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

/* Function which set Errno value */

VOID writeErrnoValue(struct SocketBase *, int);

/*
 * inline functions which changes (raises) task priority while it is
 * executing library functions
 */

static inline void ObtainSyscallSemaphore(struct SocketBase *libPtr)
{
  extern struct Task *AmiTCP_Task;

  ObtainSemaphore(&syscall_semaphore);
  libPtr->myPri = SetTaskPri(libPtr->thisTask,
			     libPtr->libCallPri = AmiTCP_Task->tc_Node.ln_Pri);
}

static inline void ReleaseSyscallSemaphore(struct SocketBase *libPtr)
{
  if (libPtr->libCallPri != (libPtr->myPri = SetTaskPri(libPtr->thisTask,
							libPtr->myPri)))
    SetTaskPri(libPtr->thisTask, libPtr->myPri);
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

#endif /* !AMIGA_API_H */
