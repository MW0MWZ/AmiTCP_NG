/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: if.c,v 3.1 1994/02/03 03:50:38 ppessi Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * if.c --- Generic Network Interface Routines
 *
 * Last modified: Thu Feb  3 01:42:51 1994 ppessi
 *
 * HISTORY
 * $Log: if.c,v $
 * Revision 3.1  1994/02/03  03:50:38  ppessi
 * Initially tested version
 *
 * Revision 1.22  1994/01/13  07:36:55  jraja
 * Added findid() function to support gethostid() system call.
 *
 * Revision 1.21  1993/10/29  02:00:17  ppessi
 * Added SIOCGARPT ioctl.
 *
 * Revision 1.20  1993/09/19  21:14:18  jraja
 * Fixed a bug with '/' placement in ifconf().
 *
 * Revision 1.19  1993/09/09  23:45:36  ppessi
 * Changed the interface name format returned by SIOCGIFCONF.
 */

/*
 * Copyright (c) 1980, 1986 Regents of the University of California.
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
 *	@(#)if.c	7.14 (Berkeley) 4/20/91
 */

/*
 * if.c --- generic network-interface management. Stock 4.4BSD.
 *
 * The address-family- and driver-independent machinery for interfaces: the master
 * interface list (ifnet), attaching an interface (if_attach), the generic ioctl
 * handler (ifioctl -> SIOCGIFFLAGS/SIOCSIFFLAGS/SIOCGIFCONF...), marking interfaces
 * up/down (if_up/if_down and the routing/protocol notifications that follow), and
 * ifunit()/ifa_ifwithaddr() lookups. Both lo0 (net/if_loop.c) and SANA-II
 * interfaces (net/if_sana.c) plug their driver-specific ifnet into this. Read this
 * to understand what every interface has in common before reading the two drivers.
 * See TCP/IP Illustrated Vol 2 chapter 4.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>
#include <sys/synch.h>

#include <net/if.h>
#ifndef AMITCP
#include <net/if_dl.h>
#endif
#include <netinet/in.h> /* for findid */

void ifinit(void);
void if_attach(struct ifnet *);
struct ifaddr *ifa_ifwithaddr(register struct sockaddr *);
struct ifaddr *ifa_ifwithdstaddr(register struct sockaddr *);
struct ifaddr *ifa_ifwithnet(struct sockaddr *);
struct ifaddr *ifaof_ifpforaddr(struct sockaddr *, register struct ifnet *);
void link_rtrequest(int, struct rtentry *, struct sockaddr *);
void if_down(register struct ifnet *);
void if_qflush(register struct ifqueue *);
void if_slowtimo(void);
struct ifnet *ifunit(register char *);
int ifioctl(struct socket *, int, caddr_t);
int ifconf(int, caddr_t);

/* Compatibility with AmiTCP/IP 2 */
#include <sys/a_ioctl.h>
#include <net/a_if.h>
struct ifnet *aifunit(register char *name);
int aifconf(int cmd, caddr_t data);

#include <kern/uipc_domain_protos.h>

static char *sprint_d(u_int n, char *buf, int buflen);
int	ifqmaxlen = IFQ_MAXLEN;

struct	ifnet *ifnet = NULL;

/*
 * Network interface utility routines.
 *
 * Routines with ifa_ifwith* names take sockaddr *'s as
 * parameters.
 */

void ifinit(void)
{
	register struct ifnet *ifp;

	for (ifp = ifnet; ifp; ifp = ifp->if_next)
		if (ifp->if_snd.ifq_maxlen == 0)
			ifp->if_snd.ifq_maxlen = ifqmaxlen;
	if_slowtimo();
}

/*
 * PORT (AmiTCP_NG): total octets in or out across every interface, for
 * SocketBaseTags(SBTC_GET_BYTES_RECEIVED / _SENT). Those tags are documented as
 * "the number of bytes sent by the TCP/IP stack" -- a stack-wide figure, which
 * this stack does not keep anywhere, so it is summed from the per-interface
 * counters that ShowNetStatus already reports. Loopback is included: it IS an
 * interface the stack sent through, and excluding it would make the total
 * disagree with the interface list a user can see.
 *
 * The result is a 64-bit pair because if_ibytes/if_obytes are 32-bit and several
 * interfaces can carry more than 4 GB between them -- a single 32-bit sum would
 * silently wrap, which is precisely the class of bug that made these counters
 * report nonsense before.
 */
