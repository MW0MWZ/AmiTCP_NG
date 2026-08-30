/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * amiga_netctl.c -- the "TCP/IP Control" port: letting a program ask the stack to
 * shut down, and giving the applications that are using it a chance to get clear
 * first.
 *
 * THE FAULT THIS EXISTS TO FIX. NetShutdown used to enumerate the interfaces and
 * remove them, and that was all. Applications holding bsdsocket.library open were
 * never told anything: no notification, no break, no grace period. They were simply
 * left blocked in calls that would never complete, and hung or reported errors. The
 * documented behaviour -- Roadshow's, and AmiTCP 4's -- is that those processes are
 * sent a BREAK (SIGBREAKF_CTRL_C) and given a few seconds to close their sockets and
 * their library base before the network goes away.
 *
 * The stack already knew how to do the hard part: api_sendbreaktotasks() (api/
 * amiga_api.c) walks the list of open bases and signals each one's task. What was
 * missing was any way for a COMMAND to ask for it. That is this port.
 *
 * WHY THE GRACE PERIOD IS A STATE MACHINE AND NOT A Delay().
 *
 * This code runs on the stack's own task -- the one whose main loop drains sana_poll()
 * and timer_poll(). Sleeping in here for five or ten seconds would stop the stack
 * dead for that whole time: no packets received, no timers serviced, nothing. That is
 * tolerable in ng_stack_quiesce(), which only runs when the machine is going down
 * anyway, but NOT here, because a shutdown request may be REFUSED and the stack then
 * has to carry on as if nothing happened. So the request is recorded with a deadline,
 * control returns to the main loop, and progress is re-checked on each pass. The stack
 * keeps running normally throughout, and a refused shutdown costs it nothing.
 *
 * The deadline is an absolute time from GetSysTime(), not a count of iterations: there
 * is no free-running tick counter in this stack to count, and the loop's cadence
 * depends on how much traffic there is.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/time.h>

#include <kern/amiga_includes.h>

#include <exec/ports.h>
#include <exec/memory.h>

#include <netctl.h>

/* From api/amiga_api.c -- the machinery that already existed. */
extern VOID  api_sendbreaktotasks(void);
extern VOID  api_hide(VOID);		/* NB: VOID, not BOOL -- see api/amiga_api.h */
extern BOOL  api_show(void);
extern struct Library *MasterSocketBase;

/*
 * How long a client is given to get out of the way.
 *
 * Four seconds, and the number is set by the protocol rather than by taste. The
 * asking end decides how long it is prepared to wait, and Roadshow's NetShutdown --
 * whose argument template we match -- defaults that to FIVE. If the stack took longer
 * than the asker is willing to wait, every default invocation would time out, recall
 * its request, and report failure on a machine where the shutdown was about to
 * succeed. So the stack must reach an answer inside the asker's window, whatever that
 * answer is: either the clients let go, or it reports how many did not.
 *
 * That is at the short end of the "5 or 10 or so" the original report asked for. The
 * limit is the 5-second client default, not this constant -- a caller that passes a
 * larger TIMEOUT is simply more patient than it needs to be, since the stack will
 * already have decided.
 */
#define NG_NETCTL_GRACE_SECS	4	/* the DEFAULT; see ng_netctl_grace_secs */

/*
 * How long clients actually get, as a TUNABLE rather than a constant.
 *
 * Four seconds is not much for an application that has to flush something before it
 * lets go, and the obvious change -- make the constant bigger -- is wrong. The limit
 * is not this number, it is THE CALLER'S PATIENCE: NetShutdown's TIMEOUT defaults to
 * five (Roadshow's default, and ours matches), and a caller that gives up sends
 * NSMC_Cancel and reports failure on a machine where the shutdown was seconds from
 * succeeding. Worse, struct NetShutdownMessage is the Roadshow protocol -- nsm_Data
 * is already the out-pointer for the client count -- so there is nowhere to tell the
 * stack how long the asker is prepared to wait. The stack simply cannot know.
 *
 * So the default stays at four, where ROADSHOW'S OWN NetShutdown still works, and
 * anyone who wants longer can ask for it:
 *
 *     RoadshowControl SET net.shutdown_grace=10
 *
 * ...and must then use a NetShutdown that will wait that long. Ours will (its
 * default timeout is comfortably above this); Roadshow's own binary waits five and
 * will report failure. That trade is the operator's to make, which is exactly why
 * this is a knob and not a new hardcoded number.
 *
 * Clamped at the point of use, not here: ChangeRoadshowData() writes straight
 * through the pointer, so this variable can hold anything an operator types.
 */
