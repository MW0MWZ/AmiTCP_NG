/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: amiga_syscalls.c,v 3.5 1994/03/22 08:41:36 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * Created: Sun Feb 14 18:35:48 1993 too
 * Last modified: Wed Feb 16 01:18:32 1994 jraja
 * 
 * HISTORY
 * $Log: amiga_syscalls.c,v $
 * Revision 3.5  1994/03/22  08:41:36  jraja
 * Added calls to the libPtr->fdCallback to the appropriate places.
 *
 * Revision 3.4  1994/02/15  23:18:42  jraja
 * Changed sdFind() to return the sd via LONG * instead of ULONG * to be
 * consistent with API types.
 *
 * Revision 3.3  1994/01/07  15:40:29  too
 * Bug fixes after revision 3.1. Now tested.
 *
 * Revision 3.2  1994/01/06  13:39:11  too
 * Moved send and recv functions to amiga_sendrecv.c
 *
 * Revision 3.1  1994/01/04  14:32:29  too
 * Added function sdFind(). Revised socket() and accept() to use sdFind()
 * and socket usage bitmask.
 *
 * Revision 1.25  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 */

/*
 * amiga_syscalls.c --- the implementations behind the socket() family vectors.
 *
 * When an application calls socket()/bind()/listen()/accept()/..., the client
 * stub (api/apicalls_gnuc.h) jumps to the matching vector, which lands HERE. These
 * functions are the bridge between the PUBLIC api (small integer file descriptors,
 * a per-opener fd table) and the BSD SOCKET LAYER (kern/uipc_socket.c, which works
 * with `struct socket *` pointers). docs/ARCHITECTURE.md section 7.
 *
 * So each function typically: validates the caller's fd, looks up the `struct
 * socket` in the caller's SocketBase fd table, calls the corresponding BSD routine
 * (socreate/sobind/solisten/soaccept/...), and translates the result + errno back
 * for the application. New descriptors are recorded in the fd table and announced
 * through the optional fdCallback (used by net.lib to keep a C runtime's own fd
 * table in sync -- see SBTC_FDCALLBACK in amiga_generic2.c).
 *
 * The functions are declared SAVEDS RAF4/RAF3/... : SAVEDS restores the small-data
 * base for callers arriving from a foreign context, and RAFn are the register-
 * argument function macros (the library ABI passes args in registers, not on the
 * stack). Read _socket() first.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/synch.h>
#include <sys/errno.h>

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>

#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include "sockargs.h"

#include <amitcp/socketbasetags.h>

#include <kern/uipc_socket_protos.h>
#include <kern/uipc_socket2_protos.h>
  

LONG SAVEDS RAF4(_socket,
		 struct SocketBase *, 	libPtr,		a6,
		 LONG,			domain,		d0,
		 LONG,			type,		d1,
		 LONG,			protocol,	d2)

#if 0
{
#endif
  
  struct socket *so;
  LONG fd, error;

  /* CHECK_TASK() also performs the first-touch lazy start of the stack in the
   * self-starting library build (see NG_ENSURE_STACK in amiga_libcallentry.h) --
   * socket() is no longer special-cased; any first API call brings the stack up. */
  CHECK_TASK();

  /*
   * PORT (AmiTCP_NG): hold the semaphore across FINDING the descriptor and
   * PUBLISHING it, the way _accept() already does.
   *
   * sdFind() only reports a free slot -- it does not claim it; the bit is set
   * by the FD_SET below. With the descriptor table shared between tasks (see
   * SBTC_CAN_SHARE_LIBRARY_BASES), two of them could pass through that gap and
   * be handed the SAME fd: both create a socket, the second overwrites
   * dTable[fd], and the first one's socket is left referenced by nothing --
   * unreachable, unclosable, and holding its mbufs forever. Serialising the
   * whole find-create-publish sequence closes it.
   */
  ObtainSyscallSemaphore(libPtr);

  if ((error = sdFind(libPtr, &fd))) {
    ReleaseSyscallSemaphore(libPtr);
    goto Return;
  }

  error = socreate(domain, &so, type, protocol);

  if (! error) {
    /*
     * Tell the link library about the new fd
     */
    if (libPtr->fdCallback)
      error = libPtr->fdCallback(fd, FDCB_ALLOC);
    if (! error) {
      so->so_refcnt = 1;		/* reference count is pure AmiTCP addition */
      /*
       * PORT (AmiTCP_NG) compatibility fix: give the socket an owner up front.
       *
       * so_pgid is who sowakeup() signals for asynchronous socket events, and
       * until now the ONLY thing that ever set it was the FIOSETOWN ioctl. A
       * program that enabled FIOASYNC without first calling FIOSETOWN therefore
       * got SS_ASYNC set, every flag correct, and no notification EVER -- the
       * guard `if (so->so_pgid)` in sowakeup() silently skipped the Signal. Not
       * an error return; a permanent wait for an event that could not arrive.
       *
       * Strict BSD does require an explicit F_SETOWN, so the old behaviour was
       * defensible in isolation. But this library exists to be a drop-in for
       * Roadshow, and software written against Roadshow/Miami/AmiTCP 4.x works
       * there and hangs here -- Cloanto document "asynchronous socket events"
       * as exactly the feature whose absence breaks Amiga Explorer. Where
       * strictness and compatibility conflict for a drop-in, compatibility wins.
       *
       * Three readers see the difference, and only two of them are gated:
       *   - sowakeup()'s SIGIO delivery -- gated by SS_ASYNC, so it still takes a
       *     deliberate FIOASYNC. This is the case the fix is for.
       *   - FIOGETOWN/SIOCGPGRP -- now answers with the creating task instead of
       *     NULL. Strictly more useful, and what a caller would expect.
       *   - sohasoutofband()'s SIGURG (kern/uipc_socket.c) -- NOT gated by
       *     SS_ASYNC or by anything else. It fires whenever a peer sends urgent
       *     data, so a program that never asked for either ioctl can now receive
       *     a SIGURG it would previously have ignored. That matches BSD, where
       *     SIGURG likewise goes to whoever owns the socket and does not require
       *     async mode -- the old silence was a consequence of having no owner at
       *     all, not a deliberate policy. Recorded here rather than papered over:
       *     it is a real behaviour change for a program that opted into nothing.
       * An explicit FIOSETOWN still overrides the default, so anything that
       * already set an owner is entirely unaffected.
       */
      so->so_pgid = libPtr;
      libPtr->dTable[fd] = so;
      FD_SET(fd, (fd_set *)(libPtr->dTable + libPtr->dTableSize));
    }
  }

  ReleaseSyscallSemaphore(libPtr);

 Return: API_STD_RETURN(error, fd);
}


