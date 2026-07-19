#ifndef API_APICALLS_SASC_H
#define API_APICALLS_SASC_H

#ifndef API_APICALLS_H
#error include <api/apicalls.h> instead of <api/apicalls_sasc.h>
#endif

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif

#ifndef SYS_TIME_H
#include <sys/time.h>
#endif

#include <stdarg.h>

#if 0
#ifndef SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifndef IN_H
#include <netinet/in.h>
#endif
#endif



extern REGARGFUN LONG _CloseSocket(register __a6 struct SocketBase *,
				   register __d0 LONG);

extern REGARGFUN LONG _connect(register __a6 struct SocketBase *,
			       register __d0 LONG,
			       register __a0 struct sockaddr *,
			       register __d1 LONG);

extern REGARGFUN LONG _recv(register __a6 struct SocketBase *,
			    register __d0 LONG,
			    register __a0 char *,
			    register __d1 LONG,
			    register __d2 LONG);

extern REGARGFUN LONG _send(register __a6 struct SocketBase *,
			    register __d0 LONG,
			    register __a0 char *,
			    register __d1 LONG,
			    register __d2 LONG);

extern REGARGFUN LONG _sendto(register __a6 struct SocketBase *,
			      register __d0 LONG,
			      register __a0 char *,
			      register __d1 LONG,
			      register __d2 LONG,
			      register __a1 struct sockaddr *,
			      register __d3 LONG);

extern REGARGFUN LONG _socket(register __a6 struct SocketBase *,
			      register __d0 LONG,
			      register __d1 LONG,
			      register __d2 LONG);


extern REGARGFUN LONG _WaitSelect(register __a6 struct SocketBase *,
				  register __d0 ULONG,
				  register __a0 fd_set *,
				  register __a1 fd_set *,
				  register __a2 fd_set *,
				  register __a3	struct timeval *,
				  register __d1 ULONG *);


extern REGARGFUN 
  struct hostent * _gethostbyaddr(register __a6 struct SocketBase *,
				  register __a0 const UBYTE *,
				  register __d0 int,
				  register __d1 int);

extern REGARGFUN LONG _gethostname(register __a6 struct SocketBase *,
				   register __a0 STRPTR,
				   register __d0 LONG);

extern REGARGFUN LONG _SetErrnoPtr(register __a6 struct SocketBase *,
				   register __a0 VOID *,
				   register __d0 UBYTE);

#define BASE_EXT_DECL
#define BASE_PAR_DECL  struct SocketBase * SocketBase,
#define BASE_PAR_DECL0 struct SocketBase * SocketBase
#define BASE_NAME SocketBase  

static __inline LONG 
CloseSocket (BASE_PAR_DECL LONG d)
{
  return _CloseSocket(SocketBase, d);
}

static __inline LONG 
connect (BASE_PAR_DECL LONG s,struct sockaddr * name,LONG  namelen)
{
  return _connect(SocketBase, s, name, namelen);
}

static __inline LONG 
recv (BASE_PAR_DECL LONG s,char * buf,LONG  len,LONG  flags)
{
  return _recv(SocketBase, s, buf, len, flags);
}

static __inline LONG 
send (BASE_PAR_DECL LONG s,char * msg,LONG  len,LONG  flags)
{
  return _send(SocketBase, s, msg, len, flags);
}

static __inline LONG 
sendto (BASE_PAR_DECL LONG s,char * msg,LONG  len,LONG  flags,struct sockaddr * to,LONG  tolen)
{
  return _sendto(SocketBase, s, msg, len, flags, to, tolen);
}

static __inline LONG 
socket (BASE_PAR_DECL LONG domain,LONG  type,LONG  protocol)
{
  return _socket(SocketBase, domain, type, protocol);
}

static __inline LONG 
WaitSelect (BASE_PAR_DECL LONG nfds,fd_set * readfds,fd_set * writefds,fd_set * execptfds,struct timeval * timeout,ULONG * maskp)
{
  return _WaitSelect(SocketBase, nfds, readfds, writefds, execptfds, timeout, maskp);
}

static struct hostent * __inline 
gethostbyaddr (BASE_PAR_DECL const UBYTE * addr, int len, int type)
{
  return _gethostbyaddr(BASE_NAME, addr, len, type);
}

static LONG __inline
gethostname(BASE_PAR_DECL STRPTR name, LONG namelen)
{
  return _gethostname(BASE_NAME, name, namelen);
}

static LONG __inline
SetErrnoPtr(BASE_PAR_DECL VOID * err_p, UBYTE size)
{
  return _SetErrnoPtr(BASE_NAME, err_p, size);
}

#undef BASE_EXT_DECL
#undef BASE_PAR_DECL
#undef BASE_PAR_DECL0
#undef BASE_NAME

#endif /* API_APICALLS_SASC_H */
