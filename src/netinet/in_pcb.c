RCS_ID_C="$Id: in_pcb.c,v 1.10 1993/06/04 11:16:15 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: in_pcb.c,v $
 * Revision 1.10  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.9  1993/05/17  00:16:44  ppessi
 * Changed RCS version. Added rcsid.
 *
 * Revision 1.8  1993/04/05  19:05:56  jraja
 * Changed storage of the spl functions  return values to type spl_t.
 * Added include for conf.h to every .c file.
 *
 * Revision 1.7  93/03/22  16:59:07  16:59:07  jraja (Jarno Tapio Rajahalme)
 * Changed bcopy()s and bzero()s with word aligned pointers to
 * aligned_b(copy|zero) ar aligned_b(copy|zero)_const. The latter is for calls
 * in which the size is constant.
 * These can be disabled by defining NOALIGN.
 *  Converted bcopys doing structure copies (on aligned pointers) to structure
 * assignments, since at least SASC produces better code with assignment.
 * 
 * Revision 1.6  93/03/13  17:14:18  17:14:18  ppessi (Pekka Pessi)
 * Fixed bugs with variable initialization.
 * 
 * Revision 1.5  93/03/05  21:09:30  21:09:30  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.4  93/03/05  03:19:49  03:19:49  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.3  93/03/04  12:10:15  12:10:15  jraja (Jarno Tapio Rajahalme)
 * Added prototype includes.
 * 
 * Revision 1.2  93/02/26  08:58:25  08:58:25  jraja (Jarno Tapio Rajahalme)
 * Made this compile with ANSI C.
 * Addedinitialization of ifaddr in function in_pcbconnect, since it might
 * have been used without.
 * 
 * Revision 1.1  92/11/17  16:28:57  16:28:57  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 *
 */

/*
 * Copyright (c) 1982, 1986, 1991 Regents of the University of California.
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
 *	@(#)in_pcb.c	7.14 (Berkeley) 4/20/91
 */

/*
 * in_pcb.c --- Internet protocol control blocks (the per-connection state).
 *
 * Stock 4.4BSD. An `inpcb` ties a socket to its Internet identity: local address
 * and port, foreign address and port, a pointer back to the socket, and a cached
 * route. Every UDP socket and every TCP connection has one (TCP wraps it in a
 * larger tcpcb). All the pcbs for a protocol live on a linked list.
 *   in_pcballoc / in_pcbdetach   create / destroy a pcb.
 *   in_pcbbind                   assign a local address+port (bind(), or an
 *                                automatic ephemeral port), rejecting conflicts.
 *   in_pcbconnect                set the foreign address+port and pick the local
 *                                address by consulting the route to the peer.
 *   in_pcblookup                 the demultiplex key: given (foreign, local)
 *                                addr/port from an arriving packet, find the pcb
 *                                (hence the socket) it belongs to. udp_input and
 *                                tcp_input call this on every packet.
 * See TCP/IP Illustrated Vol 2 chapter 22.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/ioctl.h>
/* wakeup(): in_ifdown_notify() below wakes connect()/linger-close() waiters,
 * which sleep on &so->so_timeo -- a channel sorwakeup()/sowwakeup() never touch. */
#include <kern/kern_synch_protos.h>
#include <kern/uipc_socket2_protos.h>	/* sowakeup(), behind sorwakeup/sowwakeup */

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>

#include <netinet/in_protos.h>
#include <netinet/in_pcb_protos.h>
#include <kern/uipc_socket_protos.h>
#include <net/rtsock_protos.h>

struct	in_addr zeroin_addr = {0};

int
in_pcballoc(so, head)
	struct socket *so;
	struct inpcb *head;
{
	struct mbuf *m;
	register struct inpcb *inp;

	m = m_getclr(M_DONTWAIT, MT_PCB);
	if (m == NULL)
		return (ENOBUFS);
	inp = mtod(m, struct inpcb *);
	inp->inp_head = head;
	inp->inp_socket = so;
	insque(inp, head);
	so->so_pcb = (caddr_t)inp;
	return (0);
}
	
