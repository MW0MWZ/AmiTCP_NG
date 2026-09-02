/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: ip_input.c,v 1.15 1993/06/04 11:16:15 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: ip_input.c,v $
 * Revision 1.15  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.14  1993/05/29  21:21:29  jraja
 * Removed all occurances of GATEWAY (now handled by GATEWAY configurable
 * variable), commented out use of if_matrix, since no-one needs it and
 * it may write past its memory.
 *
 * Revision 1.13  1993/05/17  00:16:44  ppessi
 * Changed RCS version. Added rcsid.
 *
 * Revision 1.12  1993/04/24  23:22:04  jraja
 * Removed #ifdef NOALIGN, now using straight structure copies.
 *
 * Revision 1.11  93/04/24  22:51:31  22:51:31  jraja (Jarno Tapio Rajahalme)
 * Removed #ifdef USECLUSTERS (now using clusters always)
 * 
 * Revision 1.10  93/04/05  19:06:07  19:06:07  jraja (Jarno Tapio Rajahalme)
 * Changed storage of the spl functions  return values to type spl_t.
 * Added include for conf.h to every .c file.
 * 
 * Revision 1.9  93/03/22  16:59:13  16:59:13  jraja (Jarno Tapio Rajahalme)
 * Changed bcopy()s and bzero()s with word aligned pointers to
 * aligned_b(copy|zero) ar aligned_b(copy|zero)_const. The latter is for calls
 * in which the size is constant.
 * These can be disabled by defining NOALIGN.
 *  Converted bcopys doing structure copies (on aligned pointers) to structure
 * assignments, since at least SASC produces better code with assignment.
 * 
 * Revision 1.8  93/03/13  17:14:21  17:14:21  ppessi (Pekka Pessi)
 * Fixed bugs with variable initialization.
 * 
 * Revision 1.7  93/03/05  03:20:06  03:20:06  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.6  93/03/04  12:14:13  12:14:13  jraja (Jarno Tapio Rajahalme)
 * Added casts to printfs.
 * 
 * Revision 1.5  93/03/03  21:39:50  21:39:50  jraja (Jarno Tapio Rajahalme)
 * Moved some data definitions from ip_var.h to here.
 * 
 * Revision 1.4  93/03/03  20:17:45  20:17:45  jraja (Jarno Tapio Rajahalme)
 * Added include for kern/uipc_domain_protos.h.
 * 
 * Revision 1.3  93/03/02  18:30:39  18:30:39  too (Tomi Ollila)
 * Changed %? to %l? on format strings
 * 
 * Revision 1.2  93/02/26  09:03:21  09:03:21  jraja (Jarno Tapio Rajahalme)
 * Made this compile with ANSI C (added prototypes).
 * chaged malloc to bsd_malloc on function ip_init().
 * Added in_addr argument with phony address to icmp_error() call in 
 * function ip_dooptions (it is not used by icmp_error in this case).
 * Added initialization of variable 'code' in function ip_forward, since it
 * might have been used withou initialization.
 * Commented out mbuf cluster dependant code, since clusters are not 
 * implemented (yet).
 * 
 * Revision 1.1  92/11/17  16:29:43  16:29:43  jraja (Jarno Tapio Rajahalme)
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
 *	@(#)ip_input.c	7.19 (Berkeley) 5/25/91
 */

/*
 * ip_input.c --- IPv4 receive path (RFC 791). Stock 4.4BSD.
 *
 * Everything arriving from an interface as IP funnels through here. The interface
 * input routine queues the packet on the IP input queue and schedules a software
 * interrupt (schednetisr(NETISR_IP)); ipintr()/ip_input() then:
 *   1. sanity-check the header (version, length, checksum -- in_cksum.c);
 *   2. if we are a gateway and the packet isn't for us, forward it (ip_forward);
 *   3. run any options; reassemble fragments (ip_reass) until a datagram is whole;
 *   4. dispatch on the protocol field to pr_input -- udp_input, tcp_input,
 *      icmp's input, or rip_input for raw sockets.
 * ip_reass()/the fragment queue and the reassembly timeout (driven by amiga_time.c)
 * are the subtle part. See TCP/IP Illustrated Vol 2 chapter 10.
 *
 * docs/ARCHITECTURE.md section 7 (inbound path). This is step 2 of "follow a
 * packet": the interface handed us an mbuf chain; we validate and route it up.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>	/* LOG_WARNING for the in_ifaddr backstop */
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/synch.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>

#include <netinet/ip_input_protos.h>
#include <netinet/ip_output_protos.h>
#include <netinet/ip_icmp_protos.h>
#include <netinet/in_cksum_protos.h>
#include <netinet/in_protos.h>
#include <kern/uipc_domain_protos.h>

#ifndef	IPFORWARDING
#define	IPFORWARDING	0
#endif
#ifndef	IPSENDREDIRECTS
#define	IPSENDREDIRECTS	1
#endif
#ifndef IPPRINTFS
#define IPPRINTFS 0
#endif

/* These three are now accessed from the configuration module
 */
int	ipforwarding = IPFORWARDING;
int	ipsendredirects = IPSENDREDIRECTS;
int	ipprintfs = IPPRINTFS;		/* this has effect only if DIAGNOSTIC */

/*
 * PORT (AmiTCP_NG): SBTC_IP_DEFAULT_TTL (44). Roadshow exposes ONE default TTL
 * for the whole stack; 4.4BSD instead keeps tcp_ttl and udp_ttl separately and
 * hard-codes MAXTTL in the raw and ICMP paths. Setting the tag therefore mirrors
 * this value into tcp_ttl and udp_ttl as well (api/amiga_generic2.c) -- a knob
 * that moved only the raw-socket TTL would be honest to BSD and useless to the
 * caller, who means "the TTL my packets go out with".
 */
int	ip_defttl = MAXTTL;

struct	ipstat	ipstat = {0};	/* ip statistics */
/* Guard the size GetNetworkStatistics reports (NG_STAT_IP_OUR = 80). Adding a
 * field here without updating that constant truncates the tail silently -- and
 * worse, publishes our new field at an offset a Roadshow-compatible caller
 * reads as one of THEIRS. tcpstat and udpstat already had this guard; ipstat
 * was the one that did not. */
typedef char ng_ipstat_size_matches_NG_STAT_IP_OUR
	[(sizeof(struct ipstat) == 80) ? 1 : -1];
struct	ipq	ipq = {0};	/* ip reass. queue */
u_short	ip_id = {0};		/* ip packet ctr, for ids */
struct	ifqueue	ipintrq = {0};	/* ip packet input queue */

extern	struct domain inetdomain;
extern	struct protosw inetsw[];
u_char	ip_protox[IPPROTO_MAX] = {0};
int	ipqmaxlen = IFQ_MAXLEN;
struct	in_ifaddr *in_ifaddr  = NULL; /* first inet address */

/*
 * We need to save the IP options in case a protocol wants to respond
 * to an incoming packet over the same route if the packet got here
 * using IP source routing.  This allows connection establishment and
 * maintenance when the remote end is on a network that is not known
 * to us.
 */
int	ip_nhops = 0;
static	struct ip_srcrt {
	struct	in_addr dst;			/* final destination */
	char	nop;				/* one NOP to align */
	char	srcopt[IPOPT_OFFSET + 1];	/* OPTVAL, OLEN and OFFSET */
	struct	in_addr route[MAX_IPOPTLEN/sizeof(struct in_addr)];
} ip_srcrt;

#if USE_IF_MATRIX
extern	int if_index;
u_long	*ip_ifmatrix = NULL;
#endif

/*
 * IP initialization: fill in IP protocol switch table.
 * All protocols not implemented in kernel go to raw IP protocol handler.
 */
extern int ip_nfrags;		/* defined below; reset by ip_init() on every stack start */

void
ip_init()
{
	register struct protosw *pr;
	register int i;

	pr = pffindproto(PF_INET, IPPROTO_RAW, SOCK_RAW);
	if (pr == 0)
		panic("ip_init");
	for (i = 0; i < IPPROTO_MAX; i++)
		ip_protox[i] = pr - inetsw;
	for (pr = inetdomain.dom_protosw;
	    pr < inetdomain.dom_protoswNPROTOSW; pr++)
		if (pr->pr_domain->dom_family == PF_INET &&
		    pr->pr_protocol && pr->pr_protocol != IPPROTO_RAW)
			ip_protox[pr->pr_protocol] = pr - inetsw;
	/*
	 * PORT (AmiTCP_NG): drop the interface-address list on every stack start.
	 *
	 * in_ifaddr is a load-time global, and its entries are MBUFS (in_control()
	 * allocates them from the pool). A stack teardown frees that pool wholesale,
	 * so every entry becomes dead memory -- but the head still points at the
	 * first one, and nothing reset it, so the next stack inherited a list of
	 * dangling pointers.
	 *
	 * sana_deinit() now scrubs each interface's addresses on the way down
	 * (sana_scrub_inet(), the same call sana_remove_interface() makes), so in
	 * normal operation this head is already empty by the time we get here.
	 *
	 * This stays as a BACKSTOP for the paths that never reach that teardown --
	 * a stack that is killed rather than stopped, or any future exit that
	 * forgets. Resetting the head cannot leak: these entries are mbufs, and the
	 * pool they came from is freed wholesale at teardown.
	 */
	{ extern struct in_ifaddr *in_ifaddr;
	  /*
	   * SAY SO when this actually finds something. A silent backstop is how the
	   * loopback gap hid: sana_deinit() scrubs every interface's addresses now,
	   * so a non-empty head here means some interface was NOT scrubbed on the way
	   * down, and the next thing to touch it walks freed pool memory. Clearing it
	   * without a word turns a teardown bug into a permanently invisible one.
	   */
	  if (in_ifaddr != NULL)
	    log(LOG_WARNING, "ip_init: in_ifaddr was not empty at stack start -- "
		"an interface was not scrubbed during teardown\n");
	  in_ifaddr = NULL; }
	ipq.next = ipq.prev = &ipq;
	/*
	 * PORT (AmiTCP_NG): reset the companion counter with the list it counts.
	 *
	 * ip_nfrags is the live-reassembly-queue count behind the IP_MAXFRAGPACKETS
	 * DoS cap in ip_reass(). It is only ever decremented by ip_freef(), and the
	 * wholesale pool teardown does not call that -- so a forced shutdown taken
	 * while n reassemblies were in flight leaves ip_nfrags == n while this reset
	 * empties the list. The restarted stack then counts up from that phantom
	 * baseline and starts evicting legitimate fragmented datagrams after 8-n
	 * concurrent reassemblies instead of 8, until enough completions or 30s
	 * timeouts drain it back down. Emptying the list without zeroing its counter
	 * is only half a reset.
	 */
	ip_nfrags = 0;
        {
	    struct timeval time;
	    get_time(&time);
	    ip_id = time.tv_sec & 0xffff;
	}
	ipintrq.ifq_maxlen = ipqmaxlen;

#if USE_IF_MATRIX
	This does not work, since if_index is not constant, but actually
	increases after this is done! So change this before enabling
	(if_index is defined in net/if.c)

	i = (if_index + 1) * (if_index + 1) * sizeof (u_long);
	if ((ip_ifmatrix = (u_long *) bsd_malloc(i, M_RTABLE, M_WAITOK)) == 0)
		panic("no memory for ip_ifmatrix");
#endif
}

struct	ip *ip_reass();

/*
 * PORT (AmiTCP_NG) hardening: bound the number of concurrent IP fragment
 * reassembly queues. The original stack only reaped a queue on its ~30-second
 * timeout, with no limit on how many could exist at once. An attacker sending
 * a stream of first-fragments that never complete (each a distinct ip_id/src)
 * pins one MT_FTABLE mbuf per queue out of the shared, FIXED-size pool for the
 * whole timeout window -- exhausting mbufs for every socket and driver on the
 * machine, not merely degrading one service. This is a single-user host rather
 * than a router, so a small cap is ample for legitimate traffic; when it is hit
 * we evict the OLDEST queue (ipq.prev, the tail -- nearest to timing out anyway)
 * to admit the newest arrival. ip_nfrags is kept in sync in ip_freef().
 */
#define IP_MAXFRAGPACKETS 8
/*
 * The IP_MAXFRAGPACKETS cap bounds the number of distinct in-flight datagrams,
 * but not the fragments WITHIN one datagram: a single ip_id fed a stream of
 * tiny, non-overlapping, never-completing fragments would grow one queue
 * without bound (min 8-byte frag granularity => up to ~8192 mbufs for one
 * 64KB datagram). Cap the per-datagram fragment count too. 64 covers a full
 * 65535-byte datagram fragmented at any MTU at/above ~1100 bytes (Ethernet's
 * 1500 needs ~45); only a near-maximal datagram over an unusually small path
 * MTU would exceed it, and then it simply times out (ipq_ttl) and the peer
 * retransmits. An all-tiny-fragment flood is bounded to IP_MAXFRAGPACKETS*64
 * mbufs.
 */
#define IP_MAXFRAGSPERPACKET 64
int	ip_nfrags = 0;			/* current number of reassembly queues */

struct	sockaddr_in ipaddr = { sizeof(ipaddr), AF_INET };
struct	route ipforward_rt = { 0 };

/*
 * Ip input routine.  Checksum and byte swap header.  If fragmented
 * try to reassemble.  Process options.  Pass to next level.
 */
void
ipintr()
{
	register struct ip *ip;
	register struct mbuf *m;
	register struct ipq *fp;
	register struct in_ifaddr *ia;
	int hlen;
	spl_t s;

next:
	/*
	 * Get next datagram off input queue and get IP header
	 * in first mbuf.
	 */
	s = splimp();
	IF_DEQUEUE(&ipintrq, m);
	splx(s);
	if (m == 0)
		return;
#if	DIAGNOSTIC
	if ((m->m_flags & M_PKTHDR) == 0)
		panic("ipintr no HDR");
#endif
	/*
	 * If no IP addresses have been set yet but the interfaces
	 * are receiving, can't do anything with incoming packets yet.
	 */
	if (in_ifaddr == NULL)
		goto bad;
	ipstat.ips_total++;
	if (m->m_len < sizeof (struct ip) &&
	    (m = m_pullup(m, sizeof (struct ip))) == 0) {
		ipstat.ips_toosmall++;
		goto next;
	}
	ip = mtod(m, struct ip *);
	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(struct ip)) {	/* minimum header length */
		ipstat.ips_badhlen++;
		goto bad;
	}
	if (hlen > m->m_len) {
		if ((m = m_pullup(m, hlen)) == 0) {
			ipstat.ips_badhlen++;
			goto next;
		}
		ip = mtod(m, struct ip *);
	}
	if ((ip->ip_sum = in_cksum(m, hlen))) {
		ipstat.ips_badsum++;
		goto bad;
	}

	/*
	 * Convert fields to host representation.
	 */
	(void)NTOHS(ip->ip_len);
	if (ip->ip_len < hlen) {
		ipstat.ips_badlen++;
		goto bad;
	}
	(void)NTOHS(ip->ip_id);
	(void)NTOHS(ip->ip_off);

	/*
	 * Check that the amount of data in the buffers
	 * is as at least much as the IP header would have us expect.
	 * Trim mbufs if longer than we expect.
	 * Drop packet if shorter than we expect.
	 */
	if (m->m_pkthdr.len < ip->ip_len) {
		ipstat.ips_tooshort++;
		goto bad;
	}
	if (m->m_pkthdr.len > ip->ip_len) {
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = ip->ip_len;
			m->m_pkthdr.len = ip->ip_len;
		} else
			m_adj(m, ip->ip_len - m->m_pkthdr.len);
	}

	/*
	 * Process options and, if not destined for us,
	 * ship it on.  ip_dooptions returns 1 when an
	 * error was detected (causing an icmp message
	 * to be sent and the original packet to be freed).
	 */
	ip_nhops = 0;		/* for source routed packets */
	if (hlen > sizeof (struct ip) && ip_dooptions(m))
		goto next;

	/*
	 * Check our list of addresses, to see if the packet is for us.
	 */
	for (ia = in_ifaddr; ia; ia = ia->ia_next) {
#define	satosin(sa)	((struct sockaddr_in *)(sa))

		if (IA_SIN(ia)->sin_addr.s_addr == ip->ip_dst.s_addr)
			goto ours;
		/*
		 * PORT (AmiTCP_NG): DHCP bootstrap reception. An interface that has
		 * not been assigned an address yet (0.0.0.0, e.g. mid-DHCP) still
		 * receives frames unicast to its hardware address. Deliver those UDP
		 * datagrams locally so a DHCP client socket can see a server's unicast
		 * OFFER/ACK -- some servers (incl. the SLIRP one) ignore the BOOTP
		 * broadcast flag and reply to the address being offered, which is not
		 * yet ours. Tightly scoped: only the receiving unnumbered interface,
		 * only L2-unicast (not broadcast/multicast) UDP. Inert once addressed.
		 */
		if (ia->ia_ifp == m->m_pkthdr.rcvif &&
		    IA_SIN(ia)->sin_addr.s_addr == INADDR_ANY &&
		    (m->m_flags & (M_BCAST | M_MCAST)) == 0 &&
		    ip->ip_p == IPPROTO_UDP)
			goto ours;
		if (
#ifdef	DIRECTED_BROADCAST
		    ia->ia_ifp == m->m_pkthdr.rcvif &&
#endif
		    (ia->ia_ifp->if_flags & IFF_BROADCAST)) {
			u_long t;

			if (satosin(&ia->ia_broadaddr)->sin_addr.s_addr ==
			    ip->ip_dst.s_addr)
				goto ours;
			if (ip->ip_dst.s_addr == ia->ia_netbroadcast.s_addr)
				goto ours;
			/*
			 * Look for all-0's host part (old broadcast addr),
			 * either for subnet or net.
			 */
			t = ntohl(ip->ip_dst.s_addr);
			if (t == ia->ia_subnet)
				goto ours;
			if (t == ia->ia_net)
				goto ours;
		}
	}
	if (ip->ip_dst.s_addr == (u_long)INADDR_BROADCAST)
		goto ours;
	if (ip->ip_dst.s_addr == INADDR_ANY)
		goto ours;

	/*
	 * Not for us; forward if possible and desirable.
	 */
	if (ipforwarding == 0) {
		ipstat.ips_cantforward++;
		m_freem(m);
	} else
		ip_forward(m, 0);
	goto next;

