#ifndef NG_NETCTL_H
#define NG_NETCTL_H
/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * The network controller port -- how a program asks the stack to shut down.
 *
 * This is Roadshow's published protocol, reimplemented. The definitions below match
 * libraries/bsdsocket.h in the Roadshow SDK exactly (names, values, layout), because
 * the whole point is that a Roadshow-aware program works against this stack unchanged:
 * NetShutdown is not the only thing that may ask the network to stop.
 *
 * WHY A PORT, RATHER THAN THE COMMAND DOING IT ITSELF. Shutting the network down is
 * not the caller's job to carry out -- it is the caller's job to ASK. Only the stack
 * knows who still holds bsdsocket.library open, and only the stack can give those
 * programs a break signal and a moment to get out of the way. A command that simply
 * ripped the interfaces out (which is what ours used to do) gives applications no
 * chance at all: they are left blocked in a call that will never complete, and hang or
 * error. So the command posts a message here and waits for an answer.
 *
 * THE CONTRACT, and every part of it matters:
 *
 *  - Look the port up under Forbid(), check ncp_Magic against NCPM_Cookie, and post
 *    the message without leaving that Forbid(). A port without the cookie is not
 *    ours, and a port found and then posted to outside a Forbid() may have been
 *    removed in between.
 *  - EVERY message taken off this port is replied to, whatever it contains, including
 *    a command we do not recognise. A sender has no timeout of its own once it has
 *    committed to waiting, so a message that is dequeued and dropped hangs it for
 *    ever. This project has already shipped that exact bug once, on the ARexx port.
 *  - Exactly one shutdown may be pending at a time. A second one gets NSME_Ignored.
 *  - nsm_Data is OPTIONAL on a shutdown request. When present it points at a ULONG in
 *    the CALLER's memory which receives the number of clients still using the network
 *    AT THE MOMENT THE STACK STOPPED. It may be NULL, and must be checked.
 *
 *    Note this is written on a SUCCESSFUL reply too, and non-zero there is not a
 *    contradiction: the shutdown proceeds once the grace period expires whether or not
 *    every program let go, so "it stopped, and two were still using it" is a perfectly
 *    ordinary outcome. A caller that only inspects this on a failure reply will simply
 *    miss the count; it will not be misled.
 */

/* The name of the public network controller message port. */
#define NETWORK_CONTROLLER_PORT_NAME "TCP/IP Control"

/* The magic cookie stored in ncp_Magic; check it before posting. */
#define NCPM_Cookie	0x20040306

struct NetControlPort
{
	struct MsgPort	ncp_Port;
	ULONG		ncp_Magic;
};

/* Send one of these to the controller port. */
struct NetShutdownMessage
{
	struct Message	nsm_Message;	/* Standard Message header */
	ULONG		nsm_Command;	/* The action to be performed */
	APTR		nsm_Data;	/* Payload */
	ULONG		nsm_Length;	/* Payload size */
	LONG		nsm_Error;	/* Whether or not the command succeeded */
	ULONG		nsm_Actual;	/* How much data was transferred */
};

/* nsm_Command values. */
#define NSMC_Shutdown	1	/* Shut down the network. nsm_Data may point to an
				   ULONG which, if the shutdown does not succeed,
				   receives the number of active clients. */
#define NSMC_Cancel	2	/* Recall a shutdown request. nsm_Data points to the
				   NetShutdownMessage being recalled. */

/* nsm_Error values. */
#define NSME_Success	0	/* Command was processed successfully */
#define NSME_Aborted	1	/* Command was aborted */
#define NSME_InUse	2	/* Network is still running: clients are using it */
#define NSME_Ignored	3	/* Command was ignored (already shutting down) */
#define NSME_NotFound	4	/* The command to be cancelled was not found */

#endif /* NG_NETCTL_H */
