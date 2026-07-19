/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: route.c,v 1.14 1993/06/04 11:16:15 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * route.c --- Generic Routing Routines
 *
 * Last modified: Fri Jun  4 00:39:33 1993 jraja
 *
 * HISTORY
 * $Log: route.c,v $
 * Revision 1.14  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.13  1993/05/16  21:09:43  ppessi
 * RCS version changed.
 *
 * Revision 1.12  1993/05/04  12:53:55  jraja
 * Enabled rtioctl() with AMITCP.
 *
 * Revision 1.11  93/05/02  17:31:25  17:31:25  jraja (Jarno Tapio Rajahalme)
 * Updated rtioctl().
 * 
 * Revision 1.10  93/04/05  17:46:20  17:46:20  jraja (Jarno Tapio Rajahalme)
 * Changed spl storage variables to spl_t.
 * Changed every .c file to use conf.h.
 * 
 * Revision 1.9  93/03/13  17:13:42  17:13:42  ppessi (Pekka Pessi)
 * Fixed variable initialization problems.
 * 
 * Revision 1.8  93/03/05  19:51:09  19:51:09  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.7  93/03/05  03:12:52  03:12:52  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version
 * 
 * Revision 1.6  93/02/25  19:52:45  19:52:45  ppessi (Pekka Pessi)
 *  Added prototypes
 * 
 * Revision 1.5  93/02/25  16:33:05  16:33:05  jraja (Jarno Tapio Rajahalme)
 * Removed SocketBase argument from rtioctl.
 * 
 * Revision 1.4  93/02/19  15:39:37  15:39:37  jraja (Jarno Tapio Rajahalme)
 * Changed rtioctl's proc argument to SocketBase.
 * 
 * Revision 1.3  93/02/04  19:03:08  19:03:08  jraja (Jarno Tapio Rajahalme)
 * Fixed bsd_free() and free() to Free().
 * 
 * Revision 1.2  92/11/20  14:43:36  14:43:36  jraja (Jarno Tapio Rajahalme)
 * added #ifndef AMITCP's to get this compile somehow
 * 
 * Revision 1.1  92/11/20  13:32:42  13:32:42  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 *
 */

/* 
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon 
 * the rights to redistribute these changes.
 */
/*
 * HISTORY
 * Log:	route.c,v
 * Revision 2.1  92/04/21  17:13:54  rwd
 * BSDSS
 * 
 *
 */

/*
 * Copyright (c) 1980, 1986, 1991 Regents of the University of California.
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
 *	@(#)route.c	7.22 (Berkeley) 6/27/91
 */

/*
 * route.c --- the routing table and route lookups. Stock 4.4BSD.
 *
 * The routing table answers "to reach destination D, which interface do I use and
 * what is the next hop?". ip_output() consults it for every outgoing packet.
 * Entries are `rtentry`s stored in a radix (PATRICIA) tree keyed by destination
 * address + mask -- the tree itself is net/radix.c; this file is the network-facing
 * layer over it.
 *   rtalloc / rtalloc1   look up the route for a destination (longest-prefix match:
 *                        host route, else network route, else the default route).
 *   rtrequest            add/delete/change a route (RTM_ADD/RTM_DELETE/...).
 *   rtredirect           apply an ICMP redirect.
 *   rtinit               install the route implied by assigning an address to an
 *                        interface (this is what makes SIOCSIFADDR reach a host).
 * Routes are cached in the pcb (in_pcb.c) so hot connections skip the lookup.
 * The routing SOCKET (how user tools like `route`/`ifconfig` read and modify the
 * table) is net/rtsock.c. See TCP/IP Illustrated Vol 2 chapters 18-19.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/ioctl.h>
#include <sys/synch.h>

#include <net/if.h>
#include <net/route.h>
#include <net/raw_cb.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#if NS
#include <netns/ns.h>
#endif
#include <net/netisr.h>

#include <net/rtsock_protos.h>
#include <net/radix_protos.h>
#include <netinet/in_protos.h>

#define	SA(p) ((struct sockaddr *)(p))

/* --- commons from route.h --- */
struct route_cb route_cb = {0};
#if 0 /* obsolete */
struct	mbuf *rthost[RTHASHSIZ] = {NULL};
struct	mbuf *rtnet[RTHASHSIZ] = {NULL};
#endif
struct	rtstat	rtstat = {0};
/* --- commons from route.h --- */