ours:
	/*
	 * If offset or IP_MF are set, must reassemble.
	 * Otherwise, nothing need be done.
	 * (We could look in the reassembly queue to see
	 * if the packet was previously fragmented,
	 * but it's not worth the time; just let them time out.)
	 */
	if (ip->ip_off &~ IP_DF) {
		if (m->m_flags & M_EXT) {		/* XXX */
			if ((m = m_pullup(m, sizeof (struct ip))) == 0) {
				ipstat.ips_toosmall++;
				goto next;
			}
			ip = mtod(m, struct ip *);
		}
		/*
		 * Look for queue of fragments
		 * of this datagram.
		 */
		for (fp = ipq.next; fp != &ipq; fp = fp->next)
			if (ip->ip_id == fp->ipq_id &&
			    ip->ip_src.s_addr == fp->ipq_src.s_addr &&
			    ip->ip_dst.s_addr == fp->ipq_dst.s_addr &&
			    ip->ip_p == fp->ipq_p)
				goto found;
		fp = 0;
found:

		/*
		 * Adjust ip_len to not reflect header,
		 * set ip_mff if more fragments are expected,
		 * convert offset of this to bytes.
		 */
		ip->ip_len -= hlen;
		((struct ipasfrag *)ip)->ipf_mff = 0;
		if (ip->ip_off & IP_MF)
			((struct ipasfrag *)ip)->ipf_mff = 1;
		ip->ip_off <<= 3;

		/*
		 * If datagram marked as having more fragments
		 * or if this is not the first fragment,
		 * attempt reassembly; if it succeeds, proceed.
		 */
		if (((struct ipasfrag *)ip)->ipf_mff || ip->ip_off) {
			ipstat.ips_fragments++;
			ip = ip_reass((struct ipasfrag *)ip, fp);
			if (ip == 0)
				goto next;
			else
				ipstat.ips_reassembled++;
			m = dtom(ip);
		} else
			if (fp)
				ip_freef(fp);
	} else
		ip->ip_len -= hlen;

	/*
	 * Switch out to protocol's input routine.
	 */
	ipstat.ips_delivered++;
	(*inetsw[ip_protox[ip->ip_p]].pr_input)(m, hlen);
	goto next;
