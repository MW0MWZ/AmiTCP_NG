/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 */

/* PORT (AmiTCP_NG): local register temp renamed _res -> _api_d0 to avoid colliding
 * with resolv.h's `#define _res (libPtr->res_state)`. */
#ifndef API_APICALLS_GNUC_H
#define API_APICALLS_GNUC_H

#ifndef API_APICALLS_H
#error include <api/apicalls.h> instead of __FILE__.
#endif

/* Forward-declare struct sockaddr for the prototypes below (accept, bind, connect,
 * getpeername, getsockname, recvfrom, sendto), so they use the real tag instead of
 * a throwaway function-prototype-scope tag. Zero behaviour/ABI impact. */
struct sockaddr;

/*
 * apicalls_gnuc.h --- gcc client-side inline stubs for bsdsocket.library.
 *
 * These are the little functions an application (or the stack calling its own
 * API) uses to invoke a library vector. Each stub loads the SocketBase into
 * register a6 and the arguments into their designated registers, then does
 * `jsr a6@(-LVO)` to jump to the function LVO bytes before the base. That is the
 * entire AmigaOS shared-library calling mechanism, laid bare. docs/ARCHITECTURE.md
 * section 5. (The SAS/C build uses apicalls_sasc.h; this is the gcc equivalent.)
 *
 * How to read one stub, e.g. CloseSocket:
 *   register LONG _api_d0 __asm("d0");                 // will hold the return value
 *   register struct SocketBase *a6 __asm("a6") = base; // library base in a6
 *   register LONG d0 __asm("d0") = fd;                 // argument in d0
 *   __asm __volatile ("jsr a6@(-0x78)"                 // call vector at LVO -120
 *     : "=r"(_api_d0) : "r"(a6),"r"(d0) : ...clobbers...);
 *   return _api_d0;
 * The BASE_* macros below parameterise "the base is the first argument" so the same
 * template generates every stub. tmp/socktest.c and tmp/udptest.c hand-write this
 * same jsr pattern for teaching purposes.
 *
 * PORT (AmiTCP_NG): under gcc 6.5 two mechanical fixes were needed here -- the d0
 * output register must NOT also appear in the clobber list, and the local return
 * temp was renamed _res -> _api_d0 to avoid colliding with resolv.h's `_res` macro.
 * See PORTING.md.
 */

/* PORT (AmiTCP_NG): sys/time.h (GNUC branch) predefines these same generic names
 * for TimerBase and never #undefs them, so #undef here before redefining for
 * SocketBase to avoid a macro-redefined warning. Does not affect sys/time.h's own
 * (already-expanded) usage. */
#undef BASE_EXT_DECL
#undef BASE_PAR_DECL
#undef BASE_PAR_DECL0
#undef BASE_NAME
#define BASE_EXT_DECL
#define BASE_PAR_DECL  struct SocketBase * SocketBase,
#define BASE_PAR_DECL0 struct SocketBase * SocketBase
#define BASE_NAME SocketBase

