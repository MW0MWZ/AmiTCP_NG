#ifndef _INLINE_SOCKET_H
#define _INLINE_SOCKET_H

/*
 * $Id: socket.h,v 3.1 1994/01/22 11:37:36 too Exp $
 * $Release$
 *
 * Gcc inlines for bsdsocket.library. 
 *
 * Copyright © 1994 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                  All rights reserved.
 *
 * Created: Thu Jan 20 18:23:27 1994 too
 * Last modified: Mon Jan 31 22:46:10 1994 too
 */

#include <sys/cdefs.h>
#include <inline/stubs.h>

__BEGIN_DECLS

#ifdef BASE_EXT_DECL
#error BASE_EXT_DECL etc declarations not allowed
#endif

#ifndef SOCKETBASE_IN_CONTEXT
#define BASE_EXT_DECL extern struct Library * SocketBase;
#define BASE_PAR_DECL
#define BASE_PAR_DECL0 void
#else
#define BASE_EXT_DECL
#define BASE_PAR_DECL struct Library * SocketBase,
#define BASE_PAR_DECL0 struct Library * SocketBase
#endif
/* #define BASE_NAME SocketBase */
  
#ifndef AMITCP_NEW_NAMES
#define STDSOCKAPI
#define		  Socket	socket
#define		    Bind	bind
#define		  Listen	listen
#define		  Accept	accept
#define		 Connect	connect
#define		    Send	send
#define		  SendTo	sendto
#define		 SendMsg	sendmsg
#define		    Recv	recv
#define		RecvFrom	recvfrom
#define		 RecvMsg	recvmsg
#define		Shutdown	shutdown
#define	      SetSockOpt	setsockopt
#define	      GetSockOpt	getsockopt
#define	     GetSockName	getsockname
#define	     GetPeerName	getpeername
#define	       Inet_Addr	inet_addr
#define	    Inet_Network	inet_network
#define	   GetHostByName	gethostbyname
#define	   GetHostByAddr	gethostbyaddr
#define	    GetNetByName	getnetbyname
#define	    GetNetByAddr	getnetbyaddr
#define	   GetServByName	getservbyname
#define    GetServByPort	getservbyport
#define   GetProtoByName	getprotobyname
#define GetProtoByNumber	getprotobynumber
#define	   GetDTableSize	getdtablesize
#define	     GetHostName	gethostname
#define	       GetHostId	gethostid
#define		 SyslogA	vsyslog
#endif