bad:
	m_freem(m);
	goto next;
}

/*
 * Take incoming datagram fragment and try to
 * reassemble it into whole datagram.  If a chain for
 * reassembly of this datagram already exists, then it
 * is given as fp; otherwise have to make a chain.
 */
struct ip *
ip_reass(ip, fp)
	register struct ipasfrag *ip;
	register struct ipq *fp;
{
	register struct mbuf *m = dtom(ip);
	register struct ipasfrag *q;
	struct mbuf *t;
	int hlen = ip->ip_hl << 2;
	int i, next;

	/*
	 * Presence of header sizes in mbufs
	 * would confuse code below.
	 */
	m->m_data += hlen;
	m->m_len -= hlen;

	/*
	 * If first fragment to arrive, create a reassembly queue.
	 */
	if (fp == 0) {
		/*
		 * PORT (AmiTCP_NG) hardening: enforce the reassembly-queue cap before
		 * creating another queue. Evict the oldest incomplete datagram to make
		 * room rather than growing without bound. ip_freef() decrements
		 * ip_nfrags, so the counter stays correct across this and every other
		 * removal path (completion and timeout).
		 */
		if (ip_nfrags >= IP_MAXFRAGPACKETS && ipq.prev != &ipq)
			ip_freef(ipq.prev);
		if ((t = m_get(M_DONTWAIT, MT_FTABLE)) == NULL)
			goto dropfrag;
		fp = mtod(t, struct ipq *);
		insque(fp, &ipq);
		ip_nfrags++;
		fp->ipq_ttl = IPFRAGTTL;
		fp->ipq_p = ip->ip_p;
		fp->ipq_id = ip->ip_id;
		fp->ipq_next = fp->ipq_prev = (struct ipasfrag *)fp;
		fp->ipq_src = ((struct ip *)ip)->ip_src;
		fp->ipq_dst = ((struct ip *)ip)->ip_dst;
		fp->ipq_nfrags = 0;
		q = (struct ipasfrag *)fp;
		goto insert;
	}

	/*
	 * PORT (AmiTCP_NG) security fix: ip_off and ip_len are declared `short`
	 * (see the note in netinet/ip.h), so a fragment offset or length above
	 * 32767 -- reachable once ip_off has been shifted left by 3 -- sign-extends
	 * to a negative int in the overlap arithmetic below, corrupting the trim/
	 * dequeue decisions (and, with a crafted set, the reassembled contents).
	 * Force every offset/length into its unsigned 0..65535 range with (u_short)
	 * casts. Compounds the IP_MAXPACKET total-length bound added above.
	 */
	/*
	 * Find a segment which begins after this one does.
	 */
	for (q = fp->ipq_next; q != (struct ipasfrag *)fp; q = q->ipf_next)
		if ((u_short)q->ip_off > (u_short)ip->ip_off)
			break;

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us.
	 */
	if (q->ipf_prev != (struct ipasfrag *)fp) {
		i = (u_short)q->ipf_prev->ip_off + (u_short)q->ipf_prev->ip_len
		    - (u_short)ip->ip_off;
		if (i > 0) {
			if (i >= (u_short)ip->ip_len)
				goto dropfrag;
			m_adj(dtom(ip), i);
			ip->ip_off += i;
			ip->ip_len -= i;
		}
	}

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	while (q != (struct ipasfrag *)fp &&
	    (u_short)ip->ip_off + (u_short)ip->ip_len > (u_short)q->ip_off) {
		i = ((u_short)ip->ip_off + (u_short)ip->ip_len) - (u_short)q->ip_off;
		if (i < (u_short)q->ip_len) {
			q->ip_len -= i;
			q->ip_off += i;
			m_adj(dtom(q), i);
			break;
		}
		{
			/*
			 * Drop the fully-covered fragment. ip_deq() reads this
			 * node's own ipf_prev/ipf_next to splice it out of the
			 * list, so it MUST run BEFORE m_freem() frees that mbuf.
			 * The original order (free, then ip_deq) was a read-after-
			 * free -- benign today only because m_free() leaves the
			 * overlaid IP header intact and splnet blocks reallocation,
			 * but a genuine use-after-free if the allocator ever scrubs
			 * or asynchronously frees payload.
			 */
			struct ipasfrag *p = q;
			q = q->ipf_next;
			ip_deq(p);
			fp->ipq_nfrags--;	/* dequeued a fully-covered frag */
			m_freem(dtom(p));
		}
	}

insert:
	/*
	 * PORT (AmiTCP_NG) hardening: reject once this datagram already holds
	 * the per-packet fragment cap. Bounds a single-ip_id tiny-fragment
	 * flood that IP_MAXFRAGPACKETS (a per-queue count, not per-fragment)
	 * does not cover. The partial reassembly is left intact and times out
	 * normally via ipq_ttl; the peer retransmits if it was legitimate.
	 */
	if (fp->ipq_nfrags >= IP_MAXFRAGSPERPACKET)
		goto dropfrag;

	/*
	 * Stick new segment in its place;
	 * check for complete reassembly.
	 */
	ip_enq(ip, q->ipf_prev);
	fp->ipq_nfrags++;
	next = 0;
	/*
	 * PORT (AmiTCP_NG) fix: compare as u_short. ip_off is a SIGNED 16-bit field,
	 * so any fragment at offset >= 32768 sign-extends negative and can never
	 * equal the positive `next`, making a fully-arrived datagram look forever
	 * incomplete -- it would then sit queued until ipq_ttl expiry instead of
	 * being recognised (and, per the bound below, cleanly rejected).
	 */
	for (q = fp->ipq_next; q != (struct ipasfrag *)fp; q = q->ipf_next) {
		if ((u_short)q->ip_off != (u_short)next)
			return (0);
		next += (u_short)q->ip_len;
	}
	if (q->ipf_prev->ipf_mff)
		return (0);

	/*
	 * PORT (AmiTCP_NG) security fix: bound the reassembled datagram before it
	 * is committed to ip_len, which is a signed 16-bit field. A fragment set
	 * whose lengths sum past IP_MAXPACKET (65535) wraps it -- the classic "ping
	 * of death": downstream code then trusts a small or negative length while a
	 * large tail rides along in the mbuf chain. Discard the whole reassembly.
	 *
	 * The bound is 32767, not IP_MAXPACKET (65535), precisely BECAUSE the field
	 * is signed: 32768..65535 is legal per RFC 791 but sign-extends negative on
	 * store, and every downstream consumer then mis-handles it (udp_input's
	 * `len > ip->ip_len` badlen test, for one, is always true against a negative
	 * ip_len). Such datagrams are therefore rejected here -- explicitly, with a
	 * stat bump -- rather than committed as a negative length. Accepting them
	 * for real would mean widening ip_len across the whole struct ip ABI.
	 */
	if (next > 32767) {
		ip_freef(fp);
		ipstat.ips_fragdropped++;
		return (0);
	}

	/*
	 * Reassembly is complete; concatenate fragments.
	 */
	q = fp->ipq_next;
	m = dtom(q);
	t = m->m_next;
	m->m_next = 0;
	m_cat(m, t);
	q = q->ipf_next;
	while (q != (struct ipasfrag *)fp) {
		t = dtom(q);
		q = q->ipf_next;
		m_cat(m, t);
	}

	/*
	 * Create header for new ip packet by
	 * modifying header of first packet;
	 * dequeue and discard fragment reassembly header.
	 * Make header visible.
	 */
	ip = fp->ipq_next;
	ip->ip_len = next;
	((struct ip *)ip)->ip_src = fp->ipq_src;
	((struct ip *)ip)->ip_dst = fp->ipq_dst;
	remque(fp);
	(void) m_free(dtom(fp));
	m = dtom(ip);
	m->m_len += (ip->ip_hl << 2);
	m->m_data -= (ip->ip_hl << 2);
	/* some debugging cruft by sklower, below, will go away soon */
	if (m->m_flags & M_PKTHDR) { /* XXX this should be done elsewhere */
		register int plen = 0;
		for (t = m; m; m = m->m_next)
			plen += m->m_len;
		t->m_pkthdr.len = plen;
	}
	return ((struct ip *)ip);

dropfrag:
	ipstat.ips_fragdropped++;
	m_freem(m);
	return (0);
}

