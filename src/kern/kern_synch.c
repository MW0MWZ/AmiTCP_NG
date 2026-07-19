/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: kern_synch.c,v 1.25 1994/04/02 10:29:54 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * HISTORY
 * $Log: kern_synch.c,v $
 * Revision 1.25  1994/04/02  10:29:54  jraja
 * Changed "wmesg" to const.
 *
 * Revision 1.24  1993/10/07  22:43:02  ppessi
 * Changed AbortIO/Remove to AbortIO/WaitIO
 *
 * Revision 1.23  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.22  1993/06/03  20:16:47  too
 * Changed checking of sigIntrMask so that signals both in wakemask and
 * sigIntrMask does not return EINTR but ERESTART
 *
 * Revision 1.21  93/06/03  00:52:51  00:52:51  jraja (Jarno Tapio Rajahalme)
 * Changed the order in which tsleep_main() checks for events: EINTR is tested
 * first, then ERESTART (user signals), then wakeup and finally time out.
 * 
 * Revision 1.20  1993/05/24  19:30:25  ppessi
 * Changed signal handling mechanism in tsleep_main() and WaitSelect()
 *
 * Revision 1.19  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.18  1993/04/26  11:54:43  too
 * Changed include paths of amiga_api.h, amiga_libcallentry.h and amiga_raf.h
 * from kern to api
 *
 * Revision 1.17  93/04/20  16:08:03  16:08:03  jraja (Jarno Tapio Rajahalme)
 * The timer request is now ensured to be referenced by the p->tsleep_timer
 * only in the send_timeout.
 * 
 * Revision 1.16  93/04/19  02:23:48  02:23:48  ppessi (Pekka Pessi)
 * Fixed various bugs with tsleep_main().
 * Timeouts and SIGINTR mask seem to work.
 * 
 * Revision 1.15  93/04/13  22:30:59  22:30:59  jraja (Jarno Tapio Rajahalme)
 * Added diagnostic to tsleep() to test that the caller has the
 * syscall_semaphore.
 * 
 * Revision 1.14  93/04/12  00:00:53  00:00:53  jraja (Jarno Tapio Rajahalme)
 * Changed wakeup to use same signal with the timer.
 * Added ULONG *sigmp argument to tsleep_main().
 * 
 * Revision 1.13  93/04/06  15:15:52  15:15:52  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.12  93/04/05  14:57:45  14:57:45  jraja (Jarno Tapio Rajahalme)
 * Added more efficient spl functions when not debugging.
 * 
 * Revision 1.11  93/03/19  14:14:49  14:14:49  too (Tomi Ollila)
 * Code changes at night 17-18 March 1993
 * 
 * Revision 1.10  93/03/17  12:07:51  12:07:51  jraja (Jarno Tapio Rajahalme)
 * Added more diagnostic to the tsleep().
 * Moved all the work from tsleep_abort_timeout to tsleep_send_timeout.
 * 
 * Revision 1.9  93/03/16  19:45:22  19:45:22  too (Tomi Ollila)
 * Code kludges...(Added zero timeout -> no timeout)
 * 
 * Revision 1.8  93/03/11  20:49:14  20:49:14  jraja (Jarno Tapio Rajahalme)
 * Changed the splsemaphore to spl_semaphore.
 * 
 * Revision 1.7  93/03/07  00:55:23  00:55:23  jraja (Jarno Tapio Rajahalme)
 * Removed redundant assignment of the Wait() result.
 * 
 * Revision 1.6  93/03/05  03:26:16  03:26:16  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.5  93/02/25  18:39:23  18:39:23  jraja (Jarno Tapio Rajahalme)
 * Moved amiga_includes little upper.
 * 
 * Revision 1.4  93/02/24  12:54:51  12:54:51  jraja (Jarno Tapio Rajahalme)
 * Changed init to remember if initialized.
 * 
 * Revision 1.3  93/02/17  12:54:16  12:54:16  jraja (Jarno Tapio Rajahalme)
 * Broke tsleep apart: tsleep_send_timeout(), tsleep_enter(), tsleep_main()
 * and tsleep_abort_timeout(). See tsleep() for usage.
 * 
 * Revision 1.2  93/02/04  18:28:22  18:28:22  jraja (Jarno Tapio Rajahalme)
 * Added sleep_init(), tsleep() and wakeup().
 * 
 * Revision 1.1  92/12/22  00:08:12  00:08:12  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 */

