RCS_ID_C="$Id: if_loop.c,v 1.7 1993/06/04 11:16:15 jraja Exp $";
/* 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * if_loop.c -- Local Loopback Device Functions
 *
 * Last modified: Fri Jun  4 00:37:54 1993 jraja
 *
 * HISTORY
 * $Log: if_loop.c,v $
 * Revision 1.7  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.6  1993/05/16  21:09:43  ppessi
 * RCS version changed.
 *
 * Revision 1.5  1993/04/11  22:21:18  jraja
 * Removed unnecessary casts from function pointers, Changed ellipses to
 * prototypes.
 *
 * Revision 1.4  93/04/05  17:45:56  17:45:56  jraja (Jarno Tapio Rajahalme)
 * Changed spl storage variables to spl_t.
 * Changed every .c file to use conf.h.
 * 
 * Revision 1.3  93/03/13  17:13:22  17:13:22  ppessi (Pekka Pessi)
 * Fixed variable initialization problems.
 * 
 * Revision 1.2  93/03/02  18:49:09  18:49:09  too (Tomi Ollila)
 * Changed %? to %l? on format strings
 * 
 * Revision 1.1  93/02/25  16:19:15  16:19:15  ppessi (Pekka Pessi)
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
 *	@(#)if_loop.c	7.13 (Berkeley) 4/26/91
 */

/*
 * Loopback interface driver for protocol testing and timing.
 */

/*
 * if_loop.c --- the loopback interface, lo0.
 *
 * lo0 is a virtual interface with no hardware behind it: anything "transmitted"
 * on it is immediately handed back to the input path, so 127.0.0.1 talks to
 * itself. It is the simplest possible `ifnet` and therefore the best first
 * example of the interface contract (compare the far more involved net/if_sana.c).
 * docs/ARCHITECTURE.md section 7. This is the exact path tmp/udptest.c drives to
 * prove the whole socket->UDP->IP->interface->IP->UDP->socket round-trip.
 *
 *   loattach()  creates lo0 during startup and installs it in the interface list.
 *   looutput()  the if_output: instead of touching hardware, it loops the mbuf
 *               chain straight back up by scheduling it onto the matching protocol
 *               input queue (e.g. IP -> ipintr via schednetisr), so it re-enters
 *               ip_input() as if it had arrived from the network.
 *   loioctl()   handles SIOCSIFADDR etc. (bringing the interface up/addressed).
 *
 * NOTE: lo0 exists as soon as useloopback=YES, but it has no ADDRESS until
 * something does SIOCSIFADDR 127.0.0.1 on it (ifconfig lo0 localhost). Until then,
 * bind(127.0.0.1) fails with EADDRNOTAVAIL -- a gotcha worth remembering.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/synch.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/route.h>


#if	INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#if NS
#include <netns/ns.h>
#include <netns/ns_if.h>
#endif

#if ISO
#include <netiso/iso.h>
#include <netiso/iso_var.h>
#endif

#include <net/if_loop_protos.h>

#define	LOMTU	(1024+512)

struct	ifnet loif = {0};

void
loattach()
{
	register struct ifnet *ifp = &loif;

	ifp->if_name = "lo";
	ifp->if_mtu = LOMTU;
	ifp->if_flags = IFF_LOOPBACK;
	ifp->if_ioctl = loioctl;
	ifp->if_output = looutput;
	ifp->if_type = IFT_LOOP;
	ifp->if_hdrlen = 0;
	ifp->if_addrlen = 0;
	if_attach(ifp);
}

int
looutput(ifp, m, dst, rt)
	struct ifnet *ifp;
	register struct mbuf *m;
	struct sockaddr *dst;
	register struct rtentry *rt;
{
	int isr;
	spl_t s;
	register struct ifqueue *ifq = 0;

	if ((m->m_flags & M_PKTHDR) == 0)
		panic("looutput no HDR");
	m->m_pkthdr.rcvif = ifp;

	if (rt && rt->rt_flags & RTF_REJECT) {
		m_freem(m);
		return (rt->rt_flags & RTF_HOST ? EHOSTUNREACH : ENETUNREACH);
	}
	ifp->if_opackets++;
	ifp->if_obytes += m->m_pkthdr.len;
	switch (dst->sa_family) {

#if INET
	case AF_INET:
		ifq = &ipintrq;
		isr = NETISR_IP;
		break;
#endif
#if NS
	case AF_NS:
		ifq = &nsintrq;
		isr = NETISR_NS;
		break;
#endif
#if ISO
	case AF_ISO:
		ifq = &clnlintrq;
		isr = NETISR_ISO;
		break;
#endif
	default:
		printf("lo%ld: can't handle af%ld\n", ifp->if_unit,
			dst->sa_family);
		m_freem(m);
		return (EAFNOSUPPORT);
	}
	s = splimp();
	if (IF_QFULL(ifq)) {
		IF_DROP(ifq);
		m_freem(m);
		splx(s);
		return (ENOBUFS);
	}
	IF_ENQUEUE(ifq, m);
	schednetisr(isr);
	ifp->if_ipackets++;
	ifp->if_ibytes += m->m_pkthdr.len;
	splx(s);
	return (0);
}

void lortrequest(int cmd, 
		 struct rtentry *rt, 
		 struct sockaddr *sa)
{
	if (rt)
		rt->rt_rmx.rmx_mtu = LOMTU;
}

/*
 * Process an ioctl request.
 */
int
loioctl(register struct ifnet *ifp, int cmd, caddr_t data)
{
	register struct ifaddr *ifa;
	int error = 0;

	switch (cmd) {

	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		ifa = (struct ifaddr *)data;
		if (ifa != 0 && ifa->ifa_addr->sa_family == AF_ISO)
			ifa->ifa_rtrequest = lortrequest;
		/*
		 * Everything else is done at a higher level.
		 */
		break;

	default:
		error = EINVAL;
	}
	return (error);
}
