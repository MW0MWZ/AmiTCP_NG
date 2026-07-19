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
#define NG_ENSURE_STACK()						\
  { extern volatile BOOL ng_stack_running;				\
    extern BOOL ng_stack_ensure_running(void);				\
    if (!ng_stack_running) (void)ng_stack_ensure_running(); }

#define CHECK_TASK()					\
  NG_ENSURE_STACK();					\
  if (libPtr->thisTask != SysBase->ThisTask) {		\
    struct Task * wTask = SysBase->ThisTask;		\
    log(LOG_CRIT, wrongTaskErrorFmt, wTask,		\
	wTask->tc_Node.ln_Name,	libPtr->thisTask,	\
	libPtr->thisTask->tc_Node.ln_Name);		\
    return -1;						\
  }      

#define CHECK_TASK_NULL()				\
  NG_ENSURE_STACK();					\
  if (libPtr->thisTask != SysBase->ThisTask) {		\
    struct Task * wTask = SysBase->ThisTask;		\
    log(LOG_CRIT, wrongTaskErrorFmt, wTask,		\
	wTask->tc_Node.ln_Name,	libPtr->thisTask,	\
	libPtr->thisTask->tc_Node.ln_Name);		\
    return NULL;					\
  }      

#define CHECK_TASK2() CHECK_TASK_NULL()

#define CHECK_TASK_VOID()				\
  NG_ENSURE_STACK();					\
  if (libPtr->thisTask != SysBase->ThisTask) {		\
    struct Task * wTask = SysBase->ThisTask;		\
    log(LOG_CRIT, wrongTaskErrorFmt, wTask,		\
	wTask->tc_Node.ln_Name,	libPtr->thisTask,	\
	libPtr->thisTask->tc_Node.ln_Name);		\
    return;						\
  }      

#define API_STD_RETURN(error, ret)	\
  if (error == 0)			\
    return ret;				\
  writeErrnoValue(libPtr, error);	\
  return -1;
						
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
