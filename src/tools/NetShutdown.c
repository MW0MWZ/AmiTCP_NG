/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * NetShutdown -- ask the stack to shut the network down, giving the applications
 * using it a chance to get clear first. Template TIMEOUT/N,QUIET/S (Roadshow's).
 *
 * WHAT THIS USED TO DO, AND WHY IT WAS WRONG. It opened bsdsocket.library, listed the
 * interfaces and removed them, one by one. Nothing was ever told: applications holding
 * the library open got no notification, no break, and no time to react -- they were
 * simply left blocked in calls that would never complete, and hung or reported errors.
 *
 * The shutdown is not this command's to perform. Only the stack knows who still holds
 * bsdsocket.library open, and only the stack can signal those tasks and wait for them.
 * So this asks, by posting a message to the stack's public "TCP/IP Control" port, and
 * waits for the answer -- which is what Roadshow's NetShutdown does, and what any other
 * Roadshow-aware program will do against this stack now that the port exists.
 *
 * IT MUST NOT OPEN bsdsocket.library. That is not a style preference: this command
 * would then be one of the clients the stack is waiting to see go away. Every run would
 * report "still in use" -- counting itself -- on an otherwise idle machine. Roadshow's
 * own client uses nothing but exec.library and dos.library, and neither does this.
 *
 * Waiting is bounded three ways: the reply, TIMEOUT seconds, or the user pressing
 * CTRL-C. On either of the last two the request is RECALLED with NSMC_Cancel, and then
 * both messages are waited for unconditionally -- once a message has been posted, the
 * memory it lives in cannot be reused until the stack has handed it back, and this
 * program's stack frame is that memory.
 */
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <netctl.h>

static void say(int quiet, const char *fmt, LONG a)
{
  if (!quiet) Printf((STRPTR)fmt, a);
}

int main(void)
{
  struct RDArgs *rda;
  LONG   args[2] = { 0, 0 };			/* TIMEOUT/N, QUIET/S */
  /*
   * How long to wait for the stack's answer. Roadshow's default is 5; ours is
   * deliberately more patient.
   *
   * This is NOT how long applications get -- the stack decides that
   * (net.shutdown_grace, default 4, see kern/amiga_netctl.c) and always answers
   * within it. This timeout only matters when the stack does not answer at all, so
   * making it larger costs nothing in every normal run.
   *
   * What it buys: raising net.shutdown_grace above 4 is useless if the asker gives
   * up first -- it would cancel a shutdown that was seconds from succeeding. With a
   * roomier default here, raising the grace Just Works with this command instead of
   * needing TIMEOUT= remembered every time. (Roadshow's own NetShutdown still waits
   * 5, so a grace above 4 will make THAT binary report failure. That is the trade,
   * and it is why the stack's default stays where it is.)
   */
  LONG   timeout = 30;
  int    quiet, rc = RETURN_OK;
  struct MsgPort *reply_port = NULL, *timer_port = NULL;
  struct timerequest *timer_req = NULL;
  struct NetControlPort  *ctl;
  struct NetShutdownMessage msg, cancel;
  ULONG  clients = 0, reply_mask, timer_mask, sigs;
  int    posted = 0, timer_running = 0;

  rda = ReadArgs((STRPTR)"TIMEOUT/N,QUIET/S", args, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)"NetShutdown"); return RETURN_ERROR; }
  if (args[0]) timeout = *(LONG *)args[0];
  if (timeout < 1) timeout = 1;
  quiet = (args[1] != 0);

  reply_port = CreateMsgPort();
  timer_port = CreateMsgPort();
  if (reply_port == NULL || timer_port == NULL) {
    say(quiet, "NetShutdown: out of memory.\n", 0);
    rc = RETURN_FAIL; goto out;
  }
  timer_req = (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_req));
  if (timer_req == NULL ||
      OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)timer_req, 0) != 0) {
    if (timer_req) { DeleteIORequest((struct IORequest *)timer_req); timer_req = NULL; }
    say(quiet, "NetShutdown: could not open timer.device.\n", 0);
    rc = RETURN_FAIL; goto out;
  }

  /* Fill the request in before going anywhere near the port. */
  { UBYTE *p = (UBYTE *)&msg; int i; for (i = 0; i < (int)sizeof(msg); i++) p[i] = 0; }
  msg.nsm_Message.mn_Node.ln_Type = NT_MESSAGE;
  msg.nsm_Message.mn_ReplyPort    = reply_port;
  msg.nsm_Message.mn_Length       = sizeof(msg);
  msg.nsm_Command                 = NSMC_Shutdown;
  msg.nsm_Data                    = (APTR)&clients;   /* tell us who is still there */
  msg.nsm_Length                  = sizeof(clients);

  /*
   * Find the port, check the cookie, and post -- all inside ONE Forbid(). A port found
   * and then posted to outside a Forbid() may have been removed in between, and a port
   * without the cookie is not the one we mean.
   */
  Forbid();
  ctl = (struct NetControlPort *)FindPort((STRPTR)NETWORK_CONTROLLER_PORT_NAME);
  if (ctl != NULL && ctl->ncp_Magic == NCPM_Cookie) {
    PutMsg(&ctl->ncp_Port, (struct Message *)&msg);
    posted = 1;
  }
  Permit();

  if (!posted) {
    /* No controller: nothing is running to shut down. Roadshow says as much and
     * returns a warning rather than an error, and so do we. */
    say(quiet, "NetShutdown: the network is not in use.\n", 0);
    rc = RETURN_WARN; goto out;
  }

  timer_req->tr_node.io_Command = TR_ADDREQUEST;
  timer_req->tr_time.tv_secs    = timeout;
  timer_req->tr_time.tv_micro   = 0;
  SendIO((struct IORequest *)timer_req);
  timer_running = 1;

  say(quiet, "NetShutdown: asking the network to shut down...\n", 0);

  reply_mask = 1UL << reply_port->mp_SigBit;
  timer_mask = 1UL << timer_port->mp_SigBit;

  sigs = Wait(reply_mask | timer_mask | SIGBREAKF_CTRL_C);

  if (!(sigs & reply_mask)) {
    /*
     * We ran out of patience, or the user did. Recall the request. Then wait for BOTH
     * messages without any escape: the stack owns them until it replies, and they live
     * in this function's stack frame.
     */
    { UBYTE *p = (UBYTE *)&cancel; int i; for (i = 0; i < (int)sizeof(cancel); i++) p[i] = 0; }
    cancel.nsm_Message.mn_Node.ln_Type = NT_MESSAGE;
    cancel.nsm_Message.mn_ReplyPort    = reply_port;
    cancel.nsm_Message.mn_Length       = sizeof(cancel);
    cancel.nsm_Command                 = NSMC_Cancel;
    cancel.nsm_Data                    = (APTR)&msg;	/* which request to recall */

    Forbid();
    ctl = (struct NetControlPort *)FindPort((STRPTR)NETWORK_CONTROLLER_PORT_NAME);
    if (ctl != NULL && ctl->ncp_Magic == NCPM_Cookie) {
      PutMsg(&ctl->ncp_Port, (struct Message *)&cancel);
      Permit();
      { int back = 0;
	while (back < 2) { WaitPort(reply_port);
			   while (GetMsg(reply_port) != NULL) back++; } }
    } else {
      /* The controller vanished between posting and recalling. Our original message
       * cannot come back from a port that no longer exists, but it may already be on
       * our reply port; take whatever is there. */
      Permit();
      WaitPort(reply_port);
      while (GetMsg(reply_port) != NULL) ;
    }

    if (sigs & SIGBREAKF_CTRL_C) {
      say(quiet, "NetShutdown: cancelled.\n", 0);
      rc = RETURN_WARN;
    } else {
      say(quiet, "NetShutdown: the network did not shut down in time.\n", 0);
      rc = RETURN_WARN;
    }
    goto out;
  }

  /* The reply came back. */
  while (GetMsg(reply_port) != NULL) ;

  switch (msg.nsm_Error) {
  case NSME_Success:
    if (!quiet) {
      if (clients == 0)
	Printf((STRPTR)"NetShutdown: the network has been shut down.\n");
      else if (clients == 1)
	Printf((STRPTR)"NetShutdown: the network has been shut down "
	       "(1 program was still using it).\n");
      else
	Printf((STRPTR)"NetShutdown: the network has been shut down "
	       "(%ld programs were still using it).\n", (LONG)clients);
    }
    rc = RETURN_OK;
    break;
  case NSME_InUse:
    if (!quiet) {
      if (clients == 1)
	Printf((STRPTR)"NetShutdown: 1 program is still using the network.\n");
      else
	Printf((STRPTR)"NetShutdown: %ld programs are still using the network.\n",
	       (LONG)clients);
    }
    rc = RETURN_WARN;
    break;
  case NSME_Ignored:
    say(quiet, "NetShutdown: the network is already shutting down.\n", 0);
    rc = RETURN_WARN;
    break;
  default:
    say(quiet, "NetShutdown: the request was not carried out.\n", 0);
    rc = RETURN_WARN;
    break;
  }

 out:
  if (timer_running) {
    if (!CheckIO((struct IORequest *)timer_req))
      AbortIO((struct IORequest *)timer_req);
    WaitIO((struct IORequest *)timer_req);
  }
  if (timer_req) {
    CloseDevice((struct IORequest *)timer_req);
    DeleteIORequest((struct IORequest *)timer_req);
  }
  if (timer_port) DeleteMsgPort(timer_port);
  if (reply_port) DeleteMsgPort(reply_port);
  FreeArgs(rda);
  return rc;
}