int    rttrash = 0;		/* routes not in table but not freed */
struct sockaddr wildcard = {0};	/* zero valued cookie for wildcard searches */
#if 0 /* obsolete */
int    rthashsize = RTHASHSIZ;	/* for netstat, etc. */
#endif

static int rtinits_done = 0;
struct radix_node_head *ns_rnhead = 0, *in_rnhead = 0;
struct radix_node *rn_match(), *rn_delete(), *rn_addroute();

void
rtinitheads()
{
	if (rtinits_done == 0 &&
#if NS
	    rn_inithead(&ns_rnhead, 16, AF_NS) &&
#endif
	    rn_inithead(&in_rnhead, 32, AF_INET))
		rtinits_done = 1;
}

/*
 * Packet routing routines.
 */
void
rtalloc(ro)
	register struct route *ro;
{
	if (ro->ro_rt && ro->ro_rt->rt_ifp && (ro->ro_rt->rt_flags & RTF_UP))
		return;				 /* XXX */
	ro->ro_rt = rtalloc1(&ro->ro_dst, 1);
}

struct rtentry *
rtalloc1(dst, report)
	register struct sockaddr *dst;
	int  report;
{
	register struct radix_node_head *rnh;
	register struct rtentry *rt;
	register struct radix_node *rn;
	struct rtentry *newrt = 0;
	int err = 0, msgtype = RTM_MISS;
	spl_t s = splnet();

	for (rnh = radix_node_head; rnh && (dst->sa_family != rnh->rnh_af); )
		rnh = rnh->rnh_next;
	if (rnh && rnh->rnh_treetop &&
	    (rn = rn_match((caddr_t)dst, rnh->rnh_treetop)) &&
	    ((rn->rn_flags & RNF_ROOT) == 0)) {
		newrt = rt = (struct rtentry *)rn;
		if (report && (rt->rt_flags & RTF_CLONING)) {
			if ((err = rtrequest(RTM_RESOLVE, dst, SA(0),
					      SA(0), 0, &newrt)) ||
			    ((rt->rt_flags & RTF_XRESOLVE)
			      && (msgtype = RTM_RESOLVE))) /* intended! */
			    goto miss;
		} else
			rt->rt_refcnt++;
	} else {
		rtstat.rts_unreach++;
	miss:	if (report)
			rt_missmsg(msgtype, dst, SA(0), SA(0), SA(0), 0, err);
	}
	splx(s);
	return (newrt);
}

void
rtfree(rt)
	register struct rtentry *rt;
{
	if (rt == 0)
		panic("rtfree");
	rt->rt_refcnt--;
	if (rt->rt_refcnt <= 0 && (rt->rt_flags & RTF_UP) == 0) {
		rttrash--;
		if (rt->rt_nodes->rn_flags & (RNF_ACTIVE | RNF_ROOT))
			panic ("rtfree 2");
		Free(rt);
	}
}

/*
 * Force a routing table entry to the specified
 * destination to go through the given gateway.
 * Normally called as a result of a routing redirect
 * message from the network layer.
 *
 * N.B.: must be called at splnet
 *
 */
void
rtredirect(dst, gateway, netmask, flags, src, rtp)
	struct sockaddr *dst, *gateway, *netmask, *src;
	int flags;
	struct rtentry **rtp;
{
	register struct rtentry *rt = NULL;
	int error = 0;
	short *stat = 0;

