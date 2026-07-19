/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: amiga_generic.c,v 3.6 1994/03/22 08:41:36 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * Created: Fri Feb 12 13:26:55 1993 too
 * Last modified: Wed Feb  2 21:35:39 1994 ppessi
 * 
 * HISTORY
 * $Log: amiga_generic.c,v $
 * Revision 3.6  1994/03/22  08:41:36  jraja
 * Added calls to the libPtr->fdCallback to the appropriate places.
 *
 * Revision 3.5  1994/02/03  04:10:55  ppessi
 * Changed the interface IOCTL codes
 *
 * Revision 3.4  1994/01/20  02:10:36  jraja
 * Added support for 0 domain for ObtainSocket() if id is unique.
 *
 * Revision 3.3  1994/01/12  07:35:25  jraja
 * Moved dtable functions (getLastSockFd(), _SetDTableSize() and
 * _getdtablesize()) to amiga_generic2.c.
 *
 * Revision 3.2  1994/01/07  15:40:29  too
 * Bug fixes after revision 3.1. Now tested.
 *
 * Revision 3.1  1994/01/04  14:38:35  too
 * Added static function getLastSock(). Revised Dup2Socket, CloseSocket,
 * SetDTableSize, ObtainSocket and ReleaseSocket to utilize socket usage
 * bitmask. Changed behaviour of Dup2Socket when fd1 == -1.
 * Moved some functions to amiga_generic2.c
 *
 * Revision 1.61  1993/12/22  08:16:22  jraja
 * Cleaned some types, added 'static' to local functions.
 *
 * Revision 1.60  1993/10/16  17:20:49  too
 * Fixed copying bug in SetDTableSize
 *
 * Revision 1.59  1993/09/14  22:15:16  jraja
 * Added zeroing of obits in WaitSelect() if new memory is allocated for it.
 *
 * Revision 1.58  1993/08/01  22:15:07  ppessi
 * Changed CloseSocket() to use api/apicall.h protos.
 *
 * Revision 1.57  1993/07/13  21:58:59  ppessi
 * Added internal prototypes; changed interface ioctl codes to 'I'
 *
 * Revision 1.56  1993/06/13  16:38:43  too
 * Added range check for fd2 in Dup2Socket()
 *
 * Revision 1.55  1993/06/11  20:18:09  too
 * Added Dup2Socket(). Chahanged ioctl to IoctlSocket()
 * Made function countSockets() which is needed to determine if last reference
 * to that socket is given away.
 *
 * Revision 1.54  1993/06/07  12:37:20  too
 * Changed inet_ntoa, netdatabase functions and WaitSelect() use
 * separate buffers for their dynamic buffers
 *
 */

/*
 * amiga_generic.c --- assorted small API entry points.
 *
 * A grab-bag of the simpler bsdsocket.library functions that do not warrant their
 * own file: Errno(), the socket-event and signal helpers, and other short vectors.
 * Like the other api/ files these are register-argument (RAFn) functions that
 * receive the caller's SocketBase and operate on it. (SocketBaseTagList, the big
 * one, lives in amiga_generic2.c.)
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>
#include <sys/protosw.h>
#include <sys/malloc.h>
#include <sys/synch.h>

#include <sys/time.h>
#include <sys/errno.h>

#include <sys/socket.h>
#include <net/route.h>

#include <kern/amiga_includes.h>

#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <api/allocdatabuffer.h>

#include <api/apicalls.h>

#include <net/if_protos.h>

#include <amitcp/socketbasetags.h>

#include <kern/uipc_domain_protos.h>
#include <kern/uipc_socket_protos.h>
#include <kern/uipc_socket2_protos.h>

/* Local protos */
static int selscan(struct SocketBase *p, 
		   fd_set *in, fd_set *ou, fd_set *ex, fd_mask *obits,
		   int nfd, int *retval, int *selitemcount_p);
void selenter(struct SocketBase * p, struct newselitem ** hdr);
static void unselect(register struct newselbuf * sb);
void selwakeup(struct newselitem **hdr);
static int soo_select(struct socket *so, int which, struct SocketBase *p);
static int countSockets(struct SocketBase * libPtr, struct socket * so);

/*
 * itimerfix copied from bsdss/server/kern/kern_time.c. since fields
 * in struct timeval in amiga are ULONGs, values less than zero need
 * not be checked. the second check, timeval less than resolution of
 * the clock is not needed in amiga...hmm, is removed also.
 */