/*
 * Random step for ephemeral port allocation (RFC 6056).
 *
 * xorshift32, seeded once on first use from ng_gather_entropy() -- the same
 * best-effort boot seed that keys the RFC 6528 ISN hash. It is not a
 * cryptographic generator and the entropy available on this machine is limited
 * (no hardware RNG, often no real clock), so the guarantee is "not predictable
 * by counting", not "unguessable". That is still the difference between an
 * attacker knowing the next port and having to search 16384 of them.
 *
 * Lazy seeding is safe: in_pcbbind is only reachable from a socket call, which
 * cannot happen before the stack has initialised, and ng_gather_entropy touches
 * only GetSysTime/FindTask/AvailMem, all live by then.
 */
#define NG_PORT_SPAN		(65535 - IPPORT_ANONMIN + 1)	/* 16384, a power of 2 */
#define NG_PORT_RANDTRIES	32	/* random probes before falling back */
#define NG_PORT_SCANTRIES	1024	/* bounded linear scan; see the call site */

static u_long ng_port_rand;		/* 0 = not yet seeded */

static u_long
ng_port_delta()
{
	extern void ng_gather_entropy(u_char *buf, int len);
	u_long d;

	if (ng_port_rand == 0) {
		ng_gather_entropy((u_char *)&ng_port_rand, sizeof(ng_port_rand));
		if (ng_port_rand == 0)		/* xorshift is dead at zero */
			ng_port_rand = 0x9E3779B9UL;
	}
	ng_port_rand ^= ng_port_rand << 13;
	ng_port_rand ^= ng_port_rand >> 17;
	ng_port_rand ^= ng_port_rand << 5;

	/* Mask, not modulo: NG_PORT_SPAN is a power of two, so this is unbiased.
	 * A zero step would re-test the port we are already on, so make it 1. */
	d = ng_port_rand & (NG_PORT_SPAN - 1);
	return (d == 0) ? 1 : d;
}

int
in_pcbbind(inp, nam)
	register struct inpcb *inp;
	struct mbuf *nam;
{
	register struct socket *so = inp->inp_socket;
	register struct inpcb *head = inp->inp_head;
	register struct sockaddr_in *sin;
	u_short lport = 0;
	/* SO_REUSEPORT (4.4BSD-Lite2 / Linux): a duplicate binding is permitted
	 * only when the socket that already holds the port ALSO opted into
	 * sharing. reuseport carries THIS socket's consent; it is 0 unless
	 * SO_REUSEPORT is set (or SO_REUSEADDR on a multicast bind, below), so
	 * with neither set every conflict is EADDRINUSE exactly as before. */
	int reuseport = (so->so_options & SO_REUSEPORT);

