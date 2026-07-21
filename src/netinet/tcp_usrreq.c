/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: tcp_usrreq.c,v 1.10 1993/06/04 11:16:15 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: tcp_usrreq.c,v $
 * Revision 1.10  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.9  1993/05/17  00:16:44  ppessi
 * Changed RCS version. Added rcsid.
 *
 * Revision 1.8  1993/04/24  17:48:13  jraja
 * Removed MCLBYTES, set default buffer size to 8 kilobytes.
 *
 * Revision 1.7  93/04/19  02:39:50  02:39:50  ppessi (Pekka Pessi)
 * Default socket buffer space made bigger
 * 
 * Revision 1.6  93/04/05  19:06:44  19:06:44  jraja (Jarno Tapio Rajahalme)
 * Changed storage of the spl functions  return values to type spl_t.
 * Added include for conf.h to every .c file.
 * 
 * Revision 1.5  93/03/16  08:28:51  08:28:51  jraja (Jarno Tapio Rajahalme)
 * Made PRU_SENSE (stat) unsupported on AmiTCP
 * 
 * Revision 1.4  93/03/05  21:09:45  21:09:45  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.3  93/03/03  21:28:27  21:28:27  jraja (Jarno Tapio Rajahalme)
 * Moved various data definitions from header files to here.
 * 
 * Revision 1.2  93/02/26  09:59:47  09:59:47  jraja (Jarno Tapio Rajahalme)
 * Made this compile with ANSI C (added prototypes).
 * Changed so_linger to so_linger.tv_sec, since so_linger changed from short
 * 
 * Revision 1.1  92/11/17  16:31:09  16:31:09  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 *
 */

/*
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
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
 *	@(#)tcp_usrreq.c	7.15 (Berkeley) 6/28/90
 */

/*
 * tcp_usrreq.c --- the socket-layer interface to TCP (the PRU_* switch).
 *
 * Stock 4.4BSD. tcp_usrreq() is TCP's pr_usrreq: the socket layer
 * (kern/uipc_socket.c) calls it to service each system call on a TCP socket, and
 * it drives the connection accordingly:
 *   PRU_ATTACH  socket()  -> tcp_newtcpcb
 *   PRU_BIND / PRU_LISTEN bind()/listen()
 *   PRU_CONNECT connect()  -> send a SYN (tcp_output), enter SYN_SENT
 *   PRU_SEND    send()     -> append to the send buffer, tcp_output
 *   PRU_RCVD    recv drained -> a window opened, maybe send a window update
 *   PRU_DISCONNECT close()  -> begin the FIN handshake
 *   PRU_ABORT              -> send RST
 * This is the clean boundary between "generic socket" and "TCP": read it to see
 * exactly which TCP action each socket call triggers. See TCP/IP Illustrated Vol 2
 * chapter 30.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/errno.h>
#include <sys/synch.h>

#include <net/if.h>
#include <net/route.h>

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

#include <netinet/tcp_usrreq_protos.h>
#include <netinet/tcp_output_protos.h>
#include <netinet/tcp_subr_protos.h>
#include <netinet/tcp_timer_protos.h>
#include <netinet/in_protos.h>
#include <netinet/in_pcb_protos.h>
#include <kern/uipc_socket2_protos.h>
#include <netinet/tcp_debug_protos.h>
#include <netinet/ip_output_protos.h>

struct	inpcb tcb = { 0 };		/* head of queue of active tcpcb's */
struct	tcpstat tcpstat = { 0 };	/* tcp statistics */
tcp_seq	tcp_iss = { 0 };		/* tcp initial send seq # */
#ifdef KPROF
int	tcp_acounts[TCP_NSTATES][PRU_NREQ] = { 0 };
#endif
/*
 * Configurable variables
 *
 * Default socket-buffer sizes. These bound single-connection TCP throughput at
 * roughly buffer/RTT, so the old 8 KB default throttled fast links badly (e.g.
 * upstream on a 100 Mbit connection). Sized here for ~100 Mbit over standard
 * 1500-byte Ethernet: 42 * 1460 (MSS = 1500 - 40) = 61320 bytes, MSS-aligned so
 * tcp_mss() does not trim it. This is the largest value that stays under BOTH
 * limits that would otherwise make soreserve() fail and leave a dead socket:
 *   - the u_short TCP window ceiling (snd_wnd/rcv_wnd max 65535), and
 *   - sbreserve()'s mbuf-accounting cap, sb_max*mclbytes/(MSIZE+mclbytes)
 *     = 65536*2048/(128+2048) = 61680 with the default mbuf config.
 * Without RFC 1323 window scaling 64 KB is the hard per-connection wall, so this
 * is the practical maximum; true high-BDP throughput needs scaling (planned).
 * Both remain tunable at runtime via the "tcp.sendspace"/"tcp.recvspace"
 * RoadshowData nodes.
 *
 * NOTE: these buffers are only a ceiling; the memory to fill them comes from the
 * shared mbuf pool, whose default cap (MBUF_CONF MAXMEM, 256 KB) is left
 * unchanged. One bulk connection with both buffers full uses ~120 KB of that
 * pool, so sustained high throughput across several connections needs MAXMEM
 * raised. The pool is allocated on demand, so the default costs light-traffic /
 * low-memory machines nothing. See docs/BUILDING.md "Throughput and memory".
 */