static inline int itimerfix(struct timeval *tv)
{
	if (tv->tv_sec > 100000000 || tv->tv_usec >= 1000000)
		return (EINVAL);
/*	if (tv->tv_sec == 0 && tv->tv_usec != 0 && tv->tv_usec < tick)
		tv->tv_usec = tick;
*/	return (0);
}

/*
 * ffs() copied directly from bsdss/server/kern/subr_xxx.c
 */
static inline int ffs(register long mask)
{
  register int bit;

  if (!mask)
    return(0);
  for (bit = 1;; ++bit) {
    if (mask&0x01)
      return(bit);
    mask >>= 1;
  }
  /* NOT REACHED */
}


/*
 * Ioctl system call
 */

LONG SAVEDS RAF4(_IoctlSocket,
		 struct SocketBase *,	libPtr,	a6,
		 LONG,			fdes,	d0,
		 ULONG,		cmd,	d1,
		 caddr_t,		data,	a0)
#if 0
{
#endif

  register int error;
  register u_int size;
  struct socket *so;

  /*
   * Note: Syscall semaphore is essential here if two processes can access
   * the same socket. (can it be changed to spl_? ?)
   */
  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);
  
  if ((error = getSock(libPtr, fdes, &so)))
    goto Return;

  /*
   * Interpret high order word to get size of the user data area
   */
  size = IOCPARM_LEN(cmd);
  if (size == 0 || size > IOCPARM_MAX) {
    error = ENOTTY;
    goto Return;
  }
  if (!(cmd & IOC_IN) && cmd & IOC_OUT)
    /*
     * Zero the buffer so the user always
     * gets back something deterministic.
     */
    bzero(data, size);
  
  switch (cmd) {
  case FIOCLEX:
  case FIONCLEX:		/* should this return error ???? */
    goto Return;

  case FIOSETOWN:
  case SIOCSPGRP:
    /*
     * data is a struct Task **, find corresponding SocketBase * and set owner
     */
    so->so_pgid = FindSocketBase(*(struct Task **)data);
    goto Return;
    
  case FIOGETOWN:
  case SIOCGPGRP:
    if (so->so_pgid)
      *(struct Task **)data = so->so_pgid->thisTask;
    else
      *(struct Task **)data = NULL;
    goto Return;

  case FIONBIO:
    if (*(int *)data)
      so->so_state |= SS_NBIO;
    else
      so->so_state &= ~SS_NBIO;
    goto Return;
    
  case FIOASYNC:
    if (*(int *)data) {
      so->so_state |= SS_ASYNC;
      so->so_rcv.sb_flags |= SB_ASYNC;
      so->so_snd.sb_flags |= SB_ASYNC;
    } else {
      so->so_state &= ~SS_ASYNC;
      so->so_rcv.sb_flags &= ~SB_ASYNC;
      so->so_snd.sb_flags &= ~SB_ASYNC;
    }
    goto Return;
    
  case FIONREAD:
    *(int *)data = so->so_rcv.sb_cc;
    goto Return;
    
  case SIOCATMARK:
    *(int *)data = (so->so_state&SS_RCVATMARK) != 0;
    goto Return;
  }

  /*
   * Interface/routing/protocol specific ioctls:
   * interface and routing ioctls should have a
   * different entry since a socket's unnecessary -- not really,
   * ifioctl needs the socket (?)
   */
  if ((IOCGROUP(cmd) & 0XDF) == 'I') {
    error = (ifioctl(so, cmd, data));
    goto Return;
  }
  if (IOCGROUP(cmd) == 'r') {
    error = (rtioctl(cmd, data));
    goto Return;
  }
  error = ((*so->so_proto->pr_usrreq)(so, PRU_CONTROL, 
	  (struct mbuf *)cmd, (struct mbuf *)data, (struct mbuf *)0));

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}


/*
 *  Semaphore to prevent select buffers from simultaneous modifications
 */
struct SignalSemaphore select_semaphore = { 0 };

struct newselbuf {
  int s_state;
#define	SB_CLEAR	0		/* wait condition cleared */
#define	SB_WILLWAIT	1		/* will wait if not cleared */
#define	SB_WAITING	2		/* waiting - wake up */
  int s_count;
  struct newselitem {
    struct	newselitem  *	si_next;	/* next selbuf in item chain */
    struct	newselitem **	si_prev;	/* back pointer */
    struct	newselbuf   *	si_selbuf;	/* 'thread' waiting */
  } s_item[0]; /* selitems are allocated at select when right
		  number of descriptors are known */
};