LONG SAVEDS RAF4(_bind,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			s,		d0,
		 caddr_t,		name,		a0,
		 LONG,			namelen,	d1)
#if 0
{
#endif  

  struct socket *so;
  struct mbuf *nam;
  LONG error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);
  
  if ((error = getSock(libPtr, s, &so)))
    goto Return;
  if ((error = sockArgs(&nam, name, namelen, MT_SONAME)))
    goto Return;
  error = sobind(so, nam);
  m_freem(nam);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}

LONG SAVEDS RAF3(_listen,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			s,		d0,
		 LONG,			backlog,	d1)
#if 0
{
#endif

  struct socket *so;
  LONG error;
  
  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);
  
  if ((error = getSock(libPtr, s, &so)))
    goto Return;
  error = solisten(so, backlog);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);

}
    
LONG SAVEDS RAF4(_accept,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			s,		d0,
		 caddr_t,		name,		a0,
		 ULONG *,		anamelen,	a1)
#if 0
{
#endif

  struct socket *so;
  struct mbuf *nam;
  spl_t old_spl;
  LONG error, fd;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  /*
   * PORT (AmiTCP_NG) security fix: `name` was checked but `anamelen` was not,
   * and it is dereferenced on both the normal path and the mbuf-exhaustion
   * path below. A caller passing a real buffer with a NULL length pointer
   * would write address 0 -- the CPU exception vectors, no MMU. A NULL
   * `name` remains valid (accept() with no peer address wanted).
   */
  if (name != NULL && anamelen == NULL) {
    error = EFAULT;
    goto Return;
  }

  if ((error = getSock(libPtr, s, &so)))
    goto Return;

  old_spl = splnet();
  if ((so->so_options & SO_ACCEPTCONN) == 0) {
    error = EINVAL;
    goto Return_spl;
  }
  if ((so->so_state & SS_NBIO) && so->so_qlen == 0) {
    error = EWOULDBLOCK;
    goto Return_spl;
  }
  while (so->so_qlen == 0 && so->so_error == 0) {
    if (so->so_state & SS_CANTRCVMORE) {
      so->so_error = ECONNABORTED;
      break;
    }
    if ((error = tsleep(libPtr, (caddr_t)&so->so_timeo, netcon, NULL))) {
      goto Return_spl;
    }
  }
  if (so->so_error) {
    error = so->so_error;
    so->so_error = 0;
    goto Return_spl;
  }
   
  if ((error = sdFind(libPtr, &fd))) {
    goto Return_spl;
  }

  /*
   * Tell the link library about the new fd
   */
  if (libPtr->fdCallback)
    if ((error = libPtr->fdCallback(fd, FDCB_ALLOC)))
      goto Return_spl;

  {
    struct socket *aso = so->so_q;
    if (soqremque(aso, 1) == 0)
      panic("accept");
    /*
     * PORT (AmiTCP_NG): re-arm FD_ACCEPT while connections remain queued.
     * The API says the stack "keeps track of each pending connection" and
     * generates a new FD_ACCEPT until all are accounted for; a bitmask cannot
     * hold two, so the count is honoured here instead -- one re-arm per
     * accept() that leaves the queue non-empty. Doing it HERE rather than in
     * GetSocketEvents() is what makes it safe: re-arming at collection time
     * while so_qlen stayed non-zero would mean an application draining "until
     * -1" before accepting never sees -1. This way a client that accepts once
     * per signal still learns there is more, and a client that drains in a
     * loop is unaffected. `so` is still the LISTENING socket at this point.
     */
    if (so->so_qlen != 0)
      soraise_event(so, FD_ACCEPT);
    so = aso;
  }

  libPtr->dTable[fd] = so;
  FD_SET(fd, (fd_set *)(libPtr->dTable + libPtr->dTableSize));
  so->so_refcnt = 1;  /* pure AmiTCP addition */
  /*
   * PORT (AmiTCP_NG) compatibility fix: same rule as _socket() -- whichever
   * SocketBase takes delivery of the descriptor owns the socket for
   * asynchronous-event purposes. An accepted connection is the case that
   * matters most for a server: it is the socket the program will actually put
   * into FIOASYNC mode, and without an owner sowakeup() would never signal it.
   * Note the owner is the ACCEPTING base, not the listener's -- the listener may
   * belong to a different task, and the task that now holds the fd is the one
   * that needs the notification. An explicit FIOSETOWN still overrides this.
   */
  so->so_pgid = libPtr;

  /*
   * PORT (AmiTCP_NG): m_get() can return NULL here (fixed mbuf pool exhausted;
   * M_WAIT does not block in this port). The connection has already been
   * accepted and installed in the fd table, so we must NOT fail the whole call
   * -- that would leak an established socket. Instead skip the peer-name
   * readback and report a zero-length name. soaccept() would dereference a NULL
   * nam, so it is guarded too. (m_freem(NULL) is itself a no-op.)
   */
  nam = m_get(M_WAIT, MT_SONAME);
  if (nam != NULL) {
    (void)soaccept(so, nam);  /* is this always successful */
    if (name) {
      if (*anamelen > nam->m_len)
	*anamelen = nam->m_len;
      /* SHOULD COPY OUT A CHAIN HERE */
      aligned_bcopy(mtod(nam, caddr_t), (caddr_t)name, (u_int)*anamelen);
    }
    m_freem(nam);
  } else if (name) {
    *anamelen = 0;
  }

 Return_spl:
  splx(old_spl);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, fd);
}