u_long	tcp_sendspace = 42 * 1460;	/* 61320 */
u_long	tcp_recvspace = 42 * 1460;	/* 61320 */

/*
 * TCP protocol interface to socket abstraction.
 */
extern	char *tcpstates[];
struct	tcpcb *tcp_newtcpcb();

/*
 * Process a TCP user request for TCP tb.  If this is a send request
 * then m is the mbuf chain of send data.  If this is a timer expiration
 * (called from the software clock routine), then timertype tells which timer.
 */
int
tcp_usrreq(so, req, m, nam, control)
	struct socket *so;
	int req;
	struct mbuf *m, *nam, *control;
{
	register struct inpcb *inp;
	register struct tcpcb *tp = 0;	/* PRU_ATTACH failure leaves tp unassigned;
					 * the SO_DEBUG tcp_trace() guard below relies on
					 * it being 0, not stack garbage (else *tp copy). */
	spl_t s;
	int error = 0;
	int ostate;

	if (req == PRU_CONTROL)
		return (in_control(so, (int)m, (caddr_t)nam,
			(struct ifnet *)control));
	if (control && control->m_len) {
		m_freem(control);
		if (m)
			m_freem(m);
		return (EINVAL);
	}

	s = splnet();
	inp = sotoinpcb(so);
	/*
	 * When a TCP is attached to a socket, then there will be
	 * a (struct inpcb) pointed at by the socket, and this
	 * structure will point at a subsidary (struct tcpcb).
	 */
	if (inp == 0 && req != PRU_ATTACH) {
		splx(s);
		return (EINVAL);		/* XXX */
	}
	if (inp) {
		tp = intotcpcb(inp);
		/* WHAT IF TP IS 0? */
#ifdef KPROF
		tcp_acounts[tp->t_state][req]++;
#endif
		ostate = tp->t_state;
	} else
		ostate = 0;
	switch (req) {

	/*
	 * TCP attaches to socket via PRU_ATTACH, reserving space,
	 * and an internet control block.
	 */
	case PRU_ATTACH:
		if (inp) {
			error = EISCONN;
			break;
		}
		error = tcp_attach(so);
		if (error)
			break;
		if ((so->so_options & SO_LINGER) && so->so_linger.tv_sec == 0)
			so->so_linger.tv_sec = TCP_LINGERTIME;
		tp = sototcpcb(so);
		break;

	/*
	 * PRU_DETACH detaches the TCP protocol from the socket.
	 * If the protocol state is non-embryonic, then can't
	 * do this directly: have to initiate a PRU_DISCONNECT,
	 * which may finish later; embryonic TCB's can just
	 * be discarded here.
	 */
	case PRU_DETACH:
		if (tp->t_state > TCPS_LISTEN)
			tp = tcp_disconnect(tp);
		else
			tp = tcp_close(tp);
		break;

	/*
	 * Give the socket an address.
	 */
	case PRU_BIND:
		error = in_pcbbind(inp, nam);
		if (error)
			break;
		break;

	/*
	 * Prepare to accept connections.
	 */
	case PRU_LISTEN:
		if (inp->inp_lport == 0)
			error = in_pcbbind(inp, (struct mbuf *)0);
		if (error == 0)
			tp->t_state = TCPS_LISTEN;
		break;

	/*
	 * Initiate connection to peer.
	 * Create a template for use in transmissions on this connection.
	 * Enter SYN_SENT state, and mark socket as connecting.
	 * Start keep-alive timer, and seed output sequence space.
	 * Send initial segment on connection.
	 */
	case PRU_CONNECT:
		if (inp->inp_lport == 0) {
			error = in_pcbbind(inp, (struct mbuf *)0);
			if (error)
				break;
		}
		error = in_pcbconnect(inp, nam);
		if (error)
			break;
		tp->t_template = tcp_template(tp);
		if (tp->t_template == 0) {
			in_pcbdisconnect(inp);
			error = ENOBUFS;
			break;
		}
		soisconnecting(so);
		tcpstat.tcps_connattempt++;
		tp->t_state = TCPS_SYN_SENT;
		tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
		tp->iss = tcp_new_isn(tp);	/* RFC 6528 randomised ISN */
		tcp_iss += TCP_ISSINCR/2;	/* advance the base M */
		tcp_sendseqinit(tp);
		error = tcp_output(tp);
		break;

	/*
	 * Create a TCP connection between two sockets.
	 */
	case PRU_CONNECT2:
		error = EOPNOTSUPP;
		break;

	/*
	 * Initiate disconnect from peer.
	 * If connection never passed embryonic stage, just drop;
	 * else if don't need to let data drain, then can just drop anyways,
	 * else have to begin TCP shutdown process: mark socket disconnecting,
	 * drain unread data, state switch to reflect user close, and
	 * send segment (e.g. FIN) to peer.  Socket will be really disconnected
	 * when peer sends FIN and acks ours.
	 *
	 * SHOULD IMPLEMENT LATER PRU_CONNECT VIA REALLOC TCPCB.
	 */
	case PRU_DISCONNECT:
		tp = tcp_disconnect(tp);
		break;

	/*
	 * Accept a connection.  Essentially all the work is
	 * done at higher levels; just return the address
	 * of the peer, storing through addr.
	 */
	case PRU_ACCEPT: {
		struct sockaddr_in *sin = mtod(nam, struct sockaddr_in *);

		nam->m_len = sizeof (struct sockaddr_in);
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_port = inp->inp_fport;
		sin->sin_addr = inp->inp_faddr;
		break;
		}

	/*
	 * Mark the connection as being incapable of further output.
	 */
	case PRU_SHUTDOWN:
		socantsendmore(so);
		tp = tcp_usrclosed(tp);
		if (tp)
			error = tcp_output(tp);
		break;

	/*
	 * After a receive, possibly send window update to peer.
	 */
	case PRU_RCVD:
		(void) tcp_output(tp);
		break;

	/*
	 * Do a send by putting data in output queue and updating urgent
	 * marker if URG set.  Possibly send more data.
	 */
	case PRU_SEND:
		sbappend(&so->so_snd, m);
		error = tcp_output(tp);
		break;

	/*
	 * Abort the TCP.
	 */
	case PRU_ABORT:
		tp = tcp_drop(tp, ECONNABORTED);
		break;

	case PRU_SENSE:
#ifdef AMITCP
				/* stat not supported in AMITCP */
		error = EOPNOTSUPP;
		break;
#else
		((struct stat *) m)->st_blksize = so->so_snd.sb_hiwat;
		(void) splx(s);
		return (0);
#endif
	case PRU_RCVOOB:
		if ((so->so_oobmark == 0 &&
		    (so->so_state & SS_RCVATMARK) == 0) ||
		    so->so_options & SO_OOBINLINE ||
		    tp->t_oobflags & TCPOOB_HADDATA) {
			error = EINVAL;
			break;
		}
		if ((tp->t_oobflags & TCPOOB_HAVEDATA) == 0) {
			error = EWOULDBLOCK;
			break;
		}
		m->m_len = 1;
		*mtod(m, caddr_t) = tp->t_iobc;
		if (((int)nam & MSG_PEEK) == 0)
			tp->t_oobflags ^= (TCPOOB_HAVEDATA | TCPOOB_HADDATA);
		break;

	case PRU_SENDOOB:
		if (sbspace(&so->so_snd) < -512) {
			m_freem(m);
			error = ENOBUFS;
			break;
		}
		/*
		 * According to RFC961 (Assigned Protocols),
		 * the urgent pointer points to the last octet
		 * of urgent data.  We continue, however,
		 * to consider it to indicate the first octet
		 * of data past the urgent section.
		 * Otherwise, snd_up should be one lower.
		 */
		sbappend(&so->so_snd, m);
		tp->snd_up = tp->snd_una + so->so_snd.sb_cc;
		tp->t_force = 1;
		error = tcp_output(tp);
		tp->t_force = 0;
		break;

	case PRU_SOCKADDR:
		in_setsockaddr(inp, nam);
		break;

	case PRU_PEERADDR:
		in_setpeeraddr(inp, nam);
		break;

	/*
	 * TCP slow timer went off; going through this
	 * routine for tracing's sake.
	 */
	case PRU_SLOWTIMO:
		tp = tcp_timers(tp, (int)nam);
		req |= (int)nam << 8;		/* for debug's sake */
		break;

	default:
		panic("tcp_usrreq");
	}
	if (tp && (so->so_options & SO_DEBUG))
		tcp_trace(TA_USER, ostate, tp, (struct tcpiphdr *)0, req);
	splx(s);
	return (error);
}