/*
 * kern_synch.c --- sleep/wakeup, re-implemented on Exec signals.
 *
 * BSD's kernel blocks a process with tsleep(channel, ...) and wakes every process
 * sleeping on a channel with wakeup(channel). The socket code lives on this: a
 * task in soreceive() with no data does tsleep on the socket's receive buffer, and
 * udp_input()/tcp_input() wakeup that buffer when data arrives.
 *
 * There is no kernel scheduler here, so this file emulates tsleep/wakeup using
 * AmigaOS: a sleeping task waits on an Exec signal, and wakeup finds the tasks
 * registered on the channel and Signal()s them. This mapping -- BSD's cooperative
 * sleep onto Exec's signals -- is one of the core adaptations that let a Unix
 * network stack run as an Amiga task. Also provides the spl (splnet/splimp/splx)
 * interrupt-priority emulation used to protect shared lists (via Forbid/Permit).
 * docs/ARCHITECTURE.md section 6.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/synch.h>
#include <sys/queue.h>
#include <sys/syslog.h>

#include <kern/amiga_includes.h>

#include <api/amiga_api.h>

/*
 * Note about spl-functions: this implementation does NOT check for software
 * interrupts when returning to level spl 0 (not needed on AmigaOS, see the 
 * bottom of this file).
 */

/*
 * Sleeping threads are hashed by 'chan' onto sleep queues.
 */
 
#define	SLEEP_QUEUE_SIZE	32	/* power of 2 */
#define	SLEEP_HASH(x)	(((int)(x)>>5) & (SLEEP_QUEUE_SIZE - 1))

queue_head_t	sleep_queue[SLEEP_QUEUE_SIZE];

/*
 * semaphore protecting sleep queues
 */
struct SignalSemaphore sleep_semaphore = { 0 };
static BOOL sleep_initialized = FALSE;

/*
 * Sleep system initialization.
 */
BOOL
sleep_init(void)
{
  register int i;

  if (!sleep_initialized) {
    /*
     * initialize the semaphore protecting sleep queues
     */
    InitSemaphore(&sleep_semaphore);
    
    /*
     * initialize the sleep queues
     */
    for (i = 0; i < SLEEP_QUEUE_SIZE; i++)
      queue_init(&sleep_queue[i]);
    
    sleep_initialized = TRUE;
  }
  return TRUE;
}

void
tsleep_send_timeout(struct SocketBase *p,
		    const struct timeval *time_out)
{

  /*
   * Make sure that the timer message is back from the timer device
   */
  if (p->tsleep_timer->tr_node.io_Message.mn_Node.ln_Type != NT_UNKNOWN) {
    /*
     * PORT (AmiTCP_NG) fix: the previous sleep may have ended via
     * tsleep_abort_timeout(), which sets this port to PA_IGNORE so a late timer
     * reply cannot spuriously wake us. But AbortIO()+WaitIO() just below wait for
     * the aborted request to return TO this port -- and under PA_IGNORE the reply
     * generates no signal, so WaitIO()'s internal Wait() never returns: the socket
     * task hangs forever. Re-arm the port to PA_SIGNAL before we wait on it. (The
     * send branch's later PA_SIGNAL assignment still covers the no-pending-timer
     * case, so this is additive, not a move.)
     */
    p->timerPort->mp_Flags = PA_SIGNAL;
    /*
     * abort previous timeout if it has not been completed yet
     */
    if (p->tsleep_timer->tr_node.io_Message.mn_Node.ln_Type != NT_REPLYMSG) {
      AbortIO((struct IORequest *)(p->tsleep_timer));
    }
    /*
     * Remove timerequest from reply port.
     */
    WaitIO((struct IORequest *)p->tsleep_timer);
    /*
     * Set the node type to NT_UNKNOWN to mark that it is referenced only by
     * the p->tsleep_timer.
     */
    p->tsleep_timer->tr_node.io_Message.mn_Node.ln_Type = NT_UNKNOWN;
#if 0
    /*
     * Make sure the signal gets cleared.
     *
     * if this is not done, the tsleep_main will do one unnecessary round
     * IF the signal was set. IS THIS WORTH OF IT?
     */
    SetSignal(0, 1 << p->timerPort->mp_SigBit);
#endif
  }
  /*
   * send timeout request if necessary
   */
  if (time_out) {
    /*
     * set the timeout
     */
    p->tsleep_timer->tr_time = *time_out;
    /*
     * Enable signalling again
     */
    p->timerPort->mp_Flags = PA_SIGNAL;
    /*
     * send the request to the timer device
     */
    BeginIO((struct IORequest *)(p->tsleep_timer));  
  }
}