void
ng_stack_byte_total(out, hip, lop)
	int out;
	u_long *hip, *lop;
{
	register struct ifnet *ifp;
	u_long hi = 0, lo = 0, add, prev;

	for (ifp = ifnet; ifp; ifp = ifp->if_next) {
		add  = (u_long)(out ? ifp->if_obytes : ifp->if_ibytes);
		prev = lo;
		lo  += add;
		if (lo < prev)		/* carry out of the low word */
			hi++;
	}
	*hip = hi;
	*lop = lo;
}

#ifdef vax
/*
 * Call each interface on a Unibus reset.
 */
ifubareset(uban)
	int uban;
{
	register struct ifnet *ifp;

	for (ifp = ifnet; ifp; ifp = ifp->if_next)
		if (ifp->if_reset)
			(*ifp->if_reset)(ifp->if_unit, uban);
}
#endif

int if_index = 0;
#ifndef AMITCP
struct ifaddr **ifnet_addrs;
static char *sprint_d();
#endif

/*
 * Attach an interface to the
 * list of "active" interfaces.
 */
void
if_attach(ifp)
     struct ifnet *ifp;
{
#ifndef AMITCP
	unsigned socksize, ifasize;
	int namelen, unitlen;
	char workbuf[12], *unitname;
	register struct sockaddr_dl *sdl;
	register struct ifaddr *ifa;
	static int if_indexlim = 8;
#endif
	register struct ifnet **p = &ifnet;

	while (*p)
		p = &((*p)->if_next);
	*p = ifp;
	ifp->if_index = ++if_index;
#ifndef AMITCP
	if (ifnet_addrs == 0 || if_index >= if_indexlim) {
		unsigned n = (if_indexlim <<= 1) * sizeof(ifa);
		struct ifaddr **q = (struct ifaddr **)
					bsd_malloc(n, M_IFADDR, M_WAITOK);
		if (ifnet_addrs) {
			aligned_bcopy((caddr_t)ifnet_addrs, (caddr_t)q, n/2);
			bsd_free((caddr_t)ifnet_addrs, M_IFADDR);
		}
		ifnet_addrs = q;
	}
	/*
	 * create a Link Level name for this device
	 */
#ifdef notanymore
	/* Exec device name can contain digits, workaround with slash */
	unitname = sprint_d((u_int)ifp->if_unit, 
			    workbuf + 1, 
			    sizeof(workbuf) - 1);
	*--unitname = '/';
#else
	unitname = sprint_d((u_int)ifp->if_unit, workbuf, sizeof(workbuf));
#endif
	namelen = strlen(ifp->if_name);
        unitlen = strlen(unitname);
#define _offsetof(t, m) ((int)((caddr_t)&((t *)0)->m))
	socksize = _offsetof(struct sockaddr_dl, sdl_data[0]) +
			       unitlen + namelen + ifp->if_addrlen;
#define ROUNDUP(a) (1 + (((a) - 1) | (sizeof(long) - 1)))
	socksize = ROUNDUP(socksize);
	if (socksize < sizeof(*sdl))
		socksize = sizeof(*sdl);
	ifasize = sizeof(*ifa) + 2 * socksize;
	ifa = (struct ifaddr *)bsd_malloc(ifasize, M_IFADDR, M_WAITOK);
	if (ifa == 0)
		return;
	ifnet_addrs[if_index - 1] = ifa;
	aligned_bzero((caddr_t)ifa, ifasize);
	sdl = (struct sockaddr_dl *)(ifa + 1);
	ifa->ifa_addr = (struct sockaddr *)sdl;
	ifa->ifa_ifp = ifp;
	sdl->sdl_len = socksize;
	sdl->sdl_family = AF_LINK;
	bcopy(ifp->if_name, sdl->sdl_data, namelen);
	bcopy(unitname, namelen + (caddr_t)sdl->sdl_data, unitlen);
	sdl->sdl_nlen = (namelen += unitlen);
	sdl->sdl_index = ifp->if_index;
	sdl = (struct sockaddr_dl *)(socksize + (caddr_t)sdl);
	ifa->ifa_netmask = (struct sockaddr *)sdl;
	sdl->sdl_len = socksize - ifp->if_addrlen;
	while (namelen != 0)
		sdl->sdl_data[--namelen] = (char)0xff;
	ifa->ifa_next = ifp->if_addrlist;
	ifa->ifa_rtrequest = link_rtrequest;
	ifp->if_addrlist = ifa;
#endif
}