	if (in_ifaddr == 0)
		return (EADDRNOTAVAIL);
	if (inp->inp_lport || inp->inp_laddr.s_addr != INADDR_ANY)
		return (EINVAL);
	if (nam == 0)
		goto noname;
	sin = mtod(nam, struct sockaddr_in *);
	if (nam->m_len != sizeof (*sin))
		return (EINVAL);
	if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
		/*
		 * Treat SO_REUSEADDR as SO_REUSEPORT for multicast: allow full
		 * duplicate binding of a multicast group + port when both this
		 * and the already-bound socket set SO_REUSEPORT, or (BSD/Linux
		 * multicast compat) both set SO_REUSEADDR. A multicast address
		 * is not a local interface address, so skip the ifa check.
		 *
		 * Only OR in the bit this socket actually set -- do NOT synthesize
		 * SO_REUSEPORT the caller never asked for, or consent becomes
		 * order-dependent (a REUSEADDR-only socket would be allowed to
		 * share with a REUSEPORT-only one in one bind order but not the
		 * other). reuseport now mirrors exactly this socket's opted-in
		 * bits, and the AND-check against the peer's so_options is
		 * symmetric: both must set the SAME reuse option.
		 */
		if (so->so_options & SO_REUSEADDR)
			reuseport |= SO_REUSEADDR;
	} else if (sin->sin_addr.s_addr != INADDR_ANY) {
		int tport = sin->sin_port;

		sin->sin_port = 0;		/* yech... */
		if (ifa_ifwithaddr((struct sockaddr *)sin) == 0)
			return (EADDRNOTAVAIL);
		sin->sin_port = tport;
	}
	lport = sin->sin_port;
	if (lport) {
		struct inpcb *t;
		u_short aport = ntohs(lport);
		int wild = 0;

		/* GROSS */
		if (aport < IPPORT_RESERVED && (so->so_state & SS_PRIV) == 0)
			return (EACCES);
		/* even GROSSER, but this is the Internet */
		if ((so->so_options & (SO_REUSEADDR|SO_REUSEPORT)) == 0 &&
		    ((so->so_proto->pr_flags & PR_CONNREQUIRED) == 0 ||
		     (so->so_options & SO_ACCEPTCONN) == 0))
			wild = INPLOOKUP_WILDCARD;
		/*
		 * Allow the conflict only if the socket already holding the port
		 * shares a reuse option we consented to (the anti-hijack rule --
		 * both sides must opt in). SO_REUSEADDR keeps its old meaning:
		 * reuseport stays 0 for it, so a plain-REUSEADDR rebind still
		 * clears a lingering (wildcarded-away) pcb but cannot steal an
		 * active co-bound socket. NB: AmigaOS has no UID/privilege model,
		 * so this is cooperative-opt-in hygiene (matching BSD/Linux
		 * SO_REUSEPORT semantics), not an enforced security boundary.
		 */
		t = in_pcblookup(head, zeroin_addr, 0, sin->sin_addr, lport, wild);
		if (t && (reuseport & t->inp_socket->so_options) == 0)
			return (EADDRINUSE);
	}
	inp->inp_laddr = sin->sin_addr;
noname:
	if (lport == 0) {
		int tries;

		/*
		 * RFC 6056 algorithm 3 (random increment). The cursor used to
		 * advance by exactly 1, which made the next ephemeral port
		 * trivially predictable -- the property Kaminsky-style DNS cache
		 * poisoning turns on, and the one several of this project's own
		 * tools were leaning on for their anti-spoofing.
		 *
		 * The step is now a random offset across the whole range, so the
		 * next port is effectively uniform over every port but the
		 * current one. The cursor stays PER HEAD (tcb and udb have their
		 * own), so TCP and UDP keep independent sequences rather than
		 * drawing consecutive values from one shared stream.
		 */
		for (tries = 0; tries < NG_PORT_RANDTRIES; tries++) {
			u_long cur = head->inp_lport;

			if (cur < IPPORT_ANONMIN)
				cur = IPPORT_ANONMIN;
			cur = IPPORT_ANONMIN +
			      (((cur - IPPORT_ANONMIN) + ng_port_delta()) % NG_PORT_SPAN);
			head->inp_lport = (u_short)cur;
			lport = htons(head->inp_lport);
			if (in_pcblookup(head, zeroin_addr, 0,
					 inp->inp_laddr, lport, 0) == 0)
				goto got_port;
		}

		/*
		 * Densely populated range: fall back to a linear scan so a nearly
		 * full table still binds. BOUNDED, unlike the original: this runs
		 * under splnet(), which on this platform is Forbid() -- a scan of
		 * all 16384 candidates, each an O(n) walk of the PCB list, would
		 * freeze the WHOLE MACHINE rather than just networking, and the
		 * unbounded original would never return at all once the range was
		 * exhausted.
		 */
		for (tries = 0; tries < NG_PORT_SCANTRIES; tries++) {
			if (++head->inp_lport < IPPORT_ANONMIN)
				head->inp_lport = IPPORT_ANONMIN;
			lport = htons(head->inp_lport);
			if (in_pcblookup(head, zeroin_addr, 0,
					 inp->inp_laddr, lport, 0) == 0)
				goto got_port;
		}
		return (EADDRNOTAVAIL);		/* no free ephemeral port */
	got_port: ;
	}
	inp->inp_lport = lport;
	return (0);
}

