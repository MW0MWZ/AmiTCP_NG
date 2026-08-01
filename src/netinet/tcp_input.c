/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: tcp_input.c,v 3.1 1994/03/26 09:53:54 too Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: tcp_input.c,v $
 * Revision 3.1  1994/03/26  09:53:54  too
 * Added a call to `controlaccess()' before accepting incoming connection.
 *
 * Revision 1.12  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.11  1993/05/17  00:16:44  ppessi
 * Changed RCS version. Added rcsid.
 *
 * Revision 1.10  1993/04/24  17:45:38  jraja
 * Changed MCLBYTES to mbconf.mclbytes. Removed compile time check for MCLBYTES
 * since it is not worth to be done at run time.
 *
 * Revision 1.9  93/04/13  22:14:29  22:14:29  jraja (Jarno Tapio Rajahalme)
 * Removed casts added earlier, because SAS did not like them :-(
 * 
 * Revision 1.8  93/04/11  22:26:51  22:26:51  jraja (Jarno Tapio Rajahalme)
 * Added STKARGFUN to protocol input & output functions (if used in protosw).
 * 
 * Revision 1.7  93/04/05  19:06:27  19:06:27  jraja (Jarno Tapio Rajahalme)
 * Changed storage of the spl functions  return values to type spl_t.
 * Added include for conf.h to every .c file.
 * 
 * Revision 1.6  93/03/22  16:59:36  16:59:36  jraja (Jarno Tapio Rajahalme)
 * Changed bcopy()s and bzero()s with word aligned pointers to
 * aligned_b(copy|zero) ar aligned_b(copy|zero)_const. The latter is for calls
 * in which the size is constant.
 * These can be disabled by defining NOALIGN.
 *  Converted bcopys doing structure copies (on aligned pointers) to structure
 * assignments, since at least SASC produces better code with assignment.
 * 
 * Revision 1.5  93/03/13  17:14:24  17:14:24  ppessi (Pekka Pessi)
 * Fixed bugs with variable initialization.
 * 
 * Revision 1.4  93/03/05  21:09:39  21:09:39  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.3  93/03/05  03:20:15  03:20:15  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.2  93/02/26  09:43:56  09:43:56  jraja (Jarno Tapio Rajahalme)
 * Made this compile with ANSI C (added prototypes).
 * Added initialization of ostate in function tcp_input() because it might have
 * been used without initialization.
 * Casted  ti->ti_len to u_short in comparison with tp->rcv_wnd.
 * Added explicit ()' around && in || in function tcp_input().
 * 
 * Revision 1.1  92/11/17  16:30:32  16:30:32  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 *
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1990 Regents of the University of California.
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
 *	@(#)tcp_input.c	7.25 (Berkeley) 6/30/90
 */

/*
 * tcp_input.c --- TCP receive processing and the state machine (RFC 793).
 *
 * Stock 4.4BSD, and the single largest, hardest file in the stack -- this is where
 * TCP earns its reputation. tcp_input() is called for every arriving TCP segment
 * and does essentially everything reactive in TCP:
 *   - find the connection's control block (tcpcb via the inpcb, in_pcb.c);
 *   - the header-prediction fast path for the common in-order data/ACK case;
 *   - the full state machine (LISTEN -> SYN_RCVD -> ESTABLISHED -> ... -> TIME_WAIT):
 *     SYN handling and connection setup, sequence-number and window validation,
 *     trimming data to the receive window;
 *   - ACK processing: freeing acknowledged send data, round-trip-time estimation,
 *     and the congestion-control response (slow start / congestion avoidance,
 *     fast retransmit / fast recovery);
 *   - queueing in-order data to the socket (waking readers) and reassembling
 *     out-of-order segments;
 *   - deciding when to send an ACK/response (which tcp_output does).
 * Do not try to absorb it in one pass. Read it alongside TCP/IP Illustrated Vol 2
 * chapters 27-30, which annotate this exact code section by section. The timers it
 * arms (retransmit, keepalive, 2MSL) fire from tcp_timer.c via amiga_time.c.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>

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

#include <netinet/tcp_input_protos.h>
#include <netinet/tcp_output_protos.h>
#include <netinet/tcp_subr_protos.h>
#include <netinet/tcp_timer_protos.h>
#include <netinet/tcp_debug_protos.h>
#include <netinet/ip_input_protos.h>
#include <netinet/in_pcb_protos.h>
#include <netinet/in_protos.h>
#include <netinet/in_cksum_protos.h>
#include <kern/uipc_socket_protos.h>
#include <kern/uipc_socket2_protos.h>
#include <kern/accesscontrol.h>

int	tcprexmtthresh = 3;
int	tcp_do_rfc3042 = 1;	/* RFC 3042 Limited Transmit on the 1st/2nd dup ack (0 disables) */
int	tcppredack = 0;	/* XXX debugging: times hdr predict ok for acks */
int	tcppreddat = 0;	/* XXX # times header prediction ok for data packets */
int	tcppcbcachemiss = 0;
u_long	tcp_now;		/* RFC 1323 timestamp clock; ticks at PR_SLOWHZ (2 Hz) */
/* RFC 5961: max challenge ACKs emitted per tcp_now window (~500 ms). Generous
 * for legitimate use (challenges are rare) while bounding a blind-RST/SYN
 * flood's cost on the net task. */
#define TCP_CHALLENGE_ACK_LIMIT 10
struct	tcpiphdr tcp_saveti = {0};
struct	inpcb *tcp_last_inpcb = &tcb;

struct	tcpcb *tcp_newtcpcb();

/*
 * Insert segment ti into reassembly queue of tcp with
 * control block tp.  Return TH_FIN if reassembly now includes
 * a segment with FIN.  The macro form does the common case inline
 * (segment is the next to be received on an established connection,
 * and the queue is empty), avoiding linkage into and removal
 * from the queue and repetition of various conversions.
 * Set DELACK for segments received in order, but ack immediately
 * when segments are out of order (so fast retransmit can work).
 */

/*
 * Note that we need to ACK in-order data. Delayed-ACK with the classic
 * "every other segment" rule: delay a lone in-order segment (to coalesce ACKs and
 * piggyback -- each ACK costs a header build + checksum + SANA transmit, so halving
 * the ACK count matters on a 68k), BUT if an ACK is already pending (this is the 2nd
 * segment) send it immediately instead of waiting for the ~40 ms delayed-ACK timer.
 * Without this, on a fast link the sender fills its window (up to 256 KB) in a couple
 * of ms and then stalls up to 40 ms per window waiting for our ACK. TF_DELACK itself
 * is the "one segment already pending" marker; tcp_output() clears both flags on send.
 */
#define	TCP_SETDELACK(tp) do { \
	if ((tp)->t_flags & TF_DELACK) \
		(tp)->t_flags = ((tp)->t_flags & ~TF_DELACK) | TF_ACKNOW; \
	else \
		(tp)->t_flags |= TF_DELACK; \
} while (0)

#define	TCP_REASS(tp, ti, m, so, flags) { \
	if ((ti)->ti_seq == (tp)->rcv_nxt && \
	    (tp)->seg_next == (struct tcpiphdr *)(tp) && \
	    (tp)->t_state == TCPS_ESTABLISHED) { \
		TCP_SETDELACK(tp); \
		(tp)->rcv_nxt += (ti)->ti_len; \
		flags = (ti)->ti_flags & TH_FIN; \
		tcpstat.tcps_rcvpack++;\
		tcpstat.tcps_rcvbyte += (ti)->ti_len;\
		sbappend(&(so)->so_rcv, (m)); \
		sorwakeup(so); \
	} else { \
		(flags) = tcp_reass((tp), (ti), (m)); \
		tp->t_flags |= TF_ACKNOW; \
	} \
}

int
tcp_reass(tp, ti, m)
	register struct tcpcb *tp;
	register struct tcpiphdr *ti;
	struct mbuf *m;
{
	register struct tcpiphdr *q;
	struct socket *so = tp->t_inpcb->inp_socket;
	int flags;

	/*
	 * Call with ti==0 after become established to
	 * force pre-ESTABLISHED data up to user socket.
	 */
	if (ti == 0)
		goto present;

	/*
	 * PORT (AmiTCP_NG) hardening: bound the out-of-order reassembly queue.
	 * Only the in-order path feeds sbappend()/sb_hiwat flow control; segments
	 * parked in this out-of-order queue are NOT counted against the socket
	 * buffer, so a peer that streams small, deliberately-gapped segments (never
	 * sending the byte that would make them contiguous) grows this list without
	 * limit, straight out of the shared FIXED mbuf pool. Bound the queued
	 * bytes to the receive buffer's REMAINING headroom (sb_hiwat - sb_cc):
	 * already-delivered-but-unread in-order bytes (sb_cc) and this
	 * out-of-order queue draw on the same fixed pool, so a slow reader that
	 * pins sb_cc near sb_hiwat must not additionally be granted a second
	 * ~sb_hiwat of out-of-order data (~2x the intended per-socket ceiling).
	 * A well-behaved sender never reaches this; a dropped segment is simply
	 * retransmitted by the peer (ordinary loss recovery), so no data is lost
	 * and no protocol rule broken. Done here, before any queue mutation, so
	 * a rejected segment leaves the existing queue untouched.
	 */
	{
		register struct tcpiphdr *p;
		long reasslen = 0;
		long room = (long)so->so_rcv.sb_hiwat - (long)so->so_rcv.sb_cc;

		if (room < 0)
			room = 0;
		for (p = tp->seg_next; p != (struct tcpiphdr *)tp;
		     p = (struct tcpiphdr *)p->ti_next)
			reasslen += (u_short)p->ti_len;
		if (reasslen + (long)(u_short)ti->ti_len > room) {
			m_freem(m);
			return (0);
		}
	}

	/*
	 * Find a segment which begins after this one does.
	 */
	for (q = tp->seg_next; q != (struct tcpiphdr *)tp;
	    q = (struct tcpiphdr *)q->ti_next)
		if (SEQ_GT(q->ti_seq, ti->ti_seq))
			break;

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us.
	 */
	if ((struct tcpiphdr *)q->ti_prev != (struct tcpiphdr *)tp) {
		register int i;
		q = (struct tcpiphdr *)q->ti_prev;
		/* conversion to int (in i) handles seq wraparound */
		i = q->ti_seq + q->ti_len - ti->ti_seq;
		if (i > 0) {
			if (i >= ti->ti_len) {
				tcpstat.tcps_rcvduppack++;
				tcpstat.tcps_rcvdupbyte += ti->ti_len;
				m_freem(m);
				return (0);
			}
			m_adj(m, i);
			ti->ti_len -= i;
			ti->ti_seq += i;
		}
		q = (struct tcpiphdr *)(q->ti_next);
	}
	tcpstat.tcps_rcvoopack++;
	tcpstat.tcps_rcvoobyte += ti->ti_len;
	REASS_MBUF(ti) = m;		/* XXX */

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	while (q != (struct tcpiphdr *)tp) {
		register int i = (ti->ti_seq + ti->ti_len) - q->ti_seq;
		if (i <= 0)
			break;
		if (i < q->ti_len) {
			q->ti_seq += i;
			q->ti_len -= i;
			m_adj(REASS_MBUF(q), i);
			break;
		}
		q = (struct tcpiphdr *)q->ti_next;
		m = REASS_MBUF((struct tcpiphdr *)q->ti_prev);
		remque(q->ti_prev);
		m_freem(m);
	}

	/*
	 * Stick new segment in its place.
	 */
	insque(ti, q->ti_prev);
	/*
	 * RFC 2018: record the just-queued out-of-order range so our ACKs can
	 * SACK it. Done here (after insque, so the range is final and actually
	 * queued) -- never for a segment the bound check above dropped. (If this
	 * segment happens to fill the front of the gap, present: below delivers it
	 * and tcp_sack_purge() removes the block again in this same call -- a few
	 * wasted cycles, externally correct.)
	 */
	if (tp->t_flags & TF_SACK_PERMIT)
		tcp_sack_addblock(tp, ti->ti_seq, ti->ti_seq + ti->ti_len);

present:
	/*
	 * Present data to user, advancing rcv_nxt through
	 * completed sequence space.
	 */
	if (TCPS_HAVERCVDSYN(tp->t_state) == 0)
		return (0);
	ti = tp->seg_next;
	if (ti == (struct tcpiphdr *)tp || ti->ti_seq != tp->rcv_nxt)
		return (0);
	if (tp->t_state == TCPS_SYN_RECEIVED && ti->ti_len)
		return (0);
	do {
		tp->rcv_nxt += ti->ti_len;
		flags = ti->ti_flags & TH_FIN;
		remque(ti);
		m = REASS_MBUF(ti);
		ti = (struct tcpiphdr *)ti->ti_next;
		if (so->so_state & SS_CANTRCVMORE)
			m_freem(m);
		else
			sbappend(&so->so_rcv, m);
	} while (ti != (struct tcpiphdr *)tp && ti->ti_seq == tp->rcv_nxt);
	/* rcv_nxt advanced through delivered data -- drop the SACK blocks it covered. */
	if (tp->t_flags & TF_SACK_PERMIT)
		tcp_sack_purge(tp);
	sorwakeup(so);
	return (flags);
}

/*
 * TCP input routine, follows pages 65-76 of the
 * protocol specification dated September, 1981 very closely.
 */
void STKARGFUN
tcp_input(m, iphlen)
	register struct mbuf *m;
	int iphlen;
{
	register struct tcpiphdr *ti;
	register struct inpcb *inp;
	struct mbuf *om = 0;
	int len, tlen, off;
	register struct tcpcb *tp = 0;
	register int tiflags;
	struct socket *so;
	int todrop, acked, ourfinisacked, needoutput = 0;
	int partialack = 0;		/* NewReno (RFC 6582) partial-ack in non-SACK recovery */
	u_long prr_prev_sacked = 0;	/* RFC 6937 PRR: SACKed octets before this ack's update */
	short ostate = 0;
	struct in_addr laddr;
	tcp_seq rst_seq = 0;	/* as-received seq, snapshotted before the duplicate-trim
				 * block rewrites ti_seq -- the RFC 5961 RST exactness test
				 * must see the ORIGINAL sequence number, not a laundered one */
	struct sackblk sackin[TCP_MAX_SACK];	/* inbound SACK blocks from tcp_dooptions */
	int sackin_n = 0;			/* how many (0 unless SACK negotiated) */
	int dropsocket = 0;
	int iss = 0;
	u_long tiwin = 0;		/* peer's window, RFC 1323-scaled (ti_win << snd_scale) */
	int    ts_present = 0;		/* RFC 1323: this segment carried a timestamp option */
	u_long ts_val = 0, ts_ecr = 0;	/* the peer's TSval and our echoed TSecr */

	tcpstat.tcps_rcvtotal++;
	/*
	 * Get IP and TCP header together in first mbuf.
	 * Note: IP leaves IP header in first mbuf.
	 */
	ti = mtod(m, struct tcpiphdr *);
	if (iphlen > sizeof (struct ip))
		ip_stripoptions(m, (struct mbuf *)0);
	if (m->m_len < sizeof (struct tcpiphdr)) {
		if ((m = m_pullup(m, sizeof (struct tcpiphdr))) == 0) {
			tcpstat.tcps_rcvshort++;
			return;
		}
		ti = mtod(m, struct tcpiphdr *);
	}

	/*
	 * Checksum extended TCP header and data.
	 */
	tlen = ((struct ip *)ti)->ip_len;
	len = sizeof (struct ip) + tlen;
	ti->ti_next = ti->ti_prev = 0;
	ti->ti_x1 = 0;
	ti->ti_len = (u_short)tlen;
	(void)HTONS(ti->ti_len);
	if ((ti->ti_sum = in_cksum(m, len))) {
		tcpstat.tcps_rcvbadsum++;
		goto drop;
	}

	/*
	 * Check that TCP offset makes sense,
	 * pull out TCP options and adjust length.		XXX
	 */
	off = ti->ti_off << 2;
	if (off < sizeof (struct tcphdr) || off > tlen) {
		tcpstat.tcps_rcvbadoff++;
		goto drop;
	}
	tlen -= off;
	/*
	 * PORT (AmiTCP_NG) security fix: ti_len is a signed 16-bit field in the
	 * tcpiphdr overlay, but nearly every use below compares it as a signed int
	 * (window trim at ~line 702, reassembly overlap math, todrop). A segment
	 * whose data length exceeds 32767 would store negative here, flip those
	 * comparisons, skip receive-window enforcement and hand an oversized segment
	 * to the socket buffer -- corrupting its byte accounting. No legal segment on
	 * any real link is this large (MTU), so drop it rather than mis-handle it.
	 */
	if (tlen > 32767) {
		tcpstat.tcps_rcvbadoff++;
		goto drop;
	}
	ti->ti_len = tlen;
	if (off > sizeof (struct tcphdr)) {
		if (m->m_len < sizeof(struct ip) + off) {
			if ((m = m_pullup(m, sizeof (struct ip) + off)) == 0) {
				tcpstat.tcps_rcvshort++;
				return;
			}
			ti = mtod(m, struct tcpiphdr *);
		}
		om = m_get(M_DONTWAIT, MT_DATA);
		if (om == 0)
			goto drop;
		om->m_len = off - sizeof (struct tcphdr);
		{ caddr_t op = mtod(m, caddr_t) + sizeof (struct tcpiphdr);
		  aligned_bcopy(op, mtod(om, caddr_t), (unsigned)om->m_len);
		  m->m_len -= om->m_len;
		  m->m_pkthdr.len -= om->m_len;
		  aligned_bcopy(op+om->m_len, op,
		   (unsigned)(m->m_len-sizeof (struct tcpiphdr)));
		}
	}
	tiflags = ti->ti_flags;

	/*
	 * Convert TCP protocol specific fields to host format.
	 */
	(void)NTOHL(ti->ti_seq);
	(void)NTOHL(ti->ti_ack);
	(void)NTOHS(ti->ti_win);
	(void)NTOHS(ti->ti_urp);

	/*
	 * Locate pcb for segment.
	 */
findpcb:
	inp = tcp_last_inpcb;
	if (inp->inp_lport != ti->ti_dport ||
	    inp->inp_fport != ti->ti_sport ||
	    inp->inp_faddr.s_addr != ti->ti_src.s_addr ||
	    inp->inp_laddr.s_addr != ti->ti_dst.s_addr) {
		inp = in_pcblookup(&tcb, ti->ti_src, ti->ti_sport,
		    ti->ti_dst, ti->ti_dport, INPLOOKUP_WILDCARD);
		if (inp)
			tcp_last_inpcb = inp;
		++tcppcbcachemiss;
	}

	/*
	 * If the state is CLOSED (i.e., TCB does not exist) then
	 * all data in the incoming segment is discarded.
	 * If the TCB exists but is in CLOSED state, it is embryonic,
	 * but should either do a listen or a connect soon.
	 */
	if (inp == 0)
		goto dropwithreset;
	tp = intotcpcb(inp);
	if (tp == 0)
		goto dropwithreset;
	if (tp->t_state == TCPS_CLOSED)
		goto drop;
	so = inp->inp_socket;
	if (so->so_options & (SO_DEBUG|SO_ACCEPTCONN)) {
		if (so->so_options & SO_DEBUG) {
			ostate = tp->t_state;
			tcp_saveti = *ti;
		}
		if (so->so_options & SO_ACCEPTCONN) {
#ifdef AMITCP
		  /*
		   * call to access control.. (AmiTCP/IP extra)
		   */
		  if (controlaccess(ti->ti_src, ti->ti_dport) == 0)
		    goto dropwithreset;
#endif		  
			so = sonewconn(so, 0);
			if (so == 0)
				goto drop;
			/*
			 * This is ugly, but ....
			 *
			 * Mark socket as temporary until we're
			 * committed to keeping it.  The code at
			 * ``drop'' and ``dropwithreset'' check the
			 * flag dropsocket to see if the temporary
			 * socket created here should be discarded.
			 * We mark the socket as discardable until
			 * we're committed to it below in TCPS_LISTEN.
			 */
			dropsocket++;
			inp = (struct inpcb *)so->so_pcb;
			inp->inp_laddr = ti->ti_dst;
			inp->inp_lport = ti->ti_dport;
#if BSD>=43
			inp->inp_options = ip_srcroute();
#endif
			tp = intotcpcb(inp);
			tp->t_state = TCPS_LISTEN;
		}
	}

	/*
	 * Segment received on connection.
	 * Reset idle time and keep-alive timer.
	 */
	tp->t_idle = 0;
	tp->t_timer[TCPT_KEEP] = tcp_keepidle;

	/*
	 * Process options if not in LISTEN state,
	 * else do it below (after getting remote address).
	 */
	if (om && tp->t_state != TCPS_LISTEN) {
		tcp_dooptions(tp, om, ti, &ts_present, &ts_val, &ts_ecr, sackin, &sackin_n);
		om = 0;
	}
	/*
	 * RFC 1323: the peer's on-wire window is in scaled units. tiwin is the full
	 * byte window (ti_win << snd_scale). snd_scale stays 0 until the handshake
	 * commits it, so during the SYN/SYN-ACK exchange -- and always, while scaling
	 * is not negotiated -- tiwin == ti_win, i.e. unscaled, as RFC 1323 requires
	 * for SYN-bearing segments. Computed here, BEFORE the state switch that
	 * commits the scale, so this segment's own window uses the pre-commit scale.
	 */
	tiwin = (u_long)ti->ti_win << tp->snd_scale;
	/*
	 * Header prediction: check for the two common cases
	 * of a uni-directional data xfer.  If the packet has
	 * no control flags, is in-sequence, the window didn't
	 * change and we're not retransmitting, it's a
	 * candidate.  If the length is zero and the ack moved
	 * forward, we're the sender side of the xfer.  Just
	 * free the data acked & wake any higher level process
	 * that was blocked waiting for space.  If the length
	 * is non-zero and the ack didn't move, we're the
	 * receiver side.  If we're getting packets in-order
	 * (the reassembly queue is empty), add the data to
	 * the socket buffer and note that we need a delayed ack.
	 */
	if (tp->t_state == TCPS_ESTABLISHED &&
	    (tiflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) == TH_ACK &&
	    (!ts_present || TSTMP_GEQ(ts_val, tp->ts_recent)) &&
	    ti->ti_seq == tp->rcv_nxt &&
	    tiwin && tiwin == tp->snd_wnd &&
	    tp->snd_nxt == tp->snd_max &&
	    tp->t_dupacks < tcprexmtthresh &&		/* not in Reno/NewReno recovery */
	    (tp->t_flags & TF_SACK_RECOVER) == 0) {	/* not while SACK-recovering: */
		/*
		 * RFC 1323: if last ACK falls within this segment's sequence
		 * numbers, record the timestamp (Braden 1993/04/26). Dormant
		 * until activation -- ts_present is 0 for a conformant peer we
		 * never sent timestamps to, so this does not run.
		 */
		if (ts_present && SEQ_LEQ(ti->ti_seq, tp->last_ack_sent) &&
		    SEQ_LT(tp->last_ack_sent, ti->ti_seq + ti->ti_len)) {
			tp->ts_recent_age = tcp_now;
			tp->ts_recent = ts_val;
		}
		if (ti->ti_len == 0) {
			if (SEQ_GT(ti->ti_ack, tp->snd_una) &&
			    SEQ_LEQ(ti->ti_ack, tp->snd_max) &&
			    tp->snd_cwnd >= tp->snd_wnd) {
				/*
				 * this is a pure ack for outstanding data.
				 */
				++tcppredack;
				/*
				 * RFC 1323 RTTM: prefer the echoed timestamp,
				 * but only when timestamps were actually
				 * negotiated (TF_RCVD_TSTMP) so an unsolicited
				 * echo can never corrupt the RTT estimate.
				 */
				if (ts_present && (tp->t_flags & TF_RCVD_TSTMP))
					tcp_xmit_timer(tp, tcp_now - ts_ecr + 1);
				else if (tp->t_rtt && SEQ_GT(ti->ti_ack,tp->t_rtseq))
					tcp_xmit_timer(tp, tp->t_rtt);
				acked = ti->ti_ack - tp->snd_una;
				tcpstat.tcps_rcvackpack++;
				tcpstat.tcps_rcvackbyte += acked;
				sbdrop(&so->so_snd, acked);
				tp->snd_una = ti->ti_ack;
				m_freem(m);

				/*
				 * If all outstanding data are acked, stop
				 * retransmit timer, otherwise restart timer
				 * using current (possibly backed-off) value.
				 * If process is waiting for space,
				 * wakeup/selwakeup/signal.  If data
				 * are ready to send, let tcp_output
				 * decide between more output or persist.
				 */
				if (tp->snd_una == tp->snd_max)
					tp->t_timer[TCPT_REXMT] = 0;
				else if (tp->t_timer[TCPT_PERSIST] == 0)
					tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;

				if (so->so_snd.sb_flags & SB_NOTIFY)
					sowwakeup(so);
				if (so->so_snd.sb_cc)
					(void) tcp_output(tp);
				return;
			}
		} else if (ti->ti_ack == tp->snd_una &&
		    tp->seg_next == (struct tcpiphdr *)tp &&
		    ti->ti_len <= sbspace(&so->so_rcv)) {
			/*
			 * this is a pure, in-sequence data packet
			 * with nothing on the reassembly queue and
			 * we have enough buffer space to take it.
			 */
			++tcppreddat;
			tp->rcv_nxt += ti->ti_len;
			tcpstat.tcps_rcvpack++;
			tcpstat.tcps_rcvbyte += ti->ti_len;
			/*
			 * Drop TCP and IP headers then add data
			 * to socket buffer
			 */
			m->m_data += sizeof(struct tcpiphdr);
			m->m_len -= sizeof(struct tcpiphdr);
			sbappend(&so->so_rcv, m);
			sorwakeup(so);
			TCP_SETDELACK(tp);
			/*
			 * The header-prediction fast path returns here without falling
			 * through to the dodata: "(needoutput || TF_ACKNOW) -> tcp_output"
			 * at the bottom of the function -- so if TCP_SETDELACK just forced
			 * the every-other-segment ACK (TF_ACKNOW), send it now. Otherwise
			 * TF_DELACK is left for tcp_fasttimo. Without this the immediate ACK
			 * would be stuck until later traffic, worse than the 40ms bound.
			 */
			if (tp->t_flags & TF_ACKNOW)
				(void) tcp_output(tp);
			return;
		}
	}

	/*
	 * Drop TCP and IP headers; TCP options were dropped above.
	 */
	m->m_data += sizeof(struct tcpiphdr);
	m->m_len -= sizeof(struct tcpiphdr);

	/*
	 * Calculate amount of space in receive window,
	 * and then do TCP input processing.
	 * Receive window is amount of space in rcv queue,
	 * but not less than advertised window.
	 */
	{ int win;

	win = sbspace(&so->so_rcv);
	if (win < 0)
		win = 0;
	tp->rcv_wnd = max(win, (int)(tp->rcv_adv - tp->rcv_nxt));
	}

	switch (tp->t_state) {

	/*
	 * If the state is LISTEN then ignore segment if it contains an RST.
	 * If the segment contains an ACK then it is bad and send a RST.
	 * If it does not contain a SYN then it is not interesting; drop it.
	 * Don't bother responding if the destination was a broadcast.
	 * Otherwise initialize tp->rcv_nxt, and tp->irs, select an initial
	 * tp->iss, and send a segment:
	 *     <SEQ=ISS><ACK=RCV_NXT><CTL=SYN,ACK>
	 * Also initialize tp->snd_nxt to tp->iss+1 and tp->snd_una to tp->iss.
	 * Fill in remote peer address fields if not previously specified.
	 * Enter SYN_RECEIVED state, and process any other fields of this
	 * segment in this state.
	 */
	case TCPS_LISTEN: {
		struct mbuf *am;
		register struct sockaddr_in *sin;

		if (tiflags & TH_RST)
			goto drop;
		if (tiflags & TH_ACK)
			goto dropwithreset;
		if ((tiflags & TH_SYN) == 0)
			goto drop;
		if (m->m_flags & M_BCAST)
			goto drop;
		am = m_get(M_DONTWAIT, MT_SONAME);	/* XXX */
		if (am == NULL)
			goto drop;
		am->m_len = sizeof (struct sockaddr_in);
		sin = mtod(am, struct sockaddr_in *);
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = ti->ti_src;
		sin->sin_port = ti->ti_sport;
		laddr = inp->inp_laddr;
		if (inp->inp_laddr.s_addr == INADDR_ANY)
			inp->inp_laddr = ti->ti_dst;
		if (in_pcbconnect(inp, am)) {
			inp->inp_laddr = laddr;
			(void) m_free(am);
			goto drop;
		}
		(void) m_free(am);
		tp->t_template = tcp_template(tp);
		if (tp->t_template == 0) {
			tp = tcp_drop(tp, ENOBUFS);
			dropsocket = 0;		/* socket is already gone */
			goto drop;
		}
		if (om) {
			tcp_dooptions(tp, om, ti, &ts_present, &ts_val, &ts_ecr, sackin, &sackin_n);
			om = 0;
		}
		if (iss)
			tp->iss = iss;
		else
			tp->iss = tcp_new_isn(tp);	/* RFC 6528 randomised ISN */
		tcp_iss += TCP_ISSINCR/2;		/* advance the base M */
		tp->irs = ti->ti_seq;
		tcp_sendseqinit(tp);
		tcp_rcvseqinit(tp);
		tp->t_flags |= TF_ACKNOW;
		tp->t_state = TCPS_SYN_RECEIVED;
		tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
		dropsocket = 0;		/* committed to socket */
		tcpstat.tcps_accepts++;
		goto trimthenstep6;
		}

	/*
	 * If the state is SYN_SENT:
	 *	if seg contains an ACK, but not for our SYN, drop the input.
	 *	if seg contains a RST, then drop the connection.
	 *	if seg does not contain SYN, then drop it.
	 * Otherwise this is an acceptable SYN segment
	 *	initialize tp->rcv_nxt and tp->irs
	 *	if seg contains ack then advance tp->snd_una
	 *	if SYN has been acked change to ESTABLISHED else SYN_RCVD state
	 *	arrange for segment to be acked (eventually)
	 *	continue processing rest of data/controls, beginning with URG
	 */
	case TCPS_SYN_SENT:
		if ((tiflags & TH_ACK) &&
		    (SEQ_LEQ(ti->ti_ack, tp->iss) ||
		     SEQ_GT(ti->ti_ack, tp->snd_max)))
			goto dropwithreset;
		if (tiflags & TH_RST) {
			if (tiflags & TH_ACK)
				tp = tcp_drop(tp, ECONNREFUSED);
			goto drop;
		}
		if ((tiflags & TH_SYN) == 0)
			goto drop;
		if (tiflags & TH_ACK) {
			tp->snd_una = ti->ti_ack;
			if (SEQ_LT(tp->snd_nxt, tp->snd_una))
				tp->snd_nxt = tp->snd_una;
		}
		tp->t_timer[TCPT_REXMT] = 0;
		tp->irs = ti->ti_seq;
		tcp_rcvseqinit(tp);
		tp->t_flags |= TF_ACKNOW;
		if (tiflags & TH_ACK && SEQ_GT(tp->snd_una, tp->iss)) {
			tcpstat.tcps_connects++;
			soisconnected(so);
			tp->t_state = TCPS_ESTABLISHED;
			/*
			 * RFC 1323: both SYNs are now exchanged -- commit the
			 * negotiated window scales, but only if BOTH sides offered
			 * scaling (we requested it AND the peer's SYN carried it).
			 * Otherwise the scales stay 0 (no scaling). Inert until
			 * TF_REQ_SCALE is set at activation.
			 */
			if ((tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE)) ==
			    (TF_RCVD_SCALE|TF_REQ_SCALE)) {
				tp->snd_scale = tp->requested_s_scale;
				tp->rcv_scale = tp->request_r_scale;
			}
			(void) tcp_reass(tp, (struct tcpiphdr *)0,
				(struct mbuf *)0);
			/*
			 * if we didn't have to retransmit the SYN,
			 * use its rtt as our initial srtt & rtt var.
			 */
			if (tp->t_rtt)
				tcp_xmit_timer(tp, tp->t_rtt);
		} else
			tp->t_state = TCPS_SYN_RECEIVED;

trimthenstep6:
		/*
		 * Advance ti->ti_seq to correspond to first data byte.
		 * If data, trim to stay within window,
		 * dropping FIN if necessary.
		 */
		ti->ti_seq++;
		if ((u_short)ti->ti_len > tp->rcv_wnd) {
			todrop = ti->ti_len - tp->rcv_wnd;
			m_adj(m, -todrop);
			ti->ti_len = tp->rcv_wnd;
			tiflags &= ~TH_FIN;
			tcpstat.tcps_rcvpackafterwin++;
			tcpstat.tcps_rcvbyteafterwin += todrop;
		}
		tp->snd_wl1 = ti->ti_seq - 1;
		tp->rcv_up = ti->ti_seq;
		goto step6;
	}

	/*
	 * States other than LISTEN or SYN_SENT.
	 * First check timestamp, if present.
	 * Then check that at least some bytes of segment are within
	 * receive window.  If segment begins before rcv_nxt,
	 * drop leading data (and SYN); if nothing left, just ack.
	 */
	/*
	 * RFC 1323 PAWS: if this segment carries a timestamp that is older
	 * than the last one we recorded, it's a stale duplicate -- ack and
	 * drop it. Gated on TF_RCVD_TSTMP, which stays clear (and ts_recent
	 * stays 0) until timestamps are activated, so this is dormant until
	 * then.
	 */
	if (ts_present && (tiflags & TH_RST) == 0 &&
	    (tp->t_flags & TF_RCVD_TSTMP) &&
	    TSTMP_LT(ts_val, tp->ts_recent)) {
		/* Check to see if ts_recent is over 24 days old. */
		if ((int)(tcp_now - tp->ts_recent_age) > TCP_PAWS_IDLE) {
			/*
			 * Invalidate ts_recent. If this segment updates
			 * ts_recent below, ts_recent_age will be reset and a
			 * valid value restored; otherwise zero at least keeps
			 * the required 0 in the echoed timestamp reply.
			 */
			tp->ts_recent = 0;
		} else {
			tcpstat.tcps_rcvduppack++;
			tcpstat.tcps_rcvdupbyte += ti->ti_len;
			tcpstat.tcps_pawsdrop++;
			goto dropafterack;
		}
	}

	/*
	 * Snapshot the as-received sequence number NOW, before the duplicate-trim
	 * block below can advance ti_seq (a segment straddling rcv_nxt has its
	 * ti_seq rewritten to exactly rcv_nxt at line ~932). The RFC 5961 RST
	 * exactness check further down must test this original value, or a spoofed
	 * RST with a guessed-low seq + straddling padding would be laundered to
	 * look exact and wrongly reset the connection.
	 */
	rst_seq = ti->ti_seq;
	todrop = tp->rcv_nxt - ti->ti_seq;
	if (todrop > 0) {
		if (tiflags & TH_SYN) {
			tiflags &= ~TH_SYN;
			ti->ti_seq++;
			if (ti->ti_urp > 1) 
				ti->ti_urp--;
			else
				tiflags &= ~TH_URG;
			todrop--;
		}
		if (todrop > ti->ti_len ||
		    (todrop == ti->ti_len && (tiflags&TH_FIN) == 0)) {
			tcpstat.tcps_rcvduppack++;
			tcpstat.tcps_rcvdupbyte += ti->ti_len;
			/*
			 * If segment is just one to the left of the window,
			 * check two special cases:
			 * 1. Don't toss RST in response to 4.2-style keepalive.
			 * 2. If the only thing to drop is a FIN, we can drop
			 *    it, but check the ACK or we will get into FIN
			 *    wars if our FINs crossed (both CLOSING).
			 * In either case, send ACK to resynchronize,
			 * but keep on processing for RST or ACK.
			 * NB post-RFC5961: a RST that matches the COMPAT_42 condition
			 * below (ti_seq == rcv_nxt-1) had rst_seq snapshotted at that
			 * pre-trim value, so the exactness check further down sees a
			 * non-exact seq and routes it to a challenge ACK -- it is no
			 * longer accepted as a reset (the correct RFC 5961 outcome).
			 */
			if ((tiflags & TH_FIN && todrop == ti->ti_len + 1)
#if TCP_COMPAT_42
			  || (tiflags & TH_RST && ti->ti_seq == tp->rcv_nxt - 1)
#endif
			   ) {
				todrop = ti->ti_len;
				tiflags &= ~TH_FIN;
				tp->t_flags |= TF_ACKNOW;
			} else {
				/*
				 * RFC 2883 D-SACK: this whole segment duplicates
				 * data at or below rcv_nxt (a spurious retransmit
				 * by the peer). Record its range as a D-SACK block
				 * so the peer learns the retransmit was needless
				 * (helps it detect reordering); force an immediate
				 * ACK to carry it. ti_seq/ti_len are still the
				 * received values here -- the m_adj trim below is
				 * bypassed by the goto.
				 */
				if ((tp->t_flags & TF_SACK_PERMIT) &&
				    ti->ti_len > 0) {
					tp->rcv_dsackblk.start = ti->ti_seq;
					tp->rcv_dsackblk.end =
					    ti->ti_seq + ti->ti_len;
					tp->t_flags |= TF_DSACK | TF_ACKNOW;
				}
				goto dropafterack;
			}
		} else {
			tcpstat.tcps_rcvpartduppack++;
			tcpstat.tcps_rcvpartdupbyte += todrop;
		}
		m_adj(m, todrop);
		ti->ti_seq += todrop;
		ti->ti_len -= todrop;
		if (ti->ti_urp > todrop)
			ti->ti_urp -= todrop;
		else {
			tiflags &= ~TH_URG;
			ti->ti_urp = 0;
		}
	}

	/*
	 * If new data are received on a connection after the
	 * user processes are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) &&
	    tp->t_state > TCPS_CLOSE_WAIT && ti->ti_len) {
		tp = tcp_close(tp);
		tcpstat.tcps_rcvafterclose++;
		goto dropwithreset;
	}

	/*
	 * If segment ends after window, drop trailing data
	 * (and PUSH and FIN); if nothing left, just ACK.
	 */
	todrop = (ti->ti_seq+ti->ti_len) - (tp->rcv_nxt+tp->rcv_wnd);
	if (todrop > 0) {
		tcpstat.tcps_rcvpackafterwin++;
		if (todrop >= ti->ti_len) {
			tcpstat.tcps_rcvbyteafterwin += ti->ti_len;
			/*
			 * If a new connection request is received
			 * while in TIME_WAIT, drop the old connection
			 * and start over if the sequence numbers
			 * are above the previous ones.
			 */
			if (tiflags & TH_SYN &&
			    tp->t_state == TCPS_TIME_WAIT &&
			    SEQ_GT(ti->ti_seq, tp->rcv_nxt)) {
				iss = tp->rcv_nxt + TCP_ISSINCR;
				tp = tcp_close(tp);
				goto findpcb;
			}
			/*
			 * If window is closed can only take segments at
			 * window edge, and have to drop data and PUSH from
			 * incoming segments.  Continue processing, but
			 * remember to ack.  Otherwise, drop segment
			 * and ack.
			 */
			if (tp->rcv_wnd == 0 && ti->ti_seq == tp->rcv_nxt) {
				tp->t_flags |= TF_ACKNOW;
				tcpstat.tcps_rcvwinprobe++;
			} else
				goto dropafterack;
		} else
			tcpstat.tcps_rcvbyteafterwin += todrop;
		m_adj(m, -todrop);
		ti->ti_len -= todrop;
		tiflags &= ~(TH_PUSH|TH_FIN);
	}

	/*
	 * RFC 1323: if the last ACK we sent falls within this segment's
	 * sequence numbers, record its timestamp (Braden 1993/04/26 -- the
	 * range test counts a SYN/FIN sequence slot). Dormant until activation:
	 * ts_present is 0 for a conformant peer we never sent timestamps to.
	 */
	if (ts_present && SEQ_LEQ(ti->ti_seq, tp->last_ack_sent) &&
	    SEQ_LT(tp->last_ack_sent, ti->ti_seq + ti->ti_len +
		   ((tiflags & (TH_SYN|TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_now;
		tp->ts_recent = ts_val;
	}

	/*
	 * If the RST bit is set examine the state:
	 *    SYN_RECEIVED STATE:
	 *	If passive open, return to LISTEN state.
	 *	If active open, inform user that connection was refused.
	 *    ESTABLISHED, FIN_WAIT_1, FIN_WAIT2, CLOSE_WAIT STATES:
	 *	Inform user that connection was reset, and close tcb.
	 *    CLOSING, LAST_ACK, TIME_WAIT STATES
	 *	Close the tcb.
	 */
	if (tiflags&TH_RST) {
	    /*
	     * RFC 5961 sec.3: only a RST whose sequence number EXACTLY matches
	     * rcv_nxt may reset the connection. An in-window-but-not-exact RST
	     * (out-of-window RSTs were already dropped by the acceptability check
	     * above) gets a challenge ACK instead, so a blind off-path RST -- which
	     * can land anywhere in the window -- cannot tear us down. Test rst_seq
	     * (the as-received seq) NOT ti->ti_seq: the duplicate-trim block above
	     * rewrites ti_seq to rcv_nxt for a straddling segment, which would
	     * otherwise launder a guessed-low spoofed RST into looking exact.
	     */
	    if (rst_seq != tp->rcv_nxt)
		goto challenge;
	    switch (tp->t_state) {

	case TCPS_SYN_RECEIVED:
		so->so_error = ECONNREFUSED;
		goto close;

	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
		so->so_error = ECONNRESET;
	close:
		tp->t_state = TCPS_CLOSED;
		tcpstat.tcps_drops++;
		tp = tcp_close(tp);
		goto drop;

	case TCPS_CLOSING:
	case TCPS_LAST_ACK:
	case TCPS_TIME_WAIT:
		tp = tcp_close(tp);
		goto drop;
	    }
	}

	/*
	 * RFC 5961 sec.4: an in-window SYN might be a blind injection, so do NOT
	 * tear the connection down the way stock 4.4BSD does (drop it and send a
	 * RST). Send a challenge ACK instead -- a peer that genuinely restarted
	 * answers it with its own RST (handled by the exact-sequence check above,
	 * and we close then), so a blind SYN cannot reset an established session.
	 */
	if (tiflags & TH_SYN)
		goto challenge;

	/*
	 * If the ACK bit is off we drop the segment and return.
	 */
	if ((tiflags & TH_ACK) == 0)
		goto drop;

	/*
	 * RFC 6675: fold this ACK's SACK blocks into the sender scoreboard before
	 * the ack ladder runs, so loss recovery (B2) sees the latest picture. Also
	 * trims blocks the cumulative ack covered. Gated on TF_SACK_PERMIT -- inert
	 * (scoreboard only maintained, not yet consulted) until the recovery code
	 * lands. snd_una is still the pre-advance value here, which is what Update
	 * clamps against.
	 *
	 * NB the ESTABLISHED header-prediction fast path (above) returns before
	 * reaching here, so a pure in-order ack does NOT freshen/trim the
	 * scoreboard -- harmless (that path only fires with no loss, hence no
	 * recovery), and the dup acks that actually signal loss fail the fast
	 * path's SEQ_GT test and always fall through to here. B2 must therefore
	 * not assume every ack has run Update.
	 */
	if (tp->t_flags & TF_SACK_PERMIT) {
		/*
		 * PRR (RFC 6937) DeliveredData needs the SACKed-octet count as it
		 * stood BEFORE this ack folds its blocks in; snapshot it here (only
		 * while recovering) so tcp_sack_prr_ack() can net out the change.
		 */
		if (tp->t_flags & TF_SACK_RECOVER)
			prr_prev_sacked = tcp_sack_snd_bytes_above(tp, tp->snd_una);
		tcp_sack_update(tp, sackin, sackin_n);
	}

	/*
	 * Ack processing.
	 */
	switch (tp->t_state) {

	/*
	 * In SYN_RECEIVED state if the ack ACKs our SYN then enter
	 * ESTABLISHED state and continue processing, otherwise
	 * send an RST.
	 */
	case TCPS_SYN_RECEIVED:
		if (SEQ_GT(tp->snd_una, ti->ti_ack) ||
		    SEQ_GT(ti->ti_ack, tp->snd_max))
			goto dropwithreset;
		tcpstat.tcps_connects++;
		soisconnected(so);
		tp->t_state = TCPS_ESTABLISHED;
		/* RFC 1323: commit the negotiated window scales (see SYN_SENT case). */
		if ((tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE)) ==
		    (TF_RCVD_SCALE|TF_REQ_SCALE)) {
			tp->snd_scale = tp->requested_s_scale;
			tp->rcv_scale = tp->request_r_scale;
			/*
			 * This handshake-completing ACK carries no SYN, so its window
			 * is already in the negotiated scale -- but tiwin was computed
			 * near the top of tcp_input() with the old snd_scale (0). Re-
			 * derive it now so step6 commits a correctly scaled snd_wnd
			 * instead of the raw (tiny) unscaled window for the first RTT.
			 */
			tiwin = (u_long)ti->ti_win << tp->snd_scale;
		}
		(void) tcp_reass(tp, (struct tcpiphdr *)0, (struct mbuf *)0);
		tp->snd_wl1 = ti->ti_seq - 1;
		/* fall into ... */

	/*
	 * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
	 * ACKs.  If the ack is in the range
	 *	tp->snd_una < ti->ti_ack <= tp->snd_max
	 * then advance tp->snd_una to ti->ti_ack and drop
	 * data from the retransmission queue.  If this ACK reflects
	 * more up to date window information we update our window information.
	 */
	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
	case TCPS_CLOSING:
	case TCPS_LAST_ACK:
	case TCPS_TIME_WAIT:

		if (SEQ_LEQ(ti->ti_ack, tp->snd_una)) {
			if (ti->ti_len == 0 && tiwin == tp->snd_wnd) {
				tcpstat.tcps_rcvdupack++;
				/*
				 * If we have outstanding data (other than
				 * a window probe), this is a completely
				 * duplicate ack (ie, window info didn't
				 * change), the ack is the biggest we've
				 * seen and we've seen exactly our rexmt
				 * threshhold of them, assume a packet
				 * has been dropped and retransmit it.
				 * Kludge snd_nxt & the congestion
				 * window so we send only this one
				 * packet.
				 *
				 * We know we're losing at the current
				 * window size so do congestion avoidance
				 * (set ssthresh to half the current window
				 * and pull our congestion window back to
				 * the new ssthresh).
				 *
				 * Dup acks mean that packets have left the
				 * network (they're now cached at the receiver) 
				 * so bump cwnd by the amount in the receiver
				 * to keep a constant cwnd packets in the
				 * network.
				 */
				/*
				 * RFC 6675: once in SACK recovery, every dup ack
				 * (its SACK blocks already folded into the
				 * scoreboard at the top of ack processing) simply
				 * clocks another pipe refill -- bypass the Reno
				 * dup-ack counter ladder entirely, so a partial ack
				 * having reset t_dupacks can't drop us into Limited
				 * Transmit or spuriously re-enter fast retransmit.
				 */
				if (tp->t_flags & TF_SACK_RECOVER) {
					/*
					 * PRR (RFC 6937): this dup ack delivered data
					 * via its SACK blocks (no cumulative advance, so
					 * acked == 0). Pace snd_cwnd before refilling.
					 */
					tcp_sack_prr_ack(tp, (long)0, prr_prev_sacked);
					(void) tcp_output(tp);
					goto drop;
				}
				if (tp->t_timer[TCPT_REXMT] == 0 ||
				    ti->ti_ack != tp->snd_una)
					tp->t_dupacks = 0;
				else if (++tp->t_dupacks == tcprexmtthresh) {
					tcp_seq onxt = tp->snd_nxt;
					u_int win =
					    min(tp->snd_wnd, tp->snd_cwnd) / 2 /
						tp->t_maxseg;

					if (win < 2)
						win = 2;
					tp->snd_ssthresh = win * tp->t_maxseg;
					tp->t_timer[TCPT_REXMT] = 0;
					tp->t_rtt = 0;
					/*
					 * RFC 6675 SACK recovery -- entered when
					 * SACK was negotiated AND snd_nxt == snd_max
					 * (the invariant tcp_output's rule-2 new-data
					 * path relies on; it holds here because we have
					 * not retransmitted yet). Fall back to Reno if
					 * either is false. Unlike Reno we neither rewind
					 * snd_nxt nor inflate cwnd: the scoreboard and
					 * the pipe estimate (tcp_sack_pipe) drive what
					 * tcp_output (re)sends, filling to cwnd (==
					 * ssthresh). snd_recover marks the recovery
					 * point; snd_highrxt starts at snd_una.
					 */
					if ((tp->t_flags & TF_SACK_PERMIT) &&
					    tp->snd_nxt == tp->snd_max) {
						tp->snd_cwnd = tp->snd_ssthresh;
						tp->snd_recover = tp->snd_max;
						tp->snd_highrxt = tp->snd_una;
						/* RFC 6937 PRR: reset the reduction bookkeeping.
						 * RecoverFS = flight size at entry, floored to
						 * one segment so it can never be a zero divisor. */
						tp->prr_delivered = 0;
						tp->prr_out = 0;
						tp->prr_recoverfs = tp->snd_max - tp->snd_una;
						if (tp->prr_recoverfs < tp->t_maxseg)
							tp->prr_recoverfs = tp->t_maxseg;
						tp->t_flags |= TF_SACK_RECOVER;
						tp->t_flags &= ~TF_SACK_RESCUE;
						(void) tcp_output(tp);
						goto drop;
					}
					/*
					 * NewReno (RFC 6582): remember the recovery
					 * point (snd_max, captured in onxt before we
					 * rewind snd_nxt) so a later partial ack -- one
					 * that advances snd_una but does not reach it --
					 * is recognised as "another loss in this window"
					 * and retransmitted, instead of ending recovery
					 * and waiting for a timeout (classic Reno's
					 * multiple-loss stall).
					 */
					tp->snd_recover = onxt;
					tp->snd_nxt = ti->ti_ack;
					tp->snd_cwnd = tp->t_maxseg;
					(void) tcp_output(tp);
					tp->snd_cwnd = tp->snd_ssthresh +
					       tp->t_maxseg * tp->t_dupacks;
					if (SEQ_GT(onxt, tp->snd_nxt))
						tp->snd_nxt = onxt;
					goto drop;
				} else if (tp->t_dupacks > tcprexmtthresh) {
					tp->snd_cwnd += tp->t_maxseg;
					(void) tcp_output(tp);
					goto drop;
				} else if (tcp_do_rfc3042) {
					/*
					 * RFC 3042 Limited Transmit. t_dupacks is 1
					 * or 2 here (below the fast-retransmit
					 * threshold). Send a segment of previously
					 * unsent data to keep the ACK clock running
					 * and raise the chance of reaching the third
					 * dup ack (fast retransmit) rather than an
					 * RTO -- most valuable when the window is
					 * small, so few dup acks would otherwise
					 * arrive. Temporarily inflate cwnd by
					 * t_dupacks*maxseg so tcp_output may inject
					 * at most 2 extra segments total (outstanding
					 * stays <= cwnd + 2*maxseg); the receiver
					 * window still bounds it. Restore cwnd right
					 * after -- congestion state is untouched until
					 * we actually enter fast retransmit above.
					 *
					 * The "2 extra segments" bound is exact in the
					 * normal window-full case (off already == the
					 * pre-inflation window). If the window was being
					 * UNDER-utilised, tcp_output's sendalot loop may
					 * drain more queued data here -- but never past
					 * the real cwnd+2*maxseg (snd_wnd is untouched),
					 * so it stays congestion-safe; this matches the
					 * upstream BSD "reuse tcp_output" approach. (This
					 * branch only runs with the rexmt timer live, i.e.
					 * snd_max > snd_una, so tcp_output's idle-restart
					 * cannot fire and be clobbered by the restore.)
					 */
					u_long ocwnd = tp->snd_cwnd;

					tp->snd_cwnd +=
					    (u_long)tp->t_dupacks * tp->t_maxseg;
					(void) tcp_output(tp);
					tp->snd_cwnd = ocwnd;
					goto drop;
				}
			} else {
				/*
				 * ti_ack <= snd_una, but this ack carries data or a
				 * window change so it failed the pure-dup-ack test
				 * above and never reached the SACK-recovery bypass.
				 * tcp_sack_update() has already folded its SACK blocks
				 * in, so during recovery it delivered data -- account
				 * for it in PRR (acked == 0, snd_una unmoved) or that
				 * delivery is silently lost and PRR paces too slowly.
				 * No goto drop: the data/window are still processed
				 * below, and cwnd is now paced for the tail output.
				 */
				if (tp->t_flags & TF_SACK_RECOVER)
					tcp_sack_prr_ack(tp, (long)0, prr_prev_sacked);
				tp->t_dupacks = 0;
			}
			break;
		}
		/*
		 * If the congestion window was inflated to account
		 * for the other side's cached packets, retract it.
		 *
		 * NewReno (RFC 6582), non-SACK path only (a SACK connection
		 * drives its own recovery via TF_SACK_RECOVER, handled below):
		 * if we are in fast recovery and this ack does NOT reach the
		 * recovery point snd_recover, it is a PARTIAL ack -- more of
		 * this window was lost. Defer to the retransmit/deflate block
		 * after snd_una advances and keep t_dupacks so we stay in
		 * recovery. Only a full ack retracts the inflation and exits.
		 */
		if ((tp->t_flags & TF_SACK_RECOVER) == 0 &&
		    tp->t_dupacks >= tcprexmtthresh &&
		    SEQ_LT(ti->ti_ack, tp->snd_recover)) {
			partialack = 1;
		} else {
			if (tp->t_dupacks > tcprexmtthresh &&
			    tp->snd_cwnd > tp->snd_ssthresh)
				tp->snd_cwnd = tp->snd_ssthresh;
			tp->t_dupacks = 0;
		}
		if (SEQ_GT(ti->ti_ack, tp->snd_max)) {
			tcpstat.tcps_rcvacktoomuch++;
			goto dropafterack;
		}
		acked = ti->ti_ack - tp->snd_una;
		tcpstat.tcps_rcvackpack++;
		tcpstat.tcps_rcvackbyte += acked;

		/*
		 * If we have a timestamp reply, use it to update the smoothed
		 * round trip time -- but only when timestamps were actually
		 * negotiated (TF_RCVD_TSTMP), so an unsolicited echo cannot
		 * corrupt the estimate. Otherwise, if the transmit timer is
		 * running and the timed sequence number was acked, use t_rtt.
		 * Since we now have an rtt measurement, cancel the timer backoff
		 * (cf., Phil Karn's retransmit alg.) and recompute the initial
		 * retransmit timer.
		 */
		if (ts_present && (tp->t_flags & TF_RCVD_TSTMP))
			tcp_xmit_timer(tp, tcp_now - ts_ecr + 1);
		else if (tp->t_rtt && SEQ_GT(ti->ti_ack, tp->t_rtseq))
			tcp_xmit_timer(tp, tp->t_rtt);

		/*
		 * If all outstanding data is acked, stop retransmit
		 * timer and remember to restart (more output or persist).
		 * If there is more data to be acked, restart retransmit
		 * timer, using current (possibly backed-off) value.
		 */
		if (ti->ti_ack == tp->snd_max) {
			tp->t_timer[TCPT_REXMT] = 0;
			needoutput = 1;
		} else if (tp->t_timer[TCPT_PERSIST] == 0)
			tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;
		/*
		 * When new data is acked, open the congestion window.
		 * If the window gives us less than ssthresh packets
		 * in flight, open exponentially (maxseg per packet).
		 * Otherwise open linearly: maxseg per window
		 * (maxseg^2 / cwnd per packet), plus a constant
		 * fraction of a packet (maxseg/8) to help larger windows
		 * open quickly enough.
		 */
		/*
		 * During RFC 6675 SACK recovery cwnd is held at ssthresh and the
		 * pipe estimate governs how much we send, so do NOT open it here;
		 * normal congestion avoidance resumes once recovery exits below.
		 * Likewise skip it on a NewReno partial ack -- the partial-window
		 * deflation below sets cwnd for us.
		 */
		if ((tp->t_flags & TF_SACK_RECOVER) == 0 && !partialack) {
			register u_int cw = tp->snd_cwnd;
			register u_int incr = tp->t_maxseg;

			if (cw > tp->snd_ssthresh)
				incr = incr * incr / cw + incr / 8;
			tp->snd_cwnd = min(cw + incr, TCP_MAXWIN << tp->snd_scale);
		}
		if (acked > so->so_snd.sb_cc) {
			tp->snd_wnd -= so->so_snd.sb_cc;
			sbdrop(&so->so_snd, (int)so->so_snd.sb_cc);
			ourfinisacked = 1;
		} else {
			sbdrop(&so->so_snd, acked);
			tp->snd_wnd -= acked;
			ourfinisacked = 0;
		}
		if (so->so_snd.sb_flags & SB_NOTIFY)
			sowwakeup(so);
		tp->snd_una = ti->ti_ack;
		if (SEQ_LT(tp->snd_nxt, tp->snd_una))
			tp->snd_nxt = tp->snd_una;

		/*
		 * RFC 6675 SACK recovery on a cumulative-ack advance. Recovery is
		 * complete once the ack reaches snd_recover (the snd_max captured at
		 * entry) -- clear the recovery flag and the scoreboard; cwnd stays at
		 * ssthresh and normal congestion avoidance resumes on later acks. A
		 * partial ack keeps us recovering: HighRxt must not lag the cumulative
		 * ack, and we schedule another pipe refill (needoutput).
		 */
		if (tp->t_flags & TF_SACK_RECOVER) {
			if (SEQ_GEQ(tp->snd_una, tp->snd_recover)) {
				tp->t_flags &= ~TF_SACK_RECOVER;
				tp->snd_numsackblks = 0;
				tp->snd_cwnd = tp->snd_ssthresh;  /* PRR: leave recovery at ssthresh */
			} else {
				if (SEQ_LT(tp->snd_highrxt, tp->snd_una))
					tp->snd_highrxt = tp->snd_una;
				/* PRR (RFC 6937): pace snd_cwnd on this cumulative advance. */
				tcp_sack_prr_ack(tp, (long)acked, prr_prev_sacked);
				needoutput = 1;
			}
		}
		/*
		 * NewReno (RFC 6582) partial ack (non-SACK path; snd_una has just
		 * advanced but has not reached snd_recover). Retransmit exactly the
		 * one segment now at snd_una -- the next hole -- with cwnd pinned to
		 * one segment so tcp_output emits it and nothing above it (no
		 * go-back-N); restore snd_nxt afterward so any later output sends new
		 * data. Then partial-window-deflate: drop the bytes this ack acked
		 * (they left the network) and add one segment back if a full segment
		 * was acked, so roughly ssthresh stays in flight. t_dupacks is left
		 * intact -- we remain in fast recovery until an ack reaches
		 * snd_recover.
		 */
		if (partialack) {
			tcp_seq onxt = tp->snd_nxt;
			u_long cw = tp->snd_cwnd;

			tp->t_rtt = 0;			/* no RTT sample off a retransmit (Karn) */
			tp->snd_nxt = tp->snd_una;
			tp->snd_cwnd = tp->t_maxseg;
			(void) tcp_output(tp);
			if (SEQ_GT(onxt, tp->snd_nxt))
				tp->snd_nxt = onxt;
			if (cw > (u_long)acked)
				cw -= acked;
			else
				cw = 0;
			if ((u_long)acked >= tp->t_maxseg)
				cw += tp->t_maxseg;
			if (cw < tp->t_maxseg)
				cw = tp->t_maxseg;
			tp->snd_cwnd = cw;
			needoutput = 1;			/* send new data if the deflated cwnd allows */
		}

		switch (tp->t_state) {

		/*
		 * In FIN_WAIT_1 STATE in addition to the processing
		 * for the ESTABLISHED state if our FIN is now acknowledged
		 * then enter FIN_WAIT_2.
		 */
		case TCPS_FIN_WAIT_1:
			if (ourfinisacked) {
				/*
				 * If we can't receive any more
				 * data, then closing user can proceed.
				 * Starting the timer is contrary to the
				 * specification, but if we don't get a FIN
				 * we'll hang forever.
				 */
				if (so->so_state & SS_CANTRCVMORE) {
					soisdisconnected(so);
					tp->t_timer[TCPT_2MSL] = tcp_maxidle;
				}
				tp->t_state = TCPS_FIN_WAIT_2;
			}
			break;

	 	/*
		 * In CLOSING STATE in addition to the processing for
		 * the ESTABLISHED state if the ACK acknowledges our FIN
		 * then enter the TIME-WAIT state, otherwise ignore
		 * the segment.
		 */
		case TCPS_CLOSING:
			if (ourfinisacked) {
				tp->t_state = TCPS_TIME_WAIT;
				tcp_canceltimers(tp);
				tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
				soisdisconnected(so);
			}
			break;

		/*
		 * In LAST_ACK, we may still be waiting for data to drain
		 * and/or to be acked, as well as for the ack of our FIN.
		 * If our FIN is now acknowledged, delete the TCB,
		 * enter the closed state and return.
		 */
		case TCPS_LAST_ACK:
			if (ourfinisacked) {
				tp = tcp_close(tp);
				goto drop;
			}
			break;

		/*
		 * In TIME_WAIT state the only thing that should arrive
		 * is a retransmission of the remote FIN.  Acknowledge
		 * it and restart the finack timer.
		 */
		case TCPS_TIME_WAIT:
			tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
			goto dropafterack;
		}
	}

step6:
	/*
	 * Update window information.
	 * Don't look at window if no ACK: TAC's send garbage on first SYN.
	 */
	if ((tiflags & TH_ACK) &&
	    (SEQ_LT(tp->snd_wl1, ti->ti_seq) || 
	     (tp->snd_wl1 == ti->ti_seq &&
	      (SEQ_LT(tp->snd_wl2, ti->ti_ack) ||
	       (tp->snd_wl2 == ti->ti_ack && tiwin > tp->snd_wnd))))) {
	        /* keep track of pure window updates */
		if (ti->ti_len == 0 &&
		    tp->snd_wl2 == ti->ti_ack && tiwin > tp->snd_wnd)
			tcpstat.tcps_rcvwinupd++;
		tp->snd_wnd = tiwin;
		tp->snd_wl1 = ti->ti_seq;
		tp->snd_wl2 = ti->ti_ack;
		if (tp->snd_wnd > tp->max_sndwnd)
			tp->max_sndwnd = tp->snd_wnd;
		needoutput = 1;
	}

	/*
	 * Process segments with URG.
	 */
	if ((tiflags & TH_URG) && ti->ti_urp &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		/*
		 * This is a kludge, but if we receive and accept
		 * random urgent pointers, we'll crash in
		 * soreceive.  It's hard to imagine someone
		 * actually wanting to send this much urgent data.
		 */
		if (ti->ti_urp + so->so_rcv.sb_cc > sb_max) {
			ti->ti_urp = 0;			/* XXX */
			tiflags &= ~TH_URG;		/* XXX */
			goto dodata;			/* XXX */
		}
		/*
		 * If this segment advances the known urgent pointer,
		 * then mark the data stream.  This should not happen
		 * in CLOSE_WAIT, CLOSING, LAST_ACK or TIME_WAIT STATES since
		 * a FIN has been received from the remote side. 
		 * In these states we ignore the URG.
		 *
		 * According to RFC961 (Assigned Protocols),
		 * the urgent pointer points to the last octet
		 * of urgent data.  We continue, however,
		 * to consider it to indicate the first octet
		 * of data past the urgent section as the original 
		 * spec states (in one of two places).
		 */
		if (SEQ_GT(ti->ti_seq+ti->ti_urp, tp->rcv_up)) {
			tp->rcv_up = ti->ti_seq + ti->ti_urp;
			so->so_oobmark = so->so_rcv.sb_cc +
			    (tp->rcv_up - tp->rcv_nxt) - 1;
			if (so->so_oobmark == 0)
				so->so_state |= SS_RCVATMARK;
			sohasoutofband(so);
			tp->t_oobflags &= ~(TCPOOB_HAVEDATA | TCPOOB_HADDATA);
		}
		/*
		 * Remove out of band data so doesn't get presented to user.
		 * This can happen independent of advancing the URG pointer,
		 * but if two URG's are pending at once, some out-of-band
		 * data may creep in... ick.
		 */
		if (ti->ti_urp <= (u_short)ti->ti_len
#ifdef SO_OOBINLINE
		     && (so->so_options & SO_OOBINLINE) == 0
#endif
		     )
			tcp_pulloutofband(so, ti, m);
	} else
		/*
		 * If no out of band data is expected,
		 * pull receive urgent pointer along
		 * with the receive window.
		 */
		if (SEQ_GT(tp->rcv_nxt, tp->rcv_up))
			tp->rcv_up = tp->rcv_nxt;
dodata:							/* XXX */

	/*
	 * Process the segment text, merging it into the TCP sequencing queue,
	 * and arranging for acknowledgment of receipt if necessary.
	 * This process logically involves adjusting tp->rcv_wnd as data
	 * is presented to the user (this happens in tcp_usrreq.c,
	 * case PRU_RCVD).  If a FIN has already been received on this
	 * connection then we just ignore the text.
	 */
	if ((ti->ti_len || (tiflags&TH_FIN)) &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		TCP_REASS(tp, ti, m, so, tiflags);
		/*
		 * Note the amount of data that peer has sent into
		 * our window, in order to estimate the sender's
		 * buffer size.
		 */
		len = so->so_rcv.sb_hiwat - (tp->rcv_adv - tp->rcv_nxt);
	} else {
		m_freem(m);
		tiflags &= ~TH_FIN;
	}

	/*
	 * If FIN is received ACK the FIN and let the user know
	 * that the connection is closing.
	 */
	if (tiflags & TH_FIN) {
		if (TCPS_HAVERCVDFIN(tp->t_state) == 0) {
			socantrcvmore(so);
			tp->t_flags |= TF_ACKNOW;
			tp->rcv_nxt++;
		}
		switch (tp->t_state) {

	 	/*
		 * In SYN_RECEIVED and ESTABLISHED STATES
		 * enter the CLOSE_WAIT state.
		 */
		case TCPS_SYN_RECEIVED:
		case TCPS_ESTABLISHED:
			tp->t_state = TCPS_CLOSE_WAIT;
			break;

	 	/*
		 * If still in FIN_WAIT_1 STATE FIN has not been acked so
		 * enter the CLOSING state.
		 */
		case TCPS_FIN_WAIT_1:
			tp->t_state = TCPS_CLOSING;
			break;

	 	/*
		 * In FIN_WAIT_2 state enter the TIME_WAIT state,
		 * starting the time-wait timer, turning off the other 
		 * standard timers.
		 */
		case TCPS_FIN_WAIT_2:
			tp->t_state = TCPS_TIME_WAIT;
			tcp_canceltimers(tp);
			tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
			soisdisconnected(so);
			break;

		/*
		 * In TIME_WAIT state restart the 2 MSL time_wait timer.
		 */
		case TCPS_TIME_WAIT:
			tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
			break;
		}
	}
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_INPUT, ostate, tp, &tcp_saveti, 0);

	/*
	 * Return any desired output.
	 */
	if (needoutput || (tp->t_flags & TF_ACKNOW))
		(void) tcp_output(tp);
	return;

dropafterack:
	/*
	 * Generate an ACK dropping incoming segment if it occupies
	 * sequence space, where the ACK reflects our state.
	 */
	if (tiflags & TH_RST)
		goto drop;
	m_freem(m);
	tp->t_flags |= TF_ACKNOW;
	(void) tcp_output(tp);
	return;

challenge:
	/*
	 * RFC 5961 challenge ACK. Acknowledge (reflecting our current state)
	 * rather than honour a possibly-blind RST or SYN, forcing an off-path
	 * attacker to guess the EXACT sequence number instead of any in-window
	 * value. Unlike dropafterack this ACKs even when the RST bit is set --
	 * that is the whole point. A legitimate peer answers the challenge (a
	 * real RST arrives at the exact sequence, or a restarted peer sends its
	 * own RST), so connectivity is preserved, only spoofing is blocked.
	 * om is already freed by tcp_dooptions on every path that reaches here.
	 */
	/*
	 * RFC 5961 sec.3.2/sec.4 also recommend rate-limiting challenge ACKs: a
	 * flood of blind RST/SYN aimed at an established 4-tuple would otherwise
	 * make us build+checksum+send an ACK per packet, burning the single,
	 * software-checksummed net task's time on a slow 68k. Cap emissions per
	 * tcp_now window (tcp_now ticks at PR_SLOWHZ = 2 Hz, so ~500 ms). Global,
	 * not per-connection: the resource being protected is total net-task
	 * work, and a legitimate peer only ever needs a handful. Over budget we
	 * silently drop (the peer retransmits, answered next window) -- and must
	 * NOT goto drop, whose m_free(om) would double-free the already-freed om.
	 */
	{
		static u_long challenge_win = 0;	/* tcp_now of the counted window */
		static int    challenge_cnt = 0;	/* challenges emitted this window */

		if (tcp_now != challenge_win) {
			challenge_win = tcp_now;
			challenge_cnt = 0;
		}
		if (++challenge_cnt > TCP_CHALLENGE_ACK_LIMIT) {
			m_freem(m);
			return;
		}
	}
	m_freem(m);
	tp->t_flags |= TF_ACKNOW;
	(void) tcp_output(tp);
	return;

dropwithreset:
	if (om) {
		(void) m_free(om);
		om = 0;
	}
	/*
	 * Generate a RST, dropping incoming segment.
	 * Make ACK acceptable to originator of segment.
	 * Don't bother to respond if destination was broadcast.
	 */
	if ((tiflags & TH_RST) || m->m_flags & M_BCAST)
		goto drop;
	if (tiflags & TH_ACK)
		tcp_respond(tp, ti, m, (tcp_seq)0, ti->ti_ack, TH_RST);
	else {
		if (tiflags & TH_SYN)
			ti->ti_len++;
		tcp_respond(tp, ti, m, ti->ti_seq+ti->ti_len, (tcp_seq)0,
		    TH_RST|TH_ACK);
	}
	/* destroy temporarily created socket */
	if (dropsocket)
		(void) soabort(so);
	return;

drop:
	if (om)
		(void) m_free(om);
	/*
	 * Drop space held by incoming segment and return.
	 */
	if (tp && (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_DROP, ostate, tp, &tcp_saveti, 0);
	m_freem(m);
	/* destroy temporarily created socket */
	if (dropsocket)
		(void) soabort(so);
	return;
}

void
tcp_dooptions(tp, om, ti, ts_present, ts_val, ts_ecr, sack, nsack)
	struct tcpcb *tp;
	struct mbuf *om;
	struct tcpiphdr *ti;
	int *ts_present;
	u_long *ts_val, *ts_ecr;
	struct sackblk *sack;		/* out: inbound SACK blocks (caller array [TCP_MAX_SACK]) */
	int *nsack;			/* out: how many were parsed */
{
	register u_char *cp;
	u_short mss;
	int opt, optlen, cnt;
	int mss_present = 0;	/* this segment (a SYN) recomputed t_maxseg */

	*nsack = 0;
	cp = mtod(om, u_char *);
	cnt = om->m_len;
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[0];
		if (opt == TCPOPT_EOL)
			break;
		if (opt == TCPOPT_NOP)
			optlen = 1;
		else {
			/*
			 * PORT (AmiTCP_NG): a non-NOP option carries a length
			 * byte, so at least two bytes must remain before we read
			 * cp[1]; and the declared optlen must be a sane value
			 * that does not run off the end of the option area.
			 * Without these guards the loop reads stale bytes past
			 * the real options (info leak / UB), and would read out
			 * of bounds should the option mbuf ever be sized tighter.
			 */
			if (cnt < 2)
				break;
			optlen = cp[1];
			if (optlen < 2 || optlen > cnt)
				break;
		}
		switch (opt) {

		default:
			continue;

		case TCPOPT_MAXSEG:
			if (optlen != 4)
				continue;
			if (!(ti->ti_flags & TH_SYN))
				continue;
			bcopy((char *) cp + 2, (char *) &mss, sizeof(mss));
			(void)NTOHS(mss);
			(void) tcp_mss(tp, mss);	/* sets t_maxseg */
			mss_present = 1;
			break;

		case TCPOPT_WINDOW:
			/*
			 * RFC 1323 window scale. Only meaningful in a SYN. Record that
			 * the peer supports scaling (TF_RCVD_SCALE) and the shift it will
			 * apply to its own advertised windows -- capped at TCP_MAX_WINSHIFT
			 * per the RFC. This becomes our snd_scale at handshake commit.
			 * Inert until scaling is activated: nothing reads these until the
			 * commit code runs, which is gated on TF_REQ_SCALE (not set yet).
			 */
			if (optlen != TCPOLEN_WINDOW)
				continue;
			if (!(ti->ti_flags & TH_SYN))
				continue;
			tp->t_flags |= TF_RCVD_SCALE;
			tp->requested_s_scale = min(cp[2], TCP_MAX_WINSHIFT);
			break;

		case TCPOPT_SACK_PERMITTED:
			/*
			 * RFC 2018 SACK-permitted. Only meaningful in a SYN. If we
			 * offered it too (TF_REQ_SACK), SACK is negotiated for this
			 * connection (TF_SACK_PERMIT). Inert until the block-emit /
			 * sender-scoreboard code (later commits) reads TF_SACK_PERMIT.
			 */
			if (optlen != TCPOLEN_SACK_PERMITTED)
				continue;
			if (!(ti->ti_flags & TH_SYN))
				continue;
			if (tp->t_flags & TF_REQ_SACK)
				tp->t_flags |= TF_SACK_PERMIT;
			break;

		case TCPOPT_SACK:
			/*
			 * RFC 2018 SACK blocks (inbound) -- the peer reporting
			 * which of OUR sent segments it holds. Copy up to
			 * TCP_MAX_SACK blocks out for the RFC 6675 sender
			 * scoreboard (net/tcp_sack.c); ignored unless SACK was
			 * negotiated. optlen must be 2 + a whole number of 8-byte
			 * blocks. cp+2/+6 are only 2-byte aligned in the option
			 * area, so read the seq words with bcopy (a 68000 faults
			 * on an unaligned long load).
			 */
			if ((tp->t_flags & TF_SACK_PERMIT) == 0)
				continue;
			if (optlen < 2 + TCPOLEN_SACK ||
			    ((optlen - 2) % TCPOLEN_SACK) != 0)
				continue;
			{
				int n = (optlen - 2) / TCPOLEN_SACK;
				int room = TCP_MAX_SACK - *nsack;
				u_char *bp = cp + 2;
				int i;

				/*
				 * Cap against the caller array's REMAINING room, not
				 * just this option's block count: a segment may carry
				 * more than one TCPOPT_SACK option, and the cumulative
				 * *nsack must never exceed sackin[TCP_MAX_SACK]. (The
				 * 40-byte option ceiling already makes overflow
				 * unreachable, but that lives in another file -- enforce
				 * it locally so a future change can't reopen a stack
				 * overwrite.)
				 */
				if (n > room)
					n = (room > 0) ? room : 0;
				for (i = 0; i < n; i++, bp += TCPOLEN_SACK) {
					tcp_seq s, e;

					bcopy((char *)bp,     (char *)&s, sizeof s);
					bcopy((char *)bp + 4, (char *)&e, sizeof e);
					(void)NTOHL(s);
					(void)NTOHL(e);
					sack[*nsack].start = s;
					sack[*nsack].end   = e;
					(*nsack)++;
				}
			}
			break;

		case TCPOPT_TIMESTAMP:
			/*
			 * RFC 1323 timestamps. Hand the peer's TSval and our echoed
			 * TSecr back to the caller (the RTT / PAWS / ts_recent-update
			 * logic lives in tcp_input). A timestamp in a SYN records that
			 * the peer does timestamps (TF_RCVD_TSTMP) and seeds ts_recent --
			 * but only if we asked for timestamps first (TF_REQ_TSTMP). Unlike
			 * window scale (whose activation is AND-gated at the ESTABLISHED
			 * transition), ts_recent and TF_RCVD_TSTMP are consumed directly by
			 * the input path, so this request-gate is what keeps the whole
			 * timestamp machinery dormant until the activation commit sets
			 * TF_REQ_TSTMP: with it clear, a peer's timestamps are parsed into
			 * the *ts_* outputs (which nothing reads yet) but never engaged.
			 * Read with bcopy: cp+2 / cp+6 are only 2-byte aligned within the
			 * option area, so a direct u_long load could address-error on a
			 * 68000.
			 */
			if (optlen != TCPOLEN_TIMESTAMP)
				continue;
			*ts_present = 1;
			bcopy((char *)cp + 2, (char *)ts_val, sizeof(*ts_val));
			(void)NTOHL(*ts_val);
			bcopy((char *)cp + 6, (char *)ts_ecr, sizeof(*ts_ecr));
			(void)NTOHL(*ts_ecr);
			if ((ti->ti_flags & TH_SYN) &&
			    (tp->t_flags & TF_REQ_TSTMP)) {
				tp->t_flags |= TF_RCVD_TSTMP;
				tp->ts_recent = *ts_val;
				tp->ts_recent_age = tcp_now;
			}
			break;
		}
	}
	/*
	 * RFC 1323: if this segment negotiated timestamps AND carried the MSS
	 * option that (re)computed t_maxseg above, fold the 12-byte per-segment
	 * timestamp overhead out of t_maxseg now -- AFTER the whole option area
	 * has been walked, so TF_RCVD_TSTMP (set by a TIMESTAMP option that may
	 * sit later in wire order than the MSS option) is already settled. Doing
	 * it here, gated on mss_present, also makes it idempotent: it fires once,
	 * on the SYN/SYN-ACK, tied to the fresh t_maxseg tcp_mss just stored --
	 * never on the timestamped data segments that also pass through here
	 * (they carry no MSS option, so mss_present stays 0). Without this the
	 * option would push every full segment 12 bytes past the path MTU.
	 */
	if (mss_present &&
	    (tp->t_flags & (TF_REQ_TSTMP|TF_RCVD_TSTMP)) ==
	    (TF_REQ_TSTMP|TF_RCVD_TSTMP))
		tp->t_maxseg -= TCPOLEN_TSTAMP_APPA;
	(void) m_free(om);
}

/*
 * Pull out of band byte out of a segment so
 * it doesn't appear in the user's data queue.
 * It is still reflected in the segment length for
 * sequencing purposes.
 */
void
tcp_pulloutofband(so, ti, m)
	struct socket *so;
	struct tcpiphdr *ti;
	register struct mbuf *m;
{
	int cnt = ti->ti_urp - 1;
	
	while (cnt >= 0) {
		if (m->m_len > cnt) {
			char *cp = mtod(m, caddr_t) + cnt;
			struct tcpcb *tp = sototcpcb(so);

			tp->t_iobc = *cp;
			tp->t_oobflags |= TCPOOB_HAVEDATA;
			bcopy(cp+1, cp, (unsigned)(m->m_len - cnt - 1));
			m->m_len--;
			return;
		}
		cnt -= m->m_len;
		m = m->m_next;
		if (m == 0)
			break;
	}
	/*
	 * PORT (AmiTCP_NG) security fix: stock BSD panic()s here when the urgent
	 * byte falls outside the segment's mbuf chain. ti_urp arrives straight off
	 * the wire, so a crafted urgent pointer could reach this -- and panic()
	 * halts the entire machine (a remote denial of service). The caller now only
	 * enters when ti_urp <= ti_len, and ti_len is bounded to 32767 (see the
	 * length check in tcp_input), so the byte is always within this segment and
	 * we never arrive here in practice. Should we somehow get here anyway, simply
	 * abandon the out-of-band pull rather than take the box down.
	 */
}

/*
 * Collect new round-trip time estimate
 * and update averages and current timeout.
 */
void
tcp_xmit_timer(tp, rtt)
	register struct tcpcb *tp;
	register short rtt;		/* the RTT sample (from t_rtt, or a timestamp) */
{
	register short delta;

	tcpstat.tcps_rttupdated++;
	if (tp->t_srtt != 0) {
		/*
		 * srtt is stored as fixed point with 3 bits after the
		 * binary point (i.e., scaled by 8).  The following magic
		 * is equivalent to the smoothing algorithm in rfc793 with
		 * an alpha of .875 (srtt = rtt/8 + srtt*7/8 in fixed
		 * point).  Adjust t_rtt to origin 0.
		 */
		delta = rtt - 1 - (tp->t_srtt >> TCP_RTT_SHIFT);
		if ((tp->t_srtt += delta) <= 0)
			tp->t_srtt = 1;
		/*
		 * We accumulate a smoothed rtt variance (actually, a
		 * smoothed mean difference), then set the retransmit
		 * timer to smoothed rtt + 4 times the smoothed variance.
		 * rttvar is stored as fixed point with 2 bits after the
		 * binary point (scaled by 4).  The following is
		 * equivalent to rfc793 smoothing with an alpha of .75
		 * (rttvar = rttvar*3/4 + |delta| / 4).  This replaces
		 * rfc793's wired-in beta.
		 */
		if (delta < 0)
			delta = -delta;
		delta -= (tp->t_rttvar >> TCP_RTTVAR_SHIFT);
		if ((tp->t_rttvar += delta) <= 0)
			tp->t_rttvar = 1;
	} else {
		/* 
		 * No rtt measurement yet - use the unsmoothed rtt.
		 * Set the variance to half the rtt (so our first
		 * retransmit happens at 2*rtt)
		 */
		tp->t_srtt = rtt << TCP_RTT_SHIFT;
		tp->t_rttvar = rtt << (TCP_RTTVAR_SHIFT - 1);
	}
	tp->t_rtt = 0;
	tp->t_rxtshift = 0;

	/*
	 * the retransmit should happen at rtt + 4 * rttvar.
	 * Because of the way we do the smoothing, srtt and rttvar
	 * will each average +1/2 tick of bias.  When we compute
	 * the retransmit timer, we want 1/2 tick of rounding and
	 * 1 extra tick because of +-1/2 tick uncertainty in the
	 * firing of the timer.  The bias will give us exactly the
	 * 1.5 tick we need.  But, because the bias is
	 * statistical, we have to test that we don't drop below
	 * the minimum feasible timer (which is 2 ticks).
	 */
	TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp),
	    tp->t_rttmin, TCPTV_REXMTMAX);
	
	/*
	 * We received an ack for a packet that wasn't retransmitted;
	 * it is probably safe to discard any error indications we've
	 * received recently.  This isn't quite right, but close enough
	 * for now (a route might have failed after we sent a segment,
	 * and the return path might not be symmetrical).
	 */
	tp->t_softerror = 0;
}

/*
 * Determine a reasonable value for maxseg size.
 * If the route is known, check route for mtu.
 * If none, use an mss that can be handled on the outgoing
 * interface without forcing IP to fragment; if bigger than
 * an mbuf cluster (mbconf.mclbytes), round down to nearest multiple of mbconf.mclbytes
 * to utilize large mbufs.  If no route is found, route has no mtu,
 * or the destination isn't local, use a default, hopefully conservative
 * size (usually 512 or the default IP max size, but no more than the mtu
 * of the interface), as we can't discover anything about intervening
 * gateways or networks.  We also initialize the congestion/slow start
 * window to be a single segment if the destination isn't local.
 * While looking at the routing entry, we also initialize other path-dependent
 * parameters from pre-set or cached values in the routing entry.
 */

int
tcp_mss(tp, offer)
	register struct tcpcb *tp;
	u_short offer;
{
	struct route *ro;
	register struct rtentry *rt;
	struct ifnet *ifp;
	register int rtt, mss;
	u_long bufsize;
	struct inpcb *inp;
	struct socket *so;
	extern int tcp_mssdflt;

	inp = tp->t_inpcb;
	ro = &inp->inp_route;

	if ((rt = ro->ro_rt) == (struct rtentry *)0) {
		/* No route yet, so try to acquire one */
		if (inp->inp_faddr.s_addr != INADDR_ANY) {
			ro->ro_dst.sa_family = AF_INET;
			ro->ro_dst.sa_len = sizeof(ro->ro_dst);
			((struct sockaddr_in *) &ro->ro_dst)->sin_addr =
				inp->inp_faddr;
			rtalloc(ro);
		}
		if ((rt = ro->ro_rt) == (struct rtentry *)0)
			/* No route -> no interface MTU to derive from. Use the concrete
			 * fallback; auto (0) or a u_short-truncating cap (>65535) -> TCP_MSS. */
			return ((tcp_mssdflt > 0 && tcp_mssdflt <= 65535) ? tcp_mssdflt : TCP_MSS);
	}
	ifp = rt->rt_ifp;
	so = inp->inp_socket;

#ifdef RTV_MTU	/* if route characteristics exist ... */
	/*
	 * While we're here, check if there's an initial rtt
	 * or rttvar.  Convert from the route-table units
	 * to scaled multiples of the slow timeout timer.
	 */
	if (tp->t_srtt == 0 && (rtt = rt->rt_rmx.rmx_rtt)) {
		if (rt->rt_rmx.rmx_locks & RTV_MTU)
			tp->t_rttmin = rtt / (RTM_RTTUNIT / PR_SLOWHZ);
		tp->t_srtt = rtt / (RTM_RTTUNIT / (PR_SLOWHZ * TCP_RTT_SCALE));
		if (rt->rt_rmx.rmx_rttvar)
			tp->t_rttvar = rt->rt_rmx.rmx_rttvar /
			    (RTM_RTTUNIT / (PR_SLOWHZ * TCP_RTTVAR_SCALE));
		else
			/* default variation is +- 1 rtt */
			tp->t_rttvar =
			    tp->t_srtt * TCP_RTTVAR_SCALE / TCP_RTT_SCALE;
		TCPT_RANGESET(tp->t_rxtcur,
		    ((tp->t_srtt >> 2) + tp->t_rttvar) >> 1,
		    tp->t_rttmin, TCPTV_REXMTMAX);
	}
	/*
	 * if there's an mtu associated with the route, use it
	 */
	if (rt->rt_rmx.rmx_mtu)
		mss = rt->rt_rmx.rmx_mtu - sizeof(struct tcpiphdr);
	else
#endif /* RTV_MTU */
	{
		mss = ifp->if_mtu - sizeof(struct tcpiphdr);

		if (mss > mbconf.mclbytes)
			mss = mss / mbconf.mclbytes * mbconf.mclbytes;

		/* Off-subnet destination: historically clamped to tcp_mssdflt (512) to
		 * avoid fragmentation on unknown remote paths. Default now is auto
		 * (tcp_mssdflt == 0) -> keep the egress MTU-40 mss; only clamp when the
		 * admin sets an explicit cap (>0). We don't set IP_DF, so an over-large
		 * segment fragments rather than black-holes. */
		if (!in_localaddr(inp->inp_faddr) && tcp_mssdflt > 0)
			mss = min(mss, tcp_mssdflt);
	}
	/*
	 * The current mss, t_maxseg, is initialized to the default value.
	 * If we compute a smaller value, reduce the current mss.
	 * If we compute a larger value, return it for use in sending
	 * a max seg size option, but don't store it for use
	 * unless we received an offer at least that large from peer.
	 * However, do not accept offers under 32 bytes.
	 */
	if (offer)
		mss = min(mss, offer);
	mss = max(mss, 32);		/* sanity */
	if (mss < tp->t_maxseg || offer != 0) {
		/*
		 * If there's a pipesize, change the socket buffer
		 * to that size.  Make the socket buffers an integral
		 * number of mss units; if the mss is larger than
		 * the socket buffer, decrease the mss.
		 */
#ifdef RTV_SPIPE
		if ((bufsize = rt->rt_rmx.rmx_sendpipe) == 0)
#endif
			bufsize = so->so_snd.sb_hiwat;
		if (bufsize < mss)
			mss = bufsize;
		else {
			bufsize = min(bufsize, sb_max) / mss * mss;
			(void) sbreserve(&so->so_snd, bufsize);
		}
		tp->t_maxseg = mss;

#ifdef RTV_RPIPE
		if ((bufsize = rt->rt_rmx.rmx_recvpipe) == 0)
#endif
			bufsize = so->so_rcv.sb_hiwat;
		if (bufsize > mss) {
			bufsize = min(bufsize, sb_max) / mss * mss;
			(void) sbreserve(&so->so_rcv, bufsize);
		}
	}
	/*
	 * Initial congestion window. The legacy 4.4BSD value was 1 segment, which
	 * left us (as the SENDER) slow-starting far slower than the IW10 peers we
	 * talk to -- a short upload spent several RTTs ramping before it reached the
	 * link rate, while a download from a modern host (IW10) hit rate almost at
	 * once. Use RFC 6928: min(tcp_iw*MSS, max(2*MSS, 14600)), tcp_iw default 10.
	 * tcp_iw is tunable (config TCP_INITIALWINDOW / roadshowdata "tcp.iw");
	 * 0 or 1 keeps the legacy single-segment behaviour.
	 *
	 * Size cwnd off tp->t_maxseg (the committed segment size), NOT the local
	 * `mss`: on a passive open tcp_mss() runs a second time from tcp_output()
	 * with offer==0, where `mss` is our egress MSS, not the peer's (smaller)
	 * negotiated value -- using it would over-size the initial burst to an
	 * MSS-constrained peer. t_maxseg is already established/preserved above on
	 * every path reaching here, so this is order-independent.
	 */
	{
		extern int tcp_iw;
		int    seg  = tcp_iw;
		u_long smss = (u_long)tp->t_maxseg;
		u_long iw, capw;
		if (seg < 1)   seg = 1;		/* 0/negative -> legacy single segment */
		if (seg > 100) seg = 100;	/* bound the burst; guards the 32-bit multiply below */
		iw   = (u_long)seg * smss;
		capw = max(2UL * smss, 14600UL);	/* RFC 6928 upper bound */
		if (iw > capw)
			iw = capw;
		tp->snd_cwnd = iw;
	}

#ifdef RTV_SSTHRESH
	if (rt->rt_rmx.rmx_ssthresh) {
		/*
		 * There's some sort of gateway or interface
		 * buffer limit on the path.  Use this to set
		 * the slow start threshhold, but set the
		 * threshold to no less than 2*mss.
		 */
		tp->snd_ssthresh = max(2 * mss, rt->rt_rmx.rmx_ssthresh);
	}
#endif /* RTV_MTU */
	return (mss);
}

/*
 * Effective TCP MSS for the interface `ifp`: the same MTU-derived floor and
 * tcp.mssdflt cap tcp_mss() uses -- interface MTU minus the IP+TCP headers, then
 * clamped by an explicit tcp.mssdflt override (0 = no override). Unlike tcp_mss()
 * (which is per-connection) there is no destination here to test, so the cap is
 * applied unconditionally -- i.e. this reports the override-bound MSS, which is
 * exactly what we want a per-interface status display to surface. Exposed so the
 * MSS shown by tools (ShowNetStatus via the QueryInterfaceTagList MSS query) is
 * the stack's own number -- including a user override -- never a tool recompute.
 */
int
ng_iface_mss(struct ifnet *ifp)
{
	extern int tcp_mssdflt;
	int mss;

	if (ifp == 0)
		return (0);
	mss = (int)ifp->if_mtu - (int)sizeof(struct tcpiphdr);
	if (mss < 0)
		mss = 0;
	if (tcp_mssdflt > 0 && tcp_mssdflt < mss)	/* an explicit cap that bites */
		mss = tcp_mssdflt;
	return (mss);
}