/*
 * Locate an interface based on a complete address.
 */

/*
 * Find an interface address suitable for host id.
 *
 * *id IS ALWAYS WRITTEN: the address on success, 0 when there is no suitable
 * one. This used to say "the *id is not touched" if nothing was found, and that
 * is no longer true -- a caller that pre-loaded *id with a fallback and expected
 * a miss to leave it alone would now have it cleared. Said plainly here because
 * the old wording invited exactly that assumption.
 *
 * Loopback does not count and neither does INADDR_ANY: an interface can be
 * IFF_UP before it has been configured, and stopping at it would hide a good
 * address further down the list.
 *
 * This routine stops after the first usable non-loopback AF_INET address.
 *
 * This routine is specially made for the gethostid() system call.
 */
void
findid(unsigned long *id)
{
  struct ifnet *ifp;
  struct ifaddr *ifa;

  spl_t s = splimp();

  /*
   * PORT (AmiTCP_NG): the loopback address is NOT this host's address, and
   * "no interface is up" must report no address at all rather than 127.0.0.1.
   *
   * The original preferred a non-loopback address but ASSIGNED whatever it
   * found first, so a machine whose only live interface was lo0 -- exactly what
   * NetShutdown leaves behind -- still handed out 127.0.0.1. Every application
   * that decides "am I online?" by asking for the host address then concluded
   * it was, which is issue #4: Roadshow gives you a socket but no address after
   * NetShutdown, and we gave both.
   *
   * An address of 0.0.0.0 is skipped rather than accepted: an interface can be
   * IFF_UP before it has been configured, and stopping there would hide a
   * perfectly good address on an interface later in the list.
   */
  *id = 0;

  for (ifp = ifnet; ifp; ifp = ifp->if_next) {
    if ((ifp->if_flags & IFF_UP) == 0)
      continue;
    if (ifp->if_flags & IFF_LOOPBACK)
      continue;
    for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next)
      if (ifa->ifa_addr->sa_family == AF_INET) {
	unsigned long a = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
	if (a != INADDR_ANY) {
	  *id = a;
	  splx(s);
	  return;
	}
      }
  }

  splx(s);
}

/*ARGSUSED*/
struct ifaddr *
ifa_ifwithaddr(addr)
	register struct sockaddr *addr;
{
	register struct ifnet *ifp;
	register struct ifaddr *ifa;

	/*
	 * PORT (AmiTCP_NG): same guard as ifa_ifwithnet()/ifaof_ifpforaddr() below,
	 * and reachable by the same dead-today route_output() RTM_CHANGE chain
	 * (ifa_ifwithroute -> ifa_ifwithaddr(gateway) with a NULL gateway).
	 *
	 * WORTH MORE ATTENTION THAN ITS SIBLINGS if a wild read ever needs chasing:
	 * this one's first touch of a NULL is `equal(addr, ...)`, which reads
	 * ((struct sockaddr *)addr)->sa_len -- OFFSET 0 -- and then bcmp()s from
	 * there. That faults at address 0 exactly, which is the signature an Enforcer
	 * report of "BYTE-READ from 00000000" describes. The sa_family tests in the
	 * other three fault at address 1 instead, so they cannot produce that report.
	 * Offsets discriminate; use them before theorising.
	 */
	if (addr == (struct sockaddr *)0)
		return ((struct ifaddr *)0);

#define	equal(a1, a2) \
  (bcmp((caddr_t)(a1), (caddr_t)(a2), ((struct sockaddr *)(a1))->sa_len) == 0)
	for (ifp = ifnet; ifp; ifp = ifp->if_next)
	    for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != addr->sa_family)
			continue;
		if (equal(addr, ifa->ifa_addr))
			return (ifa);
		if ((ifp->if_flags & IFF_BROADCAST) && ifa->ifa_broadaddr &&
		    equal(ifa->ifa_broadaddr, addr))
			return (ifa);
	}
	return ((struct ifaddr *)0);
}
/*
 * Locate the point to point interface with a given destination address.
 */
