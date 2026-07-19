#ifndef CLIB_SOCKET_PROTOS_H
#define CLIB_SOCKET_PROTOS_H

/*
 * $Id: socket_protos.h,v 3.5 1994/01/20 02:40:08 jraja Exp $
 * $Release$
 *
 * Prototypes of AmiTCP/IP bsdsocket.library
 * 
 * Copyright © 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                  Helsinki University of Technology, Finland.
 *                  All rights reserved.
 */

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif

#ifndef SYS_TIME_H
#include <sys/time.h>
#endif

#ifndef SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifndef IN_H
#include <netinet/in.h>
#endif

#ifndef AMITCP_NEW_NAMES
#define STDSOCKAPI
#define Socket socket
#define Bind bind
#define Listen listen
#define Accept accept
#define Connect connect
#define Send send
#define SendTo sendto
#define SendMsg sendmsg
#define Recv recv
#define RecvFrom recvfrom
#define RecvMsg recvmsg
#define Shutdown shutdown
#define SetSockOpt setsockopt
#define GetSockOpt getsockopt
#define GetSockName getsockname
#define GetPeerName getpeername
#define Inet_Addr inet_addr
#define Inet_Network inet_network
#define GetHostByName gethostbyname
#define GetHostByAddr gethostbyaddr
#define GetNetByName getnetbyname
#define GetNetByAddr getnetbyaddr
#define GetServByName getservbyname
#define GetServByPort getservbyport
#define GetProtoByName getprotobyname
#define GetProtoByNumber getprotobynumber
#define GetDTableSize getdtablesize
#define GetHostName gethostname
#define GetHostId gethostid
#define SyslogA vsyslog
#define Syslog syslog
#endif

LONG Socket(LONG domain, LONG type, LONG protocol);
LONG Bind(LONG s, const struct sockaddr *name, LONG namelen);
LONG Listen(LONG s, LONG backlog);
LONG Accept(LONG s, struct sockaddr *addr, LONG *addrlen);
LONG Connect(LONG s, const struct sockaddr *name, LONG namelen);
LONG Send(LONG s, const UBYTE *msg, LONG len, LONG flags);
LONG SendTo(LONG s, const UBYTE *msg, LONG len, LONG flags, 
		  const struct sockaddr *to, LONG tolen);
LONG SendMsg(LONG s, struct msghdr * msg, LONG flags);
LONG Recv(LONG s, UBYTE *buf, LONG len, LONG flags);	/* V3 */
LONG RecvFrom(LONG s, UBYTE *buf, LONG len, LONG flags, 
		    struct sockaddr *from, LONG *fromlen);
LONG RecvMsg(LONG s, struct msghdr * msg, LONG flags);	/* V3 */
LONG Shutdown(LONG s, LONG how);
LONG SetSockOpt(LONG s, LONG level, LONG optname, 
		     const void *optval, LONG optlen);
LONG GetSockOpt(LONG s, LONG level, LONG optname, 
		     void *optval, LONG *optlen);
LONG GetSockName(LONG s, struct sockaddr *name, LONG *namelen);
LONG GetPeerName(LONG s, struct sockaddr *name, LONG *namelen);

LONG IoctlSocket(LONG d, ULONG request, char *argp);
LONG CloseSocket(LONG d);
LONG WaitSelect(LONG nfds, fd_set *readfds, fd_set *writefds, fd_set *exeptfds,
		struct timeval *timeout, ULONG *maskp);

LONG Dup2Socket(LONG fd1, LONG fd2);		       /* V2 */

LONG GetDTableSize(void);			       /* V3 */
void SetSocketSignals(ULONG SIGINTR, ULONG SIGIO, ULONG SIGURG);
LONG SetErrnoPtr(void *errno_p, LONG size);
LONG SocketBaseTagList(struct TagItem *tagList);	/* V3 */
LONG SocketBaseTags(...);				/* V3 */

LONG Errno(void);

LONG GetHostName(STRPTR hostname, LONG size);          /* V3 */
ULONG GetHostId(void);				       /* V3 */

LONG ObtainSocket(LONG id, LONG domain, LONG type, LONG protocol);
LONG ReleaseSocket(LONG fd, LONG id);
LONG ReleaseCopyOfSocket(LONG fd, LONG id);

/* Arpa/inet functions */
ULONG Inet_Addr(const UBYTE *);
ULONG Inet_Network(const UBYTE *);
char *Inet_NtoA(ULONG s_addr);
ULONG Inet_MakeAddr(ULONG net, ULONG lna);
ULONG Inet_LnaOf(LONG s_addr);
ULONG Inet_NetOf(LONG s_addr);

/* NetDB functions */
struct hostent  *GetHostByName(const UBYTE *name);
struct hostent  *GetHostByAddr(const UBYTE *addr, LONG len, LONG type);
struct netent   *GetNetByName(const UBYTE *name);
struct netent   *GetNetByAddr(LONG net, LONG type);
struct servent  *GetServByName(const UBYTE *name, const UBYTE *proto);
struct servent  *GetServByPort(LONG port, const UBYTE *proto);
struct protoent *GetProtoByName(const UBYTE *name);
struct protoent *GetProtoByNumber(LONG proto);

/* Syslog functions */
void SyslogA(ULONG pri, const char *fmt, LONG *);
void Syslog(ULONG pri, const char *fmt, ...);

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
#undef Syslog
#endif

#endif /* !CLIB_SOCKET_PROTOS_H */