int ng_netctl_grace_secs = NG_NETCTL_GRACE_SECS;

/* Sanity bounds for the above. A zero would tear down under applications that were
 * given no chance at all; a huge one would hang a shutdown for minutes with the
 * asker long gone. */
#define NG_NETCTL_GRACE_MIN	1
#define NG_NETCTL_GRACE_MAX	60

static ULONG
ctl_grace_secs(void)
{
  int s = ng_netctl_grace_secs;
  if (s < NG_NETCTL_GRACE_MIN) s = NG_NETCTL_GRACE_MIN;
  if (s > NG_NETCTL_GRACE_MAX) s = NG_NETCTL_GRACE_MAX;
  return (ULONG)s;
}

/* How often, during the grace period, the break is repeated. Sending it once is not
 * enough: a task may only reach a point where it can notice the signal after doing
 * some work, and ng_stack_quiesce() has always asked more than once for that reason. */
#define NG_NETCTL_RENOTIFY_SECS	1

static struct NetControlPort *ctl_port;

/* The one shutdown request in flight, if any, and when we stop waiting for it. */
static struct NetShutdownMessage *ctl_pending;
static ULONG ctl_deadline;		/* absolute seconds */
static ULONG ctl_renotify;		/* absolute seconds; next break to send */
/* Set once the grace period has expired and we are going down regardless. Read by
 * ng_stack_quiesce(), which would otherwise refuse on the clients that are left. */
static int   ctl_forced;

/* Seconds, absolute. */
static ULONG
ctl_now(void)
{
  struct timeval tv;
  GetSysTime(&tv);
  return (ULONG)tv.tv_secs;
}

/*
 * How many clients are still using the network.
 *
 * lib_OpenCnt counts the stack's own base as well, and NETTRACE holds one open for
 * the whole run and is deliberately never broken (api_sendbreaktotasks skips it), so
 * the number a human cares about is one less than the raw count. ng_stack_quiesce()
 * has always reported it that way; reporting it differently here would have
 * NetShutdown contradict the stack's own log message.
 */
static LONG
ctl_clients(void)
{
  LONG n = (LONG)MasterSocketBase->lib_OpenCnt - 1;
  return (n > 0) ? n : 0;
}

/* Reply a message, always. See the header: a dequeued message that is never replied
 * hangs its sender for ever, and that bug has been shipped here once already. */
static void
ctl_reply(struct NetShutdownMessage *m, LONG err)
{
  m->nsm_Error = err;
  ReplyMsg((struct Message *)m);
}

/*
 * The grace period is over and somebody is still holding on. GO ANYWAY.
 *
 * This is a deliberate choice and it is the whole point of the feature. The breaks are
 * a COURTESY -- a few seconds' warning so a well-written program can close its sockets
 * and save its work -- not a veto. An operator who asks for the network to be shut down
 * has asked for the network to be shut down; a single application that ignores the
 * warning, or is wedged and cannot answer it, must not be able to hold the machine's
 * networking hostage indefinitely.
 *
 * The caller is still TOLD how many were still there when we went, through nsm_Data,
 * so a script can report it. The outcome is a success: the thing that was asked for
 * happened.
 */
static void
ctl_proceed_anyway(struct NetShutdownMessage *m)
{
  LONG n = ctl_clients();

  /*
   * nsm_Data is OPTIONAL and points into the CALLER's memory. NULL means "do not
   * want the count", which the specification allows explicitly. The alignment test is
   * cheap insurance: a longword write to an odd address is an Address Error on a
   * 68000, and this pointer came from another program.
   */
  if (m->nsm_Data != NULL && (((ULONG)m->nsm_Data & 1) == 0)) {
    *(ULONG *)m->nsm_Data = (ULONG)n;
    m->nsm_Actual = sizeof(ULONG);
  }
  log(LOG_NOTICE, "network shutting down: %ld client%s did not close in time\n",
      (long)n, (n == 1) ? "" : "s");
  ctl_reply(m, NSME_Success);
}