/*ARGSUSED*/
struct ifaddr *
ifa_ifwithdstaddr(addr)
	register struct sockaddr *addr;
{
	register struct ifnet *ifp;
	register struct ifaddr *ifa;

	/* PORT (AmiTCP_NG): as ifa_ifwithaddr() above -- same dead-today chain, and
	 * this one also reaches equal(), i.e. a read at offset 0 of a NULL. */
	if (addr == (struct sockaddr *)0)
		return ((struct ifaddr *)0);

	for (ifp = ifnet; ifp; ifp = ifp->if_next)
	    if (ifp->if_flags & IFF_POINTOPOINT)
		for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr->sa_family != addr->sa_family)
				continue;
			if (equal(addr, ifa->ifa_dstaddr))
				return (ifa);
	}
	return ((struct ifaddr *)0);
}

/*
 * Find an interface on a specific network.  If many, choice
 * is first found.
 */
struct ifaddr *
ifa_ifwithnet(addr)
	struct sockaddr *addr;
{
	register struct ifnet *ifp;
	register struct ifaddr *ifa;
	u_int af;

	/*
	 * PORT (AmiTCP_NG): a NULL addr read sa_family and then walked addr->sa_data
	 * byte-by-byte against each ifaddr below -- a wild read with no MMU to stop it.
	 *
	 * NOT merely hypothetical: route_output()'s RTM_CHANGE handler (net/rtsock.c)
	 * passes `gate` down through ifa_ifwithroute() to here, and RTM_CHANGE does not
	 * require RTA_GATEWAY -- unlike RTM_ADD, which refuses a NULL gate outright.
	 * A `route change` that only alters flags on a directly-connected route
	 * produces exactly that. It cannot fire TODAY only because route_output is
	 * #define'd to NULL under AMITCP and socreate() refuses PF_ROUTE; the day that
	 * is reactivated, this becomes live. Guard it now, while it is free.
	 *
	 * Note this is NOT the Enforcer hit being chased: sa_family is at offset 1 of
	 * struct sockaddr, so a NULL here faults at address 1, and that report says 0.
	 */
	if (addr == (struct sockaddr *)0)
		return ((struct ifaddr *)0);
	af = addr->sa_family;

	if (af >= AF_MAX)
		return (0);
#ifndef AMITCP
	if (af == AF_LINK) {
	    register struct sockaddr_dl *sdl = (struct sockaddr_dl *)addr;
	    if (sdl->sdl_index && sdl->sdl_index <= if_index)
		return (ifnet_addrs[sdl->sdl_index - 1]);
	}
#endif
	for (ifp = ifnet; ifp; ifp = ifp->if_next)
	    for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next) {
		register char *cp, *cp2, *cp3;
		register char *cplim;
		if (ifa->ifa_addr->sa_family != af || ifa->ifa_netmask == 0)
			continue;
		cp = addr->sa_data;
		cp2 = ifa->ifa_addr->sa_data;
		cp3 = ifa->ifa_netmask->sa_data;
		cplim = ifa->ifa_netmask->sa_len + (char *)ifa->ifa_netmask;
		for (; cp3 < cplim; cp3++)
			if ((*cp++ ^ *cp2++) & *cp3)
				break;
		if (cp3 == cplim)
			return (ifa);
	    }
	return ((struct ifaddr *)0);
}

/*
 * Find an interface address specific to an interface best matching
 * a given address.
 */