LONG SAVEDS RAF4(_connect,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			s,		d0,
		 caddr_t,		name,		a0,
		 LONG,			namelen,	d1)
#if 0
{
#endif

  /*register*/ struct socket *so;
  struct mbuf *nam;
  LONG error;
  spl_t old_spl;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  if ((error = getSock(libPtr, s, &so)))
    goto Return;
  if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING)) {
    error = EALREADY;
    goto Return;
  }
  if ((error = sockArgs(&nam, name, namelen, MT_SONAME)))
    goto Return;
  error = soconnect(so, nam);
  if (error)
    goto bad;
  if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING)) {
    m_freem(nam);
    error = EINPROGRESS;
    goto Return;
  }	
  old_spl = splnet();
  while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0)
    if ((error = tsleep(libPtr,(caddr_t)&so->so_timeo, netcon, NULL)))
      break;
  if (error == 0) {
    error = so->so_error;
    so->so_error = 0;
  }
  splx(old_spl);
 bad:
  so->so_state &= ~SS_ISCONNECTING;
  m_freem(nam);
  if (error == ERESTART)
    error = EINTR;

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);

}

LONG SAVEDS RAF3(_shutdown,
		 struct SocketBase *,	libPtr,	a6,
		 LONG,			s,	d0,
		 LONG,			how,	d1)