/*
 * Begin a shutdown request: hide the API so nothing new can attach, break the tasks
 * that are attached, and start the clock. Does NOT decide anything yet.
 */
static void
ctl_begin(struct NetShutdownMessage *m)
{
  ULONG now = ctl_now();

  api_hide();				/* no NEW openers from here on */
  api_sendbreaktotasks();		/* CTRL-C to every task holding a base */

  ULONG grace = ctl_grace_secs();	/* read ONCE: the log must state the number
					 * actually used, not re-read a tunable that
					 * could change between the two */

  ctl_pending  = m;
  ctl_deadline = now + grace;
  ctl_renotify = now + NG_NETCTL_RENOTIFY_SECS;

  log(LOG_NOTICE, "network shutdown requested: %ld client%s notified, "
      "%ld seconds to close\n", (long)ctl_clients(),
      (ctl_clients() == 1) ? "" : "s", (long)grace);
}

/*
 * Called from the stack's main loop. Returns TRUE when the caller should proceed to
 * shut the stack down -- the clients are clear and the requester has been told so.
 */
static int
ctl_progress(void)
{
  ULONG now;

  if (ctl_pending == NULL)
    return 0;

  if (ctl_clients() <= 0) {
    /* Everybody let go. Reply BEFORE any teardown starts: the requester is blocked
     * waiting for this, and ReplyMsg() does not block, so there is no reason to make
     * it wait for the dismantling as well. */
    log(LOG_NOTICE, "network shutting down: all clients closed\n");
    ctl_reply(ctl_pending, NSME_Success);
    ctl_pending = NULL;
    return 1;
  }

  now = ctl_now();

  if ((LONG)(now - ctl_deadline) >= 0) {
    struct NetShutdownMessage *m = ctl_pending;
    ctl_pending = NULL;
    ctl_forced  = 1;			/* quiesce must not refuse on the stragglers */
    ctl_proceed_anyway(m);
    return 1;
  }

  if ((LONG)(now - ctl_renotify) >= 0) {
    api_sendbreaktotasks();		/* ask again -- see NG_NETCTL_RENOTIFY_SECS */
    ctl_renotify = now + NG_NETCTL_RENOTIFY_SECS;
  }
  return 0;
}

/*
 * Handle one message. Every path replies.
 */
static int
ctl_dispatch(struct NetShutdownMessage *m)
{
  switch (m->nsm_Command) {

  case NSMC_Shutdown:
    if (ctl_pending != NULL) {
      /* The protocol has a code for exactly this; do not try to queue two. */
      ctl_reply(m, NSME_Ignored);
      return 0;
    }
    ctl_begin(m);
    return ctl_progress();		/* a machine with no clients stops at once */

  case NSMC_Cancel:
    /* nsm_Data identifies the request being recalled. */
    if (ctl_pending != NULL && m->nsm_Data == (APTR)ctl_pending) {
      struct NetShutdownMessage *p = ctl_pending;
      ctl_pending = NULL;
      ctl_forced  = 0;
      api_show();			/* the shutdown is off; let clients back in */
      log(LOG_NOTICE, "network shutdown cancelled\n");
      ctl_reply(p, NSME_Aborted);	/* the original request */
      ctl_reply(m, NSME_Success);	/* the recall itself */
    } else {
      ctl_reply(m, NSME_NotFound);
    }
    return 0;

  default:
    /*
     * Something we do not understand. It STILL gets replied: this port is public and
     * anything at all can be posted to it, and a sender left un-replied waits for
     * ever. There is no "unknown command" code in the protocol, so the nearest
     * truthful one is that we did not carry it out.
     */
    log(LOG_WARNING, "network control: unknown command %ld ignored\n",
	(long)m->nsm_Command);
    ctl_reply(m, NSME_Aborted);
    return 0;
  }
}

/*
 * Is a shutdown request in flight?
 *
 * ng_stack_quiesce() asks, because api_hide()/api_show() are a plain flag with no
 * owner and no nesting: if an unrelated CTRL-C (the console, or an ARexx KILL) fails
 * to quiesce while a netctl shutdown is still inside its grace period, its api_show()
 * would re-admit new openers on behalf of a request that had never given up the door.
 * Whoever hid it must be the one to show it again.
 */