struct ifaddr *
ifaof_ifpforaddr(addr, ifp)
	struct sockaddr *addr;
	register struct ifnet *ifp;
{
	register struct ifaddr *ifa;
	register char *cp, *cp2, *cp3;
	register char *cplim;
	struct ifaddr *ifa_maybe = 0;
	u_int af;

	/* PORT (AmiTCP_NG): same guard as ifa_ifwithnet() above, same reason -- this
	 * one also walks addr->sa_data in lockstep against ifa_addr/ifa_netmask. */
	if (addr == (struct sockaddr *)0)
		return ((struct ifaddr *)0);
	af = addr->sa_family;

	if (af >= AF_MAX)
		return (0);
	for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != af)
			continue;
		ifa_maybe = ifa;
		if (ifa->ifa_netmask == 0) {
			if (equal(addr, ifa->ifa_addr) ||
			    (ifa->ifa_dstaddr && equal(addr, ifa->ifa_dstaddr)))
				return (ifa);
			continue;
		}
		cp = addr->sa_data;
		cp2 = ifa->ifa_addr->sa_data;
		cp3 = ifa->ifa_netmask->sa_data;
		cplim = ifa->ifa_netmask->sa_len + (char *)ifa->ifa_netmask;
		for (; cp3 < cplim; cp3++)
			if ((*cp++ ^ *cp2++) & *cp3)
				break;
		if (cp3 == cplim)
			return (ifa);
	}
	return (ifa_maybe);
}

#include <net/route.h>

/*
 * Default action when installing a route with a Link Level gateway.
 * Lookup an appropriate real ifa to point to.
 * This should be moved to /sys/net/link.c eventually.
 */
void
link_rtrequest(int cmd, register struct rtentry *rt, struct sockaddr *sa)
{
	register struct ifaddr *ifa;
	struct sockaddr *dst;
	struct ifnet *ifp;

	if (cmd != RTM_ADD || ((ifa = rt->rt_ifa) == 0) ||
	    ((ifp = ifa->ifa_ifp) == 0) || ((dst = rt_key(rt)) == 0))
		return;
	if ((ifa = ifaof_ifpforaddr(dst, ifp))) {
		rt->rt_ifa = ifa;
		if (ifa->ifa_rtrequest && 
		    ifa->ifa_rtrequest != link_rtrequest)
			ifa->ifa_rtrequest(cmd, rt, sa);
	}
}

/*
 * Mark an interface down and notify protocols of
 * the transition.
 * NOTE: must be called at splnet or eqivalent.
 */
void
if_down(ifp)
	register struct ifnet *ifp;
{
	register struct ifaddr *ifa;

	ifp->if_flags &= ~IFF_UP;
	for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next)
		pfctlinput(PRC_IFDOWN, ifa->ifa_addr);
	if_qflush(&ifp->if_snd);
}

/*
 * Flush an interface queue.
 */
void
if_qflush(ifq)
	register struct ifqueue *ifq;
{
	register struct mbuf *m, *n;

	n = ifq->ifq_head;
	while ((m = n)) {
		n = m->m_act;
		m_freem(m);
	}
	ifq->ifq_head = 0;
	ifq->ifq_tail = 0;
	ifq->ifq_len = 0;
}

/*
 * Handle interface watchdog timer routines.  Called
 * from softclock, we decrement timers (if set) and
 * call the appropriate interface routine on expiration.
 */
/*
 * PORT (AmiTCP_NG): coarse seconds clock, refreshed here once per tick (~1 s).
 *
 * arpresolve() needs "what time is it" on the SEND path of every packet, to pace
 * ARP requests and to decide when a resolved entry should be re-checked. Calling
 * get_time() there would add a timer.device library-vector call to a path that
 * already carries a semaphore -- on a 68000, per packet, in exactly the
 * high-rate multi-connection case the pacing exists to survive. Real BSD reads a
 * global the clock interrupt maintains; this is the same idea, and 1 s
 * granularity is finer than anything that consumes it (a 1/s rate limit and 60 s
 * thresholds).
 */
unsigned long ng_now_secs = 0;