	/* verify the gateway is directly reachable */
	if (ifa_ifwithnet(gateway) == 0) {
		error = ENETUNREACH;
		goto done;
	}
	rt = rtalloc1(dst, 0);
	/*
	 * If the redirect isn't from our current router for this dst,
	 * it's either old or wrong.  If it redirects us to ourselves,
	 * we have a routing loop, perhaps as a result of an interface
	 * going down recently.
	 */
#define	equal(a1, a2) (bcmp((caddr_t)(a1), (caddr_t)(a2), (a1)->sa_len) == 0)
	if (!(flags & RTF_DONE) && rt && !equal(src, rt->rt_gateway))
		error = EINVAL;
	else if (ifa_ifwithaddr(gateway))
		error = EHOSTUNREACH;
	if (error)
		goto done;
	/*
	 * Create a new entry if we just got back a wildcard entry
	 * or the the lookup failed.  This is necessary for hosts
	 * which use routing redirects generated by smart gateways
	 * to dynamically build the routing tables.
	 */
	if ((rt == 0) || (rt_mask(rt) && rt_mask(rt)->sa_len < 2))
		goto create;
	/*
	 * Don't listen to the redirect if it's
	 * for a route to an interface. 
	 */
	if (rt->rt_flags & RTF_GATEWAY) {
		if (((rt->rt_flags & RTF_HOST) == 0) && (flags & RTF_HOST)) {
			/*
			 * Changing from route to net => route to host.
			 * Create new route, rather than smashing route to net.
			 */
		create:
			flags |=  RTF_GATEWAY | RTF_DYNAMIC;
			error = rtrequest((int)RTM_ADD, dst, gateway,
				    SA(0), flags,
				    (struct rtentry **)0);
			stat = &rtstat.rts_dynamic;
		} else {
			/*
			 * Smash the current notion of the gateway to
			 * this destination.  Should check about netmask!!!
			 */
			if (gateway->sa_len <= rt->rt_gateway->sa_len) {
				Bcopy(gateway, rt->rt_gateway, gateway->sa_len);
				rt->rt_flags |= RTF_MODIFIED;
				flags |= RTF_MODIFIED;
				stat = &rtstat.rts_newgateway;
			} else
				error = ENOSPC;
		}
	} else
		error = EHOSTUNREACH;
done:
	if (rt) {
		if (rtp && !error)
			*rtp = rt;
		else
			rtfree(rt);
	}
	if (error)
		rtstat.rts_badredirect++;
	else
		(void) (stat && (*stat)++);
	rt_missmsg(RTM_REDIRECT, dst, gateway, netmask, src, flags, error);
}

/*
* Routing table ioctl interface.
*/
int
rtioctl(req, data)
	int req;
	caddr_t data;
{
#if !defined(COMPAT_43) && !defined(AMITCP)
	return (EOPNOTSUPP);
#else
	register struct ortentry *entry = (struct ortentry *)data;
	int error;
	struct sockaddr netmask;

	if (req == SIOCADDRT)
		req = RTM_ADD;
	else if (req == SIOCDELRT)
		req = RTM_DELETE;
	else
		return (EINVAL);

#ifndef AMITCP /* no protection on AmigaOS */
	if (error = suser(p->p_ucred, &p->p_acflag))
		return (error);
#endif /* AMITCP */

#if BYTE_ORDER != BIG_ENDIAN
	if (entry->rt_dst.sa_family == 0 && entry->rt_dst.sa_len < 16) {
		entry->rt_dst.sa_family = entry->rt_dst.sa_len;
		entry->rt_dst.sa_len = 16;
	}
	if (entry->rt_gateway.sa_family == 0 && entry->rt_gateway.sa_len < 16) {
		entry->rt_gateway.sa_family = entry->rt_gateway.sa_len;
		entry->rt_gateway.sa_len = 16;
	}
#else
	/*
	 * PORT (AmiTCP_NG) security fix: rt_dst/rt_gateway in the legacy
	 * ortentry are each a fixed sizeof(struct sockaddr) (16 bytes). The original
	 * only defaulted a ZERO sa_len to 16; a caller-supplied LARGER value flowed
	 * unclamped into rtrequest()'s Bcopy(gateway, rt->rt_gateway, sa_len),
	 * splicing adjacent caller memory into the PERSISTENT routing table (info
	 * disclosure) and oversizing the route allocation. suser() is compiled out
	 * under AMITCP, so this SIOCADDRT path is unprivileged -- clamp it.
	 */
	if (entry->rt_dst.sa_len == 0)
		entry->rt_dst.sa_len = 16;
	else if (entry->rt_dst.sa_len > sizeof(struct sockaddr))
		return (EINVAL);
	if (entry->rt_gateway.sa_len == 0)
		entry->rt_gateway.sa_len = 16;
	else if (entry->rt_gateway.sa_len > sizeof(struct sockaddr))
		return (EINVAL);
#endif
	if ((entry->rt_flags & RTF_HOST) == 0)
		switch (entry->rt_dst.sa_family) {
#if INET
		case AF_INET:
			{
#if 0
			  /*
			   * Should not use icmpmask, since it is accessed from
			   * different tasks!
			   */
				extern struct sockaddr_in icmpmask;
				struct sockaddr_in *dst_in = 
					(struct sockaddr_in *)&entry->rt_dst;

				in_sockmaskof(dst_in->sin_addr, &icmpmask);
				netmask = (struct sockaddr *)&icmpmask;
#else
				in_sockmaskof(((struct sockaddr_in *)&entry->rt_dst)->sin_addr, (struct sockaddr_in *)&netmask);
#endif
			}
			break;
#endif
#if NS
		case AF_NS:
			{
				extern struct sockaddr_ns ns_netmask;
				netmask = (struct sockaddr)ns_netmask;
			}
#endif
		}
	error = rtrequest(req, &(entry->rt_dst), &(entry->rt_gateway),
			  (entry->rt_flags & RTF_HOST) ? NULL : &netmask,
			  entry->rt_flags, (struct rtentry **)0);
	rt_missmsg((req == RTM_ADD ? RTM_OLDADD : RTM_OLDDEL),
		   &(entry->rt_dst), &(entry->rt_gateway),
		   (entry->rt_flags & RTF_HOST) ? NULL : &netmask,
		   SA(0), entry->rt_flags, error);
	return (error);
#endif
}