/*
 * Free a fragment reassembly header and all
 * associated datagrams.
 */
void
ip_freef(fp)
	struct ipq *fp;
{
	register struct ipasfrag *q, *p;

	for (q = fp->ipq_next; q != (struct ipasfrag *)fp; q = p) {
		p = q->ipf_next;
		ip_deq(q);
		m_freem(dtom(q));
	}
	remque(fp);
	(void) m_free(dtom(fp));
	/* PORT (AmiTCP_NG) hardening: keep the reassembly-queue counter in sync.
	 * This is the single choke point for queue removal (eviction, completion
	 * and timeout all route through here). */
	if (ip_nfrags > 0)
		ip_nfrags--;
}

/*
 * Put an ip fragment on a reassembly chain.
 * Like insque, but pointers in middle of structure.
 */
void
ip_enq(p, prev)
	register struct ipasfrag *p, *prev;
{

	p->ipf_prev = prev;
	p->ipf_next = prev->ipf_next;
	prev->ipf_next->ipf_prev = p;
	prev->ipf_next = p;
}

/*
 * To ip_enq as remque is to insque.
 */
void
ip_deq(p)
	register struct ipasfrag *p;
{

	p->ipf_prev->ipf_next = p->ipf_next;
	p->ipf_next->ipf_prev = p->ipf_prev;
}