void
if_slowtimo()
{
	register struct ifnet *ifp;
	spl_t s;
	struct timeval tvnow;

	get_time(&tvnow);
	ng_now_secs = (unsigned long)tvnow.tv_sec;

	s = splimp();

	/*
	 * PORT (AmiTCP_NG): re-enabled (was fully compiled out under #ifndef AMITCP
	 * -- "no timeouts in our network interfaces"). Walk the interfaces and fire
	 * each one's if_watchdog when its if_timer counts down to 0. The SANA driver
	 * uses this to re-arm receive requests retired under mbuf-pool pressure (see
	 * sana_watchdog / sana_rearm_reads in net/if_sana.c) -- the backstop that
	 * recovers a receive ring bled to zero, which no device completion would wake.
	 * The if_watchdog is called with the ifnet pointer (AmiTCP_NG convention; the
	 * only watchdog in the tree, sana_watchdog, expects it). The ~1 s periodic
	 * timer that calls this is scheduled from amiga_time.c.
	 */
	for (ifp = ifnet; ifp; ifp = ifp->if_next) {
		if (ifp->if_timer == 0 || --ifp->if_timer)
			continue;
		if (ifp->if_watchdog)
			(*ifp->if_watchdog)(ifp);
	}
	splx(s);
#ifndef AMITCP
	/*
	 * Timeouts are scheduled from amiga_time.c in AmiTCP/IP.
	 */
	timeout(if_slowtimo, (caddr_t)0, hz / IFNET_SLOWHZ);
#endif
}

/*
 * Map interface name to
 * interface structure pointer.
 */
struct ifnet *
ifunit(name)
	register char *name;
{
	register struct ifnet *ifp;
	register char *cp;
	int unit;
	unsigned len;
	char *ep, c;

	/*
	 * PORT (AmiTCP_NG): stock ifunit() REQUIRED a trailing digit and returned
	 * NULL for a name without one -- so an interface named e.g. "wifipi" (a
	 * Roadshow-style name taken verbatim from the config file, no unit suffix)
	 * could never be looked up, and AddNetInterface failed with ENXIO right
	 * after successfully creating it. A name with no digit is now treated as the
	 * whole base with unit 0 (the code below already null-terminates at cp and
	 * parses zero digits, so it Just Works). Only a name that fills IFNAMSIZ with
	 * no terminator is still rejected.
	 *
	 * PORT (AmiTCP_NG): the unit is the TRAILING run of digits, not the first
	 * digit found. Splitting at the first digit assumed a name never STARTS with
	 * one, which is not true of the names people actually use: a 3Com PCMCIA card
	 * config file called "3c589" split into base "" + unit 3, so the interface
	 * came out named "3" (github issue #6). Trailing-run splitting round-trips
	 * every name -- "3c589" -> "3c"+589 -> "3c589", "eth0" -> "eth"+0, "wifipi"
	 * -> "wifipi"+0 -- and MUST match sana_add_interface() in net/if_sana.c, or an
	 * interface is created under a name this cannot find.
	 */
	/*
	 * PORT (AmiTCP_NG): refuse a NULL name instead of walking from address 0.
	 * The loop below dereferences `name` with no check, so a NULL argument reads
	 * bytes from location 0 -- on a machine with no MMU that is a silent read of
	 * the exception vector table, and Enforcer reports it as
	 * "BYTE-READ from 00000000 ... MOVE.B (A1)+,D1".
	 *
	 * Every caller found today either passes an embedded ifr_name array (which
	 * cannot be NULL) or guards first -- ng_canon_ifname() in the Roadshow layer
	 * does exactly that, one call above this one. So this is hardening, not a
	 * known live bug. It is worth having anyway: a lookup helper this widely
	 * called should not depend on every caller remembering, and the same class of
	 * omission in vcsprintf()'s %s was a real hole.
	 */
	if (name == NULL)
		return ((struct ifnet *)0);

	for (cp = name; cp < name + IFNAMSIZ && *cp; cp++)
		;
	if (cp == name + IFNAMSIZ)
		return ((struct ifnet *)0);
	while (cp > name && cp[-1] >= '0' && cp[-1] <= '9')
		cp--;
	/*
	 * Save first char of unit, and pointer to it,
	 * so we can put a null there to avoid matching
	 * initial substrings of interface names.
	 */
	len = cp - name + 1;
	c = *cp;
	ep = cp;
	/*
	 * Clamped to what if_unit (a short) can actually hold. Both this and
	 * sana_add_interface() clamp the same way: if one stored a truncated unit and
	 * the other compared the full value, the interface would be unfindable.
	 */
	for (unit = 0; *cp >= '0' && *cp <= '9'; ) {
		unit = unit * 10 + *cp++ - '0';
		if (unit > 32767) {
			unit = 32767;
			while (*cp >= '0' && *cp <= '9')
				cp++;
			break;
		}
	}
	*ep = 0;

	for (ifp = ifnet; ifp; ifp = ifp->if_next) {
		if (bcmp(ifp->if_name, name, len))
			continue;
		if (unit == ifp->if_unit)
			break;
	}
	*ep = c;
	{
	    extern struct ifnet *iface_find(char *, short unit);
	    if (ifp == 0)
		ifp = iface_find(name, unit);
	}
	return (ifp);
}