int
tcp_ctloutput(op, so, level, optname, mp)
	int op;
	struct socket *so;
	int level, optname;
	struct mbuf **mp;
{
	int error = 0;
	struct inpcb *inp = sotoinpcb(so);
	register struct tcpcb *tp = intotcpcb(inp);
	register struct mbuf *m;

	if (level != IPPROTO_TCP)
		return (ip_ctloutput(op, so, level, optname, mp));

	switch (op) {

	case PRCO_SETOPT:
		m = *mp;
		switch (optname) {

		case TCP_NODELAY:
			if (m == NULL || m->m_len < sizeof (int))
				error = EINVAL;
			else if (*mtod(m, int *))
				tp->t_flags |= TF_NODELAY;
			else
				tp->t_flags &= ~TF_NODELAY;
			break;

		case TCP_MAXSEG:	/* not yet */
		default:
			error = EINVAL;
			break;
		}
		if (m)
			(void) m_free(m);
		break;

	case PRCO_GETOPT:
		*mp = m = m_get(M_WAIT, MT_SOOPTS);
		/* PORT (AmiTCP_NG): pool may be dry -- guard before m_len write
		 * (M_WAIT does not block here). */
		if (m == 0)
			return (ENOBUFS);
		m->m_len = sizeof(int);

		switch (optname) {
		case TCP_NODELAY:
			*mtod(m, int *) = tp->t_flags & TF_NODELAY;
			break;
		case TCP_MAXSEG:
			*mtod(m, int *) = tp->t_maxseg;
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	}
	return (error);
}

/*
 * Attach TCP protocol to socket, allocating
 * internet protocol control block, tcp control block,
 * bufer space, and entering LISTEN state if to accept connections.
 */
int
tcp_attach(so)
	struct socket *so;
{
	register struct tcpcb *tp;
	struct inpcb *inp;
	int error;