int
ng_netctl_pending(void)
{
  return (ctl_pending != NULL);
}

/*
 * Has a shutdown request run out of patience? ng_stack_quiesce() asks: its own rule is
 * to refuse while anybody still holds a base, which is right for a stray CTRL-C but
 * wrong for an explicit "shut the network down" that has already given fair warning.
 */
int
ng_netctl_forced(void)
{
  return ctl_forced;
}

/*
 * Create the port. Returns its signal mask for the main loop's Wait(), or 0 on
 * failure -- which is NOT fatal to the stack: without this port the machine simply
 * has no way to be asked to shut down politely, exactly as before this existed.
 */
ULONG
ng_netctl_init(void)
{
  struct NetControlPort *p;

  if (ctl_port != NULL)			/* already up (respawned stack) */
    return 1UL << ctl_port->ncp_Port.mp_SigBit;

  p = AllocVec(sizeof(*p), MEMF_PUBLIC | MEMF_CLEAR);
  if (p == NULL)
    return 0;

  p->ncp_Port.mp_SigBit = AllocSignal(-1);
  if (p->ncp_Port.mp_SigBit == -1) {
    FreeVec(p);
    return 0;
  }
  p->ncp_Port.mp_Node.ln_Type = NT_MSGPORT;
  p->ncp_Port.mp_Node.ln_Pri  = 0;
  p->ncp_Port.mp_Node.ln_Name = (char *)NETWORK_CONTROLLER_PORT_NAME;
  p->ncp_Port.mp_Flags        = PA_SIGNAL;
  p->ncp_Port.mp_SigTask      = FindTask(NULL);
  NewList(&p->ncp_Port.mp_MsgList);

  /*
   * The cookie goes in BEFORE the port is public. Callers are told to check it and
   * refuse to post without it, so a port that is visible for even an instant without
   * one is a port somebody may decline to use -- or worse, trust wrongly.
   */
  p->ncp_Magic = NCPM_Cookie;

  ctl_port = p;
  AddPort(&p->ncp_Port);

  return 1UL << p->ncp_Port.mp_SigBit;
}

/*
 * Drain the port and advance any shutdown in progress. Returns TRUE if the stack
 * should now shut down.
 *
 * Called on every pass of the main loop, not only when the port signalled: a grace
 * period in progress has to be re-examined as time passes, and no message arrives to
 * prompt that.
 */
int
ng_netctl_poll(void)
{
  struct NetShutdownMessage *m;
  int shutdown = 0;

  if (ctl_port == NULL)
    return 0;

  while ((m = (struct NetShutdownMessage *)GetMsg(&ctl_port->ncp_Port)) != NULL)
    if (ctl_dispatch(m))
      shutdown = 1;

  if (ctl_progress())
    shutdown = 1;

  return shutdown;
}

/*
 * Take the port down.
 *
 * ALL OF IT UNDER ONE Forbid(). A caller looks the port up, checks the cookie and
 * posts, all inside its own Forbid(), so doing our side atomically is what makes the
 * two safe against each other. Remove the port first so nothing new can find it, then
 * reply everything already queued -- a message that arrived between a last drain and
 * DeleteMsgPort() would otherwise vanish with the port and hang its sender for ever,
 * which is the same fault as dropping one, reached at teardown instead of in service.
 *
 * Nothing is logged from inside the Forbid(): logging can block.
 */
void
ng_netctl_deinit(void)
{
  struct NetControlPort *p = ctl_port;
  struct NetShutdownMessage *m;

  if (p == NULL)
    return;

  if (ctl_pending != NULL) {
    struct NetShutdownMessage *q = ctl_pending;
    ctl_pending = NULL;
    ctl_reply(q, NSME_Success);		/* it got what it asked for */
  }

  ctl_port = NULL;

  Forbid();
  RemPort(&p->ncp_Port);
  while ((m = (struct NetShutdownMessage *)GetMsg(&p->ncp_Port)) != NULL) {
    m->nsm_Error = NSME_Aborted;
    ReplyMsg((struct Message *)m);
  }
  if (p->ncp_Port.mp_SigBit != -1)
    FreeSignal(p->ncp_Port.mp_SigBit);
  Permit();

  FreeVec(p);
}
