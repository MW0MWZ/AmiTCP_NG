RCS_ID_C="$Id: in_proto.c,v 1.8 1993/06/04 11:16:15 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: in_proto.c,v $
 * Revision 1.8  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.7  1993/05/17  00:16:44  ppessi
 * Changed RCS version. Added rcsid.
 *
 * Revision 1.6  1993/04/11  22:25:48  jraja
 * Added STKARGFUN to protocol input & output functions (if used in protosw).
 *
 * Revision 1.5  93/04/05  19:05:59  19:05:59  jraja (Jarno Tapio Rajahalme)
 * Changed storage of the spl functions  return values to type spl_t.
 * Added include for conf.h to every .c file.
 * 
 * Revision 1.4  93/03/05  21:09:33  21:09:33  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.3  93/03/05  03:19:56  03:19:56  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.2  93/02/26  09:00:07  09:00:07  jraja (Jarno Tapio Rajahalme)
 * Made this compile with ANSI C (added prototypes).
 * 
 * Revision 1.1  92/11/17  16:29:08  16:29:08  jraja (Jarno Tapio Rajahalme)
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
 *	@(#)in_proto.c	7.5 (Berkeley) 6/28/90
 */

/*
 * in_proto.c --- the Internet domain's protocol switch table. Stock 4.4BSD.
 *
 * Small but central: this file DEFINES the `protosw inetsw[]` array and the
 * `domain inetdomain` -- the concrete table that uipc_domain.c's dispatch machinery
 * walks. Each row wires one protocol's function pointers into the stack, e.g.:
 *   { SOCK_DGRAM,  &inetdomain, IPPROTO_UDP,  ..., udp_input, 0, udp_ctlinput,
 *     udp_ctloutput, udp_usrreq, udp_init, ... }
 *   { SOCK_STREAM, &inetdomain, IPPROTO_TCP,  ..., tcp_input, 0, tcp_ctlinput,
 *     tcp_ctloutput, tcp_usrreq, tcp_init, tcp_fasttimo, tcp_slowtimo, ... }
 *   plus rows for raw IP, ICMP, and the raw wildcard.
 * Want to know which function handles incoming UDP, or which timeout TCP registers?
 * It is all right here in one table. Read this next to sys/protosw.h and
 * kern/uipc_domain.c. See TCP/IP Illustrated Vol 2 chapter 7.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <netinet/in_proto_protos.h>

/*
 * TCP/IP protocol family: IP, ICMP, UDP, TCP.
 */
#ifdef AMITCP
#include <netinet/tcp.h>
#include <netinet/ip_icmp_protos.h>
#include <netinet/ip_input_protos.h>
#include <netinet/ip_output_protos.h>
#include <netinet/raw_ip_protos.h>
#include <netinet/tcp_input_protos.h>
#include <netinet/tcp_subr_protos.h>
#include <netinet/tcp_timer_protos.h>
#include <netinet/tcp_usrreq_protos.h>
#include <netinet/udp_usrreq_protos.h>
#else
int	ip_output(),ip_ctloutput();
int	ip_init(),ip_slowtimo(),ip_drain();
int	icmp_input();
int	udp_input(),udp_ctlinput();
int	udp_usrreq();
int	udp_init();
int	tcp_input(),tcp_ctlinput();
int	tcp_usrreq(),tcp_ctloutput();
int	tcp_init(),tcp_fasttimo(),tcp_slowtimo(),tcp_drain();
int	rip_input(),rip_output(),rip_ctloutput(), rip_usrreq();
#endif

/*
 * IMP protocol family: raw interface.
 * Using the raw interface entry to get the timer routine
 * in is a kludge.
 */
#if NIMP > 0
int	rimp_output(), hostslowtimo();
#endif

#if NSIP
int	idpip_input(), nsip_ctlinput();
#endif

#if TPIP
int	tpip_input(), tpip_ctlinput(), tp_ctloutput(), tp_usrreq();
int	tp_init(), tp_slowtimo(), tp_drain();
#endif

#if EON
int	eoninput(), eonctlinput(), eonprotoinit();
#endif /* EON */

extern	struct domain inetdomain;

struct protosw inetsw[] = {
{ 0,		&inetdomain,	0,		0,
  0,		ip_output,	0,		0,
  0,
  ip_init,	0,		ip_slowtimo,	ip_drain,
},
{ SOCK_DGRAM,	&inetdomain,	IPPROTO_UDP,	PR_ATOMIC|PR_ADDR,
  udp_input,	0,		(void (*)(int, struct sockaddr *, void *))udp_ctlinput,	ip_ctloutput,
  udp_usrreq,
  udp_init,	0,		0,		0,
},
{ SOCK_STREAM,	&inetdomain,	IPPROTO_TCP,	PR_CONNREQUIRED|PR_WANTRCVD,
  tcp_input,	0,		(void (*)(int, struct sockaddr *, void *))tcp_ctlinput,	tcp_ctloutput,
  tcp_usrreq,
  tcp_init,	tcp_fasttimo,	tcp_slowtimo,	tcp_drain,
},
{ SOCK_RAW,	&inetdomain,	IPPROTO_RAW,	PR_ATOMIC|PR_ADDR,
  rip_input,	rip_output,	0,		rip_ctloutput,
  rip_usrreq,
  0,		0,		0,		0,
},
{ SOCK_RAW,	&inetdomain,	IPPROTO_ICMP,	PR_ATOMIC|PR_ADDR,
  icmp_input,	rip_output,	0,		rip_ctloutput,
  rip_usrreq,
  0,		0,		0,		0,
},
#if TPIP
{ SOCK_SEQPACKET,&inetdomain,	IPPROTO_TP,	PR_CONNREQUIRED|PR_WANTRCVD,
  tpip_input,	0,		tpip_ctlinput,		tp_ctloutput,
  tp_usrreq,
  tp_init,	0,		tp_slowtimo,	tp_drain,
},
#endif
/* EON (ISO CLNL over IP) */
#if EON
{ SOCK_RAW,	&inetdomain,	IPPROTO_EON,	0,
  eoninput,	0,		eonctlinput,		0,
  0,
  eonprotoinit,	0,		0,		0,
},
#endif
#if NSIP
{ SOCK_RAW,	&inetdomain,	IPPROTO_IDP,	PR_ATOMIC|PR_ADDR,
  idpip_input,	rip_output,	nsip_ctlinput,	0,
  rip_usrreq,
  0,		0,		0,		0,
},
#endif
	/* raw wildcard */
{ SOCK_RAW,	&inetdomain,	0,		PR_ATOMIC|PR_ADDR,
  rip_input,	rip_output,	0,		rip_ctloutput,
  rip_usrreq,
  0,		0,		0,		0,
},
};

struct domain inetdomain =
    { AF_INET, "internet", NULL, NULL, NULL, 
      inetsw, &inetsw[sizeof(inetsw)/sizeof(inetsw[0])], NULL };

#if NIMP > 0
extern	struct domain impdomain;

struct protosw impsw[] = {
{ SOCK_RAW,	&impdomain,	0,		PR_ATOMIC|PR_ADDR,
  0,		rimp_output,	0,		0,
  rip_usrreq,
  0,		0,		hostslowtimo,	0,
},
};

struct domain impdomain =
    { AF_IMPLINK, "imp", NULL, NULL, NULL,
      impsw, &impsw[sizeof (impsw)/sizeof(impsw[0])], NULL };
#endif

#if NHY > 0
/*
 * HYPERchannel protocol family: raw interface.
 */
int	rhy_output();
extern	struct domain hydomain;

struct protosw hysw[] = {
{ SOCK_RAW,	&hydomain,	0,		PR_ATOMIC|PR_ADDR,
  0,		rhy_output,	0,		0,
  rip_usrreq,
  0,		0,		0,		0,
},
};

struct domain hydomain =
    { AF_HYLINK, "hy", NULL, NULL, NULL,
      hysw, &hysw[sizeof (hysw)/sizeof(hysw[0])], NULL };
#endif