/*
 * IP timer processing;
 * if a timer expires on a reassembly
 * queue, discard it.
 */
void
ip_slowtimo()
{
	register struct ipq *fp;
	spl_t s = splnet();

	fp = ipq.next;
	if (fp == 0) {
		splx(s);
		return;
	}
	/*
	 * LAST-RESORT TRAP, same reasoning as in_pcblookup(): this walk runs under
	 * splnet() == Forbid(), and it is driven by a TIMER, so a corrupt ipq does
	 * not wait for anyone to call an API -- it freezes the machine within one
	 * slowtimo tick of the stack coming up. The NULL check matters because the
	 * loop test is `fp != &ipq`, which NULL passes; the existing fp == 0 test
	 * above only covers the FIRST element, not one reached mid-walk.
	 *
	 * ip_init() now re-runs on every stack start (see domaininit()), so ipq can
	 * no longer be inherited stale from a torn-down stack. This is the trap.
	 */
	{
		int ng_walk = 0;

		while (fp != &ipq) {
			if (fp == 0 || ++ng_walk > 2000)
				break;
			--fp->ipq_ttl;
			fp = fp->next;
			if (fp == 0)
				break;
			if (fp->prev->ipq_ttl == 0) {
				ipstat.ips_fragtimeout++;
				ip_freef(fp->prev);
			}
		}
	}
	splx(s);
}

/*
 * Drain off all datagram fragments.
 */
void
ip_drain()
{

	while (ipq.next != &ipq) {
		ipstat.ips_fragdropped++;
		ip_freef(ipq.next);
	}
}

extern struct in_ifaddr *ifptoia();
struct in_ifaddr *ip_rtaddr();

/*
 * Do option processing on a datagram,
 * possibly discarding it if bad options are encountered,
 * or forwarding it if source-routed.
 * Returns 1 if packet has been forwarded/freed,
 * 0 if the packet should be processed further.
 */
int
ip_dooptions(m)
	struct mbuf *m;
{
	register struct ip *ip = mtod(m, struct ip *);
	register u_char *cp;
	register struct ip_timestamp *ipt;
	register struct in_ifaddr *ia;
	int opt, optlen, cnt, off, code, type = ICMP_PARAMPROB, forward = 0;
	/* PORT (AmiTCP_NG): dest is passed to icmp_error() below on the bad-option
	 * paths but is only assigned on the source-route/redirect branches. Stock
	 * BSD leaves it uninitialised (icmp_error ignores it except for REDIRECT, so
	 * it is harmless today) -- zero it so no stale stack value can ever leak. */
	struct in_addr *sin, dest = { 0 };
	n_time ntime;
	