void select_init(void)
{
  InitSemaphore(&select_semaphore);
}

/*
 *  symbolic names FREAD and FWRITE aren'n needed anywhere else if only
 *  socket descriptors are in use.
 */
#define FREAD	0x1
#define FWRITE	0x2

/*
 * Select(), selscan(), selenter(), selwakeup() and unselect() are taken from
 * ../server/kern/sys_generic.h in bsdss distribution. soo_select() is
 * taken from ../server/kern/sys_socket.h, same distribution.
 */

/* 
 * Althrough the fd_set is used as the type of the masks, the size
 * of the fd_set is not fixed, and is indeed calculated from nfds.
 */
LONG SAVEDS RAF7(_WaitSelect,
		 struct SocketBase *,	libPtr,		a6,
		 ULONG,			nfds,		d0,
		 fd_set *,		readfds,	a0,
		 fd_set *,		writefds,	a1,
		 fd_set *,		exeptfds,	a2,
		 struct timeval *,	timeout,	a3,
		 ULONG *,		sigmp,		d1)
#if 0
{
#endif
  
  fd_mask * obits;
  u_int obitsize;  /* in bytes */
  int error, retval;
  int selitemcount;
  ULONG sigmask = sigmp ? *sigmp : 0;

  struct newselbuf * newselbuf;
  
  CHECK_TASK();

  if (nfds > libPtr->dTableSize)
    nfds = libPtr->dTableSize;	/* forgiving; slightly wrong */
  
  if (timeout && itimerfix(timeout)) {
    error = EINVAL;
    goto Return;
  }
  
  /*
   *	We use selscan twice each time around the loop.
   *	The first time, we actually put ourself on device chains.
   *	The second time, we make a "fast" check for a true condition.
   *	selenter does nothing and we don't need to use unselect.
   *	We only need to zero obits once because if selscan
   *	turns on a bit it returns non-zero and we stop.
   *
   *	The "fast" scan is also done before the loop is ever entered.
   *
   *	We need to allocate some temporary buffers for output bit masks
   *	and, later for selitems. The amount of selitems needed is
   *	calculated during the first selscan -- when selenter does nothing
   */

  obitsize = howmany(nfds, NFDBITS) * sizeof (fd_mask);
  if (allocDataBuffer(&libPtr->selitems, 3 * obitsize) == FALSE) {
    error = ENOMEM;
    goto Return;
  }
  obits = (fd_mask *)libPtr->selitems.db_Addr;

  aligned_bzero((caddr_t)obits, 3 * obitsize);

  /*
   *	We make a first pass to check for conditions
   *	which are already true.  Clearing uu_sb
   *	will keep selenter from doing anything.
   */
  libPtr->p_sb = 0;
  error = selscan(libPtr, readfds, writefds, exeptfds,
		  obits, nfds, &retval, &selitemcount);

  /*
   *	Check if the 'real' loop should be entered.
   */
  if (!error && !retval) {
    if (timeout == NULL || timeout->tv_secs != 0L || timeout->tv_micro != 0L) {
	
      /*
       * Send timeout if there is any.
       */
      if (timeout)
	tsleep_send_timeout(libPtr, timeout);

      /*
       * Now, make sure there is room for all the selitems...
       */
      if (allocDataBuffer(&libPtr->selitems,
			  3 * obitsize + sizeof (struct newselbuf) +
			  selitemcount * sizeof(struct newselitem)) == FALSE) {
	error = ENOMEM;
	goto Return;
      }
      /*
       * We need to clear obits again _if_ the memory area for it has 
       * changed.
       */
      if ((fd_mask *)libPtr->selitems.db_Addr != obits) {
	obits = (fd_mask *)libPtr->selitems.db_Addr;
	aligned_bzero((caddr_t)obits, 3 * obitsize);
      }
      newselbuf = (struct newselbuf *)((caddr_t)obits + (3 * obitsize));
      
      while (TRUE) {

	/*
	 *	Now we get serious about blocking.
	 *	selenter will put us on device chains.
	 *	We don't need select_lock here because
	 *	no other thread can get at selbuf yet.
	 */
	
	newselbuf->s_state = SB_WILLWAIT;
	newselbuf->s_count = 0;
	libPtr->p_sb = newselbuf;
	
	error = selscan(libPtr, readfds, writefds, exeptfds,
			obits, nfds, &retval, &selitemcount);
	if (error || retval) {
	  ObtainSemaphore(&select_semaphore);
	  unselect(newselbuf);
	  ReleaseSemaphore(&select_semaphore);
	  break;
	}
	
	/*
	 *	If the state is already SB_CLEAR, then a selwakeup
	 *	slipped in after the selscan.
	 */
	
	ObtainSemaphore(&select_semaphore);
	if (newselbuf->s_state == SB_CLEAR) {
	  unselect(newselbuf);
	  ReleaseSemaphore(&select_semaphore);
	}
	else {
	  newselbuf->s_state = SB_WAITING;
	  tsleep_enter(libPtr, (caddr_t)newselbuf, "select");
	  ReleaseSemaphore(&select_semaphore);
	  /* ReleaseSemaphore(&syscall_semaphore); don't have this */
	  
	  error = tsleep_main(libPtr, sigmask);
	  
	  /* ObtainSemaphore(&syscall_semaphore); see above */
	  ObtainSemaphore(&select_semaphore);
	  unselect(newselbuf);
	  ReleaseSemaphore(&select_semaphore);
	  
	  if (error != 0) {
	    /*
	     * The break at the end of this block means that selscan will NOT 
	     * be done after this. This provides faster response to the 
	     * used defined signals.
	     *
	     * Note also that the semantics of the tsleep_abort_timeout() has 
	     * changed; it is now accepted to call it even after the timeout
	     * has expired.
	     */
	    if (error == ERESTART || error == EWOULDBLOCK)
	      error = 0;
	    
	    break;		/* do not scan now */
	  }
	  
	}
	/*
	 *	Scan for true conditions. Clearing uu_sb
	 *	will keep selenter from doing anything.
	 */
	libPtr->p_sb = 0;
	error = selscan(libPtr, readfds, writefds, exeptfds,
			obits, nfds, &retval, &selitemcount);
	if (error || retval)
	  break;
      } /* while (TRUE) */	

      if (timeout)		/* abort the timeout if any */
	tsleep_abort_timeout(libPtr, timeout);
    }
  }

 Return:
  libPtr->p_sb = 0;

#define	putbits(name, x) \
  if (name) { \
    aligned_bcopy(((caddr_t)obits) + (x * obitsize), \
		  (caddr_t)name, obitsize); \
   }

  if (error == 0) {
    if (sigmp)
      *sigmp &= SetSignal(0L, sigmask);
    
    putbits(readfds, 0);
    putbits(writefds, 1);
    putbits(exeptfds, 2);
  }
#undef putbits

  API_STD_RETURN(error, retval);
}

