/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: uipc_socket.c,v 1.28 1993/12/18 15:28:09 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: uipc_socket.c,v $
 * Revision 1.28  1993/12/18  15:28:09  jraja
 * Disabled code using M_EOR, unless USE_M_EOR is defined.
 *
 * Revision 1.27  1993/11/17  12:12:48  jraja
 * Added check for the SB_SEL flag to sohasoutofband() to prevent superfluous
 * calls to selwakeup().
 *
 * Revision 1.26  1993/11/17  09:20:33  jraja
 * Fixed selwakeup bug in sohasoutofband(), there was mixture of net2 & bsdss
 * -style selwakeup usage.
 *
 * Revision 1.25  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.24  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.23  1993/04/28  13:42:48  too
 * Fixed setting of linger value in getsockopt()
 *
 * Revision 1.22  93/04/26  11:54:49  11:54:49  too (Tomi Ollila)
 * Changed include paths of amiga_api.h, amiga_libcallentry.h and amiga_raf.h
 * from kern to api
 * 
 * Revision 1.21  93/04/24  22:45:52  22:45:52  jraja (Jarno Tapio Rajahalme)
 * Removed Define for USECLUSTERS
 * 
 * Revision 1.20  93/04/24  17:52:15  17:52:15  jraja (Jarno Tapio Rajahalme)
 * Changed MCLBYTES to mbconf.mclbytes.
 * 
 * Revision 1.19  93/04/12  00:37:56  00:37:56  jraja (Jarno Tapio Rajahalme)
 * Added #include <kern/amiga_includes.h> for Signal().
 * 
 * Revision 1.18  93/04/12  00:29:11  00:29:11  jraja (Jarno Tapio Rajahalme)
 * Added signalling of task (SIGURG).
 * 
 * Revision 1.17  93/04/11  22:28:37  22:28:37  jraja (Jarno Tapio Rajahalme)
 * Removed one extra argument from call to pr_usrreq.
 * Added parenthesis around &&'s inside ||'s.
 * 
 * Revision 1.16  93/04/06  15:16:08  15:16:08  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.15  93/03/21  16:59:37  16:59:37  jraja (Jarno Tapio Rajahalme)
 * Changed linger time to seconds. Removed obsolete test on timeout value for
 * send/receive timeout (was checked to fit in a short, but since the timeouts are
 * now timeval's the check is not needed.
 * 
 * Revision 1.14  93/03/19  14:14:56  14:14:56  too (Tomi Ollila)
 * Code changes at night 17-18 March 1993
 * 
 * Revision 1.13  93/03/17  12:06:35  12:06:35  jraja (Jarno Tapio Rajahalme)
 * Fixed tsleep() call to find socket base if necessary and to give NULL
 * timeout if timeval is all zeroes.
 * 
 * Revision 1.12  93/03/16  19:47:16  19:47:16  too (Tomi Ollila)
 * uses multible mbufs (broken)
 * 
 * Revision 1.11  93/03/13  17:13:06  17:13:06  ppessi (Pekka Pessi)
 * Fixed bugs with variable initializations. Works with UDP.
 * 
 * Revision 1.10  93/03/05  03:26:23  03:26:23  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.9  93/03/04  10:57:17  10:57:17  jraja (Jarno Tapio Rajahalme)
 * Added prototype inclusion for kern/uipc_domain_protos_.h.h
 * 
 * Revision 1.8  93/03/04  09:53:45  09:53:45  jraja (Jarno Tapio Rajahalme)
 * Removed some redundant includes.
 * 
 * Revision 1.7  93/02/27  11:03:55  11:03:55  too (Tomi Ollila)
 * Added Prototypes
 * 
 * Revision 1.6  93/02/25  13:01:48  13:01:48  too (Tomi Ollila)
 * Added static inlines, sys/cdefs etc.
 * 
 * Revision 1.5  93/02/24  18:14:47  18:14:47  too (Tomi Ollila)
 * Compiles. support for so_linger and sb_timeo as struct timeval type added.
 * 
 * Revision 1.4  93/02/23  11:37:56  11:37:56  too (Tomi Ollila)
 * Made AMITCP combatible, not compiled yet
 * 
 * Revision 1.3  93/01/09  12:56:26  12:56:26  jraja (Jarno Tapio Rajahalme)
 * Added #ifdef USECLUSTERS to disable cluster dependant code.
 * 
 * Revision 1.2  92/11/20  15:14:49  15:14:49  jraja (Jarno Tapio Rajahalme)
 * Added #ifndef AMITCP's to make this compile.
 * 
 * Revision 1.1  92/11/19  12:07:27  12:07:27  jraja (Jarno Tapio Rajahalme)
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
 * Log:	uipc_socket.c,v
 * Revision 2.1  92/04/21  17:12:32  rwd
 * BSDSS
 * 
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
 *	@(#)uipc_socket.c	7.28 (Berkeley) 5/4/91
 */

/*
 * uipc_socket.c --- the socket layer (the top of the BSD networking stack).
 *
 * Stock 4.4BSD-Lite (see the Berkeley copyright above); this is the code annotated
 * in Stevens & Wright, TCP/IP Illustrated Vol 2, chapters 15-16. It sits directly
 * below the api/ layer: api/amiga_syscalls.c has already turned a file descriptor
 * into a `struct socket *` and calls in here. docs/ARCHITECTURE.md section 7.
 *
 * A `struct socket` is protocol-independent: it holds a send buffer and a receive
 * buffer (sockbufs, managed in uipc_socket2.c) and a pointer to its protocol's
 * protosw. The functions here implement the socket half of each system call and
 * dispatch the protocol half through pr_usrreq / pr_send:
 *   socreate   socket()    - make a socket, ask the protocol to attach (PRU_ATTACH)
 *   sobind     bind()      - PRU_BIND
 *   solisten   listen()    - PRU_LISTEN
 *   soconnect  connect()   - PRU_CONNECT
 *   soaccept   accept()    - PRU_ACCEPT
 *   sosend     send/write  - copy user data into mbufs, hand to the protocol
 *   soreceive  recv/read   - pull data out of the receive sockbuf for the user
 *   soclose    close()     - PRU_DETACH, tear down
 * The send/receive buffers plus the sleep/wakeup around them are the crux; read
 * sosend() and soreceive() to see how flow control and blocking work.
 *
 * AmiTCP note: sleeping (tsleep/wakeup) is emulated on Exec signals (kern/synch),
 * and the reference count so_refcnt is an AmiTCP addition for the library model.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <sys/synch.h>
#include <api/amiga_api.h>

#include <kern/amiga_includes.h>

#include <kern/uipc_socket_protos.h>
#include <kern/uipc_socket2_protos.h>
#include <kern/uipc_domain_protos.h>
#include <kern/amiga_select_protos.h>

/*
 * Socket operation routines.
 * These routines are called by the routines in
 * sys_socket.c or from a system process, and
 * implement the semantics of socket operations by
 * switching out to the protocol specific routines.
 */
int
socreate(dom, aso, type, proto)
        int dom;
        struct socket **aso;
	register int type;
	int proto;
{
#ifndef AMITCP
    	struct proc *p = (struct proc*)cthread_data(cthread_self());
#endif /* AMITCP */
	register struct protosw *prp;
	register struct socket *so;
	register int error;

	if (proto)
		prp = pffindproto(dom, proto, type);
	else
		prp = pffindtype(dom, type);
	if (prp == 0)
		return (EPROTONOSUPPORT);
	if (prp->pr_type != type)
		return (EPROTOTYPE);
	MALLOC(so, struct socket *, sizeof(*so), M_SOCKET, M_WAIT);
	/*
	 * PORT (AmiTCP_NG) fix: unlike stock BSD, this port's allocator
	 * does NOT block for M_WAIT (the mbuf/malloc pool is fixed-size) -- it can
	 * and does return NULL under memory pressure. The original dereferenced `so`
	 * unconditionally in the bzero right below. Check it.
	 */
	if (so == NULL)
		return (ENOBUFS);
	aligned_bzero_const((caddr_t)so, sizeof(*so));
	so->so_type = type;
#ifndef AMITCP
	if (p->p_ucred->cr_uid == 0)
#endif /* AMITCP */
		so->so_state = SS_PRIV; /* all sockets have the force  now */
	so->so_proto = prp;
	/*
	 * PORT (AmiTCP_NG) security fix: a protosw may carry a NULL
	 * pr_usrreq -- e.g. the PF_ROUTE routing socket is a stub under AMITCP
	 * (route_usrreq is #defined to NULL). Calling through it jumps to address 0,
	 * an instant crash on this MMU-less target for any process that opens such a
	 * socket. Refuse cleanly instead.
	 */
	if (prp->pr_usrreq == NULL) {
		so->so_state |= SS_NOFDREF;
		sofree(so);
		return (EOPNOTSUPP);
	}
	error =
	    (*prp->pr_usrreq)(so, PRU_ATTACH,
		(struct mbuf *)0, (struct mbuf *)proto, (struct mbuf *)0);
	if (error) {
		so->so_state |= SS_NOFDREF;
		sofree(so);
		return (error);
	}
	*aso = so;
	return (0);
}

int
sobind(so, nam)
	struct socket *so;
	struct mbuf *nam;
{
	spl_t s = splnet();
	int error;

	error =
	    (*so->so_proto->pr_usrreq)(so, PRU_BIND,
		(struct mbuf *)0, nam, (struct mbuf *)0);
	splx(s);
	return (error);
}

int
solisten(so, backlog)
	register struct socket *so;
	int backlog;
{
	spl_t s = splnet(), error;

	error =
	    (*so->so_proto->pr_usrreq)(so, PRU_LISTEN,
		(struct mbuf *)0, (struct mbuf *)0, (struct mbuf *)0);
	if (error) {
		splx(s);
		return (error);
	}
	if (so->so_q == 0)
		so->so_options |= SO_ACCEPTCONN;
	if (backlog < 0)
		backlog = 0;
	so->so_qlimit = min(backlog, SOMAXCONN);
	splx(s);
	return (0);
}

void
sofree(so)
	register struct socket *so;
{

	if (so->so_pcb || (so->so_state & SS_NOFDREF) == 0)
		return;
	if (so->so_head) {
		if (!soqremque(so, 0) && !soqremque(so, 1))
			panic("sofree dq");
		so->so_head = 0;
	}
	sbrelease(&so->so_snd);
	sorflush(so);
	FREE(so, M_SOCKET);
}

/*
 * Close a socket on last file table reference removal.
 * Initiate disconnect if connected.
 * Free socket when disconnect complete.
 */
int
soclose(so)
	register struct socket *so;
{
	spl_t s = splnet();		/* conservative */
	int error = 0;
	struct SocketBase *socketBase;
	struct timeval *timeo;

	if (so->so_options & SO_ACCEPTCONN) {
		while (so->so_q0)
			(void) soabort(so->so_q0);
		while (so->so_q)
			(void) soabort(so->so_q);
	}
	if (so->so_pcb == 0)
		goto discard;
	if (so->so_state & SS_ISCONNECTED) {
		if ((so->so_state & SS_ISDISCONNECTING) == 0) {
			error = sodisconnect(so);
			if (error)
				goto drop;
		}
		if (so->so_options & SO_LINGER) {
			if ((so->so_state & SS_ISDISCONNECTING) &&
			    (so->so_state & SS_NBIO))
				goto drop;
			/*
			 * Find socket base for the tsleep()
			 */
			if ((socketBase = FindSocketBase(FindTask(NULL)))
			    == NULL)
				goto drop; /* couldn't find */
			timeo = (so->so_linger.tv_sec 
				 || so->so_linger.tv_usec) ?
					 &so->so_linger : NULL;
			while (so->so_state & SS_ISCONNECTED)
				if ((error = tsleep(socketBase,
						   (caddr_t)&so->so_timeo,
						   netcls, timeo)))
					break;
		}
	}
drop:
	if (so->so_pcb) {
		int error2 =
		    (*so->so_proto->pr_usrreq)(so, PRU_DETACH,
			(struct mbuf *)0, (struct mbuf *)0, (struct mbuf *)0);
		if (error == 0)
			error = error2;
	}
discard:
	if (so->so_state & SS_NOFDREF)
		panic("soclose: NOFDREF");
	so->so_state |= SS_NOFDREF;
	sofree(so);
	splx(s);
	return (error);
}

/*
 * Must be called at splnet...
 */
int
soabort(so)
	struct socket *so;
{

	return (
	    (*so->so_proto->pr_usrreq)(so, PRU_ABORT,
		(struct mbuf *)0, (struct mbuf *)0, (struct mbuf *)0));
}

int
soaccept(so, nam)
	register struct socket *so;
	struct mbuf *nam;
{
	spl_t s = splnet();
	int error;

	if ((so->so_state & SS_NOFDREF) == 0)
		panic("soaccept: !NOFDREF");
	so->so_state &= ~SS_NOFDREF;
	error = (*so->so_proto->pr_usrreq)(so, PRU_ACCEPT,
	    (struct mbuf *)0, nam, (struct mbuf *)0);
	splx(s);
	return (error);
}

int
soconnect(so, nam)
	register struct socket *so;
	struct mbuf *nam;
{
	spl_t s;
	int error;

	if (so->so_options & SO_ACCEPTCONN)
		return (EOPNOTSUPP);
	s = splnet();
	/*
	 * If protocol is connection-based, can only connect once.
	 * Otherwise, if connected, try to disconnect first.
	 * This allows user to disconnect by connecting to, e.g.,
	 * a null address.
	 */
	if (so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING) &&
	    ((so->so_proto->pr_flags & PR_CONNREQUIRED) ||
	    (error = sodisconnect(so))))
		error = EISCONN;
	else
		error = (*so->so_proto->pr_usrreq)(so, PRU_CONNECT,
		    (struct mbuf *)0, nam, (struct mbuf *)0);
	splx(s);
	return (error);
}

#ifndef AMITCP
soconnect2(so1, so2)
	register struct socket *so1;
	struct socket *so2;
{
	spl_t s = splnet();
	int error;

	error = (*so1->so_proto->pr_usrreq)(so1, PRU_CONNECT2,
	    (struct mbuf *)0, (struct mbuf *)so2, (struct mbuf *)0);
	splx(s);
	return (error);
}
#endif

int
sodisconnect(so)
	register struct socket *so;
{
	spl_t s = splnet();
	int error;

	if ((so->so_state & SS_ISCONNECTED) == 0) {
		error = ENOTCONN;
		goto bad;
	}
	if (so->so_state & SS_ISDISCONNECTING) {
		error = EALREADY;
		goto bad;
	}
	error = (*so->so_proto->pr_usrreq)(so, PRU_DISCONNECT,
	    (struct mbuf *)0, (struct mbuf *)0, (struct mbuf *)0);
bad:
	splx(s);
	return (error);
}

#ifdef AMITCP
/*
 * uioread() replaces uiomove() in sosend
 */

static inline void uioread(caddr_t cp, int n, struct uio *uio)
{
  struct iovec *iov;
  u_int cnt;

  while (n > 0 && uio->uio_resid) {
    iov = uio->uio_iov;
    cnt = iov->iov_len;
    if (cnt == 0) {
      uio->uio_iov++;
      uio->uio_iovcnt--;
      continue;
    }
    if (cnt > n)
      cnt = n;

    bcopy(iov->iov_base, cp, cnt); /* wrong direction //pp */

    iov->iov_base += cnt;
    iov->iov_len -= cnt;
    uio->uio_resid -= cnt;
    cp += cnt;
    n -= cnt;
  }
}

#endif /* AMITCP */


/*
 * Send on a socket.
 * If send must go all at once and message is larger than
 * send buffering, then hard error.
 * Lock against other senders.
 * If must go all at once and not enough room now, then
 * inform user that this would block and do nothing.
 * Otherwise, if nonblocking, send as much as possible.
 * The data to be sent is described by "uio" if nonzero,
 * otherwise by the mbuf chain "top" (which must be null
 * if uio is not).  Data provided in mbuf chain must be small
 * enough to send all at once.
 *
 * Returns nonzero on error, timeout or signal; callers
 * must check for short counts if EINTR/ERESTART are returned.
 * Data and control buffers are freed on return.
 */
int
sosend(so, addr, uio, top, control, flags)
	register struct socket *so;
	struct mbuf *addr;
	struct uio *uio;
	struct mbuf *top;
	struct mbuf *control;
	int flags;
{
#ifndef AMITCP /* proc not defined */
    	struct proc *p = (struct proc*)cthread_data(cthread_self());
#endif /* AMITCP */
	struct mbuf **mp;
	register struct mbuf *m;
	register long space, len, resid;
	int clen = 0, error, dontroute, mlen;
	spl_t s;
	int atomic = sosendallatonce(so) || top;

	if (uio)
		resid = uio->uio_resid;
	else
		resid = top->m_pkthdr.len;

	dontroute =
	    (flags & MSG_DONTROUTE) && (so->so_options & SO_DONTROUTE) == 0 &&
	    (so->so_proto->pr_flags & PR_ATOMIC);
#ifndef AMITCP
	p->p_stats->p_ru.ru_msgsnd++;
#endif /* AMITCP */
	if (control)
		clen = control->m_len;
#define	snderr(errno)	{ error = errno; splx(s); goto release; }

restart:
	if ((error = sblock(&so->so_snd, uio->uio_procp)))
		goto out;
	do {
		s = splnet();
		if (so->so_state & SS_CANTSENDMORE)
			snderr(EPIPE);
		if (so->so_error)
			snderr(so->so_error);
		if ((so->so_state & SS_ISCONNECTED) == 0) {
			if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
				if ((so->so_state & SS_ISCONFIRMING) == 0 &&
				    !(resid == 0 && clen != 0))
					snderr(ENOTCONN);
			} else if (addr == 0)
				snderr(EDESTADDRREQ);
		}
		space = sbspace(&so->so_snd);
		if (flags & MSG_OOB)
			space += 1024;
		if (space < resid + clen &&
		    (atomic || space < so->so_snd.sb_lowat || space < clen)) {
			if ((atomic && resid > so->so_snd.sb_hiwat) ||
			    clen > so->so_snd.sb_hiwat)
				snderr(EMSGSIZE);
			if (so->so_state & SS_NBIO)
				snderr(EWOULDBLOCK);
			sbunlock(&so->so_snd);
			error = sbwait(&so->so_snd, uio->uio_procp);
			splx(s);
			if (error)
				goto out;
			goto restart;
		}
		splx(s);
		mp = &top;
		space -= clen;
		do {
		    if (uio == NULL) {
			/*
			 * Data is prepackaged in "top".
			 */
			resid = 0;
#ifdef USE_M_EOR
			if (flags & MSG_EOR)
				top->m_flags |= M_EOR;
#endif
		    } else do {
			/*
			 * PORT (AmiTCP_NG): stock BSD's M_WAIT alloc blocks
			 * until it succeeds, so m was assumed non-NULL. This
			 * port's fixed mbuf pool does NOT block -- MGET/MGETHDR
			 * return NULL when dry -- so the writes below would
			 * dereference NULL. Bail through the existing release
			 * path, which frees the partial chain in top.
			 */
			if (top == 0) {
				MGETHDR(m, M_WAIT, MT_DATA);
				mlen = MHLEN;
				if (m == 0) { error = ENOBUFS; goto release; }
				m->m_pkthdr.len = 0;
				m->m_pkthdr.rcvif = (struct ifnet *)0;
			} else {
				MGET(m, M_WAIT, MT_DATA);
				mlen = MLEN;
				if (m == 0) { error = ENOBUFS; goto release; }
			}
			if (resid >= MINCLSIZE && space >= mbconf.mclbytes) {
				MCLGET(m, M_WAIT);
				if ((m->m_flags & M_EXT) == 0)
					goto nopages;
				mlen = mbconf.mclbytes;
#ifdef	MAPPED_MBUFS
				len = min(mbconf.mclbytes, resid);
#else
				if (top == 0) {
					len = min(mbconf.mclbytes - max_hdr, resid);
					m->m_data += max_hdr;
				} else
					len = min(mbconf.mclbytes, resid);
#endif
				space -= mbconf.mclbytes;
			} else
			  {
nopages:
				len = min(min(mlen, resid), space);
				space -= len;
				/*
				 * For datagram protocols, leave room
				 * for protocol headers in first mbuf.
				 */
				if (atomic && top == 0 && len < mlen)
					MH_ALIGN(m, len);
			}
			uioread(mtod(m, caddr_t), (int)len, uio);
			resid = uio->uio_resid;
			m->m_len = len;
			*mp = m;
			top->m_pkthdr.len += len;
			if (error)
				goto release;
			mp = &m->m_next;
			if (resid <= 0) {
#ifdef USE_M_EOR
				if (flags & MSG_EOR)
					top->m_flags |= M_EOR;
#endif
				break;
			}
		    } 
#if defined(AMITCP) && 0
		    /* all the data in 1 mbuf chain */
		    while (space > 0);
#else
		    while (space > 0 && atomic);
#endif
		    if (dontroute)
			    so->so_options |= SO_DONTROUTE;
		    s = splnet();				/* XXX */
		    error = (*so->so_proto->pr_usrreq)(so,
			(flags & MSG_OOB) ? PRU_SENDOOB : PRU_SEND,
			top, addr, control);
		    splx(s);
		    if (dontroute)
			    so->so_options &= ~SO_DONTROUTE;
		    clen = 0;
		    control = 0;
		    top = 0;
		    mp = &top;
		    if (error)
			goto release;
		} while (resid && space > 0);
	} while (resid);

release:
	sbunlock(&so->so_snd);
out:
	if (top)
		m_freem(top);
	if (control)
		m_freem(control);
	return (error);
}