struct ifaddr *
ifa_ifwithroute(flags, dst, gateway)
int	flags;
struct sockaddr	*dst, *gateway;
{
	register struct ifaddr *ifa;
	if ((flags & RTF_GATEWAY) == 0) {
		/*
		 * If we are adding a route to an interface,
		 * and the interface is a pt to pt link
		 * we should search for the destination
		 * as our clue to the interface.  Otherwise
		 * we can use the local address.
		 */
		ifa = 0;
		if (flags & RTF_HOST) 
			ifa = ifa_ifwithdstaddr(dst);
		if (ifa == 0)
			ifa = ifa_ifwithaddr(gateway);
	} else {
		/*
		 * If we are adding a route to a remote net
		 * or host, the gateway may still be on the
		 * other end of a pt to pt link.
		 */
		ifa = ifa_ifwithdstaddr(gateway);
	}
	if (ifa == 0)
		ifa = ifa_ifwithnet(gateway);
	if (ifa == 0) {
		struct rtentry *rt = rtalloc1(dst, 0);
		if (rt == 0)
			return (0);
		rt->rt_refcnt--;
		if ((ifa = rt->rt_ifa) == 0)
			return (0);
	}
	if (ifa->ifa_addr->sa_family != dst->sa_family) {
		struct ifaddr *oifa = ifa, *ifaof_ifpforaddr();
		ifa = ifaof_ifpforaddr(dst, ifa->ifa_ifp);
		if (ifa == 0)
			ifa = oifa;
	}
	return (ifa);
}

#define ROUNDUP(a) (a>0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int
rtrequest(req, dst, gateway, netmask, flags, ret_nrt)
	int req, flags;
	struct sockaddr *dst, *gateway, *netmask;
	struct rtentry **ret_nrt;
{
	int len, error = 0;
	spl_t s = splnet();
	register struct rtentry *rt;
	register struct radix_node *rn;
	register struct radix_node_head *rnh;
	struct ifaddr *ifa, *ifa_ifwithdstaddr();
	struct sockaddr *ndst;
	u_char af = dst->sa_family;
#define senderr(x) { error = x ; goto bad; }

	if (rtinits_done == 0)
		rtinitheads();
	for (rnh = radix_node_head; rnh && (af != rnh->rnh_af); )
		rnh = rnh->rnh_next;
	if (rnh == 0)
		senderr(ESRCH);
	if (flags & RTF_HOST)
		netmask = 0;
	switch (req) {
	case RTM_DELETE:
		if (ret_nrt && (rt = *ret_nrt)) {
			RTFREE(rt);
			*ret_nrt = 0;
		}
		if ((rn = rn_delete((caddr_t)dst, (caddr_t)netmask, 
					rnh->rnh_treetop)) == 0)
			senderr(ESRCH);
		if (rn->rn_flags & (RNF_ACTIVE | RNF_ROOT))
			panic ("rtrequest delete");
		rt = (struct rtentry *)rn;
		rt->rt_flags &= ~RTF_UP;
		if ((ifa = rt->rt_ifa) && ifa->ifa_rtrequest)
			ifa->ifa_rtrequest(RTM_DELETE, rt, SA(0));
		rttrash++;
		if (rt->rt_refcnt <= 0)
			rtfree(rt);
		break;

	case RTM_RESOLVE:
		if (ret_nrt == 0 || (rt = *ret_nrt) == 0)
			senderr(EINVAL);
		ifa = rt->rt_ifa;
		flags = rt->rt_flags & ~RTF_CLONING;
		gateway = rt->rt_gateway;
		if ((netmask = rt->rt_genmask) == 0)
			flags |= RTF_HOST;
		goto makeroute;

	case RTM_ADD:
		if ((ifa = ifa_ifwithroute(flags, dst, gateway)) == 0)
			senderr(ENETUNREACH);
	makeroute:
		len = sizeof (*rt) + ROUNDUP(gateway->sa_len)
		    + ROUNDUP(dst->sa_len);
		R_Malloc(rt, struct rtentry *, len);
		if (rt == 0)
			senderr(ENOBUFS);
		Bzero(rt, len);
		ndst = (struct sockaddr *)(rt + 1);
		if (netmask) {
			rt_maskedcopy(dst, ndst, netmask);
		} else
			Bcopy(dst, ndst, dst->sa_len);
		rn = rn_addroute((caddr_t)ndst, (caddr_t)netmask,
					rnh->rnh_treetop, rt->rt_nodes);
		if (rn == 0) {
			Free(rt);
			senderr(EEXIST);
		}
		rt->rt_ifa = ifa;
		rt->rt_ifp = ifa->ifa_ifp;
		rt->rt_flags = RTF_UP | flags;
		rt->rt_gateway = (struct sockaddr *)
					(rn->rn_key + ROUNDUP(dst->sa_len));
		Bcopy(gateway, rt->rt_gateway, gateway->sa_len);
		if (req == RTM_RESOLVE)
			rt->rt_rmx = (*ret_nrt)->rt_rmx; /* copy metrics */
		if (ifa->ifa_rtrequest)
			ifa->ifa_rtrequest(req, rt, SA(ret_nrt ? *ret_nrt : 0));
		if (ret_nrt) {
			*ret_nrt = rt;
			rt->rt_refcnt++;
		}
		break;
	}
bad:
	splx(s);
	return (error);
}

