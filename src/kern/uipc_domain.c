RCS_ID_C="$Id: uipc_domain.c,v 1.11 1993/06/04 11:16:15 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: uipc_domain.c,v $
 * Revision 1.11  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.10  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.9  1993/04/06  15:16:00  jraja
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 *
 * Revision 1.8  93/03/13  17:13:03  17:13:03  ppessi (Pekka Pessi)
 * Fixed bugs with variable initializations. Works with UDP.
 * 
 * Revision 1.7  93/03/05  03:26:19  03:26:19  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.6  93/03/04  09:57:53  09:57:53  jraja (Jarno Tapio Rajahalme)
 * Fixed includes.
 * 
 * Revision 1.5  93/03/03  19:56:06  19:56:06  jraja (Jarno Tapio Rajahalme)
 * Moved 'domains' definition here from sys/domain.h.
 * 
 * Revision 1.4  93/02/24  12:55:09  12:55:09  jraja (Jarno Tapio Rajahalme)
 * Changed init to remember if initialized.
 * 
 * Revision 1.3  93/02/24  10:28:25  10:28:25  jraja (Jarno Tapio Rajahalme)
 * Changed max_linkhdr to 0.
 * 
 * Revision 1.2  93/02/24  09:59:36  09:59:36  jraja (Jarno Tapio Rajahalme)
 * Fixed timeout()'s, commented out unix and route domains.
 * 
 * Revision 1.1  92/11/19  12:06:47  12:06:47  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
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
 *	@(#)uipc_domain.c	7.9 (Berkeley) 3/4/91
 */

/*
 * uipc_domain.c --- the protocol switch (protosw / domain) glue.
 *
 * Stock 4.4BSD. This is the small but pivotal file that lets the socket layer be
 * protocol-agnostic. A `domain` is a family (here the Internet domain, AF_INET,
 * defined in netinet/in_proto.c) containing a table of `protosw` entries -- one
 * per protocol (TCP, UDP, ICMP, raw IP). Each protosw is a set of function
 * pointers: pr_input (from below), pr_output (from above), pr_usrreq (from the
 * socket layer), and pr_ctlinput/pr_init/pr_fasttimo/pr_slowtimo. See sys/protosw.h.
 *
 * domaininit() (called from init_all) walks every domain and runs each protocol's
 * pr_init(). pffindproto()/pffindtype() look up the right protosw when a socket is
 * created, so socreate() knows which protocol to attach. Once you understand this
 * dispatch table, the rest of the netinet/ code reads as "fill in the boxes".
 * Compare TCP/IP Illustrated Vol 2 chapter 7.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>

#include <kern/amiga_includes.h>
#include <kern/uipc_domain_protos.h>

/* --- start moved from sys/domain.h --- */
struct	domain *domains = NULL;
/* --- end moved from sys/domain.h --- */

static BOOL domain_initialized = FALSE;

#define	ADDDOMAIN(x)	{ \
	extern struct domain __CONCAT(x,domain); \
	__CONCAT(x,domain.dom_next) = domains; \
	domains = &__CONCAT(x,domain); \
}

BOOL
domaininit()
{
	register struct domain *dp;
	register struct protosw *pr;

	if (domain_initialized)
	  return TRUE;

#undef unix
#ifndef AMITCP
	ADDDOMAIN(unix);
#endif /* AMITCP */
	ADDDOMAIN(route);
#if INET
	ADDDOMAIN(inet);
#endif
#if NS
	ADDDOMAIN(ns);
#endif
#if ISO
	ADDDOMAIN(iso);
#endif
#if RMP
	ADDDOMAIN(rmp);
#endif
#if CCITT
	ADDDOMAIN(ccitt);
#endif
#if NIMP > 0
	ADDDOMAIN(imp);
#endif

	for (dp = domains; dp; dp = dp->dom_next) {
		if (dp->dom_init)
			(*dp->dom_init)();
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_init)
				(*pr->pr_init)();
	}
#ifdef AMITCP
  /*
   * No space needed for the link header with SanaII drivers
   */
  max_linkhdr = 0;
#else
if (max_linkhdr < 16)		/* XXX */
max_linkhdr = 16;
#endif
	max_hdr = max_linkhdr + max_protohdr;
	max_datalen = MHLEN - max_hdr;

#ifndef AMITCP
	/*
	 * Timeouts are scheduled from amiga_main.c in AmiTCP/IP
	 */
	pffasttimo();
	pfslowtimo();
#endif
	domain_initialized = TRUE;
	return TRUE;
}

struct protosw *
pffindtype(family, type)
	int family, type;
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			goto found;
	return (0);
found:
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
		if (pr->pr_type && pr->pr_type == type)
			return (pr);
	return (0);
}

struct protosw *
pffindproto(family, protocol, type)
	int family, protocol, type;
{
	register struct domain *dp;
	register struct protosw *pr;
	struct protosw *maybe = 0;

	if (family == 0)
		return (0);
	for (dp = domains; dp; dp = dp->dom_next)
		if (dp->dom_family == family)
			goto found;
	return (0);
found:
	for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++) {
		if ((pr->pr_protocol == protocol) && (pr->pr_type == type))
			return (pr);

		if (type == SOCK_RAW && pr->pr_type == SOCK_RAW &&
		    pr->pr_protocol == 0 && maybe == (struct protosw *)0)
			maybe = pr;
	}
	return (maybe);
}

void
pfctlinput(cmd, sa)
	int cmd;
	struct sockaddr *sa;
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_ctlinput)
				(*pr->pr_ctlinput)(cmd, sa, (caddr_t) 0);
}

void
pfslowtimo()
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_slowtimo)
				(*pr->pr_slowtimo)();
#ifndef AMITCP
	/*
	 * Timeouts are scheduled from amiga_main.c in AmiTCP/IP
	 */
	timeout(pfslowtimo, (caddr_t)0, hz/2);
#endif /* AMITCP */       
}

void
pffasttimo()
{
	register struct domain *dp;
	register struct protosw *pr;

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_fasttimo)
				(*pr->pr_fasttimo)();
#ifndef AMITCP
	/*
	 * Timeouts are scheduled from amiga_main.c in AmiTCP/IP
	 */
	timeout(pffasttimo, (caddr_t)0, hz/5);
#endif /* AMITCP */       
}