#ifdef AMITCP
/*
 * uiowrite() replaces uiomove() in soreceive
 */

static inline void uiowrite(caddr_t cp, int n, struct uio *uio)
{
  struct iovec *iov;
  u_int cnt;

  while (n > 0 && uio->uio_resid) {
    iov = uio->uio_iov;
    cnt = iov->iov_len;
    if (cnt == 0) {
      uio->uio_iov++;
      uio->uio_iovcnt--;
      continue;
    }
    if (cnt > n)
      cnt = n;

    bcopy(cp, iov->iov_base, cnt);

    iov->iov_base += cnt;
    iov->iov_len -= cnt;
    uio->uio_resid -= cnt;
    cp += cnt;
    n -= cnt;
  }
}
#endif /* AMITCP */
/*
 * Implement receive operations on a socket.
 * We depend on the way that records are added to the sockbuf
 * by sbappend*.  In particular, each record (mbufs linked through m_next)
 * must begin with an address if the protocol so specifies,
 * followed by an optional mbuf or mbufs containing ancillary data,
 * and then zero or more mbufs of data.
 * In order to avoid blocking network interrupts for the entire time here,
 * we splx() while doing the actual copy to user space.
 * Although the sockbuf is locked, new data may still be appended,
 * and thus we must maintain consistency of the sockbuf during that time.
 *
 * The caller may receive the data as a single mbuf chain by supplying
 * an mbuf **mp0 for use in returning the chain.  The uio is then used
 * only for the count in uio_resid.
 */