void
tsleep_abort_timeout(struct SocketBase *p,
		     const struct timeval *time_out)
{
  if (time_out) {
    /*
     * do not signal us any more
     */
    p->timerPort->mp_Flags = PA_IGNORE;
  }
}

void
tsleep_enter(struct SocketBase *p,
	     caddr_t chan,		/* 'channel' to wait on */
	     const char *wmesg)		/* reason to sleep */
{
  register queue_t	q;
  
  /*
   * Zero is a reserved value, used to indicate
   * that we have been woken up and are no longer on
   * the sleep queues.
   */
  
#if DIAGNOSTIC  
  if (chan == 0)
    panic("tsleep");
#endif
  
  /*
   * The sleep_semaphore protects the sleep queues and
   * p_ fields in SocketBases.
   *
   * When the process is in a sleep queue its p_wchan field is nonzero.
   */
  ObtainSemaphore(&sleep_semaphore);
  p->p_wchan = chan;
  p->p_wmesg = wmesg;
  q = &sleep_queue[SLEEP_HASH(chan)];
  queue_enter(q, p, struct SocketBase *, p_sleep_link);
  ReleaseSemaphore(&sleep_semaphore);
}

int
tsleep_main(struct SocketBase *p, ULONG wakemask)
{
  ULONG sigmask, bmask, timermask;
  struct timerequest *timerReply;
  register queue_t q;
  int result;

  /* 
   * Set the signal mask for the wait
   */
  timermask = 1 << p->timerPort->mp_SigBit;
  sigmask = timermask | p->sigIntrMask | wakemask;

  for (;;) {
    /* 
     * wait for timeout, wakeup or interrupt
     */
    bmask = Wait(sigmask);

    /*
     * Check if we were interrupted
     */
    if (bmask & p->sigIntrMask & ~wakemask) {
      result = EINTR;
      break;
    }

    /*
     * Check for user signals
     */
    if (bmask & wakemask) {
      result = ERESTART;
      break;
    }

    /*
     * check if we were woken up. 
     *
     * If p->p_chan is zero then the wakener has removed us from
     * the sleep queue.
     */
    if (p->p_wchan == 0) {
      /*
       * Set back the signals which interrupted us so that user program can
       * detect them
       */
      bmask &= p->sigIntrMask|wakemask;
      if (bmask)
	SetSignal(bmask, bmask);
      
      return 0;			/* return success */
    }

    /*
     * check if we got the timer reply signal and message
     */
    if (bmask & timermask &&
	(timerReply = (struct timerequest *)GetMsg(p->timerPort)) && 
	timerReply == p->tsleep_timer) { /* sanity check */
      /*
       * timeout expired.
       *
       * Set the node type to NT_UNKNOWN to mark that it is referenced only by
       * the p->tsleep_timer.
       */
      timerReply->tr_node.io_Message.mn_Node.ln_Type = NT_UNKNOWN;

      result = EWOULDBLOCK;
      break;
    }
    
  } /* for */

  /* Return path when sleeper has to be removed from the sleep queue */

  /*
   * Set back the signals which interrupted us so that user program can
   * detect them
   */
  bmask &= p->sigIntrMask | wakemask;
  if (bmask)
    SetSignal(bmask, bmask);

  /*
   * remove from the sleep queue
   */
  ObtainSemaphore(&sleep_semaphore);
  /*
   * If p_chan is nonzero then we still are on the sleep queue and
   * need to be removed from there.
   */
  if (p->p_wchan != 0) {
    q = &sleep_queue[SLEEP_HASH(p->p_wchan)];
    p->p_wchan = (char *)0;
    queue_remove(q, p, struct SocketBase *, p_sleep_link);
  }
  ReleaseSemaphore(&sleep_semaphore);

  return result;
}

/*
 * General sleep call. 
 * NOTE: caller is assumed to hold the syscall_semaphore!         \* XXX *\
 * Suspends current process until a wakeup is made on chan.
 * Sleeps at most the time specified in a time_out (NULL means no timeout).
 * Lowers the current spl-level to 0 while in sleep.
 * Returns 0 if awakened, EWOULDBLOCK if the timeout expires and
 * EINTR if interrupted.
 */