	if (so->so_snd.sb_hiwat == 0 || so->so_rcv.sb_hiwat == 0) {
		error = soreserve(so, tcp_sendspace, tcp_recvspace);
		if (error)
			return (error);
	}
	error = in_pcballoc(so, &tcb);
	if (error)
		return (error);
	inp = sotoinpcb(so);
	tp = tcp_newtcpcb(inp);
	if (tp == 0) {
		int nofd = so->so_state & SS_NOFDREF;	/* XXX */

		so->so_state &= ~SS_NOFDREF;	/* don't free the socket yet */
		in_pcbdetach(inp);
		so->so_state |= nofd;
		return (ENOBUFS);
	}
	tp->t_state = TCPS_CLOSED;
	return (0);
}

/*
 * Initiate (or continue) disconnect.
 * If embryonic state, just send reset (once).
 * If in ``let data drain'' option and linger null, just drop.
 * Otherwise (hard), mark socket disconnecting and drop
 * current input data; switch states based on user close, and
 * send segment to peer (with FIN).
 */
struct tcpcb *
tcp_disconnect(tp)
	register struct tcpcb *tp;
{
	struct socket *so = tp->t_inpcb->inp_socket;

	if (tp->t_state < TCPS_ESTABLISHED)
		tp = tcp_close(tp);
	else if ((so->so_options & SO_LINGER) && so->so_linger.tv_sec == 0)
		tp = tcp_drop(tp, 0);
	else {
		soisdisconnecting(so);
		sbflush(&so->so_rcv);
		tp = tcp_usrclosed(tp);
		if (tp)
			(void) tcp_output(tp);
	}
	return (tp);
}

/*
 * User issued close, and wish to trail through shutdown states:
 * if never received SYN, just forget it.  If got a SYN from peer,
 * but haven't sent FIN, then go to FIN_WAIT_1 state to send peer a FIN.
 * If already got a FIN from peer, then almost done; go to LAST_ACK
 * state.  In all other cases, have already sent FIN to peer (e.g.
 * after PRU_SHUTDOWN), and just have to play tedious game waiting
 * for peer to send FIN or not respond to keep-alives, etc.
 * We can let the user exit from the close as soon as the FIN is acked.
 */
struct tcpcb *
tcp_usrclosed(tp)
	register struct tcpcb *tp;
{

	switch (tp->t_state) {

	case TCPS_CLOSED:
	case TCPS_LISTEN:
	case TCPS_SYN_SENT:
		tp->t_state = TCPS_CLOSED;
		tp = tcp_close(tp);
		break;

	case TCPS_SYN_RECEIVED:
	case TCPS_ESTABLISHED:
		tp->t_state = TCPS_FIN_WAIT_1;
		break;

	case TCPS_CLOSE_WAIT:
		tp->t_state = TCPS_LAST_ACK;
		break;
	}
	if (tp && tp->t_state >= TCPS_FIN_WAIT_2)
		soisdisconnected(tp->t_inpcb->inp_socket);
	return (tp);
}