int
soreceive(so, paddr, uio, mp0, controlp, flagsp)
	register struct socket *so;
	struct mbuf **paddr;
	struct uio *uio;
	struct mbuf **mp0;
	struct mbuf **controlp;
	int *flagsp;
{
#ifndef AMITCP
    	struct proc *p = (struct proc*)cthread_data(cthread_self());
#endif
	register struct mbuf *m, **mp;
	register int flags, len, error, offset;
	spl_t s;
	struct protosw *pr = so->so_proto;
	struct mbuf *nextrecord;
	int moff, type;

	mp = mp0;
	if (paddr)
		*paddr = 0;
	if (controlp)
		*controlp = 0;
	if (flagsp)
		flags = *flagsp &~ MSG_EOR;
	else
		flags = 0;
	if (flags & MSG_OOB) {
		m = m_get(M_WAIT, MT_DATA);
		/* PORT (AmiTCP_NG): pool may be dry -- m_get can return NULL,
		 * which the PRU_RCVOOB handler and the mtod() below would
		 * dereference. */
		if (m == 0)
			return (ENOBUFS);
		error = (*pr->pr_usrreq)(so, PRU_RCVOOB,
		    m, (struct mbuf *)(flags & MSG_PEEK), (struct mbuf *)0);
		if (error)
			goto bad;

		do {
			uiowrite(mtod(m, caddr_t),
			    (int) min(uio->uio_resid, m->m_len), uio);
			m = m_free(m);
		} while (uio->uio_resid && error == 0 && m);

bad:
		if (m)
			m_freem(m);
		return (error);
	}
	if (mp)
		*mp = (struct mbuf *)0;

	if (so->so_state & SS_ISCONFIRMING && uio->uio_resid)
		(*pr->pr_usrreq)(so, PRU_RCVD, (struct mbuf *)0,
		    (struct mbuf *)0, (struct mbuf *)0);


restart:
	if ((error = sblock(&so->so_rcv, uio->uio_procp)))
		return (error);
	s = splnet();

	m = so->so_rcv.sb_mb;
	/*
	 * If we have less data than requested, block awaiting more
	 * (subject to any timeout) if:
	 *   1. the current count is less than the low water mark, or
	 *   2. MSG_WAITALL is set, and it is possible to do the entire
	 *	receive operation at once if we block (resid <= hiwat).
	 * If MSG_WAITALL is set but resid is larger than the receive buffer,
	 * we have to do the receive in sections, and thus risk returning
	 * a short count if a timeout or signal occurs after we start.
	 */

	while (m == 0 || (so->so_rcv.sb_cc < uio->uio_resid &&
			  (so->so_rcv.sb_cc < so->so_rcv.sb_lowat ||
			   ((flags & MSG_WAITALL)
			    && uio->uio_resid <= so->so_rcv.sb_hiwat)) &&
			  m->m_nextpkt == 0)) {
#if DIAGNOSTIC
	        if (m == 0 && so->so_rcv.sb_cc)
			panic("receive 1");
#endif
		if (so->so_error) {
			if (m)
				break;
			error = so->so_error;
			if ((flags & MSG_PEEK) == 0)
				so->so_error = 0;
			goto release;
		}
		if (so->so_state & SS_CANTRCVMORE) {
			if (m)
				break;
			else
				goto release;
		}
		for (; m; m = m->m_next)
			if (m->m_type == MT_OOBDATA
#ifdef USE_M_EOR
  || (m->m_flags & M_EOR)
#endif
			    ) {
				m = so->so_rcv.sb_mb;
				goto dontblock;
			}
		if ((so->so_state & (SS_ISCONNECTED|SS_ISCONNECTING)) == 0 &&
		    (so->so_proto->pr_flags & PR_CONNREQUIRED)) {
			error = ENOTCONN;
			goto release;
		}

		if (uio->uio_resid == 0)
			goto release;

		if (so->so_state & SS_NBIO) {
			error = EWOULDBLOCK;
			goto release;
		}
		sbunlock(&so->so_rcv);
		error = sbwait(&so->so_rcv, uio->uio_procp);
		splx(s);
		if (error)
			return (error);
		goto restart;
	}
dontblock:
#ifndef AMITCP
	p->p_stats->p_ru.ru_msgrcv++;
#endif
	nextrecord = m->m_nextpkt;
	if (pr->pr_flags & PR_ADDR) {
#if DIAGNOSTIC
		if (m->m_type != MT_SONAME)
			panic("receive 1a");
#endif
		if (flags & MSG_PEEK) {
			if (paddr)
				*paddr = m_copy(m, 0, m->m_len);
			m = m->m_next;
		} else {
			sbfree(&so->so_rcv, m);
			if (paddr) {
				*paddr = m;
				so->so_rcv.sb_mb = m->m_next;
				m->m_next = 0;
				m = so->so_rcv.sb_mb;
			} else {
				MFREE(m, so->so_rcv.sb_mb);
				m = so->so_rcv.sb_mb;
			}
		}
	}
	while (m && m->m_type == MT_CONTROL && error == 0) {
		if (flags & MSG_PEEK) {
			if (controlp)
				*controlp = m_copy(m, 0, m->m_len);
			m = m->m_next;
		} else {
			sbfree(&so->so_rcv, m);
			if (controlp) {
				if (pr->pr_domain->dom_externalize &&
				    mtod(m, struct cmsghdr *)->cmsg_type ==
				    SCM_RIGHTS)
				   error = (*pr->pr_domain->dom_externalize)(m);
				*controlp = m;
				so->so_rcv.sb_mb = m->m_next;
				m->m_next = 0;
				m = so->so_rcv.sb_mb;
			} else {
				MFREE(m, so->so_rcv.sb_mb);
				m = so->so_rcv.sb_mb;
			}
		}
		if (controlp)
			controlp = &(*controlp)->m_next;
	}
	if (m) {
		if ((flags & MSG_PEEK) == 0)
			m->m_nextpkt = nextrecord;
		type = m->m_type;
		if (type == MT_OOBDATA)
			flags |= MSG_OOB;
	}
	moff = 0;
	offset = 0;
	while (m &&  uio->uio_resid > 0 &&  error == 0) {
		if (m->m_type == MT_OOBDATA) {
			if (type != MT_OOBDATA)
				break;
		} else if (type == MT_OOBDATA)
			break;
#if DIAGNOSTIC
		else if (m->m_type != MT_DATA && m->m_type != MT_HEADER)
			panic("receive 3");
#endif
		so->so_state &= ~SS_RCVATMARK;

		len = uio->uio_resid;

		if (so->so_oobmark && len > so->so_oobmark - offset)
			len = so->so_oobmark - offset;
		if (len > m->m_len - moff)
			len = m->m_len - moff;
		/*
		 * If mp is set, just pass back the mbufs.
		 * Otherwise copy them out via the uio, then free.
		 * Sockbuf must be consistent here (points to current mbuf,
		 * it points to next record) when we drop priority;
		 * we must note any additions to the sockbuf when we
		 * block interrupts again.
		 */

		if (mp == 0) {
			splx(s);
			uiowrite(mtod(m, caddr_t) + moff, (int)len, uio);
			s = splnet();
		} else
			uio->uio_resid -= len;

		if (len == m->m_len - moff) {
#ifdef USE_M_EOR
			if (m->m_flags & M_EOR)
				flags |= MSG_EOR;
#endif
			if (flags & MSG_PEEK) {
				m = m->m_next;
				moff = 0;
			} else {
				nextrecord = m->m_nextpkt;
				sbfree(&so->so_rcv, m);
				if (mp) {
					*mp = m;
					mp = &m->m_next;
					so->so_rcv.sb_mb = m = m->m_next;
					*mp = (struct mbuf *)0;
				} else {
					MFREE(m, so->so_rcv.sb_mb);
					m = so->so_rcv.sb_mb;
				}
				if (m)
					m->m_nextpkt = nextrecord;
			}
		} else {
			if (flags & MSG_PEEK)
				moff += len;
			else {
				if (mp)
					*mp = m_copym(m, 0, len, M_WAIT);
				m->m_data += len;
				m->m_len -= len;
				so->so_rcv.sb_cc -= len;
			}
		}
		if (so->so_oobmark) {
			if ((flags & MSG_PEEK) == 0) {
				so->so_oobmark -= len;
				if (so->so_oobmark == 0) {
					so->so_state |= SS_RCVATMARK;
					break;
				}
			} else
				offset += len;
		}
		if (flags & MSG_EOR)
			break;
		/*
		 * If the MSG_WAITALL flag is set (for non-atomic socket),
		 * we must not quit until "uio->uio_resid == 0" or an error
		 * termination.  If a signal/timeout occurs, return
		 * with a short count but without error.
		 * Keep sockbuf locked against other readers.
		 */
		while (flags & MSG_WAITALL && m == 0 && uio->uio_resid > 0 &&
		       !sosendallatonce(so)) {
			if (so->so_error || so->so_state & SS_CANTRCVMORE)
				break;
			error = sbwait(&so->so_rcv, uio->uio_procp);
			if (error) {
				sbunlock(&so->so_rcv);
				splx(s);
				return (0);
			}
			if ((m = so->so_rcv.sb_mb))
				nextrecord = m->m_nextpkt;
		}
	}
	if ((flags & MSG_PEEK) == 0) {
		if (m == 0)
			so->so_rcv.sb_mb = nextrecord;
		else if (pr->pr_flags & PR_ATOMIC) {
			flags |= MSG_TRUNC;
			(void) sbdroprecord(&so->so_rcv);
		}
		if (pr->pr_flags & PR_WANTRCVD && so->so_pcb)
			(*pr->pr_usrreq)(so, PRU_RCVD, (struct mbuf *)0,
			    (struct mbuf *)flags, /* (struct mbuf *)0, */
			    (struct mbuf *)0); /* BUG! One extra arg! */
	}
	if (flagsp)
		*flagsp |= flags;
release:
	sbunlock(&so->so_rcv);
	splx(s);
	return (error);
}

