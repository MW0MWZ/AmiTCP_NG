#ifndef AMIGA_TIME_H
#define AMIGA_TIME_H
/*
 * $Id: amiga_time.h,v 1.13 1993/06/04 11:16:15 jraja Exp $
 * 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * $Log: amiga_time.h,v $
 * Revision 1.13  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.12  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.11  1993/03/13  17:13:02  ppessi
 * Fixed bugs with variable initializations. Works with UDP.
 *
 * Revision 1.10  93/03/10  23:37:28  23:37:28  jraja (Jarno Tapio Rajahalme)
 * Added comments.
 * 
 * Revision 1.9  93/03/10  23:08:17  23:08:17  jraja (Jarno Tapio Rajahalme)
 * Changed timer_init() to return signal mask.
 * 
 * Revision 1.8  93/03/10  22:07:28  22:07:28  jraja (Jarno Tapio Rajahalme)
 * Moved some functions to amiga_time.c.
 * 
 * Revision 1.7  93/03/05  12:31:42  12:31:42  jraja (Jarno Tapio Rajahalme)
 * Changed TimerBase to struct Library also in GCC.
 * 
 * Revision 1.6  93/03/05  03:26:13  03:26:13  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.5  93/03/04  09:43:34  09:43:34  jraja (Jarno Tapio Rajahalme)
 * Fixed includes.
 * 
 * Revision 1.4  93/03/03  15:43:40  15:43:40  jraja (Jarno Tapio Rajahalme)
 * Moved timeval related definitions to sys/time.h.
 * 
 * Revision 1.3  93/02/24  12:54:10  12:54:10  jraja (Jarno Tapio Rajahalme)
 * Changed uxkern to kern.
 * 
 * Revision 1.2  93/02/04  18:59:24  18:59:24  jraja (Jarno Tapio Rajahalme)
 * Added prototypes and inlines for the Timer module (kern/amiga_time.c).
 * 
 * Revision 1.1  93/01/06  19:07:34  19:07:34  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 */

#ifndef SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

#ifndef AMIGA_INCLUDES_H
#include <kern/amiga_includes.h>
#endif

/*
 * Globals defined in amiga_time.c
 */
extern struct Library     *TimerBase;

/*
 * Define an extended timerequest to make implementing the UNIX kernel function
 * timeout() easier.
 */

typedef void (*TimerCallback_t)(void);

struct timeoutRequest {
  struct timerequest timeout_request;	/* timer.device sees only this */
  struct timeval     timeout_timeval;   /* timeout interval */
  TimerCallback_t    timeout_function;  /* timeout function to be called */
};


/*
 * Command field must be TR_ADDREQUEST before this is called!
 * A request may be sent again ONLY AFTER PREVIOUS REQUEST HAS BEEN RETURNED!
 */
static inline void
sendTimeoutRequest(struct timeoutRequest *tr)
{
  tr->timeout_request.tr_time = tr->timeout_timeval;
  SendIO((struct IORequest *)&(tr->timeout_request));
}

/*
 * This MUST be called at splsoftclock()
 */
static inline void
handleTimeoutRequest(struct timeoutRequest *tr)
{
  /*
   * call the function
   */
  (*(tr->timeout_function))();
}

/*
 * prototypes for functions defined in kern/amiga_time.c
 */
ULONG timer_init(void);
void timer_deinit(void);
void timer_send(void);
struct timeoutRequest * createTimeoutRequest(TimerCallback_t fun,
					     ULONG seconds, ULONG micros);
void deleteTimeoutRequest(struct timeoutRequest *tr);
BOOL timer_poll(VOID);

#endif /* AMIGA_TIME_H */