	cp = (u_char *)(ip + 1);
	cnt = (ip->ip_hl << 2) - sizeof (struct ip);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			optlen = cp[IPOPT_OLEN];
			if (optlen <= 0 || optlen > cnt) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
		}
		switch (opt) {

		default:
			break;

		/*
		 * Source routing with record.
		 * Find interface with current destination address.
		 * If none on this machine then drop if strictly routed,
		 * or do nothing if loosely routed.
		 * Record interface address and bring up next address
		 * component.  If strictly routed make sure next
		 * address is on directly accessible net.
		 */
		case IPOPT_LSRR:
		case IPOPT_SSRR:
			/*
			 * PORT (AmiTCP_NG) security fix: the option must be long
			 * enough to contain the pointer byte we are about to read.
			 * The generic check above only enforces optlen <= cnt.
			 */
			if (optlen < IPOPT_OFFSET + 1) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			ipaddr.sin_addr = ip->ip_dst;
			ia = (struct in_ifaddr *)
				ifa_ifwithaddr((struct sockaddr *)&ipaddr);
			if (ia == 0) {
				if (opt == IPOPT_SSRR) {
					type = ICMP_UNREACH;
					code = ICMP_UNREACH_SRCFAIL;
					goto bad;
				}
				/*
				 * Loose routing, and not at next destination
				 * yet; nothing to do except forward.
				 */
				break;
			}
			off--;			/* 0 origin */
			/*
			 * PORT (AmiTCP_NG) security fix: cast the sizeof to int.
			 * optlen is int but sizeof is size_t, so the subtraction was
			 * evaluated UNSIGNED: for optlen < 4 it wrapped to ~0xFFFFFFFF
			 * and this test was false for every possible off, letting an
			 * attacker-chosen off (up to 254) reach the bcopy below and
			 * read far past the option -- and past the mbuf, which on this
			 * no-MMU target does not trap. Keeping it signed makes a short
			 * option take the end-of-route branch, as intended.
			 */
			if (off > optlen - (int)sizeof(struct in_addr)) {
				/*
				 * End of source route.  Should be for us.
				 */
				save_rte(cp, ip->ip_src);
				break;
			}
			/*
			 * locate outgoing interface
			 */
			bcopy((caddr_t)(cp + off), (caddr_t)&ipaddr.sin_addr,
			    sizeof(ipaddr.sin_addr));
			if (opt == IPOPT_SSRR) {
#define	INA	struct in_ifaddr *
#define	SA	struct sockaddr *
			    if ((ia = (INA)ifa_ifwithdstaddr((SA)&ipaddr)) == 0)
				ia = in_iaonnetof(in_netof(ipaddr.sin_addr));
			} else
				ia = ip_rtaddr(ipaddr.sin_addr);
			if (ia == 0) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_SRCFAIL;
				goto bad;
			}
			ip->ip_dst = ipaddr.sin_addr;
			bcopy((caddr_t)&(IA_SIN(ia)->sin_addr),
			    (caddr_t)(cp + off), sizeof(struct in_addr));
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			forward = 1;
			break;

		case IPOPT_RR:
			/* PORT (AmiTCP_NG) security fix: see IPOPT_LSRR above -- the
			 * option must be long enough to hold the pointer byte. */
			if (optlen < IPOPT_OFFSET + 1) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			/*
			 * If no space remains, ignore.
			 */
			off--;			/* 0 origin */
			/*
			 * PORT (AmiTCP_NG) security fix: cast the sizeof to int. As in
			 * the LSRR/SSRR case above this comparison was unsigned, so for
			 * optlen < 4 it was false for every off and execution fell
			 * through to the bcopy below -- a 4-byte WRITE at cp + off with
			 * off attacker-chosen up to 254, landing outside the mbuf.
			 */
			if (off > optlen - (int)sizeof(struct in_addr))
				break;
			ipaddr.sin_addr = ip->ip_dst;
			/*
			 * locate outgoing interface; if we're the destination,
			 * use the incoming interface (should be same).
			 */
			if ((ia = (INA)ifa_ifwithaddr((SA)&ipaddr)) == 0 &&
			    (ia = ip_rtaddr(ipaddr.sin_addr)) == 0) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_HOST;
				goto bad;
			}
			bcopy((caddr_t)&(IA_SIN(ia)->sin_addr),
			    (caddr_t)(cp + off), sizeof(struct in_addr));
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			break;

		case IPOPT_TS:
			code = cp - (u_char *)ip;
			ipt = (struct ip_timestamp *)cp;
			if (ipt->ipt_len < 5)
				goto bad;
			/*
			 * PORT (AmiTCP_NG) fix: bound ipt_ptr BELOW as well as
			 * above. RR and LSRR/SSRR both reject an offset under
			 * IPOPT_MINOFF a few hundred lines up; the timestamp
			 * option only ever checked the upper end, and every
			 * pointer derived from it is `cp + ipt_ptr - 1`.
			 *
			 * With the minimum accepted ipt_len of 5, ipt_ptr == 0
			 * passes the upper test (0 > 5-4 is false) and that
			 * expression becomes cp - 1 -- one byte BEFORE the
			 * option. When the timestamp is the first option, which
			 * is the ordinary case, cp is ip+1, so the four-byte
			 * timestamp store lands on the last octet of ip_dst.
			 * ip_dooptions() runs before ipintr() decides whether
			 * the packet is addressed to us, so a remote sender
			 * could rewrite part of the destination address the
			 * delivery test then reads. Attacker-chosen input, no
			 * handshake or privilege needed, and nothing in normal
			 * traffic ever sets ipt_ptr to 0 -- so it would never
			 * have shown up in testing.
			 *
			 * IPOPT_MINOFF is the same floor the sibling options
			 * use; for a timestamp the first slot starts at offset
			 * 5, so anything below that is malformed.
			 */
			if (ipt->ipt_ptr < 5)
				goto bad;
			if (ipt->ipt_ptr > ipt->ipt_len - sizeof (long)) {
				if (++ipt->ipt_oflw == 0)
					goto bad;
				break;
			}
			sin = (struct in_addr *)(cp + ipt->ipt_ptr - 1);
			switch (ipt->ipt_flg) {

			case IPOPT_TS_TSONLY:
				break;

			case IPOPT_TS_TSANDADDR:
				if (ipt->ipt_ptr + sizeof(n_time) +
				    sizeof(struct in_addr) > ipt->ipt_len)
					goto bad;
				ia = ifptoia(m->m_pkthdr.rcvif);
				if (ia == 0)
					continue;	/* interface carries no address
							 * (e.g. brought UP with no
							 * ADDRESS=): IA_SIN(0) would
							 * splice low memory into the
							 * option and echo it back. Skip
							 * the whole option, as stock
							 * 4.4BSD and the PRESPEC case
							 * below do -- NOT `break`, which
							 * would fall through and stamp a
							 * timestamp into the slot
							 * reserved for addr+ts. */
				/* sin = cp+ipt_ptr-1 may be ODD (attacker-controlled);
				 * a struct store here is an Address Error trap on
				 * 68000/010. bcopy (CopyMem) is alignment-safe, as the
				 * RR/LSRR handlers above already use. */
				bcopy((caddr_t)&(IA_SIN(ia)->sin_addr), (caddr_t)sin,
				    sizeof(struct in_addr));
				ipt->ipt_ptr += sizeof(struct in_addr);
				break;

			case IPOPT_TS_PRESPEC:
				if (ipt->ipt_ptr + sizeof(n_time) +
				    sizeof(struct in_addr) > ipt->ipt_len)
					goto bad;
				bcopy((caddr_t)sin, (caddr_t)&ipaddr.sin_addr,
				    sizeof(struct in_addr));	/* sin may be odd -- see above */
				if (ifa_ifwithaddr((SA)&ipaddr) == 0)
					continue;
				ipt->ipt_ptr += sizeof(struct in_addr);
				break;

			default:
				goto bad;
			}
			ntime = iptime();
			bcopy((caddr_t)&ntime, (caddr_t)cp + ipt->ipt_ptr - 1,
			    sizeof(n_time));
			ipt->ipt_ptr += sizeof(n_time);
		}
	}
	if (forward) {
		ip_forward(m, 1);
		return (1);
	} else
		return (0);
