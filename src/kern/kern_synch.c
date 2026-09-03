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

/*
 * PORT (AmiTCP_NG): build a per-call sleep context on the caller's stack.
 *
 * Everything here is done IN THE CALLING TASK, which is the whole point: the
 * signal bit and the port's mp_SigTask must belong to the task that is going to
 * Wait(), or it can never be woken. The timer request borrows the device and
 * unit the base already opened, so there is no OpenDevice() -- see the note in
 * sys/synch.h about Forbid().
 *
 * Returns 0, or ENOMEM if the task has no free signal bit left.
 */
int
ng_sleepctx_init(struct ng_sleepctx *sc, struct SocketBase *p)
{
  sc->sc_task	   = SysBase->ThisTask;
  sc->sc_wchan	   = (caddr_t)0;
  sc->sc_wmesg	   = NULL;
  sc->sc_timerbusy = FALSE;

  sc->sc_sigbit = AllocSignal(-1L);
  if (sc->sc_sigbit == -1)
    return (ENOMEM);

  /* A message port built by hand. CreateMsgPort() would allocate the bit in
   * this task too, but it must then be freed with DeleteMsgPort() by this same
   * task -- doing it ourselves keeps the whole thing on the stack. */
  sc->sc_port.mp_Node.ln_Succ = NULL;	/* never AddPort()ed, but do not leave */
  sc->sc_port.mp_Node.ln_Pred = NULL;	/* stack garbage in list linkage       */
  sc->sc_port.mp_Node.ln_Type = NT_MSGPORT;
  sc->sc_port.mp_Node.ln_Pri  = 0;
  sc->sc_port.mp_Node.ln_Name = NULL;
  sc->sc_port.mp_Flags	      = PA_IGNORE;   /* enabled only around a timeout */
  sc->sc_port.mp_SigBit	      = sc->sc_sigbit;
  sc->sc_port.mp_SigTask      = sc->sc_task;
  NewList(&sc->sc_port.mp_MsgList);

  /* Clone the opened timer. io_Device/io_Unit are what OpenDevice() filled in
   * on the base's request; every other field we set ourselves. */
  aligned_bzero((caddr_t)&sc->sc_timer, sizeof(sc->sc_timer));
  sc->sc_timer.tr_node.io_Message.mn_Node.ln_Type = NT_UNKNOWN;
  sc->sc_timer.tr_node.io_Message.mn_ReplyPort	  = &sc->sc_port;
  sc->sc_timer.tr_node.io_Message.mn_Length	  = sizeof(sc->sc_timer);
  sc->sc_timer.tr_node.io_Device = p->tsleep_timer->tr_node.io_Device;
  sc->sc_timer.tr_node.io_Unit	 = p->tsleep_timer->tr_node.io_Unit;
  sc->sc_timer.tr_node.io_Command = TR_ADDREQUEST;

  return (0);
}

/*
 * Tear the context down before its stack frame dies.
 *
 * Reclaiming the timer request is not optional: a request still queued at
 * timer.device would be written back into a stack frame that no longer exists,
 * corrupting whatever now occupies it -- there is no MMU to catch that.
 */
void
ng_sleepctx_done(struct ng_sleepctx *sc)
{
  if (sc->sc_timerbusy) {
    /* Re-arm signalling: AbortIO()+WaitIO() below wait for the reply to come
     * back to this port, and under PA_IGNORE the reply raises no signal, so
     * WaitIO() would never return. */
    sc->sc_port.mp_Flags = PA_SIGNAL;
    if (sc->sc_timer.tr_node.io_Message.mn_Node.ln_Type != NT_REPLYMSG)
      AbortIO((struct IORequest *)&sc->sc_timer);
    WaitIO((struct IORequest *)&sc->sc_timer);
    sc->sc_timerbusy = FALSE;
  }
  if (sc->sc_sigbit != -1) {
    /* Safe: this runs in sc_task, the task that allocated the bit. */
    FreeSignal((LONG)sc->sc_sigbit);
    sc->sc_sigbit = -1;
  }
}