/* 
 * Althrough the fd_set is used as the type of the masks, the size
 * of the fd_set is not fixed, and is indeed calculated from nfd.
 */
static int 
selscan(struct SocketBase *p,
	fd_set * in,
	fd_set * ou,
	fd_set * ex,
	fd_mask * obits,
	int nfd,
	int * retval,
	int * selitemcount_p)
{
	register int which, i, j, selitemcount = 0;
	register fd_mask bits;
	register fd_set * cbits = NULL;
	struct socket *so;
	int flag = 0;
	int n = 0;
	int error = 0;
	u_int obitsize = howmany(nfd, NFDBITS); /* in longwords */

	for (which = 0; which < 3; which++) {
		switch (which) {

		case 0:
			flag = FREAD;
			cbits = in;
			break;
		case 1:
			flag = FWRITE;
			cbits = ou;
			break;
		case 2:
			flag = 0;
			cbits = ex;
			break;
		}
		if (cbits != NULL) {
		  for (i = 0; i < nfd; i += NFDBITS) {
			bits = cbits->fds_bits[i/NFDBITS];
			while ((j = ffs(bits)) && i + --j < nfd) {
				bits &= ~(1 << j);
				so = p->dTable[i + j];
				if (so == NULL) {
				  error = EBADF;
				  break;
				}
				selitemcount++;
				if (soo_select(so, flag, p)) {
				  FD_SET(i + j, (fd_set *)
					 (obits + (which * obitsize)));
				  n++;
				}
			}
		  }
		}
	}
	*retval = n;
	*selitemcount_p = selitemcount;
	return (error);
}

/*
 * Add a select event to the current select buffer.
 * Chain all select buffers that are waiting for
 * the same event.
 */