/*
 * Interface ioctls.
 */
int
ifioctl(so, cmd, data)
	struct socket *so;
	int cmd;
	caddr_t data;
{
	register struct ifnet *ifp;
	register struct ifreq *ifr;
#ifndef AMITCP
	int error;
#endif
	extern int arpioctl(int cmd, caddr_t data);

	switch (cmd) {

	case SIOCGIFCONF:
		return (ifconf(cmd, data));

#ifdef COMPAT_AMITCP2
	case ASIOCGIFCONF:
		return (aifconf(cmd, data));
#endif

#if INET && NETHER > 0
	case SIOCSARP:
	case SIOCDARP:
#ifndef AMITCP /* no protection on AmigaOS */
		if (error = suser(p->p_ucred, &p->p_acflag))
			return (error);
		/* FALL THROUGH */
#else
	case SIOCGARPT:
#endif /* AMITCP */
	case SIOCGARP:
		return (arpioctl(cmd, data));
#endif
	}

#ifndef COMPAT_AMITCP2
	/* Do we have old ioctl? */
	if (IOCGROUP(cmd) == 'I') {
		struct aifreq *aifr;
		aifr = (struct aifreq *)data;
		/* Nobody needs interface name after we have got ifp */
		ifp = aifunit(aifr->ifr_name);
		ifr = (struct ifreq *)(data + AIFNAMSIZ - IFNAMSIZ);
		cmd -= ASIOCSIFADDR - SIOCSIFADDR;
	} 
	else 
#endif
	{
		ifr = (struct ifreq *)data;
		ifp = ifunit(ifr->ifr_name);
	}

	if (ifp == 0)
		return (ENXIO);
	switch (cmd) {

	case SIOCGIFFLAGS:
		ifr->ifr_flags = ifp->if_flags;
		break;

	case SIOCGIFMETRIC:
		ifr->ifr_metric = ifp->if_metric;
		break;

	case SIOCSIFFLAGS:
#ifndef AMITCP /* no protection on AmigaOS */
		if (error = suser(p->p_ucred, &p->p_acflag))
			return (error);
#endif /* AMITCP */
		/* if_down() is kludged for Sana-II driver ioctl */
		if (ifp->if_flags & IFF_UP && (ifr->ifr_flags & IFF_UP) == 0) {
			spl_t s = splimp();
			if_down(ifp);
			splx(s);
		}
		ifp->if_flags = (ifp->if_flags & IFF_CANTCHANGE) |
			(ifr->ifr_flags &~ IFF_CANTCHANGE);
		if (ifp->if_ioctl)
			(void) (*ifp->if_ioctl)(ifp, cmd, data);
		break;

	case SIOCSIFMETRIC:
#ifndef AMITCP /* no protection on AmigaOS */
		if (error = suser(p->p_ucred, &p->p_acflag))
			return (error);
#endif /* AMITCP */
		ifp->if_metric = ifr->ifr_metric;
		break;

	default:
		if (so->so_proto == 0)
			return (EOPNOTSUPP);
		return ((*so->so_proto->pr_usrreq)(so,
						   PRU_CONTROL,
						   (struct mbuf *)cmd,
						   (struct mbuf *)ifr,
						   (struct mbuf *)ifp));
	}
	return (0);
}

/*
 * Return interface configuration
 * of system.  List may be used
 * in later ioctl's (above) to get
 * other information.
 */