void
tsleep_send_timeout(struct ng_sleepctx *sc,
		    const struct timeval *time_out)
{

  /*
   * The request is fresh -- it was built by ng_sleepctx_init() a moment ago on
   * this very stack frame -- so there is no leftover from a previous sleep to
   * reap. That whole dance (and the PA_IGNORE/WaitIO deadlock it had to work
   * around) belonged to a request that was reused for the life of the library
   * base; per-call state makes it unnecessary.
   */
  if (time_out) {
    sc->sc_timer.tr_time = *time_out;
    sc->sc_port.mp_Flags = PA_SIGNAL;
    sc->sc_timerbusy	 = TRUE;
    BeginIO((struct IORequest *)&sc->sc_timer);
  }
}

void
tsleep_abort_timeout(struct ng_sleepctx *sc,
		     const struct timeval *time_out)
{
  if (time_out) {
    /*
     * Stop the port signalling, so a timer reply that lands between here and
     * the reap cannot raise a signal nobody is waiting on.
     *
     * Vestigial, strictly: every caller follows this with ng_sleepctx_done(),
     * which re-arms PA_SIGNAL anyway because it has to wait for the aborted
     * request to come back. It mattered when the request lived for the whole
     * life of the base and a late reply could wake the NEXT sleep; a per-call
     * request has no next sleep to disturb. Kept because it costs a store and
     * keeps the abort/reap pair symmetrical, not because anything depends on
     * it.
     */
    sc->sc_port.mp_Flags = PA_IGNORE;
  }
}

void
tsleep_enter(struct ng_sleepctx *sc,
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
   * The sleep_semaphore protects the sleep queues and the sc_ fields of the
   * contexts linked into them.
   *
   * While a context is in a sleep queue its sc_wchan field is nonzero.
   */
  ObtainSemaphore(&sleep_semaphore);
  sc->sc_wchan = chan;
  sc->sc_wmesg = wmesg;
  q = &sleep_queue[SLEEP_HASH(chan)];
  queue_enter(q, sc, struct ng_sleepctx *, sc_link);
  ReleaseSemaphore(&sleep_semaphore);
}

