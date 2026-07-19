RCS_ID_C="$Id: tcp_debug.c,v 1.10 1993/12/21 22:17:22 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: tcp_debug.c,v $
 * Revision 1.10  1993/12/21  22:17:22  jraja
 * Added one 'const' to avoid warnings...
 *
 * Revision 1.9  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.8  1993/05/17  00:16:44  ppessi
 * Changed RCS version. Added rcsid.
 *
 * Revision 1.7  1993/04/05  19:06:24  jraja
 * Changed storage of the spl functions  return values to type spl_t.
 * Added include for conf.h to every .c file.
 *
 * Revision 1.6  93/03/22  16:59:34  16:59:34  jraja (Jarno Tapio Rajahalme)
 * Changed bcopy()s and bzero()s with word aligned pointers to
 * aligned_b(copy|zero) ar aligned_b(copy|zero)_const. The latter is for calls
 * in which the size is constant.
 * These can be disabled by defining NOALIGN.
 *  Converted bcopys doing structure copies (on aligned pointers) to structure
 * assignments, since at least SASC produces better code with assignment.
 * 
 * Revision 1.5  93/03/13  17:14:23  17:14:23  ppessi (Pekka Pessi)
 * Fixed bugs with variable initialization.
 * 
 * Revision 1.4  93/03/03  21:29:59  21:29:59  jraja (Jarno Tapio Rajahalme)
 * Moved various data definitions from header files to here.
 * 
 * Revision 1.3  93/03/02  19:02:22  19:02:22  too (Tomi Ollila)
 * Changed %? to %l? on format strings
 * 
 * Revision 1.2  93/02/26  09:29:59  09:29:59  jraja (Jarno Tapio Rajahalme)
 * Made this compile with ANSI C (added prototypes).
 * Made one structure copy to explicitly use bcopy to be able to use inline
 * bcopy().
 * Changed old style string concat / ** / to ansi ##.
 * 
 * Revision 1.1  92/11/17  16:30:11  16:30:11  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 *
 */

/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp_debug.c	7.6 (Berkeley) 6/28/90
 */

#ifdef TCPDEBUG
/* load symbolic names */
#define PRUREQUESTS
#define TCPSTATES
#define	TCPTIMERS
#define	TANAMES
#endif

/*
 * tcp_debug.c --- the TCP debug trace ring buffer (SO_DEBUG). Stock 4.4BSD.
 *
 * When a TCP socket has SO_DEBUG set, tcp_trace() records a snapshot at each
 * interesting point (input, output, user request, timer) into a circular buffer:
 * the connection state, sequence numbers, and what happened. A companion tool
 * (trpt) can later dump this to reconstruct exactly how a connection evolved --
 * the period's equivalent of a TCP flight recorder. Purely diagnostic; compiled
 * only when TCPDEBUG is defined. See TCP/IP Illustrated Vol 2 chapter 29.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/errno.h>

#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_debug.h>

#include <netinet/tcp_debug_protos.h>

/* --- start moved from tcp_debug.h --- */
struct	tcp_debug tcp_debug[TCP_NDEBUG] = {0};
int	tcp_debx = 0; 

#ifdef TCPDEBUG
char	*tanames[] =
    { "input", "output", "user", "respond", "drop" };
/* --- end moved from tcp_debug.h --- */
/* --- start moved from sys/protosw.h --- */
char *prurequests[] = {
	"ATTACH",	"DETACH",	"BIND",		"LISTEN",
	"CONNECT",	"ACCEPT",	"DISCONNECT",	"SHUTDOWN",
	"RCVD",		"SEND",		"ABORT",	"CONTROL",
	"SENSE",	"RCVOOB",	"SENDOOB",	"SOCKADDR",
	"PEERADDR",	"CONNECT2",	"FASTTIMO",	"SLOWTIMO",
	"PROTORCV",	"PROTOSEND"
};
/* --- end moved from sys/protosw.h --- */
/* --- start moved from netinet/tcp_timers.h --- */
char *tcptimers[] =
    { "REXMT", "PERSIST", "KEEP", "2MSL" };