/*
 * The "primary" local interface address for source selection: the first
 * genuinely usable non-loopback address. PORT (AmiTCP_NG): our self-start
 * addresses lo0 (127.0.0.1) before any real interface, so lo0 heads in_ifaddr --
 * using the raw list head as "the primary interface" (as stock BSD does) would
 * hand out 127.0.0.1 as a source address. Require IFF_UP and a real
 * (non-0.0.0.0) address, so a DOWN interface (e.g. one taken Offline, whose
 * address is cleared to 0.0.0.0 but which stays linked in in_ifaddr) or an
 * unnumbered interface is never handed back as the source.
 *
 * Broadcast-capable interfaces are PREFERRED but not required: one caller is
 * picking a source for a limited broadcast (rtalloc cannot route
 * 255.255.255.255, so it lands here), which wants a broadcast interface if one
 * exists -- but demanding IFF_BROADCAST outright excluded point-to-point links
 * entirely and sent a PPP/SLIP-only machine back to the lo0 head. Fall back to
 * the head only if nothing usable exists at all.
 */
static struct in_ifaddr *
in_primary_ifaddr(void)
{
	register struct in_ifaddr *ia;

	register struct in_ifaddr *fallback = (struct in_ifaddr *)0;

	for (ia = in_ifaddr; ia; ia = ia->ia_next) {
		if ((ia->ia_ifp->if_flags & IFF_UP) == 0 ||
		    (ia->ia_ifp->if_flags & IFF_LOOPBACK) != 0 ||
		    ia->ia_addr.sin_addr.s_addr == INADDR_ANY)
			continue;
		if (ia->ia_ifp->if_flags & IFF_BROADCAST)
			return (ia);		/* preferred */
		/*
		 * Point-to-point (SLIP/CSLIP/PPP, or anything configured
		 * a_point2point -- see net/sana2config.c) carries no
		 * IFF_BROADCAST. Requiring that flag outright made a
		 * p2p-only machine match nothing and fall through to the raw
		 * list head, which is lo0, so 127.0.0.1 was handed out as a
		 * source address -- the very thing this function exists to
		 * prevent. Usable, just not broadcast-capable: keep it as a
		 * fallback rather than preferring it over a real broadcast
		 * interface, since one call site is choosing a source for a
		 * limited broadcast.
		 */
		if (fallback == (struct in_ifaddr *)0)
			fallback = ia;
	}
	return (fallback ? fallback : in_ifaddr);
}

/*
 * Connect from a socket to a specified address.
 * Both address and port must be specified in argument sin.
 * If don't have a local address for this socket yet,
 * then pick one.
 */
int
in_pcbconnect(inp, nam)
	register struct inpcb *inp;
	struct mbuf *nam;
{
	struct in_ifaddr *ia;
	struct sockaddr_in *ifaddr = NULL;
	register struct sockaddr_in *sin = mtod(nam, struct sockaddr_in *);