void selenter(struct SocketBase *	p,
	      struct newselitem **	hdr)
{
	register struct newselbuf *sb = p->p_sb;

	if (sb) {
	    register struct newselitem **sip = hdr;
	    register struct newselitem *si;

	    ObtainSemaphore(&select_semaphore);

	    /* initialize this select item */
	    si = &sb->s_item[sb->s_count++];
	    si->si_selbuf = sb;

	    /* and add it to the device chain */
	    if ((si->si_next = *sip))
		si->si_next->si_prev = &si->si_next;
	    *(si->si_prev = sip) = si;

	    ReleaseSemaphore(&select_semaphore);
	}
}

static void
unselect(register struct newselbuf * sb)
{
	int i;

	/*
	 *	We unchain the select items.
	 *	This assumes select_lock is held.
	 */

	for (i = 0; i < sb->s_count; i++) {
	    register struct newselitem *si = &sb->s_item[i];

	    /*
	     *	We remove this item from its device chain.
	     *	If selwakeup already got to it, then this is a nop.
	     */

	    *si->si_prev = si->si_next;
	}
}

/*
 * Wakeup all threads waiting on the same select event.
 * Do not clear the other select events that each thread
 * is waiting on; thread will do that itself.
 */
void selwakeup(struct newselitem **hdr)
{
	register struct newselitem **sip;
	register struct newselitem *si;

	ObtainSemaphore(&select_semaphore);

	for (sip = hdr; (si = *sip) != 0; sip = &si->si_next) {
	    register struct newselbuf *sb;
	    int old_state;

	    /* for when unselect tries to dequeue this item */
	    si->si_prev = &si->si_next;

	    /* check for wakeup */
	    sb = si->si_selbuf;
	    old_state = sb->s_state;
	    sb->s_state = SB_CLEAR;
	    if (old_state == SB_WAITING)
		wakeup((caddr_t)sb);
	}

	/* clear the device chain */
	*(struct newselitem **)hdr = 0;
	ReleaseSemaphore(&select_semaphore);
}

static int
soo_select(struct socket *so,
	       int which,
	       struct SocketBase *p)
{
	register spl_t s = splnet();

	switch (which) {

	case FREAD:
		if (soreadable(so)) {
			splx(s);
			return (1);
		}
		sbselqueue(&so->so_rcv, p);
		break;

	case FWRITE:
		if (sowriteable(so)) {
			splx(s);
			return (1);
		}
		sbselqueue(&so->so_snd, p);
		break;

	case 0:
		if (so->so_oobmark ||
		    (so->so_state & SS_RCVATMARK)) {
			splx(s);
			return (1);
		}
		sbselqueue(&so->so_rcv, p);
		break;
	}
	splx(s);
	return (0);
}

/*
 * countSockets() counts how many references ONE task (SocketBase) have to a
 * socket
 */

static int
countSockets(struct SocketBase * libPtr, struct socket * so)
{
  int i, count = 0;

  for (i = 0; i < libPtr->dTableSize; i++)
    if (libPtr->dTable[i] == so)
      count++;

  return count;
}

LONG SAVEDS RAF2(_CloseSocket,
		 struct SocketBase *,	libPtr,	a6,
		 LONG,			fd,	d0)
#if 0
{
#endif

  register int error;
  struct socket *so;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);

  /*
   * PORT (AmiTCP_NG) security fix: validate fd BEFORE touching the
   * usage bitmask. FD_ISSET/FD_CLR are raw, unbounded fds_bits[fd/NFDBITS]
   * accesses; an out-of-range fd (double-close, stale/garbage descriptor) would
   * otherwise read, and then via FD_CLR write-clear, a bit at an arbitrary
   * offset from the dTable allocation -- memory corruption on this MMU-less
   * target. Every other fd entry point range-checks first (see getSock()).
   */
  if ((unsigned)fd >= (unsigned)libPtr->dTableSize) {
    error = EBADF;
    goto Return;
  }

  /*
   * Check from used sockets bitmask if this socket is in use
   */
  if (! FD_ISSET(fd, (fd_set *)(libPtr->dTable + libPtr->dTableSize))) {
    error = EBADF;
    goto Return;
  }

  /*
   * If the link library cannot free the fd, then we cannot do it either.
   */
  if (libPtr->fdCallback)
    if ((error = libPtr->fdCallback(fd, FDCB_FREE)))
      goto Return;

  FD_CLR(fd, (fd_set *)(libPtr->dTable + libPtr->dTableSize));

  if ((error = getSock(libPtr, fd, &so))) {
/*    error = ENOTSOCK; -- well, bit set, but socket == NULL */
    error = 0; /* ignore silently */
    goto Return;
  }
  if (so->so_pgid == libPtr && countSockets(libPtr, so) == 1) 
    so->so_pgid = NULL;		/* not ours any more */

  /*
   * Decrease the reference count of a socket (AmiTCP addition) and return if
   * not zero.
   */
  if (--so->so_refcnt <= 0)
    error = soclose(so);

  /*
   * now clear socket from descriptor table. Socket usage bitmask has been
   * cleared earlier
   */
  libPtr->dTable[fd] = NULL;

 Return: 
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, 0);
}