bad:
	icmp_error(m, type, code, dest); /* dest is not used */
	return (1);
}

/*
 * Given address of next destination (final or next hop),
 * return internet address info of interface to be used to get there.
 */
struct in_ifaddr *
ip_rtaddr(dst)
	 struct in_addr dst;
{
	register struct sockaddr_in *sin;

	sin = (struct sockaddr_in *) &ipforward_rt.ro_dst;

	if (ipforward_rt.ro_rt == 0 || dst.s_addr != sin->sin_addr.s_addr) {
		if (ipforward_rt.ro_rt) {
			RTFREE(ipforward_rt.ro_rt);
			ipforward_rt.ro_rt = 0;
		}
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = dst;

		rtalloc(&ipforward_rt);
	}
	if (ipforward_rt.ro_rt == 0)
		return ((struct in_ifaddr *)0);
	return ((struct in_ifaddr *) ipforward_rt.ro_rt->rt_ifa);
}

/*
 * Save incoming source route for use in replies,
 * to be picked up later by ip_srcroute if the receiver is interested.
 */
void
save_rte(option, dst)
	u_char *option;
	struct in_addr dst;
{
	unsigned olen;

	olen = option[IPOPT_OLEN];
#if DIAGNOSTIC
	if (ipprintfs)
		printf("save_rte: olen %ld\n", olen);
#endif
	/*
	 * PORT (AmiTCP_NG) security fix: bound olen BELOW as well as above.
	 * ip_nhops is computed as (olen - IPOPT_OFFSET - 1) / 4 in unsigned
	 * arithmetic, so an olen under 3 wraps it to ~1073741823, which
	 * ip_srcroute() then uses as an array index and a length. The callers
	 * now reject such options before reaching here; this keeps the
	 * invariant local to the function that depends on it.
	 */
	if (olen < IPOPT_OFFSET + 1 ||
	    olen > sizeof(ip_srcrt) - (1 + sizeof(dst)))
		return;
	bcopy((caddr_t)option, (caddr_t)ip_srcrt.srcopt, olen);
	ip_nhops = (olen - IPOPT_OFFSET - 1) / sizeof(struct in_addr);
	ip_srcrt.dst = dst;
}

/*
 * Retrieve incoming source route for use in replies,
 * in the same form used by setsockopt.
 * The first hop is placed before the options, will be removed later.
 */
struct mbuf *
ip_srcroute()
{
	register struct in_addr *p, *q;
	register struct mbuf *m;

	if (ip_nhops == 0)
		return ((struct mbuf *)0);
	m = m_get(M_DONTWAIT, MT_SOOPTS);
	if (m == 0)
		return ((struct mbuf *)0);

#define OPTSIZ	(sizeof(ip_srcrt.nop) + sizeof(ip_srcrt.srcopt))

	/* length is (nhops+1)*sizeof(addr) + sizeof(nop + srcrt header) */
	m->m_len = ip_nhops * sizeof(struct in_addr) + sizeof(struct in_addr) +
	    OPTSIZ;
#if DIAGNOSTIC
	if (ipprintfs)
		printf("ip_srcroute: nhops %ld mlen %ld", ip_nhops, m->m_len);
#endif

	/*
	 * First save first hop for return route
	 */
	p = &ip_srcrt.route[ip_nhops - 1];
	*(mtod(m, struct in_addr *)) = *p--;
#if DIAGNOSTIC
	if (ipprintfs)
		printf(" hops %lx", ntohl(mtod(m, struct in_addr *)->s_addr));
#endif

	/*
	 * Copy option fields and padding (nop) to mbuf.
	 */
	ip_srcrt.nop = IPOPT_NOP;
	ip_srcrt.srcopt[IPOPT_OFFSET] = IPOPT_MINOFF;
	aligned_bcopy_const((caddr_t)&ip_srcrt.nop,
	    mtod(m, caddr_t) + sizeof(struct in_addr), OPTSIZ);
	q = (struct in_addr *)(mtod(m, caddr_t) +
	    sizeof(struct in_addr) + OPTSIZ);
#undef OPTSIZ
	/*
	 * Record return path as an IP source route,
	 * reversing the path (pointers are now aligned).
	 */
	while (p >= ip_srcrt.route) {
#if DIAGNOSTIC
		if (ipprintfs)
			printf(" %lx", ntohl(q->s_addr));
#endif
		*q++ = *p--;
	}
	/*
	 * Last hop goes to final destination.
	 */
	*q = ip_srcrt.dst;
#if DIAGNOSTIC
	if (ipprintfs)
		printf(" %lx\n", ntohl(q->s_addr));
#endif
	return (m);
}

/*
 * Strip out IP options, at higher
 * level protocol in the kernel.
 * Second argument is buffer to which options
 * will be moved, and return value is their length.
 * XXX should be deleted; last arg currently ignored.
 */
void
ip_stripoptions(m, mopt)
	register struct mbuf *m;
	struct mbuf *mopt;
{
	register int i;
	struct ip *ip = mtod(m, struct ip *);
	register caddr_t opts;
	int olen;