int
ifconf(cmd, data)
	int cmd;
	caddr_t data;
{
	register struct ifconf *ifc = (struct ifconf *)data;
	register struct ifnet *ifp = ifnet;
	register struct ifaddr *ifa;
	register char *cp, *ep;
	struct ifreq ifr, *ifrp;
	int space = ifc->ifc_len, error = 0;

	ifrp = ifc->ifc_req;
#ifndef AMITCP
	ep = ifr.ifr_name + sizeof (ifr.ifr_name) - 2;
#endif
	for (; space > sizeof (ifr) && ifp; ifp = ifp->if_next) {
#ifdef AMITCP
		ep = sprint_d(ifp->if_unit, ifr.ifr_name, sizeof(ifr.ifr_name));
		/* Copy the interface name into ifr */
		bcopy(ifp->if_name, ifr.ifr_name, ep - ifr.ifr_name);
		/* Find the end of interface name */
		for (cp = ifr.ifr_name; cp < ep && *cp; cp++)
			;
		/* Append unit number to it */
		for (; (*cp = *ep); cp++, ep++)
			;
#else
		bcopy(ifp->if_name, ifr.ifr_name, sizeof (ifr.ifr_name) - 2);
		for (cp = ifr.ifr_name; cp < ep && *cp; cp++)
			;
		*cp++ = '0' + ifp->if_unit; *cp = '\0';
#endif
		if ((ifa = ifp->if_addrlist) == 0) {
			aligned_bzero_const((caddr_t)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
#ifdef AMITCP
			*ifrp = ifr;
#else
			error = copyout((caddr_t)&ifr, (caddr_t)ifrp, sizeof (ifr));
			if (error)
				break;
#endif
			space -= sizeof (ifr), ifrp++;
		} else 
		    for ( ; space > sizeof (ifr) && ifa; ifa = ifa->ifa_next) {
			register struct sockaddr *sa = ifa->ifa_addr;
#ifdef COMPAT_43
			if (cmd == OSIOCGIFCONF) {
				struct osockaddr *osa =
					 (struct osockaddr *)&ifr.ifr_addr;
				ifr.ifr_addr = *sa;
				osa->sa_family = sa->sa_family;
				error = copyout((caddr_t)&ifr, (caddr_t)ifrp,
						sizeof (ifr));
				ifrp++;
			} else
#endif
			if (sa->sa_len <= sizeof(*sa)) {
				ifr.ifr_addr = *sa;
#ifdef AMITCP
				*ifrp = ifr;
#else
				error = copyout((caddr_t)&ifr, (caddr_t)ifrp,
						sizeof (ifr));
#endif
				ifrp++;
			} else {
				/*
				 * PORT (AmiTCP_NG) fix: keep this comparison
				 * signed. `space` is int and sizeof() is size_t,
				 * so once repeated iterations drove space
				 * negative the test converted it to a huge
				 * unsigned value, skipped the break, and let the
				 * bcopy below run past the caller's ifc_req
				 * buffer. Unreachable in this build (only
				 * sockaddr_in, whose sa_len is exactly
				 * sizeof(struct sockaddr), is ever attached --
				 * the AF_LINK code that would produce a longer
				 * one is #ifndef AMITCP), but the guard should
				 * not depend on that staying true.
				 */
				space -= sa->sa_len - sizeof(*sa);
				if (space < (int)sizeof (ifr))
					break;
#ifdef AMITCP
				aligned_bcopy_const((caddr_t)&ifr, 
						    (caddr_t)ifrp,
						    sizeof (ifr.ifr_name));
				aligned_bcopy((caddr_t)sa,
				      (caddr_t)&ifrp->ifr_addr, sa->sa_len);
#else
				error = copyout((caddr_t)&ifr, (caddr_t)ifrp,
						sizeof (ifr.ifr_name));
				if (error == 0)
				    error = copyout((caddr_t)sa,
				      (caddr_t)&ifrp->ifr_addr, sa->sa_len);
#endif
				ifrp = (struct ifreq *)
					(sa->sa_len + (caddr_t)&ifrp->ifr_addr);
			}
#ifndef AMITCP
			if (error)
				break;
#endif
			space -= sizeof (ifr);
		}
	}
	ifc->ifc_len -= space;
	return (error);
}

int aifconf(int cmd, caddr_t data)
{
	return ENOSYS;
}

static char *
sprint_d(n, buf, buflen)
	u_int n;
	char *buf;
	int buflen;
{
	register char *cp = buf + buflen - 1;

	*cp = 0;
	do {
		cp--;
		*cp = "0123456789"[n % 10];
		n /= 10;
	} while (n != 0);
	return (cp);
}