int
tsleep(struct SocketBase *p,  /* Library base through which this call came */
       caddr_t chan,	      /* 'channel' to wait on */
       const char *wmesg,	      /* reason to sleep */
       const struct timeval *time_out) /* timeout as timeval structure */
{
  int result;
  spl_t old_spl;

#if DIAGNOSTIC
  extern struct Task *AmiTCP_Task;
  if (FindTask(NULL) == AmiTCP_Task) {
    log(LOG_ERR, "AmiTCP did tsleep() itself!");
    return (-1);
  }
#endif 
  
#if DIAGNOSTIC
  if (p == NULL) {
    log(LOG_ERR, "tsleep() called with NULL SocketBase pointer!");
    return (-1);
  }
#endif 

#if DIAGNOSTIC
  if (SysBase->ThisTask != syscall_semaphore.ss_Owner) {
    log(LOG_ERR, "tsleep() called with NO syscall_semaphore!");
    return (-1);
  }
#endif
    
  tsleep_send_timeout(p, time_out);
  
  tsleep_enter(p, chan, wmesg);

  /*
   * release spl-level while in sleep.
   * 
   * NOTE: syscall_semaphore must be freed as well!
   */

  old_spl = spl0();
  ReleaseSemaphore(&syscall_semaphore);	                     /* XXX */

  result = tsleep_main(p, 0);

  /*
   * return old spl-level
   */
  ObtainSemaphore(&syscall_semaphore);	                     /* XXX */
  splx(old_spl);

  /*
   * abort the timeout request if necessary
   */
  if (result != EWOULDBLOCK) 
    tsleep_abort_timeout(p, time_out);

  return (result);
}

void
wakeup(caddr_t chan)
{
  register queue_t q;
  struct SocketBase *p, *next;
  
#if DIAGNOSTIC
  if (chan == 0) {
    log(LOG_ERR, "wakeup on chan zero");
    return;
  }
#endif 

  ObtainSemaphore(&sleep_semaphore);
  q = &sleep_queue[SLEEP_HASH(chan)];
  
  p = (struct SocketBase *)queue_first(q);
  while (!queue_end(q, (queue_entry_t)p)) {
    next = (struct SocketBase *)queue_next(&p->p_sleep_link);
    if (p->p_wchan == chan) {
      /*
       * mark sleeper as woken up
       */
      p->p_wchan = NULL;
      /*
       * remove sleeper from the sleep queue
       */
      queue_remove(q, p, struct SocketBase *, p_sleep_link);
      /*
       * signal process to take attention
       */
      Signal(p->thisTask, 1 << p->timerPort->mp_SigBit);
    }
    p = next;
  }
  ReleaseSemaphore(&sleep_semaphore);
}

/*
 * Spl-level emulation:
 *
 * In this implementation the processor priority levels are modelled
 * either with one semaphore (ifdef DEBUG) or by Exec Task switch
 * disabling feature. Semaphore is used while debugging for security (to
 * be able to single step almost everywhere). The production version uses
 * ExecBase's TDNestCnt for speed (prevent unnecessary task switches).
 * 
 * Note that both ways lead to the fact that when someone sets, say, splimp()
 * he will WAIT for the holder of splnet() to finish.
 */

#ifdef DEBUG
/*
 * spl_semaphore is used as mutex for all spl-levels
 *
 * Note that InitSemaphore() requires the signalSemaphore to be initialized
 * to zero (here done statically).
 */
struct SignalSemaphore spl_semaphore = { 0 };

/*
 * spl_level holds the current pseudo priority level.
 * NOTE: this may be accessed only while holding the spl_semaphore.
 */
spl_t spl_level = SPL0;
static BOOL spl_initialized = FALSE;

BOOL
spl_init(void)
{
  if (!spl_initialized) {
    /*
     * Initialize spl_semaphore for use. After this call any number of 
     * tasks may use spl-functions.
     */
    InitSemaphore(&spl_semaphore);
    spl_initialized = TRUE;
  }
  return TRUE;
}

spl_t
spl_n(spl_t new_level)
{
  register spl_t old_level;

  ObtainSemaphore(&spl_semaphore);
  old_level = spl_level;
  spl_level = new_level;
  
  if (new_level > old_level) {	/* raise level */
    if (old_level == 0)		/* lock when raising over zero */
      /* 
       * so. return without releasing the lock
       */
      return old_level;
  }
  else if (new_level < old_level) {	/* lower level */
    if (new_level == 0)			/* unlock when lowering to zero */
      /*
       * now release the lock kept above
       */
      ReleaseSemaphore(&spl_semaphore);
  }
  ReleaseSemaphore(&spl_semaphore);

  return old_level;
}

#else

BOOL
spl_init(void)
{
  return TRUE;
}

/*
 * spl_n is defined as an inline in <sys/synch.h>
 */
#endif /* DEBUG */