struct SocketNode {
  struct MinNode	sn_Node;
  LONG			sn_Id;
  struct socket *	sn_Socket;
};

/*
 * checkId() checks that there are no released sockets w/ given id.
 * used from function makeId().
 */

static LONG checkId(int id)
{
  struct Node * sn;

  Forbid();
  for (sn = releasedSocketList.lh_Head; sn->ln_Succ; sn = sn->ln_Succ)
    if (((struct SocketNode *)sn)->sn_Id == id) {
      Permit();
      return EINVAL;
    }
  Permit();	
  return 0;
}

#define FIRSTUNIQUEID 65536

/*
 * makeId() gets next unique id for socket to be released if *id is -1
 * otherwise calls checkId for given *id.
 * used from functions ReleaseSocket() and ReleaseCopyOfSocket().
 */
static LONG makeId(LONG id)
{
  static LONG uniqueId = FIRSTUNIQUEID;

  if (id == UNIQUE_ID) {
    do {
      uniqueId += sizeof (void *);
      if (uniqueId < 0)
	uniqueId = FIRSTUNIQUEID;
    } while (checkId(uniqueId) != 0);

    return uniqueId;
  }
  else {
    if (checkId(id) == 0)
      return id;
    else
      return -1; /* error */
  }
}

LONG SAVEDS RAF3(_ReleaseSocket,
		 struct SocketBase *,	libPtr, a6,
		 LONG,			fd,	d0,
		 LONG,			id,	d1)
#if 0		 
{
#endif
  struct SocketNode *sn;
  struct socket *so;
  int error = 0;

  CHECK_TASK();

  if ((ULONG)id >= FIRSTUNIQUEID && (id = makeId(id)) == -1) {
    error = EINVAL;
    goto Return;
  }

  if ((error = getSock(libPtr, fd, &so)) != 0)
    goto Return;
 
  if ((sn = AllocMem(sizeof (struct SocketNode), MEMF_PUBLIC)) == NULL) {
    error = ENOMEM;
    goto Return;
  }
  /*
   * Tell the link library that the fd is free
   */
  if (libPtr->fdCallback)
    if ((error = libPtr->fdCallback(fd, FDCB_FREE))) {
      FreeMem(sn, sizeof (struct SocketNode));
      goto Return;
    }
  
  if (so->so_pgid == libPtr && countSockets(libPtr, so) == 1) 
    so->so_pgid = NULL;		/* not ours any more */
  sn->sn_Id = id;
  sn->sn_Socket = so;
  libPtr->dTable[fd] = NULL;
  FD_CLR(fd, (fd_set *)(libPtr->dTable + libPtr->dTableSize));
  
  Forbid();
  AddTail(&releasedSocketList, (struct Node *)sn);
  Permit();

 Return: API_STD_RETURN(error, id);
}

LONG SAVEDS RAF3(_ReleaseCopyOfSocket,
		 struct SocketBase *,	libPtr, a6,
		 LONG,			fd,	d0,
		 LONG,			id,	d1)
#if 0		 
{
#endif
  struct SocketNode *sn;
  struct socket *so;
  int error = 0;

  CHECK_TASK();

  if ((ULONG)id >= FIRSTUNIQUEID && (id = makeId(id)) == -1) {
    error = EINVAL;
    goto Return;
  }

  if ((error = getSock(libPtr, fd, &so)) != 0)
    goto Return;

  if ((sn = AllocMem(sizeof (struct SocketNode), MEMF_PUBLIC)) == NULL) {
    error = ENOMEM;
    goto Return;
  }
  /* so_pgid left intact */
  sn->sn_Id = id;
  sn->sn_Socket = so;
  so->so_refcnt++;
  Forbid();
  AddTail(&releasedSocketList, (struct Node *)sn);
  Permit();

 Return: API_STD_RETURN(error, id);
}