	if (nam->m_len != sizeof (*sin))
		return (EINVAL);
	if (sin->sin_family != AF_INET)
		return (EAFNOSUPPORT);
	if (sin->sin_port == 0)
		return (EADDRNOTAVAIL);
	if (in_ifaddr) {
		/*
		 * If the destination address is INADDR_ANY,
		 * use the primary local address.
		 *
		 * PORT (AmiTCP_NG): INADDR_BROADCAST is deliberately NOT rewritten to
		 * a directed broadcast here. A limited broadcast (255.255.255.255) is
		 * left as-is on the wire (what DHCP and RFC 919 want) and is emitted,
		 * unrouted, by ip_broadcast_flood(); rewriting it to the directed
		 * broadcast only mattered for the old routed path and depended on the
		 * (loopback) list head. The source address for the send is chosen from
		 * a broadcast-capable interface below (rtalloc fails for 255.255.255.255,
		 * so the in_primary_ifaddr() fallback provides it).
		 */
#define	satosin(sa)	((struct sockaddr_in *)(sa))
		if (sin->sin_addr.s_addr == INADDR_ANY)
		    sin->sin_addr = IA_SIN(in_primary_ifaddr())->sin_addr;
	}
	if (inp->inp_laddr.s_addr == INADDR_ANY) {
		register struct route *ro;
		struct ifnet *ifp;

		ia = (struct in_ifaddr *)0;
		/* 
		 * If route is known or can be allocated now,
		 * our src addr is taken from the i/f, else punt.
		 */
		ro = &inp->inp_route;
		if (ro->ro_rt &&
		    (satosin(&ro->ro_dst)->sin_addr.s_addr !=
			sin->sin_addr.s_addr || 
		    inp->inp_socket->so_options & SO_DONTROUTE)) {
			RTFREE(ro->ro_rt);
			ro->ro_rt = (struct rtentry *)0;
		}
		if ((inp->inp_socket->so_options & SO_DONTROUTE) == 0 && /*XXX*/
		    (ro->ro_rt == (struct rtentry *)0 ||
		    ro->ro_rt->rt_ifp == (struct ifnet *)0)) {
			/* No route yet, so try to acquire one */
			ro->ro_dst.sa_family = AF_INET;
			ro->ro_dst.sa_len = sizeof(struct sockaddr_in);
			((struct sockaddr_in *) &ro->ro_dst)->sin_addr =
				sin->sin_addr;
			rtalloc(ro);
		}
		/*
		 * If we found a route, use the address
		 * corresponding to the outgoing interface
		 * unless it is the loopback (in case a route
		 * to our address on another net goes to loopback).
		 */
		if (ro->ro_rt && (ifp = ro->ro_rt->rt_ifp) &&
		    (ifp->if_flags & IFF_LOOPBACK) == 0)
			for (ia = in_ifaddr; ia; ia = ia->ia_next)
				if (ia->ia_ifp == ifp)
					break;
		if (ia == 0) {
			int fport = sin->sin_port;

			sin->sin_port = 0;
			ia = (struct in_ifaddr *)
			    ifa_ifwithdstaddr((struct sockaddr *)sin);
			sin->sin_port = fport;
			if (ia == 0)
				ia = in_iaonnetof(in_netof(sin->sin_addr));
			if (ia == 0)
				ia = in_primary_ifaddr();	/* not the raw head (lo0) */
			if (ia == 0)
				return (EADDRNOTAVAIL);
		}
		ifaddr = (struct sockaddr_in *)&ia->ia_addr;
	}
	if (in_pcblookup(inp->inp_head,
	    sin->sin_addr,
	    sin->sin_port,
	    inp->inp_laddr.s_addr ? inp->inp_laddr : ifaddr->sin_addr,
	    inp->inp_lport,
	    0))
		return (EADDRINUSE);
	if (inp->inp_laddr.s_addr == INADDR_ANY) {
		if (inp->inp_lport == 0) {
			/*
			 * The result was discarded here for as long as this
			 * function existed, and that was safe: the implicit bind
			 * takes the auto-allocation path, which could only ever
			 * succeed -- it looped until it found a free port.
			 *
			 * Bounding that loop (so an exhausted range returns
			 * instead of spinning forever under Forbid()) made this
			 * call able to fail, and a discarded failure would leave
			 * inp_lport at 0 while we went on to set the foreign
			 * address and report success: a "connected" PCB that no
			 * reply can ever match. Propagate it.
			 */
			int error = in_pcbbind(inp, (struct mbuf *)0);

			if (error)
				return (error);
		}
		inp->inp_laddr = ifaddr->sin_addr;
	}
	inp->inp_faddr = sin->sin_addr;
	inp->inp_fport = sin->sin_port;
	return (0);
}

void
in_pcbdisconnect(inp)
	struct inpcb *inp;
{

	inp->inp_faddr.s_addr = INADDR_ANY;
	inp->inp_fport = 0;
	if (inp->inp_socket->so_state & SS_NOFDREF)
		in_pcbdetach(inp);
}