int
tsleep_main(struct ng_sleepctx *sc, struct SocketBase *p, ULONG wakemask)
{
  ULONG sigmask, bmask, timermask;
  struct timerequest *timerReply;
  register queue_t q;
  int result;

  /* 
   * Set the signal mask for the wait
   */
  timermask = 1 << sc->sc_port.mp_SigBit;
  sigmask = timermask | p->sigIntrMask | wakemask;

  for (;;) {
    /*
     * PORT (AmiTCP_NG) lost-wakeup fix: check for an already-delivered wakeup
     * BEFORE sleeping. A wakeup that fired during tsleep()'s
     * tsleep_send_timeout() (after tsleep_enter() registered us) has cleared
     * sc_wchan, but its Signal may have been swallowed by that WaitIO() (shared
     * timerPort bit) -- so Wait()ing here could block forever. Catch it up front.
     * On the first pass no signals have been consumed by this loop, so returning
     * without restoring bmask is correct.
     *
     * Trade-off: if an interrupt signal (sigIntrMask) is ALSO already pending in
     * this same race, this early return reports the wakeup (0) ahead of the
     * interrupt, unlike the in-loop path below which gives EINTR priority. That
     * interrupt is NOT lost -- AmigaOS signals are level-triggered/sticky and we
     * never Wait() here, so the bit stays set and is caught on the task's next
     * blocking call; detection is merely deferred by one syscall. Accepted.
     */
    if (sc->sc_wchan == 0)
      return (0);

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
    if (sc->sc_wchan == 0) {
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
	(timerReply = (struct timerequest *)GetMsg(&sc->sc_port)) &&
	timerReply == &sc->sc_timer) { /* sanity check */
      /*
       * timeout expired.
       *
       * Set the node type to NT_UNKNOWN to mark that it is referenced only by
       * the p->tsleep_timer.
       */
      timerReply->tr_node.io_Message.mn_Node.ln_Type = NT_UNKNOWN;
      sc->sc_timerbusy = FALSE;	/* it came back; nothing left to reap */

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
  if (sc->sc_wchan != 0) {
    q = &sleep_queue[SLEEP_HASH(sc->sc_wchan)];
    sc->sc_wchan = (char *)0;
    queue_remove(q, sc, struct ng_sleepctx *, sc_link);
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
  /*
   * PORT (AmiTCP_NG): the priority-boost bookkeeping (callerTask, myPri,
   * libCallPri) lives on the SocketBase, which was safe only while one task
   * could ever be inside a given base. Releasing syscall_semaphore below lets
   * a SHARER task enter the same base and overwrite all three; if we then
   * restored from the struct after waking we would hand OUR original priority
   * to THAT task -- leaving us permanently boosted and calling SetTaskPri() on
   * a task pointer that may since have exited. Snapshot into locals instead.
   */
  struct Task *boostTask;
  BYTE boostPri, boostCallPri;
  struct ng_sleepctx sc;	/* this call's sleep state, on our own stack */

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
    
  /*
   * PORT (AmiTCP_NG): build this call's sleep context on our own stack.
   *
   * Any task may block here now, including one that merely shares the base:
   * the reply port, its signal bit and the sleep-queue node all belong to
   * whoever is running, so wakeup() can reach them and two tasks cannot
   * collide on one slot. See sys/synch.h for why this cannot instead be
   * per-task state owned by the base.
   */
  if (ng_sleepctx_init(&sc, p) != 0) {
    log(LOG_WARNING, "tsleep: task %s has no free signal bit to block on",
	SysBase->ThisTask->tc_Node.ln_Name ?
	  (STRPTR)SysBase->ThisTask->tc_Node.ln_Name : (STRPTR)"?");
    return (ENOMEM);
  }

  /*
   * PORT (AmiTCP_NG) lost-wakeup fix: register on the sleep queue BEFORE reaping
   * any stale timer. tsleep_send_timeout() can block in WaitIO() (AbortIO+WaitIO
   * of a leftover timer request from a prior sleep that ended non-timeout), and
   * the net task can wakeup() us at any moment WITHOUT holding syscall_semaphore.
   * If we were not yet on the sleep queue during that WaitIO(), a wakeup would be
   * LOST -- wakeup() would find no registered sleeper -- and recv() would hang
   * with data already buffered. Registering first means the wakeup finds us and
   * clears sc_wchan; the top-of-loop check in tsleep_main() then observes it
   * even if the wakeup's Signal was consumed (it shares the port's bit).
   */
  tsleep_enter(&sc, chan, wmesg);

  tsleep_send_timeout(&sc, time_out);

  /*
   * release spl-level while in sleep.
   * 
   * NOTE: syscall_semaphore must be freed as well!
   */

  old_spl = spl0();
  /*
   * Give the priority boost back before we sleep: we are about to stop running,
   * so holding the net task's priority buys nothing, and the struct fields that
   * record it must be free for whoever runs next on this base (see the locals'
   * declaration above).
   */
  boostTask     = p->callerTask;
  boostPri      = p->myPri;
  boostCallPri  = p->libCallPri;
  if (boostTask != NULL)
    SetTaskPri(boostTask, boostPri);
  ReleaseSemaphore(&syscall_semaphore);	                     /* XXX */

  result = tsleep_main(&sc, p, 0);

  /*
   * return old spl-level
   */
  ObtainSemaphore(&syscall_semaphore);	                     /* XXX */
  /*
   * Re-establish the boost from our OWN snapshot, and re-stamp the base so the
   * eventual ReleaseSyscallSemaphore() restores this task and not a sharer that
   * ran while we slept. Safe to write here: holding syscall_semaphore again
   * means no other task is inside the library.
   */
  if (boostTask != NULL) {
    p->callerTask = boostTask;
    p->libCallPri = boostCallPri;
    p->myPri      = SetTaskPri(boostTask, boostCallPri);
  }
  splx(old_spl);

  /*
   * abort the timeout request if necessary
   */
  if (result != EWOULDBLOCK)
    tsleep_abort_timeout(&sc, time_out);

  /* Reclaim the timer and the signal bit BEFORE this frame goes away. */
  ng_sleepctx_done(&sc);

  return (result);
}

/*
 * PORT (AmiTCP_NG): is a task ACTUALLY asleep on this channel right now?
 *
 * The receive-stall detector needs this because SB_WAIT cannot answer it.
 * sowakeup() clears SB_WAIT and THEN calls wakeup(), which returns void -- so a
 * wakeup that reached nobody leaves the flag false with the reader still asleep,
 * and a read that ended by timeout or signal leaves it true with nobody asleep.
 * The sleep queue is the only thing that knows.
 *
 * AttemptSemaphore, never ObtainSemaphore -- and NOT because blocking under
 * Forbid() is unsafe. It is not: Wait() under Forbid() is a documented AmigaOS
 * guarantee, and wakeup() below already takes this same semaphore unconditionally
 * while TCP holds splnet(). The hazard is subtler. A task's Wait() is precisely
 * where it steps outside Forbid()'s protection so another task can run, and the
 * caller here is midway through tcp_slowtimo()'s walk of tcb -- a walk whose only
 * protection against a concurrent soclose() mutating the list IS that splnet()
 * (in_pcb.c documents exactly this). Blocking would reopen the window the outer
 * lock exists to close. Returns -1 for "could not tell"; the caller is on a
 * 500ms timer and simply asks again.
 */
int
ng_sleeper_on(caddr_t chan)
{
  register queue_t q;
  struct ng_sleepctx *sc;
  int found = 0;

  if (chan == 0 || !sleep_initialized)
    return (-1);
  if (!AttemptSemaphore(&sleep_semaphore))
    return (-1);

  q = &sleep_queue[SLEEP_HASH(chan)];
  sc = (struct ng_sleepctx *)queue_first(q);
  while (!queue_end(q, (queue_entry_t)sc)) {
    if (sc->sc_wchan == chan) { found = 1; break; }
    sc = (struct ng_sleepctx *)queue_next(&sc->sc_link);
  }

  ReleaseSemaphore(&sleep_semaphore);
  return (found);
}

void
wakeup(caddr_t chan)
{
  register queue_t q;
  struct ng_sleepctx *sc, *next;
  
#if DIAGNOSTIC
  if (chan == 0) {
    log(LOG_ERR, "wakeup on chan zero");
    return;
  }
#endif 

  ObtainSemaphore(&sleep_semaphore);
  q = &sleep_queue[SLEEP_HASH(chan)];
  
  sc = (struct ng_sleepctx *)queue_first(q);
  while (!queue_end(q, (queue_entry_t)sc)) {
    next = (struct ng_sleepctx *)queue_next(&sc->sc_link);
    if (sc->sc_wchan == chan) {
      /*
       * mark sleeper as woken up
       */
      sc->sc_wchan = NULL;
      /*
       * remove sleeper from the sleep queue
       */
      queue_remove(q, sc, struct ng_sleepctx *, sc_link);
      /*
       * signal process to take attention
       */
      /* Each sleeper owns its port and its signal bit, so this reaches the
       * task that is actually blocked -- not the base's opener. That is what
       * lets a task sharing the base block at all. */
      Signal(sc->sc_task, 1 << sc->sc_port.mp_SigBit);
    }
    sc = next;
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