LONG SAVEDS RAF5(_ObtainSocket,
		 struct SocketBase *,	libPtr, 	a6,
		 LONG,			id,		d0,
		 LONG,			domain,		d1,
		 LONG,			type,		d2,
		 LONG,			protocol,	d3)
#if 0
{
#endif

  struct protosw *prp;
  struct Node * sn;
  int error = 0;
  LONG fd;

  CHECK_TASK();

  if (domain == 0) {
    if (id < FIRSTUNIQUEID) {
      error = EPFNOSUPPORT;
      goto Return;
    }
  }
  else {
    if (protocol)
      prp = pffindproto(domain, protocol, type);
    else
      prp = pffindtype(domain, type);
    /*
     * PORT (AmiTCP_NG) fix: when the protocol lookup fails prp is
     * NULL, and the original then dereferenced prp->pr_type below -- a
     * deterministic NULL-deref crash for any bad domain/type/protocol tuple.
     * Bail out first.
     */
    if (prp == 0) {
      error = EPROTONOSUPPORT;
      goto Return;
    }
    if (prp->pr_type != type)
      error = EPROTOTYPE;
    if (error)
      goto Return;
  }  

  if ((error = sdFind(libPtr, &fd)) != 0)
    goto Return;

  /*
   * Tell the link library about the new fd
   */
  if (libPtr->fdCallback)
    if ((error = libPtr->fdCallback(fd, FDCB_ALLOC)))
      goto Return;
  
  Forbid();
  for (sn = releasedSocketList.lh_Head; sn->ln_Succ; sn = sn->ln_Succ)
    if (((struct SocketNode *)sn)->sn_Id == id) {
      /*
       * PORT (AmiTCP_NG) fix: on the domain==0 "match by unique id
       * alone" fast path, prp was never assigned -- comparing it here read an
       * uninitialised pointer. Only compare protocols when a domain was given.
       */
      if (domain != 0 && prp != ((struct SocketNode *)sn)->sn_Socket->so_proto)
	continue;
      Remove(sn);
      Permit();
      libPtr->dTable[fd] = ((struct SocketNode *)sn)->sn_Socket;
      FD_SET(fd, (fd_set *)(libPtr->dTable + libPtr->dTableSize));
      FreeMem(sn, sizeof (struct SocketNode));
      goto Return;
    }
  /* here if sdFind succeeded but search of socket failed */
  Permit();

  /*
   * Free the just allocated fd
   */
  if (libPtr->fdCallback)
    libPtr->fdCallback(fd, FDCB_FREE);

  error = EWOULDBLOCK;
  
 Return: API_STD_RETURN(error, fd);
}

LONG SAVEDS RAF3(_Dup2Socket,
		 struct SocketBase *,	libPtr, 	a6,
		 LONG,			fd1,		d0,
		 LONG,			fd2,		d1)
#if 0     
{
#endif
  LONG newfd;
  int error = 0;
  struct socket *so;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr); /*many tasks may have access to the socket */

  if (fd1 == -1)
    so = NULL;
  else if ((error = getSock(libPtr, fd1, &so)))
    goto Return;

  if (fd2 == -1)
    if ((error = sdFind(libPtr, &newfd)))
      goto Return;
    else
      fd2 = newfd;
  else {
    if ((ULONG)fd2 >= libPtr->dTableSize) {
      error = EBADF;
      goto Return;
    }
    if (libPtr->dTable[fd2] != NULL)
      CloseSocket(libPtr, fd2);
    /*
     * Check if the fd is free on the link library
     */
    if (libPtr->fdCallback)
      if ((error = libPtr->fdCallback(fd2, FDCB_CHECK)))
	goto Return;

  }
  /*
   * Tell the link library about the new fd
   */
  if (libPtr->fdCallback)
    if ((error = libPtr->fdCallback(fd2, FDCB_ALLOC)))
      goto Return;

  FD_SET(fd2, (fd_set *)(libPtr->dTable + libPtr->dTableSize));
  
  if (so != NULL) {
    so->so_refcnt++;
    libPtr->dTable[fd2] = so;
  }

 Return:
  ReleaseSyscallSemaphore(libPtr);
  API_STD_RETURN(error, fd2);
}