void
in_pcbdetach(inp)
	struct inpcb *inp;
{
	struct socket *so = inp->inp_socket;

	so->so_pcb = 0;
	sofree(so);
	if (inp->inp_options)
		(void)m_free(inp->inp_options);
	if (inp->inp_route.ro_rt)
		rtfree(inp->inp_route.ro_rt);
	remque(inp);
	(void) m_free(dtom(inp));
}

void
in_setsockaddr(inp, nam)
	register struct inpcb *inp;
	struct mbuf *nam;
{
	register struct sockaddr_in *sin;
	
	nam->m_len = sizeof (*sin);
	sin = mtod(nam, struct sockaddr_in *);
	aligned_bzero_const((caddr_t)sin, sizeof (*sin));
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_port = inp->inp_lport;
	sin->sin_addr = inp->inp_laddr;
}

void
in_setpeeraddr(inp, nam)
	struct inpcb *inp;
	struct mbuf *nam;
{
	register struct sockaddr_in *sin;
	
	nam->m_len = sizeof (*sin);
	sin = mtod(nam, struct sockaddr_in *);
	aligned_bzero_const((caddr_t)sin, sizeof (*sin));
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(*sin);
	sin->sin_port = inp->inp_fport;
	sin->sin_addr = inp->inp_faddr;
}

/*
 * Pass some notification to all connections of a protocol
 * associated with address dst.  The local address and/or port numbers
 * may be specified to limit the search.  The "usual action" will be
 * taken, depending on the ctlinput cmd.  The caller must filter any
 * cmds that are uninteresting (e.g., no error in the map).
 * Call the protocol specific routine (if any) to report
 * any errors for each matching socket.
 *
 * Must be called at splnet.
 */
void
in_pcbnotify(head, dst, fport, laddr, lport, cmd, notify)
	struct inpcb *head;
	struct sockaddr *dst;
	u_short fport, lport;
	struct in_addr laddr;
	int cmd;
        void (* notify)(register struct inpcb * inp, int error);
{
	register struct inpcb *inp, *oinp;
	struct in_addr faddr;
	int errno;
	extern u_char inetctlerrmap[];

	/* PORT (AmiTCP_NG) fix: >= , not > -- inetctlerrmap[cmd] is read below and
	 * cmd == PRC_NCMDS is one past its end. This is a protocol-agnostic entry
	 * point, so the guard must not rely on today's callers staying in range. */
	if ((unsigned)cmd >= PRC_NCMDS || dst->sa_family != AF_INET)
		return;
	faddr = ((struct sockaddr_in *)dst)->sin_addr;
	if (faddr.s_addr == INADDR_ANY)
		return;

	/*
	 * Redirects go to all references to the destination,
	 * and use in_rtchange to invalidate the route cache.
	 * Dead host indications: notify all references to the destination.
	 * Otherwise, if we have knowledge of the local port and address,
	 * deliver only to that socket.
	 */
	if (PRC_IS_REDIRECT(cmd) || cmd == PRC_HOSTDEAD) {
		fport = 0;
		lport = 0;
		laddr.s_addr = 0;
		if (cmd != PRC_HOSTDEAD)
			notify = in_rtchange;
	}
	errno = inetctlerrmap[cmd];
	for (inp = head->inp_next; inp != head;) {
		if (inp->inp_faddr.s_addr != faddr.s_addr ||
		    inp->inp_socket == 0 ||
		    (lport && inp->inp_lport != lport) ||
		    (laddr.s_addr && inp->inp_laddr.s_addr != laddr.s_addr) ||
		    (fport && inp->inp_fport != fport)) {
			inp = inp->inp_next;
			continue;
		}
		oinp = inp;
		inp = inp->inp_next;
		if (notify)
			(*notify)(oinp, errno);
	}
}