int
soshutdown(so, how)
	register struct socket *so;
	register int how;
{
	register struct protosw *pr = so->so_proto;

	how++; /* now how: 1 - no receives, 2 - no sends, 3 - no either */
	if (how & 1)
	  sorflush(so);
	if (how & 2)
	  return ((*pr->pr_usrreq)(so, PRU_SHUTDOWN, (struct mbuf *)0, 
				   (struct mbuf *)0, (struct mbuf *)0));
	return (0);
}

void
sorflush(so)
	register struct socket *so;
{
	register struct sockbuf *sb = &so->so_rcv;
	register struct protosw *pr = so->so_proto;
	register spl_t s;
	struct sockbuf asb;

	sb->sb_flags |= SB_NOINTR;
	(void) sblock(sb, NULL);
	s = splimp();
	socantrcvmore(so);
	sbunlock(sb);
	asb = *sb;
	aligned_bzero_const((caddr_t)sb, sizeof (*sb));
	splx(s);
	if (pr->pr_flags & PR_RIGHTS && pr->pr_domain->dom_dispose)
		(*pr->pr_domain->dom_dispose)(asb.sb_mb);
	sbrelease(&asb);
}

/*
 * in some fields of socket structure previous short fields of ticks are
 * changed to struct timeval fields. (actually so_linger and sb_timeo)
 * sosetopt() and sogetopt() does transformations needed for those.
 */