/* --- end moved from netinet/tcp_timers.h --- */
/* --- start moved from tcp_fsm.h --- */
char *tcpstates[] = {
	"CLOSED",	"LISTEN",	"SYN_SENT",	"SYN_RCVD",
	"ESTABLISHED",	"CLOSE_WAIT",	"FIN_WAIT_1",	"CLOSING",
	"LAST_ACK",	"FIN_WAIT_2",	"TIME_WAIT",
};
/* --- end moved from tcp_fsm.h --- */
int	tcpconsdebug = 0;
#endif

/*
 * Tcp debug routines
 */
void
tcp_trace(act, ostate, tp, ti, req)
	short act, ostate;
	struct tcpcb *tp;
	struct tcpiphdr *ti;
	int req;
{
	tcp_seq seq, ack;
	int len, flags;
	struct tcp_debug *td = &tcp_debug[tcp_debx++];

	if (tcp_debx == TCP_NDEBUG)
		tcp_debx = 0;
	td->td_time = iptime();
	td->td_act = act;
	td->td_ostate = ostate;
	td->td_tcb = (caddr_t)tp;
	if (tp)
		td->td_cb = *tp;
	else
		aligned_bzero_const((caddr_t)&td->td_cb, sizeof (*tp));
	if (ti)
		td->td_ti = *ti;
	else
		aligned_bzero_const((caddr_t)&td->td_ti, sizeof (*ti));
	td->td_req = req;
#ifdef TCPDEBUG
	if (tcpconsdebug == 0)
		return;
	if (tp)
		printf("%lx %s:", (u_long)tp, tcpstates[ostate]);
	else
		printf("???????? ");
	printf("%s ", tanames[act]);
	switch (act) {

	case TA_INPUT:
	case TA_OUTPUT:
	case TA_DROP:
		if (ti == 0)
			break;
		seq = ti->ti_seq;
		ack = ti->ti_ack;
		len = ti->ti_len;
		if (act == TA_OUTPUT) {
			seq = ntohl(seq);
			ack = ntohl(ack);
			len = ntohs((u_short)len);
		}
		if (act == TA_OUTPUT)
			len -= sizeof (struct tcphdr);
		if (len)
			printf("[%lx..%lx)", seq, seq+len);
		else
			printf("%lx", seq);
		printf("@%lx, urp=%lx", ack, ti->ti_urp);
		flags = ti->ti_flags;
		if (flags) {
#ifndef lint
			const char *cp = "<";
#define pf(f) { if (ti->ti_flags&TH_##f) { printf("%s%s", cp, "f"); cp = ","; } }
			pf(SYN); pf(ACK); pf(FIN); pf(RST); pf(PUSH); pf(URG);
#endif
			printf(">");
		}
		break;

	case TA_USER:
		printf("%s", prurequests[req&0xff]);
		if ((req & 0xff) == PRU_SLOWTIMO)
			printf("<%s>", tcptimers[req>>8]);
		break;
	}
	if (tp)
		printf(" -> %s", tcpstates[tp->t_state]);
	/* print out internal state of tp !?! */
	printf("\n");
	if (tp == 0)
		return;
	printf("\trcv_(nxt,wnd,up) (%lx,%lx,%lx) snd_(una,nxt,max) (%lx,%lx,%lx)\n",
	    tp->rcv_nxt, tp->rcv_wnd, tp->rcv_up, tp->snd_una, tp->snd_nxt,
	    tp->snd_max);
	printf("\tsnd_(wl1,wl2,wnd) (%lx,%lx,%lx)\n",
	    tp->snd_wl1, tp->snd_wl2, tp->snd_wnd);
#endif /* TCPDEBUG */
}