#if 0
{
#endif

  struct socket *so;
  LONG error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  if ((error = getSock(libPtr, s, &so)))
    goto Return;

  error = soshutdown(so, how);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}

LONG SAVEDS RAF6(_setsockopt,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			s,		d0,
		 LONG,			level,		d1,
		 LONG,			name,		d2,
		 caddr_t,		val,		a0,
		 ULONG,			valsize,	d3)
#if 0
{
#endif

  struct socket *so;
  struct mbuf *m = NULL;
  LONG error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  if ((error = getSock(libPtr, s, &so)))
    goto Return;
  if (valsize > MLEN) { /* unsigned catches negative values */
    error = EINVAL;
    goto Return;
  }
  if (val) {
    m = m_get(M_WAIT, MT_SOOPTS);
    if (m == NULL) {
      error = ENOBUFS;
      goto Return;
    }
    bcopy(val, mtod(m, caddr_t), valsize); /* aligned ? */
    m->m_len = (int)valsize;
  }
  error = sosetopt(so, level, name, m);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}


LONG SAVEDS RAF6(_getsockopt,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			s,		d0,
		 LONG,			level,		d1,
		 LONG,			name,		d2,
		 caddr_t,		val,		a0,
		 ULONG *,		avalsize,	a1)
#if 0
{
#endif

  struct socket *so;
  struct mbuf *m = NULL;
  ULONG valsize, error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  if ((error = getSock(libPtr, s, &so)))
    goto Return;
  
  /*
   * PORT (AmiTCP_NG) security fix: `val` was checked but `avalsize` was not,
   * yet it is read here and written back below. A caller passing a real
   * buffer with a NULL length pointer would read and then write address 0 --
   * the CPU exception vectors, with no MMU to catch it.
   */
  if (val != NULL && avalsize == NULL) {
    error = EFAULT;
    goto Return;
  }

  if (val)
    valsize = *avalsize;
  else
    valsize = 0;
  
  if ((error = sogetopt(so, level, name, &m)) == 0
      && val && valsize && m != NULL) {
    if (valsize > m->m_len)  /* valsize is ULONG */
      valsize = m->m_len;
    bcopy(mtod(m, caddr_t), val, (u_int)valsize); /* aligned ? */
    *avalsize = valsize;
  }
  if (m != NULL)
    (void) m_free(m);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}

LONG SAVEDS RAF4(_getsockname,
		 struct SocketBase *,	libPtr,	a6,
		 LONG,			fdes,	d0,
		 caddr_t,		asa,	a0,
		 ULONG *,		alen,	a1)