int
sosetopt(so, level, optname, m0)
	register struct socket *so;
	int level, optname;
	struct mbuf *m0;
{
	int error = 0;
	register struct mbuf *m = m0;

	if (level != SOL_SOCKET) {
		if (so->so_proto && so->so_proto->pr_ctloutput)
			return ((*so->so_proto->pr_ctloutput)
				  (PRCO_SETOPT, so, level, optname, &m0));
		error = ENOPROTOOPT;
	} else {
		switch (optname) {

		case SO_LINGER:
			if (m == NULL || m->m_len != sizeof (struct linger)) {
				error = EINVAL;
				goto bad;
			}
			{
			  int val = mtod(m, struct linger *)->l_linger;
#ifdef AMITCP
			  /*
			   * l_linger is in seconds
			   */
			  so->so_linger.tv_sec = val;
			  so->so_linger.tv_usec = 0;
#else
			  so->so_linger.tv_sec = val / hz;
			  so->so_linger.tv_usec = (val % hz) * tick;
#endif
			}
			/* fall thru... */

		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
		case SO_BROADCAST:
		case SO_REUSEADDR:
		case SO_OOBINLINE:
			if (m == NULL || m->m_len < sizeof (int)) {
				error = EINVAL;
				goto bad;
			}
			if (*mtod(m, int *))
				so->so_options |= optname;
			else
				so->so_options &= ~optname;
			break;

		case SO_SNDBUF:
		case SO_RCVBUF:
		case SO_SNDLOWAT:
		case SO_RCVLOWAT:
			if (m == NULL || m->m_len < sizeof (int)) {
				error = EINVAL;
				goto bad;
			}
			switch (optname) {

			case SO_SNDBUF:
			case SO_RCVBUF:
				if (sbreserve(optname == SO_SNDBUF ?
				    &so->so_snd : &so->so_rcv,
				    (u_long) *mtod(m, int *)) == 0) {
					error = ENOBUFS;
					goto bad;
				}
				break;

			case SO_SNDLOWAT:
				so->so_snd.sb_lowat = *mtod(m, int *);
				break;
			case SO_RCVLOWAT:
				so->so_rcv.sb_lowat = *mtod(m, int *);
				break;
			}
			break;

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
		    {
			struct timeval *tv;

			if (m == NULL || m->m_len < sizeof (*tv)) {
				error = EINVAL;
				goto bad;
			}
			tv = mtod(m, struct timeval *);

			switch (optname) {

			case SO_SNDTIMEO:
				so->so_snd.sb_timeo = *tv;
				break;
			case SO_RCVTIMEO:
				so->so_rcv.sb_timeo = *tv;
				break;
			}
			break;
		    }

		default:
			error = ENOPROTOOPT;
			break;
		}
	}
bad:
	if (m)
		(void) m_free(m);
	return (error);
}