/*
 * PORT (AmiTCP_NG): tell the sockets on an interface that it has gone away.
 *
 * BSD raises PRC_IFDOWN from if_down() and then throws it away: inetctlerrmap[]
 * maps it to 0, so tcp_ctlinput() and udp_ctlinput() return before doing
 * anything. Applications are left to discover the loss when their next operation
 * fails, or when a blocked one finally times out -- which is exactly the "the
 * program still thinks the network is up" complaint.
 *
 * in_pcbnotify() cannot be reused for this. It matches on inp_faddr, the FOREIGN
 * address, against the address it is given; if_down() supplies our own LOCAL
 * address, so the walk would look for sockets talking TO us and hit either
 * nothing or, worse, unrelated sockets that happen to have this address as their
 * peer. The selector here is the local address instead.
 *
 * Sockets bound to INADDR_ANY are NOT matched while any other interface is still
 * carrying an address: they are not tied to one interface, and erroring them
 * because a DIFFERENT interface went down would be wrong on a multi-homed
 * machine. A server listening on 0.0.0.0 keeps listening, while its established
 * connections -- which do carry the real local address -- are told.
 *
 * When the LAST interface goes, that reasoning inverts: there is no network left
 * for them to be waiting on, and leaving them blocked for ever is worse than
 * telling them. The caller passes match_any for that case. Loopback survives, so
 * an application serving 0.0.0.0 purely for local clients gets an error it did
 * not strictly need -- acceptable, because so_error is consumed once and does not
 * destroy the socket, and the alternative is an application that hangs.
 *
 * We set so_error DIRECTLY rather than going through tcp_notify(), and that
 * departure is deliberate. tcp_notify() stashes a SOFT error on the tcpcb
 * (t_softerror) precisely because an ICMP-derived signal may be spoofed or
 * transient and should not kill a live connection on its own. This is not that:
 * the interface really has gone, on our own authority. Do not "correct" this to a
 * softerror by analogy with the neighbouring ICMP paths.
 *
 * All three wakeups are required. sorwakeup()/sowwakeup() reach readers, writers
 * and select(); neither touches &so->so_timeo, which is where a task blocked in
 * connect() sleeps WITHOUT A TIMEOUT (api/amiga_syscalls.c), and where a
 * lingering close() waits. Omitting it leaves exactly those callers hanging --
 * the case this whole exercise exists to fix. soraise_event() is reached from
 * inside sowakeup(), so a Roadshow SBTC_SIGEVENTMASK client gets FD_ERROR for
 * free once so_error is set.
 *
 * The caller must hold splimp()/splnet(): that is what keeps a concurrent
 * soclose() (which holds splnet across in_pcbdetach) from freeing a pcb under
 * the walk. Waking from this level is the stack's normal condition -- every
 * received segment does it from the same held spl.
 */
void
in_ifdown_notify(struct in_addr laddr, int error, int match_any)
{
	struct inpcb *head, *inp, *oinp;
	struct socket *so;
	int which;
	extern struct inpcb tcb, udb;

	if (!match_any && laddr.s_addr == INADDR_ANY)
		return;

	for (which = 0; which < 2; which++) {
		head = which ? &udb : &tcb;
		for (inp = head->inp_next; inp != NULL && inp != head; ) {
			oinp = inp;
			inp = inp->inp_next;	/* advance first, as in_pcbnotify does */
			if (match_any) {
				/* The LAST interface has gone: there is no network left
				 * to wait for, so the sockets that were not tied to any
				 * one interface are told too. */
				if (oinp->inp_laddr.s_addr != INADDR_ANY)
					continue;
			} else if (oinp->inp_laddr.s_addr != laddr.s_addr)
				continue;
			if ((so = oinp->inp_socket) == NULL)
				continue;
			so->so_error = error;
			wakeup((caddr_t)&so->so_timeo);	/* connect()/linger close() */
			sorwakeup(so);
			sowwakeup(so);
		}
	}
}

/*
 * Check for alternatives when higher level complains
 * about service problems.  For now, invalidate cached
 * routing information.  If the route was created dynamically
 * (by a redirect), time to try a default gateway again.
 */
void
in_losing(inp)
	struct inpcb *inp;
{
	register struct rtentry *rt;