static __inline LONG 
Accept (BASE_PAR_DECL LONG s,struct sockaddr * addr,LONG * addrlen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  addr;
  register LONG *a1 __asm("a1") =  addrlen;
  __asm __volatile ("jsr a6@(-0x30)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
Bind (BASE_PAR_DECL LONG s,const struct sockaddr * name,LONG  namelen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register const struct sockaddr *a0 __asm("a0") =  name;
  register LONG d1 __asm("d1") =  namelen;
  __asm __volatile ("jsr a6@(-0x24)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
CloseSocket (BASE_PAR_DECL LONG d)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = d;
  __asm __volatile ("jsr a6@(-0x78)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
Connect (BASE_PAR_DECL LONG s,const struct sockaddr * name,LONG  namelen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register const struct sockaddr *a0 __asm("a0") =  name;
  register LONG d1 __asm("d1") =  namelen;
  __asm __volatile ("jsr a6@(-0x36)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
Dup2Socket (BASE_PAR_DECL LONG fd1,LONG  fd2)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = fd1;
  register LONG d1 __asm("d1") =  fd2;
  __asm __volatile ("jsr a6@(-0x108)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
Errno (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  __asm __volatile ("jsr a6@(-0xa2)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
GetDTableSize (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  __asm __volatile ("jsr a6@(-0x8a)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct hostent *
GetHostByAddr (BASE_PAR_DECL const UBYTE *addr,LONG  len,LONG  type)
{
  BASE_EXT_DECL
  register struct hostent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE *a0 __asm("a0") = addr;
  register LONG d0 __asm("d0") =  len;
  register LONG d1 __asm("d1") =  type;
  __asm __volatile ("jsr a6@(-0xd8)"
  : "=r" (_res)
  : "r" (a6), "r" (a0), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct hostent *
GetHostByName (BASE_PAR_DECL const UBYTE *name)
{
  BASE_EXT_DECL
  register struct hostent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE *a0 __asm("a0") = name;
  __asm __volatile ("jsr a6@(-0xd2)"
  : "=r" (_res)
  : "r" (a6), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline ULONG 
GetHostId (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register ULONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  __asm __volatile ("jsr a6@(-0x120)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
GetHostName (BASE_PAR_DECL const UBYTE *hostname,LONG  size)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE *a0 __asm("a0") = hostname;
  register LONG d0 __asm("d0") =  size;
  __asm __volatile ("jsr a6@(-0x11a)"
  : "=r" (_res)
  : "r" (a6), "r" (a0), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct netent *
GetNetByAddr (BASE_PAR_DECL LONG net,LONG  type)
{
  BASE_EXT_DECL
  register struct netent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = net;
  register LONG d1 __asm("d1") =  type;
  __asm __volatile ("jsr a6@(-0xe4)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct netent *
GetNetByName (BASE_PAR_DECL const UBYTE *name)
{
  BASE_EXT_DECL
  register struct netent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE *a0 __asm("a0") = name;
  __asm __volatile ("jsr a6@(-0xde)"
  : "=r" (_res)
  : "r" (a6), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
GetPeerName (BASE_PAR_DECL LONG s,struct sockaddr * hostname,LONG * namelen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  hostname;
  register LONG *a1 __asm("a1") =  namelen;
  __asm __volatile ("jsr a6@(-0x6c)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct protoent *
GetProtoByName (BASE_PAR_DECL const UBYTE *name)
{
  BASE_EXT_DECL
  register struct protoent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE *a0 __asm("a0") = name;
  __asm __volatile ("jsr a6@(-0xf6)"
  : "=r" (_res)
  : "r" (a6), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct protoent *
GetProtoByNumber (BASE_PAR_DECL LONG proto)
{
  BASE_EXT_DECL
  register struct protoent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = proto;
  __asm __volatile ("jsr a6@(-0xfc)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct servent *
GetServByName (BASE_PAR_DECL const UBYTE *name,const UBYTE * proto)
{
  BASE_EXT_DECL
  register struct servent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE *a0 __asm("a0") = name;
  register const UBYTE *a1 __asm("a1") =  proto;
  __asm __volatile ("jsr a6@(-0xea)"
  : "=r" (_res)
  : "r" (a6), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct servent *
GetServByPort (BASE_PAR_DECL LONG port,const UBYTE * proto)
{
  BASE_EXT_DECL
  register struct servent * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = port;
  register const UBYTE *a0 __asm("a0") =  proto;
  __asm __volatile ("jsr a6@(-0xf0)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
GetSockName (BASE_PAR_DECL LONG s,struct sockaddr * hostname,LONG * namelen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  hostname;
  register LONG *a1 __asm("a1") =  namelen;
  __asm __volatile ("jsr a6@(-0x66)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
GetSockOpt (BASE_PAR_DECL LONG s,LONG  level,LONG  optname,void * optval,LONG * optlen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  level;
  register LONG d2 __asm("d2") =  optname;
  register void *a0 __asm("a0") =  optval;
  register LONG *a1 __asm("a1") =  optlen;
  __asm __volatile ("jsr a6@(-0x60)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1","d2", "memory");
  return _res;
}
static __inline ULONG 
Inet_Addr (BASE_PAR_DECL const UBYTE * cp)
{
  BASE_EXT_DECL
  register ULONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE * a0 __asm("a0") = cp;
  __asm __volatile ("jsr a6@(-0xb4)"
  : "=r" (_res)
  : "r" (a6), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline ULONG 
Inet_LnaOf (BASE_PAR_DECL LONG in)
{
  BASE_EXT_DECL
  register ULONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = in;
  __asm __volatile ("jsr a6@(-0xba)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline ULONG 
Inet_MakeAddr (BASE_PAR_DECL ULONG net,ULONG  host)
{
  BASE_EXT_DECL
  register ULONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register ULONG d0 __asm("d0") = net;
  register ULONG d1 __asm("d1") =  host;
  __asm __volatile ("jsr a6@(-0xc6)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline ULONG 
Inet_NetOf (BASE_PAR_DECL LONG in)
{
  BASE_EXT_DECL
  register ULONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = in;
  __asm __volatile ("jsr a6@(-0xc0)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline ULONG 
Inet_Network (BASE_PAR_DECL const UBYTE * cp)
{
  BASE_EXT_DECL
  register ULONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register const UBYTE * a0 __asm("a0") = cp;
  __asm __volatile ("jsr a6@(-0xcc)"
  : "=r" (_res)
  : "r" (a6), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline char *
Inet_NtoA (BASE_PAR_DECL ULONG in)
{
  BASE_EXT_DECL
  register char * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register ULONG d0 __asm("d0") = in;
  __asm __volatile ("jsr a6@(-0xae)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
IoctlSocket (BASE_PAR_DECL LONG d,ULONG  request,char * argp)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = d;
  register ULONG d1 __asm("d1") =  request;
  register char *a0 __asm("a0") =  argp;
  __asm __volatile ("jsr a6@(-0x72)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
Listen (BASE_PAR_DECL LONG s,LONG  backlog)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  backlog;
  __asm __volatile ("jsr a6@(-0x2a)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
ObtainSocket (BASE_PAR_DECL LONG id,LONG  domain,LONG  type,LONG  protocol)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = id;
  register LONG d1 __asm("d1") =  domain;
  register LONG d2 __asm("d2") =  type;
  register LONG d3 __asm("d3") =  protocol;
  __asm __volatile ("jsr a6@(-0x90)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2), "r" (d3)
  : "a0","a1","d0","d1","d2","d3", "memory");
  return _res;
}
static __inline LONG 
Recv (BASE_PAR_DECL LONG s,UBYTE * buf,LONG  len,LONG  flags)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register UBYTE *a0 __asm("a0") =  buf;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  __asm __volatile ("jsr a6@(-0x4e)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2)
  : "a0","a1","d0","d1","d2", "memory");
  return _res;
}
static __inline LONG 
RecvFrom (BASE_PAR_DECL LONG s,UBYTE * buf,LONG  len,LONG  flags,struct sockaddr * from,LONG * fromlen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register UBYTE *a0 __asm("a0") =  buf;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  register struct sockaddr *a1 __asm("a1") =  from;
  register LONG *a2 __asm("a2") =  fromlen;
  __asm __volatile ("jsr a6@(-0x48)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2), "r" (a1), "r" (a2)
  : "a0","a1","a2","d0","d1","d2", "memory");
  return _res;
}
static __inline LONG 
RecvMsg (BASE_PAR_DECL LONG s,struct msghdr *  msg,LONG  flags)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register struct msghdr * a0 __asm("a0") =  msg;
  register LONG d1 __asm("d1") =  flags;
  __asm __volatile ("jsr a6@(-0x114)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
ReleaseCopyOfSocket (BASE_PAR_DECL LONG fd,LONG  id)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = fd;
  register LONG d1 __asm("d1") =  id;
  __asm __volatile ("jsr a6@(-0x9c)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
ReleaseSocket (BASE_PAR_DECL LONG fd,LONG  id)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = fd;
  register LONG d1 __asm("d1") =  id;
  __asm __volatile ("jsr a6@(-0x96)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
Send (BASE_PAR_DECL LONG s,const UBYTE * msg,LONG  len,LONG  flags)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register const UBYTE *a0 __asm("a0") =  msg;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  __asm __volatile ("jsr a6@(-0x42)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2)
  : "a0","a1","d0","d1","d2", "memory");
  return _res;
}
static __inline LONG 
SendMsg (BASE_PAR_DECL LONG s,struct msghdr *  msg,LONG  flags)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register struct msghdr * a0 __asm("a0") =  msg;
  register LONG d1 __asm("d1") =  flags;
  __asm __volatile ("jsr a6@(-0x10e)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
SendTo (BASE_PAR_DECL LONG s,const UBYTE * msg,LONG  len,LONG  flags,const struct sockaddr * to,LONG  tolen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register const UBYTE *a0 __asm("a0") =  msg;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  register const struct sockaddr *a1 __asm("a1") =  to;
  register LONG d3 __asm("d3") =  tolen;
  __asm __volatile ("jsr a6@(-0x3c)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2), "r" (a1), "r" (d3)
  : "a0","a1","d0","d1","d2","d3", "memory");
  return _res;
}
static __inline void 
SetErrnoPtr (BASE_PAR_DECL void *errno_p,LONG  size)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = SocketBase;
  register void *a0 __asm("a0") = errno_p;
  register LONG d0 __asm("d0") =  size;
  __asm __volatile ("jsr a6@(-0xa8)"
  : /* no output */
  : "r" (a6), "r" (a0), "r" (d0)
  : "a0","a1","d0","d1", "memory");
}
static __inline LONG 
SetSockOpt (BASE_PAR_DECL LONG s,LONG  level,LONG  optname,const void * optval,LONG  optlen)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  level;
  register LONG d2 __asm("d2") =  optname;
  register const void *a0 __asm("a0") =  optval;
  register LONG d3 __asm("d3") =  optlen;
  __asm __volatile ("jsr a6@(-0x5a)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2), "r" (a0), "r" (d3)
  : "a0","a1","d0","d1","d2","d3", "memory");
  return _res;
}
static __inline void 
SetSocketSignals (BASE_PAR_DECL ULONG SIGINTR,ULONG  SIGIO,ULONG  SIGURG)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = SocketBase;
  register ULONG d0 __asm("d0") = SIGINTR;
  register ULONG d1 __asm("d1") =  SIGIO;
  register ULONG d2 __asm("d2") =  SIGURG;
  __asm __volatile ("jsr a6@(-0x84)"
  : /* no output */
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2)
  : "a0","a1","d0","d1","d2", "memory");
}
static __inline LONG 
Shutdown (BASE_PAR_DECL LONG s,LONG  how)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  how;
  __asm __volatile ("jsr a6@(-0x54)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline LONG 
Socket (BASE_PAR_DECL LONG domain,LONG  type,LONG  protocol)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = domain;
  register LONG d1 __asm("d1") =  type;
  register LONG d2 __asm("d2") =  protocol;
  __asm __volatile ("jsr a6@(-0x1e)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2)
  : "a0","a1","d0","d1","d2", "memory");
  return _res;
}
static __inline LONG 
SocketBaseTagList (BASE_PAR_DECL struct TagItem *taglist)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register struct TagItem *a0 __asm("a0") = taglist;
  __asm __volatile ("jsr a6@(-0x126)"
  : "=r" (_res)
  : "r" (a6), "r" (a0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}

#ifndef NO_INLINE_STDARG
#define SocketBaseTags(tags...) \
    ({ ULONG _tags[] = { tags }; SocketBaseTagList(_tags); })
#endif

static __inline LONG 
WaitSelect (BASE_PAR_DECL LONG nfds,fd_set * readfds,fd_set * writefds,fd_set * execptfds,struct timeval * timeout,ULONG * maskp)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = nfds;
  register fd_set *a0 __asm("a0") =  readfds;
  register fd_set *a1 __asm("a1") =  writefds;
  register fd_set *a2 __asm("a2") =  execptfds;
  register struct timeval *a3 __asm("a3") =  timeout;
  register ULONG *d1 __asm("d1") =  maskp;
  __asm __volatile ("jsr a6@(-0x7e)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1), "r" (a2), "r" (a3), "r" (d1)
  : "a0","a1","a2","a3","d0","d1", "memory");
  return _res;
}


#ifdef STDSOCKAPI
#undef STDSOCKAPI
#undef Socket
#undef Bind
#undef Listen
#undef Accept
#undef Connect
#undef Send
#undef SendTo
#undef SendMsg
#undef Recv
#undef RecvFrom
#undef RecvMsg
#undef Shutdown
#undef SetSockOpt
#undef GetSockOpt
#undef GetSockName
#undef GetPeerName
#undef Inet_Addr
#undef Inet_Network
#undef GetHostByName
#undef GetHostByAddr
#undef GetNetByName
#undef GetNetByAddr
#undef GetServByName
#undef GetServByPort
#undef GetProtoByName
#undef GetProtoByNumber
#undef GetDTableSize
#undef GetHostName
#undef GetHostId
#undef SyslogA
#endif


static __inline LONG 
InheritSocket (BASE_PAR_DECL LONG id)
{
  BASE_EXT_DECL
  register LONG  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = SocketBase;
  register LONG d0 __asm("d0") = id;
  register LONG d1 __asm("d1") =  0;
  __asm __volatile ("jsr a6@(-0x90)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}


#ifdef SOCKETBASE_IN_CONTEXT

#ifdef AMITCP_NEW_NAMES

#define		     Socket(r...)	Socket(SocketBase, r)
#define		       Bind(r...)	Bind(SocketBase, r)
#define		     Listen(r...)	Listen(SocketBase, r)
#define		     Accept(r...)	Accept(SocketBase, r)
#define		    Connect(r...)	Connect(SocketBase, r)
#define		     SendTo(r...)	SendTo(SocketBase, r)
#define		       Send(r...)	Send(SocketBase, r)
#define		   RecvFrom(r...)	RecvFrom(SocketBase, r)
#define		       Recv(r...)	Recv(SocketBase, r)
#define		   Shutdown(r...)	Shutdown(SocketBase, r)
#define		 SetSockOpt(r...)	SetSockOpt(SocketBase, r)
#define		 GetSockOpt(r...)	GetSockOpt(SocketBase, r)
#define		GetSockName(r...)	GetSockName(SocketBase, r)
#define		GetPeerName(r...)	GetPeerName(SocketBase, r)

#define	      GetDTableSize()		GetDTableSize(SocketBase)

#define		  Inet_Addr(r...)	Inet_Addr(SocketBase, r)
#define	       Inet_Network(r...)	Inet_Network(SocketBase, r)
#define	      GetHostByName(r...)	GetHostByName(SocketBase, r)
#define	      GetHostByAddr(r...)	GetHostByAddr(SocketBase, r)
#define	       GetNetByName(r...)	GetNetByName(SocketBase, r)
#define	       GetNetByAddr(r...)	GetNetByAddr(SocketBase, r)
#define	      GetServByName(r...)	GetServByName(SocketBase, r)
#define	      GetServByPort(r...)	GetServByPort(SocketBase, r)
#define	     GetProtoByName(r...)	GetProtoByName(SocketBase, r)
#define	   GetProtoByNumber(r...)	GetProtoByNumber(SocketBase, r)

#define		    SyslogA(r...)	SyslogA(SocketBase, r)

#define		    SendMsg(r...)	SendMsg(SocketBase, r)
#define		    RecvMsg(r...)	RecvMsg(SocketBase, r)

#define		GetHostName(r...)	GetHostName(SocketBase, r)
#define		  GetHostId()		GetHostId(SocketBase)

#else /* AMITCP_OLD_NAMES */

#define		     socket(r...)	socket(SocketBase, r)
#define		       bind(r...)	bind(SocketBase, r)
#define		     listen(r...)	listen(SocketBase, r)
#define		     accept(r...)	accept(SocketBase, r)
#define		    connect(r...)	connect(SocketBase, r)
#define		     sendto(r...)	sendto(SocketBase, r)
#define		       send(r...)	send(SocketBase, r)
#define		   recvfrom(r...)	recvfrom(SocketBase, r)
#define		       recv(r...)	recv(SocketBase, r)
#define		   shutdown(r...)	shutdown(SocketBase, r)
#define		 setsockopt(r...)	setsockopt(SocketBase, r)
#define		 getsockopt(r...)	getsockopt(SocketBase, r)
#define		getsockname(r...)	getsockname(SocketBase, r)
#define		getpeername(r...)	getpeername(SocketBase, r)

#define	      getdtablesize()		getdtablesize(SocketBase)

#define		  inet_addr(r...)	inet_addr(SocketBase, r)
#define	       inet_network(r...)	inet_network(SocketBase, r)

#define	      gethostbyname(r...)	gethostbyname(SocketBase, r)
#define	      gethostbyaddr(r...)	gethostbyaddr(SocketBase, r)
#define	       getnetbyname(r...)	getnetbyname(SocketBase, r)
#define	       getnetbyaddr(r...)	getnetbyaddr(SocketBase, r)
#define	      getservbyname(r...)	getservbyname(SocketBase, r)
#define	      getservbyport(r...)	getservbyport(SocketBase, r)
#define	     getprotobyname(r...)	getprotobyname(SocketBase, r)
#define	   getprotobynumber(r...)	getprotobynumber(SocketBase, r)

#define		    vsyslog(r...)	vsyslog(SocketBase, r)

#define		    sendmsg(r...)	sendmsg(SocketBase, r)
#define		    recvmsg(r...)	recvmsg(SocketBase, r)

#define		gethostname(r...)	gethostname(SocketBase, r)
#define		  gethostid()		gethostid(SocketBase)

#endif /* AMITCP_NEW_NAMES */

#define		IoctlSocket(r...)	IoctlSocket(SocketBase, r)
#define		CloseSocket(r...)	CloseSocket(SocketBase, r)

#define		 WaitSelect(r...)	WaitSelect(SocketBase, r)
#define	   SetSocketSignals(r...)	SetSocketSignals(SocketBase, r)

#define	       ObtainSocket(r...)	ObtainSocket(SocketBase, r)
#define	      ReleaseSocket(r...)	ReleaseSocket(SocketBase, r)
#define ReleaseCopyOfSocket(r...)	ReleaseCopyOfSocket(SocketBase, r)
#define		      Errno()		Errno(SocketBase)
#define		SetErrnoPtr(r...)	SetErrnoPtr(SocketBase, r)

#define		  Inet_NtoA(r...)	Inet_NtoA(SocketBase, r)
#define		 Inet_LnaOf(r...)	Inet_LnaOf(SocketBase, r)
#define		 Inet_NetOf(r...)	Inet_NetOf(SocketBase, r)
#define	      Inet_MakeAddr(r...)	Inet_MakeAddr(SocketBase, r)

#define   SocketBaseTagList(r...)	SocketBaseTagList(SocketBase, r)

#define	      InheritSocket(r...)	InheritSocket(SocketBase, r)

#endif /* SOCKETBASE_IN_CONTEXT */


#ifndef AMITCP_NEW_NAMES

#include <netinet/in.h>

static __inline LONG
  select(BASE_PAR_DECL LONG nfds, fd_set *readfds, fd_set *writefds, fd_set *exeptfds, struct timeval *timeout)
{
  return WaitSelect(nfds, readfds, writefds, exeptfds, timeout, NULL);
}

static __inline char * 
  inet_ntoa(BASE_PAR_DECL struct in_addr addr) 
{
  return Inet_NtoA(addr.s_addr);
}
static __inline struct in_addr
  inet_makeaddr(BASE_PAR_DECL int net, int host)
{
  struct in_addr addr;

  addr.s_addr = Inet_MakeAddr(net, host);
  return addr;
}
static __inline unsigned long
  inet_lnaof(BASE_PAR_DECL struct in_addr addr) 
{
  return Inet_LnaOf(addr.s_addr);
}
static __inline unsigned long
  inet_netof(BASE_PAR_DECL struct in_addr addr)
{
  return Inet_NetOf(addr.s_addr);
}

#ifdef SOCKETBASE_IN_CONTEXT

#define	       select(r...)	select(SocketBase, r)
#define     inet_ntoa(r...)	inet_ntoa(SocketBase, r)
#define inet_makeaddr(r...)	inet_makeaddr(SocketBase, r)
#define    inet_lnaof(r...)	inet_lnaof(SocketBase, r)
#define    inet_netof(r...)	inet_netof(SocketBase, r)

#endif /* SOCKETBASE_IN_CONTEXT */

#endif /* ! AMITCP_NEW_NAMES */


#undef BASE_EXT_DECL
#undef BASE_PAR_DECL
#undef BASE_PAR_DECL0
/* #undef BASE_NAME */

__END_DECLS

#endif /* _INLINE_SOCKET_H */