int
sogetopt(so, level, optname, mp)
	register struct socket *so;
	int level, optname;
	struct mbuf **mp;
{
	register struct mbuf *m;

	if (level != SOL_SOCKET) {
		if (so->so_proto && so->so_proto->pr_ctloutput) {
			return ((*so->so_proto->pr_ctloutput)
				  (PRCO_GETOPT, so, level, optname, mp));
		} else
			return (ENOPROTOOPT);
	} else {
		m = m_get(M_WAIT, MT_SOOPTS);
		/* PORT (AmiTCP_NG): pool may be dry -- guard before m_len write. */
		if (m == 0)
			return (ENOBUFS);
		m->m_len = sizeof (int);

		switch (optname) {

		case SO_LINGER:
			m->m_len = sizeof (struct linger);
			mtod(m, struct linger *)->l_onoff =
				so->so_options & SO_LINGER;
			mtod(m, struct linger *)->l_linger =
#ifndef AMITCP
			  so->so_linger.tv_sec * hz +
			    so->so_linger.tv_usec / tick;
#else
			/*
			 * l_linger is in seconds
			 */
			  so->so_linger.tv_sec;
#endif			
			break;

		case SO_USELOOPBACK:
		case SO_DONTROUTE:
		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_REUSEADDR:
		case SO_BROADCAST:
		case SO_OOBINLINE:
			*mtod(m, int *) = so->so_options & optname;
			break;

		case SO_TYPE:
			*mtod(m, int *) = so->so_type;
			break;

		case SO_ERROR:
			*mtod(m, int *) = so->so_error;
			so->so_error = 0;
			break;

		case SO_SNDBUF:
			*mtod(m, int *) = so->so_snd.sb_hiwat;
			break;

		case SO_RCVBUF:
			*mtod(m, int *) = so->so_rcv.sb_hiwat;
			break;

		case SO_SNDLOWAT:
			*mtod(m, int *) = so->so_snd.sb_lowat;
			break;

		case SO_RCVLOWAT:
			*mtod(m, int *) = so->so_rcv.sb_lowat;
			break;

		case SO_SNDTIMEO:
		case SO_RCVTIMEO:
		    {
			struct timeval *tv= (optname == SO_SNDTIMEO ?
			     &so->so_snd.sb_timeo : &so->so_rcv.sb_timeo);

			m->m_len = sizeof(struct timeval);
			mtod(m, struct timeval *)->tv_sec = tv->tv_sec;
			mtod(m, struct timeval *)->tv_usec = tv->tv_usec;
			break;
		    }

		default:
			(void)m_free(m);
			return (ENOPROTOOPT);
		}
		*mp = m;
		return (0);
	}
}

void
sohasoutofband(so)
	register struct socket *so;
{
#ifdef AMITCP
	if (so->so_pgid)
		Signal(so->so_pgid->thisTask, so->so_pgid->sigUrgMask);
#else
	struct proc *p;

	if (so->so_pgid < 0)
		gsignal(-so->so_pgid, SIGURG);
	else if (so->so_pgid > 0 && (p = pfind(so->so_pgid)) != 0)
		psignal(p, SIGURG);
#endif
	if (so->so_rcv.sb_flags & SB_SEL) {
		so->so_rcv.sb_flags &= ~SB_SEL; /* do not notify us any more */
		selwakeup(&so->so_rcv.sb_sel);
	}
}