	if ((rt = inp->inp_route.ro_rt)) {
		rt_missmsg(RTM_LOSING, &inp->inp_route.ro_dst,
			    rt->rt_gateway, (struct sockaddr *)rt_mask(rt),
			    (struct sockaddr *)0, rt->rt_flags, 0);
		if (rt->rt_flags & RTF_DYNAMIC)
			(void) rtrequest(RTM_DELETE, rt_key(rt),
				rt->rt_gateway, rt_mask(rt), rt->rt_flags, 
				(struct rtentry **)0);
		inp->inp_route.ro_rt = 0;
		rtfree(rt);
		/*
		 * A new route can be allocated
		 * the next time output is attempted.
		 */
	}
}

/*
 * After a routing change, flush old routing
 * and allocate a (hopefully) better one.
 */
void
in_rtchange(inp, error)
	register struct inpcb *inp;
        int error;
{
	if (inp->inp_route.ro_rt) {
		rtfree(inp->inp_route.ro_rt);
		inp->inp_route.ro_rt = 0;
		/*
		 * A new route can be allocated the next time
		 * output is attempted.
		 */
	}
}

struct inpcb *
in_pcblookup(head, faddr, fport, laddr, lport, flags)
	struct inpcb *head;
	struct in_addr faddr, laddr;
	u_short fport, lport;
	int flags;
{
	register struct inpcb *inp, *match = 0;
	int matchwild = 3, wildcard;
	int ng_walk = 0;			/* DIAG: runaway-walk detector */

	for (inp = head->inp_next; inp != head; inp = inp->inp_next) {
		/*
		 * LAST-RESORT TRAP. This walk runs under splnet(), which is Forbid()
		 * here, so a corrupt chain does not merely fail -- it stops the whole
		 * machine, silently, taking the log task with it. The cause is fixed
		 * at source (domaininit() now re-runs the per-protocol init on every
		 * stack start, so the heads cannot be inherited stale), but a list
		 * this cheap to check should never again be able to hang an Amiga.
		 *
		 * NULL FIRST, and it matters: the loop test is `inp != head`, which
		 * NULL passes, so without this the body dereferences address 0. On
		 * this platform that reads the exception vectors rather than faulting,
		 * and inp_next then holds garbage -- quite possibly an ODD address,
		 * which on a 68000/68010 is an Address Error on the next iteration.
		 * "Bounded" is not "safe": the count below would often never be
		 * reached.
		 */
		if (inp == NULL)
			break;			/* treat as no match */
		if (++ng_walk > 2000) {
			extern volatile int ng_pcb_runaway;
			extern volatile unsigned long ng_pcb_head, ng_pcb_first,
						     ng_pcb_cur, ng_pcb_curnext;
			ng_pcb_runaway = ng_walk;
			ng_pcb_head    = (unsigned long)head;
			ng_pcb_first   = (unsigned long)head->inp_next;
			ng_pcb_cur     = (unsigned long)inp;
			ng_pcb_curnext = (unsigned long)inp->inp_next;
			break;
		}
		if (inp->inp_lport != lport)
			continue;
		wildcard = 0;
		if (inp->inp_laddr.s_addr != INADDR_ANY) {
			if (laddr.s_addr == INADDR_ANY)
				wildcard++;
			else if (inp->inp_laddr.s_addr != laddr.s_addr)
				continue;
		} else {
			if (laddr.s_addr != INADDR_ANY)
				wildcard++;
		}
		if (inp->inp_faddr.s_addr != INADDR_ANY) {
			if (faddr.s_addr == INADDR_ANY)
				wildcard++;
			else if (inp->inp_faddr.s_addr != faddr.s_addr ||
			    inp->inp_fport != fport)
				continue;
		} else {
			if (faddr.s_addr != INADDR_ANY)
				wildcard++;
		}
		if (wildcard && (flags & INPLOOKUP_WILDCARD) == 0)
			continue;
		if (wildcard < matchwild) {
			match = inp;
			matchwild = wildcard;
			if (matchwild == 0)
				break;
		}
	}
	return (match);
}