static __inline LONG 
CloseSocket (BASE_PAR_DECL LONG d)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = d;
  __asm __volatile ("jsr a6@(-0x78)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
Errno (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xa2)"
  : "=r" (_api_d0)
  : "r" (a6)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline ULONG 
Inet_LnaOf (BASE_PAR_DECL LONG in)
{
  BASE_EXT_DECL
  register ULONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG a0 __asm("a0") = in;
  __asm __volatile ("jsr a6@(-0xba)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline ULONG 
Inet_MakeAddr (BASE_PAR_DECL int net,int  host)
{
  BASE_EXT_DECL
  register ULONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register int d0 __asm("d0") = net;
  register int d1 __asm("d1") =  host;
  __asm __volatile ("jsr a6@(-0xc6)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline ULONG 
Inet_NetOf (BASE_PAR_DECL LONG in)
{
  BASE_EXT_DECL
  register ULONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG a0 __asm("a0") = in;
  __asm __volatile ("jsr a6@(-0xc0)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline char *
Inet_NtoA (BASE_PAR_DECL ULONG in)
{
  BASE_EXT_DECL
  register char * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register ULONG a0 __asm("a0") = in;
  __asm __volatile ("jsr a6@(-0xae)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
ObtainSocket (BASE_PAR_DECL LONG id,LONG  domain,LONG  type,LONG  protocol)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = id;
  register LONG d1 __asm("d1") =  domain;
  register LONG d2 __asm("d2") =  type;
  register LONG d3 __asm("d3") =  protocol;
  __asm __volatile ("jsr a6@(-0x90)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2), "r" (d3)
  : "a0","a1","d1","d2","d3", "memory");
  return _api_d0;
}
static __inline LONG 
ReleaseCopyOfSocket (BASE_PAR_DECL LONG id,LONG  fd)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = id;
  register LONG d1 __asm("d1") =  fd;
  __asm __volatile ("jsr a6@(-0x9c)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
ReleaseSocket (BASE_PAR_DECL LONG id,LONG  fd)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = id;
  register LONG d1 __asm("d1") =  fd;
  __asm __volatile ("jsr a6@(-0x96)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
SetDTableSize (BASE_PAR_DECL LONG size)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = size;
  __asm __volatile ("jsr a6@(-0x8a)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
/* PORT (AmiTCP_NG): the implementation (_SetErrnoPtr in amiga_generic2.c) returns
 * LONG in d0 and internal callers test the result (< 0); the SFD-generated stub
 * declared void, so those call sites hit "void value not ignored". Return LONG. */
static __inline LONG
SetErrnoPtr (BASE_PAR_DECL void *errno_p,LONG  size)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register void *a0 __asm("a0") = errno_p;
  register LONG d0 __asm("d0") =  size;
  __asm __volatile ("jsr a6@(-0xa8)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0), "r" (d0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline void 
SetSocketSignals (BASE_PAR_DECL ULONG SIGINTR,ULONG  SIGIO,ULONG  SIGURG)
{
  BASE_EXT_DECL
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register ULONG d0 __asm("d0") = SIGINTR;
  register ULONG d1 __asm("d1") =  SIGIO;
  register ULONG d2 __asm("d2") =  SIGURG;
  __asm __volatile ("jsr a6@(-0x84)"
  : /* no output */
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2)
  : "a0","a1","d1","d2", "memory");
}
static __inline void 
Syslog (BASE_PAR_DECL ULONG level,const STRPTR  format,va_list  ap)
{
  BASE_EXT_DECL
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register ULONG d0 __asm("d0") = level;
  register const STRPTR a0 __asm("a0") =  format;
  register va_list a1 __asm("a1") =  ap;
  __asm __volatile ("jsr a6@(-0x102)"
  : /* no output */
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d1", "memory");
}
static __inline LONG 
WaitSelect (BASE_PAR_DECL LONG nfds,fd_set * readfds,fd_set * writefds,fd_set * execptfds,struct timeval * timeout,ULONG * maskp)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = nfds;
  register fd_set *a0 __asm("a0") =  readfds;
  register fd_set *a1 __asm("a1") =  writefds;
  register fd_set *a2 __asm("a2") =  execptfds;
  register struct timeval *a3 __asm("a3") =  timeout;
  register ULONG *d1 __asm("d1") =  maskp;
  __asm __volatile ("jsr a6@(-0x7e)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1), "r" (a2), "r" (a3), "r" (d1)
  : "a0","a1","a2","a3","d1", "memory");
  return _api_d0;
}
static __inline LONG 
accept (BASE_PAR_DECL LONG s,struct sockaddr * addr,LONG * addrlen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  addr;
  register LONG *a1 __asm("a1") =  addrlen;
  __asm __volatile ("jsr a6@(-0x30)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
bind (BASE_PAR_DECL LONG s,struct sockaddr * name,LONG  namelen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  name;
  register LONG d1 __asm("d1") =  namelen;
  __asm __volatile ("jsr a6@(-0x24)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
connect (BASE_PAR_DECL LONG s,struct sockaddr * name,LONG  namelen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  name;
  register LONG d1 __asm("d1") =  namelen;
  __asm __volatile ("jsr a6@(-0x36)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct hostent *
gethostbyaddr (BASE_PAR_DECL const char *addr,LONG  len,LONG  type)
{
  BASE_EXT_DECL
  register struct hostent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register const char *a0 __asm("a0") = addr;
  register LONG d0 __asm("d0") =  len;
  register LONG d1 __asm("d1") =  type;
  __asm __volatile ("jsr a6@(-0xd8)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0), "r" (d0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct hostent *
gethostbyname (BASE_PAR_DECL const char *name)
{
  BASE_EXT_DECL
  register struct hostent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register const char *a0 __asm("a0") = name;
  __asm __volatile ("jsr a6@(-0xd2)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct netent *
getnetbyaddr (BASE_PAR_DECL LONG net,LONG  type)
{
  BASE_EXT_DECL
  register struct netent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = net;
  register LONG d1 __asm("d1") =  type;
  __asm __volatile ("jsr a6@(-0xe4)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct netent *
getnetbyname (BASE_PAR_DECL const char *name)
{
  BASE_EXT_DECL
  register struct netent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register const char *a0 __asm("a0") = name;
  __asm __volatile ("jsr a6@(-0xde)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
getpeername (BASE_PAR_DECL LONG s,struct sockaddr * hostname,LONG * namelen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  hostname;
  register LONG *a1 __asm("a1") =  namelen;
  __asm __volatile ("jsr a6@(-0x6c)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct protoent *
getprotobyname (BASE_PAR_DECL const char *name)
{
  BASE_EXT_DECL
  register struct protoent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register const char *a0 __asm("a0") = name;
  __asm __volatile ("jsr a6@(-0xf6)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct protoent *
getprotobynumber (BASE_PAR_DECL LONG proto)
{
  BASE_EXT_DECL
  register struct protoent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = proto;
  __asm __volatile ("jsr a6@(-0xfc)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct servent *
getservbyname (BASE_PAR_DECL const char *name,const char * proto)
{
  BASE_EXT_DECL
  register struct servent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register const char *a0 __asm("a0") = name;
  register const char *a1 __asm("a1") =  proto;
  __asm __volatile ("jsr a6@(-0xea)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0), "r" (a1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline struct servent *
getservbyport (BASE_PAR_DECL LONG port,const char * proto)
{
  BASE_EXT_DECL
  register struct servent * _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = port;
  register const char *a0 __asm("a0") =  proto;
  __asm __volatile ("jsr a6@(-0xf0)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
getsockname (BASE_PAR_DECL LONG s,struct sockaddr * hostname,LONG * namelen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register struct sockaddr *a0 __asm("a0") =  hostname;
  register LONG *a1 __asm("a1") =  namelen;
  __asm __volatile ("jsr a6@(-0x66)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
getsockopt (BASE_PAR_DECL LONG s,LONG  level,LONG  optname,char * optval,LONG * optlen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  level;
  register LONG d2 __asm("d2") =  optname;
  register char *a0 __asm("a0") =  optval;
  register LONG *a1 __asm("a1") =  optlen;
  __asm __volatile ("jsr a6@(-0x60)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2), "r" (a0), "r" (a1)
  : "a0","a1","d1","d2", "memory");
  return _api_d0;
}
static __inline ULONG 
inet_addr (BASE_PAR_DECL const char * cp)
{
  BASE_EXT_DECL
  register ULONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register const char * a0 __asm("a0") = cp;
  __asm __volatile ("jsr a6@(-0xb4)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline ULONG 
inet_network (BASE_PAR_DECL const char * cp)
{
  BASE_EXT_DECL
  register ULONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register const char * a0 __asm("a0") = cp;
  __asm __volatile ("jsr a6@(-0xcc)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
ioctl (BASE_PAR_DECL LONG d,ULONG  request,char * argp)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = d;
  register ULONG d1 __asm("d1") =  request;
  register char *a0 __asm("a0") =  argp;
  __asm __volatile ("jsr a6@(-0x72)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1), "r" (a0)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
listen (BASE_PAR_DECL LONG s,LONG  backlog)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  backlog;
  __asm __volatile ("jsr a6@(-0x2a)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
recv (BASE_PAR_DECL LONG s,char * buf,LONG  len,LONG  flags)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register char *a0 __asm("a0") =  buf;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  __asm __volatile ("jsr a6@(-0x4e)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2)
  : "a0","a1","d1","d2", "memory");
  return _api_d0;
}
static __inline LONG 
recvfrom (BASE_PAR_DECL LONG s,char * buf,LONG  len,LONG  flags,struct sockaddr * from,LONG * fromlen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register char *a0 __asm("a0") =  buf;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  register struct sockaddr *a1 __asm("a1") =  from;
  register LONG *a2 __asm("a2") =  fromlen;
  __asm __volatile ("jsr a6@(-0x48)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2), "r" (a1), "r" (a2)
  : "a0","a1","a2","d1","d2", "memory");
  return _api_d0;
}
static __inline LONG 
send (BASE_PAR_DECL LONG s,char * msg,LONG  len,LONG  flags)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register char *a0 __asm("a0") =  msg;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  __asm __volatile ("jsr a6@(-0x42)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2)
  : "a0","a1","d1","d2", "memory");
  return _api_d0;
}
static __inline LONG 
sendto (BASE_PAR_DECL LONG s,char * msg,LONG  len,LONG  flags,struct sockaddr * to,LONG  tolen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register char *a0 __asm("a0") =  msg;
  register LONG d1 __asm("d1") =  len;
  register LONG d2 __asm("d2") =  flags;
  register struct sockaddr *a1 __asm("a1") =  to;
  register LONG d3 __asm("d3") =  tolen;
  __asm __volatile ("jsr a6@(-0x3c)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (a0), "r" (d1), "r" (d2), "r" (a1), "r" (d3)
  : "a0","a1","d1","d2","d3", "memory");
  return _api_d0;
}
static __inline LONG 
setsockopt (BASE_PAR_DECL LONG s,LONG  level,LONG  optname,char * optval,LONG  optlen)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  level;
  register LONG d2 __asm("d2") =  optname;
  register char *a0 __asm("a0") =  optval;
  register LONG d3 __asm("d3") =  optlen;
  __asm __volatile ("jsr a6@(-0x5a)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2), "r" (a0), "r" (d3)
  : "a0","a1","d1","d2","d3", "memory");
  return _api_d0;
}
static __inline LONG 
shutdown (BASE_PAR_DECL LONG s,LONG  how)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = s;
  register LONG d1 __asm("d1") =  how;
  __asm __volatile ("jsr a6@(-0x54)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d1", "memory");
  return _api_d0;
}
static __inline LONG 
socket (BASE_PAR_DECL LONG domain,LONG  type,LONG  protocol)
{
  BASE_EXT_DECL
  register LONG  _api_d0  __asm("d0");
  register struct SocketBase* a6 __asm("a6") = BASE_NAME;
  register LONG d0 __asm("d0") = domain;
  register LONG d1 __asm("d1") =  type;
  register LONG d2 __asm("d2") =  protocol;
  __asm __volatile ("jsr a6@(-0x1e)"
  : "=r" (_api_d0)
  : "r" (a6), "r" (d0), "r" (d1), "r" (d2)
  : "a0","a1","d1","d2", "memory");
  return _api_d0;
}
#undef BASE_EXT_DECL
#undef BASE_PAR_DECL
#undef BASE_PAR_DECL0
#undef BASE_NAME

#endif /* API_APICALLS_GNUC_H */