#if 0
{
#endif

  /*register*/
  struct socket *so;
  struct mbuf *m;
  LONG error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  /*
   * PORT (AmiTCP_NG) security fix: both out-parameters are dereferenced
   * below with no check. Stock BSD relied on copyin/copyout faulting on a
   * bad caller pointer; with no MMU and no user/kernel boundary a NULL here
   * writes over the CPU exception vectors at address 0. recvit() in
   * amiga_sendrecv.c already guards its length pointer this way.
   */
  if (asa == NULL || alen == NULL) {
    error = EFAULT;
    goto Return;
  }

  if ((error = getSock(libPtr, fdes, &so)))
    goto Return;

  m = m_getclr(M_WAIT, MT_SONAME);
  if (m == NULL) {
    error = ENOBUFS;
    goto Return;
  }
  if ((error = (*so->so_proto->pr_usrreq)(so, PRU_SOCKADDR, 0, m, 0)))
    goto bad;
  if (*alen > m->m_len)
    *alen = m->m_len;
  aligned_bcopy(mtod(m, caddr_t), (caddr_t)asa, (u_int)*alen);

 bad:
  m_freem(m);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}

LONG SAVEDS RAF4(_getpeername,
		 struct SocketBase *,	libPtr,	a6,
		 LONG,			fdes,	d0,
		 caddr_t,		asa,	a0,
		 ULONG *,		alen,	a1)
#if 0
{
#endif

  /*register*/
  struct socket *so;
  struct mbuf *m;
  LONG error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  /* PORT (AmiTCP_NG) security fix: see _getsockname above -- both
   * out-parameters are dereferenced below and neither was checked. */
  if (asa == NULL || alen == NULL) {
    error = EFAULT;
    goto Return;
  }

  if ((error = getSock(libPtr, fdes, &so)))
    goto Return;

  if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0) {
    error = ENOTCONN;
    goto Return;
  }

  m = m_getclr(M_WAIT, MT_SONAME);
  if (m == NULL) {
    error = ENOBUFS;
    goto Return;
  }

  if ((error = (*so->so_proto->pr_usrreq)(so, PRU_PEERADDR, 0, m, 0)))
    goto bad;
  if (*alen > m->m_len)
    *alen = m->m_len;
  aligned_bcopy(mtod(m, caddr_t), (caddr_t)asa, (u_int)*alen);

 bad:
  m_freem(m);

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}

LONG sockArgs(struct mbuf **mp,
		     caddr_t buf,	/* aligned */
		     LONG buflen,
		     LONG type)
{
  register struct mbuf *m;
  LONG error = 0;

  if ((u_int)buflen > MLEN)
    return (EINVAL);

  m = m_get(M_WAIT, type);
  if (m == NULL)
    return (ENOBUFS);
  m->m_len = buflen;

  aligned_bcopy(buf, mtod(m, caddr_t), (u_int)buflen);
  *mp = m;
  if (type == MT_SONAME)
    mtod(m, struct sockaddr *)->sa_len = buflen;

  return (error);
}

/*
 * sdFind replaces old fdAlloc. This version now looks for free socket
 * from socket usage bitmask stored right after descriptor table
 */

LONG sdFind(struct SocketBase * libPtr, LONG *fdp)
{
  int bit, moffset;
  ULONG * smaskp;
  int mlongs = (libPtr->dTableSize - 1) / NFDBITS + 1;

  moffset = 0, smaskp = (ULONG  *)(libPtr->dTable + libPtr->dTableSize);
  while (mlongs) {
    unsigned long cmask = *smaskp;
    unsigned long dmask = cmask + 1;

    if (dmask == 0) {
      mlongs--, smaskp++, moffset += 32;
      continue;	/* current  mask is full (cmask = 0xFFFFFFFF) */
    }
    cmask = ((cmask ^ dmask) >> 1) + 1; /* now only one bit set ! */

    bit = (cmask & 0xFFFF0000)? 16: 0;
    if (cmask & 0xFF00FF00) bit += 8;
    if (cmask & 0xF0F0F0F0) bit += 4;
    if (cmask & 0xCCCCCCCC) bit += 2;
    if (cmask & 0xAAAAAAAA) bit += 1;

    /*
     * Check if link library agrees with us on the next free fd...
     */
    if (libPtr->fdCallback)
      if (libPtr->fdCallback(moffset + bit, FDCB_CHECK)) {
	*smaskp |= cmask; /* mark this fd as used */
	continue; /* search for the next _bit_ */
      }

    if (moffset + bit >= libPtr->dTableSize)
      break;
    else {
      *fdp = moffset + bit;
      return 0;
    }
  }
  return (EMFILE);
}
