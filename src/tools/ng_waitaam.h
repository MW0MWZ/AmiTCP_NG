#ifndef NG_WAITAAM_H
#define NG_WAITAAM_H
/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * A BOUNDED wait for an address-allocation reply, shared by the tools that ask the
 * stack to configure an interface (AddNetInterface, ConfigureNetInterface).
 *
 * Both used to do a bare WaitPort() after BeginInterfaceConfig(), trusting the
 * aam_Timeout they had just set to guarantee a reply. It does not: that timeout is
 * honoured by the DHCP helper, and if the helper never gets far enough to honour
 * anything -- because a device command it issued never completed, say -- no reply is
 * ever sent and the command waits for ever. That is exactly what was observed: a
 * hung AddNetInterface with no output at all, and no clue as to why.
 *
 * A tool that hangs is worse than one that fails: a failure can be reported, scripted
 * around, and read in a log.
 *
 * ON TIMEOUT, CALL ng_orphan_port() BEFORE RETURNING. These are short-lived CLI
 * commands: the process exits moments later, and CreateMsgPort() set mp_SigTask to
 * THIS process's Task. When the stack finally replies, ReplyMsg() would Signal()
 * through that pointer -- by then a freed Task structure, quite possibly reused by an
 * unrelated running task. Signalling a recycled Task does not merely set a stale flag;
 * under Disable() it can relink Exec's own queues, so the blast radius is the
 * scheduler. Neutralising the port first means a late reply is simply linked onto a
 * list nobody reads.
 *
 * ON TIMEOUT THE MESSAGE IS STILL THE STACK'S. It was posted, the stack owns it until
 * it replies, and it may reply at any later moment -- so neither the message nor the
 * port it replies to may be freed. Callers must LEAK both on a timeout. That is a
 * bounded leak in an already-broken situation, and the alternative is the stack
 * writing into memory the tool has handed back.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>

/*
 * Wait up to `secs` for a message on `port`.
 * Returns 1 if one arrived (the caller should GetMsg it), 0 on timeout.
 */
static int ng_wait_reply(struct MsgPort *port, long secs)
{
  struct MsgPort     *tport;
  struct timerequest *treq;
  ULONG got, pmask, tmask;

  if (port == NULL)
    return 0;
  if (secs < 1)
    secs = 1;

  tport = CreateMsgPort();
  treq  = tport ? (struct timerequest *)CreateIORequest(tport, sizeof(*treq)) : NULL;
  if (treq != NULL &&
      OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)treq, 0) != 0) {
    DeleteIORequest((struct IORequest *)treq);
    treq = NULL;
  }
  if (treq == NULL) {
    /* No timer available. Fall back to the old unbounded wait rather than refusing
     * to do the operation -- no worse than before this existed. */
    if (tport) DeleteMsgPort(tport);
    WaitPort(port);
    return 1;
  }

  treq->tr_node.io_Command = TR_ADDREQUEST;
  treq->tr_time.tv_secs    = secs;
  treq->tr_time.tv_micro   = 0;
  SendIO((struct IORequest *)treq);

  pmask = 1UL << port->mp_SigBit;
  tmask = 1UL << tport->mp_SigBit;
  got   = Wait(pmask | tmask);

  if (!CheckIO((struct IORequest *)treq))
    AbortIO((struct IORequest *)treq);
  WaitIO((struct IORequest *)treq);
  CloseDevice((struct IORequest *)treq);
  DeleteIORequest((struct IORequest *)treq);
  DeleteMsgPort(tport);

  return (got & pmask) ? 1 : 0;
}

/*
 * Make an abandoned reply port harmless. After this a late ReplyMsg() links the
 * message onto the port's list and signals nobody. The port is deliberately NOT
 * deleted -- the stack may still write into it.
 */
static void ng_orphan_port(struct MsgPort *port)
{
  if (port == NULL)
    return;
  Forbid();
  port->mp_Flags   = PA_IGNORE;
  port->mp_SigTask = NULL;
  Permit();
}

#endif /* NG_WAITAAM_H */
