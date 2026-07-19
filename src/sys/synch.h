/*
 * $Id: synch.h,v 1.18 1994/04/02 10:29:54 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: synch.h,v $
 * Revision 1.18  1994/04/02  10:29:54  jraja
 * Changed "wmesg" to const.
 *
 * Revision 1.17  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.16  1993/05/24  19:28:56  ppessi
 * Changed signal handling mechanism in tsleep_main() and WaitSelect()
 *
 * Revision 1.15  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.14  1993/04/19  22:35:05  jraja
 * Fixed return value of splsoftclock(), splnet() and splimp() (with !DEBUG).
 *
 * Revision 1.13  93/04/13  22:01:32  22:01:32  jraja (Jarno Tapio Rajahalme)
 * Added argument name to the spl_n prototype :-)
 * 
 * Revision 1.12  93/04/12  00:01:52  00:01:52  jraja (Jarno Tapio Rajahalme)
 * Added ULONG *sigmp argument to tsleep_main().
 * 
 * Revision 1.11  93/04/05  15:00:50  15:00:50  jraja (Jarno Tapio Rajahalme)
 * Added more efficient spl implementation when not debugging.
 * Added spl return typedef spl_t.
 * 
 * Revision 1.10  93/04/05  09:31:33  09:31:33  jraja (Jarno Tapio Rajahalme)
 * Changed includes to use <sys/time.h> (again) (by ppessi).
 * 
 * Revision 1.9  93/03/04  09:39:30  09:39:30  jraja (Jarno Tapio Rajahalme)
 * Changed _AMIGA_API_H_ to AMIGA_API_H.
 * 
 * Revision 1.8  93/03/03  15:58:46  15:58:46  jraja (Jarno Tapio Rajahalme)
 * Cleanup.
 * 
 * Revision 1.7  93/03/03  12:27:18  12:27:18  jraja (Jarno Tapio Rajahalme)
 * Changed incomplete structure definitions to #includes
 * 
 * Revision 1.6  93/02/24  12:56:39  12:56:39  jraja (Jarno Tapio Rajahalme)
 * Changed sleep_init() and spl_init() to return BOOL.
 * 
 * Revision 1.5  93/02/17  12:55:54  12:55:54  jraja (Jarno Tapio Rajahalme)
 * Added prototypes for tsleep sub-functions.
 * 
 * Revision 1.4  93/02/04  18:19:47  18:19:47  jraja (Jarno Tapio Rajahalme)
 * Added sleep_init(), tsleep() and wakeup() prototypes.
 * Removed extern definitions of splsemaphore and spl_level, because they
 * are needed only internally to kern/kern_synch.c.
 * 
 * Revision 1.3  92/12/22  00:12:16  00:12:16  jraja (Jarno Tapio Rajahalme)
 * spl-definitions (written from scratch, really...)
 * 
 * Revision 1.2  92/11/20  15:57:01  15:57:01  jraja (Jarno Tapio Rajahalme)
 * Added #ifndef AMITCP's to make this compile.
 * 
 * Revision 1.1  92/11/20  15:48:05  15:48:05  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 */

#ifndef SYS_SYNCH_H
#define SYS_SYNCH_H

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifndef SYS_TIME_H
#include <sys/time.h>
#endif

#if !defined(AMIGA_INCLUDES_H) && !defined(DEBUG)
#include <kern/amiga_includes.h> /* for the inline spl_n() */
#endif

extern BOOL sleep_init(void);
extern void tsleep_send_timeout(struct SocketBase *, const struct timeval *);
extern void tsleep_abort_timeout(struct SocketBase *, const struct timeval *);
extern void tsleep_enter(struct SocketBase *, caddr_t, const char *);
extern int  tsleep_main(struct SocketBase *, ULONG blockmask);
extern int  tsleep(struct SocketBase *, caddr_t, const char *,const struct timeval *);
extern void wakeup(caddr_t);

/*
 * Spl-levels used in this implementation
 */
#define SPL0         0
#define SPLSOFTCLOCK 1
#define SPLNET       2
#define SPLIMP       3

/*
 * Spl-function prototypes and definitions.
 *
 * spl_t is the return type of the spl_n(). It should be used when defining
 * storage to store the return value, using int may be little slower :-) 
 */

extern BOOL spl_init(void);

#ifdef DEBUG

typedef int spl_t;

extern spl_t spl_n(spl_t newlevel);

#define spl0()          spl_n(SPL0)
#define splsoftclock()  spl_n(SPLSOFTCLOCK)
#define splnet()        spl_n(SPLNET)
#define splimp()        spl_n(SPLIMP)
#define splx(s)         spl_n(s)

#else

typedef BYTE spl_t;		/* the type of SysBase->TDNestCnt */

extern struct ExecBase * SysBase;

static inline spl_t
#if __SASC
spl_n(int new_level)		/* int because SAS can't inline otherwise :( */
#else
spl_n(spl_t new_level)
#endif
{
  spl_t old_level = SysBase->TDNestCnt;

  if (new_level != SPL0)
    SysBase->TDNestCnt = new_level;
  else {
    SysBase->TDNestCnt = 1;
    Permit();
  }
  return old_level;
}

static inline spl_t
#if __SASC
spl_const(int new_level)	/* int because SAS can't inline otherwise :( */
#else
spl_const(spl_t new_level)
#endif
{
  spl_t old_level = SysBase->TDNestCnt;

  SysBase->TDNestCnt = new_level;

  return old_level;
}

static inline spl_t
spl_0(void)
{
  spl_t oldlevel = SysBase->TDNestCnt;

  if (oldlevel != SPL0) {
    SysBase->TDNestCnt = 1;
    Permit();
  }

  return oldlevel;
}

#define spl0()          spl_0()
#define splsoftclock()  spl_const(SPLSOFTCLOCK)
#define splnet()        spl_const(SPLNET)
#define splimp()        spl_const(SPLIMP)
#define splx(s)         spl_n(s)

#endif /* DEBUG */

#endif /* !SYS_SYNCH_H */