	olen = (ip->ip_hl<<2) - sizeof (struct ip);
	opts = (caddr_t)(ip + 1);
	i = m->m_len - (sizeof (struct ip) + olen);
	aligned_bcopy(opts  + olen, opts, (unsigned)i);
	m->m_len -= olen;
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len -= olen;
	ip->ip_hl = sizeof(struct ip) >> 2;
}

u_char inetctlerrmap[PRC_NCMDS] = {
	0,		0,		0,		0,
	0,		EMSGSIZE,	EHOSTDOWN,	EHOSTUNREACH,
	EHOSTUNREACH,	EHOSTUNREACH,	ECONNREFUSED,	ECONNREFUSED,
	EMSGSIZE,	EHOSTUNREACH,	0,		0,
	0,		0,		0,		0,
	ENOPROTOOPT
};

/*
 * Forward a packet.  If some error occurs return the sender
 * an icmp packet.  Note we can't always generate a meaningful
 * icmp message because icmp doesn't have a large enough repertoire
 * of codes and types.
 *
 * If not forwarding, just drop the packet.  This could be confusing
 * if ipforwarding was zero but some routing protocol was advancing
 * us as a gateway to somewhere.  However, we must let the routing
 * protocol deal with that.
 *
 * The srcrt parameter indicates whether the packet is being forwarded
 * via a source route.
 */
void
ip_forward(m, srcrt)
	struct mbuf *m;
	int srcrt;
{
	register struct ip *ip = mtod(m, struct ip *);
	register struct sockaddr_in *sin;
	register struct rtentry *rt;
	int error, type = 0, code = 0;
	struct mbuf *mcopy;
	struct in_addr dest;

	dest.s_addr = 0;
#if DIAGNOSTIC
	if (ipprintfs)
		printf("forward: src %lx dst %lx ttl %lx\n", ip->ip_src.s_addr,
			ip->ip_dst.s_addr, ip->ip_ttl);
#endif
	if (m->m_flags & M_BCAST || in_canforward(ip->ip_dst) == 0) {
		ipstat.ips_cantforward++;
		m_freem(m);
		return;
	}
	(void)HTONS(ip->ip_id);
	if (ip->ip_ttl <= IPTTLDEC) {
		icmp_error(m, ICMP_TIMXCEED, ICMP_TIMXCEED_INTRANS, dest);
		return;
	}
	ip->ip_ttl -= IPTTLDEC;

	sin = (struct sockaddr_in *)&ipforward_rt.ro_dst;
	if ((rt = ipforward_rt.ro_rt) == 0 ||
	    ip->ip_dst.s_addr != sin->sin_addr.s_addr) {
		if (ipforward_rt.ro_rt) {
			RTFREE(ipforward_rt.ro_rt);
			ipforward_rt.ro_rt = 0;
		}
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		sin->sin_addr = ip->ip_dst;

		rtalloc(&ipforward_rt);
		if (ipforward_rt.ro_rt == 0) {
			icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_HOST, dest);
			return;
		}
		rt = ipforward_rt.ro_rt;
	}

	/*
	 * Save at most 64 bytes of the packet in case
	 * we need to generate an ICMP message to the src.
	 */
	mcopy = m_copy(m, 0, imin((int)ip->ip_len, 64));

#if USE_IF_MATRIX
	ip_ifmatrix[rt->rt_ifp->if_index +
	     if_index * m->m_pkthdr.rcvif->if_index]++;
#endif
	/*
	 * If forwarding packet using same interface that it came in on,
	 * perhaps should send a redirect to sender to shortcut a hop.
	 * Only send redirect if source is sending directly to us,
	 * and if packet was not source routed (or has any options).
	 * Also, don't send redirect if forwarding using a default route
	 * or a route modified by a redirect.
	 */
#define	satosin(sa)	((struct sockaddr_in *)(sa))
	if (rt->rt_ifp == m->m_pkthdr.rcvif &&
	    (rt->rt_flags & (RTF_DYNAMIC|RTF_MODIFIED)) == 0 &&
	    satosin(rt_key(rt))->sin_addr.s_addr != 0 &&
	    ipsendredirects && !srcrt) {
		struct in_ifaddr *ia;
		u_long src = ntohl(ip->ip_src.s_addr);
		u_long dst = ntohl(ip->ip_dst.s_addr);

		if ((ia = ifptoia(m->m_pkthdr.rcvif)) &&
		   (src & ia->ia_subnetmask) == ia->ia_subnet) {
		    if (rt->rt_flags & RTF_GATEWAY)
			dest = satosin(rt->rt_gateway)->sin_addr;
		    else
			dest = ip->ip_dst;
		    /*
		     * If the destination is reached by a route to host,
		     * is on a subnet of a local net, or is directly
		     * on the attached net (!), use host redirect.
		     * (We may be the correct first hop for other subnets.)
		     */
#define	RTA(rt)	((struct in_ifaddr *)(rt->rt_ifa))
		    type = ICMP_REDIRECT;
		    if ((rt->rt_flags & RTF_HOST) ||
		        (rt->rt_flags & RTF_GATEWAY) == 0)
			    code = ICMP_REDIRECT_HOST;
		    else if (RTA(rt)->ia_subnetmask != RTA(rt)->ia_netmask &&
		        (dst & RTA(rt)->ia_netmask) ==  RTA(rt)->ia_net)
			    code = ICMP_REDIRECT_HOST;
		    else
			    code = ICMP_REDIRECT_NET;
#if DIAGNOSTIC
		    if (ipprintfs)
		        printf("redirect (%ld) to %lx\n", code, dest.s_addr);
#endif
		}
	}

	error = ip_output(m, (struct mbuf *)0, &ipforward_rt, IP_FORWARDING);
	if (error)
		ipstat.ips_cantforward++;
	else {
		ipstat.ips_forward++;
		if (type)
			ipstat.ips_redirectsent++;
		else {
			if (mcopy)
				m_freem(mcopy);
			return;
		}
	}
	if (mcopy == NULL)
		return;
	switch (error) {

	case 0:				/* forwarded, but need redirect */
		/* type, code set above */
		break;

	case ENETUNREACH:		/* shouldn't happen, checked above */
	case EHOSTUNREACH:
	case ENETDOWN:
	case EHOSTDOWN:
	default:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_HOST;
		break;

	case EMSGSIZE:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_NEEDFRAG;
		ipstat.ips_cantfrag++;
		break;

	case ENOBUFS:
		type = ICMP_SOURCEQUENCH;
		code = 0;
		break;
	}
	icmp_error(mcopy, type, code, dest);
}