void
rt_maskedcopy(src, dst, netmask)
struct sockaddr *src, *dst, *netmask;
{
	register u_char *cp1 = (u_char *)src;
	register u_char *cp2 = (u_char *)dst;
	register u_char *cp3 = (u_char *)netmask;
	u_char *cplim = cp2 + *cp3;
	u_char *cplim2 = cp2 + *cp1;

	*cp2++ = *cp1++; *cp2++ = *cp1++; /* copies sa_len & sa_family */
	cp3 += 2;
	if (cplim > cplim2)
		cplim = cplim2;
	while (cp2 < cplim)
		*cp2++ = *cp1++ & *cp3++;
	if (cp2 < cplim2)
		bzero((caddr_t)cp2, (unsigned)(cplim2 - cp2));
}
/*
 * Set up a routing table entry, normally
 * for an interface.
 */
int
rtinit(ifa, cmd, flags)
	register struct ifaddr *ifa;
	int cmd, flags;
{
	register struct rtentry *rt;
	register struct sockaddr *dst;
	register struct sockaddr *deldst;
	struct mbuf *m = 0;
	int error;

	dst = flags & RTF_HOST ? ifa->ifa_dstaddr : ifa->ifa_addr;
	if (ifa->ifa_flags & IFA_ROUTE) {
		if ((rt = ifa->ifa_rt) && (rt->rt_flags & RTF_UP) == 0) {
			RTFREE(rt);
			ifa->ifa_rt = 0;
		}
	}
	if (cmd == RTM_DELETE) {
		if ((flags & RTF_HOST) == 0 && ifa->ifa_netmask) {
			m = m_get(M_WAIT, MT_SONAME);
			/* PORT (AmiTCP_NG): pool may be dry -- m_get can return
			 * NULL (M_WAIT does not block here). Nothing is allocated
			 * yet, so just report ENOBUFS. */
			if (m == 0)
				return (ENOBUFS);
			deldst = mtod(m, struct sockaddr *);
			rt_maskedcopy(dst, deldst, ifa->ifa_netmask);
			dst = deldst;
		}
		if ((rt = rtalloc1(dst, 0))) {
			rt->rt_refcnt--;
			if (rt->rt_ifa != ifa) {
				if (m)
					(void) m_free(m);
				return (flags & RTF_HOST ? EHOSTUNREACH
							: ENETUNREACH);
			}
		}
	}
	error = rtrequest(cmd, dst, ifa->ifa_addr, ifa->ifa_netmask,
			flags | ifa->ifa_flags, &ifa->ifa_rt);
	if (m)
		(void) m_free(m);
	if (cmd == RTM_ADD && error == 0 && (rt = ifa->ifa_rt)
						&& rt->rt_ifa != ifa) {
		rt->rt_ifa = ifa;
		rt->rt_ifp = ifa->ifa_ifp;
	}
	return (error);
}
