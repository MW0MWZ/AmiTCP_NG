RCS_ID_C="$Id: amiga_libtables.c,v 3.3 1994/01/11 19:36:40 too Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * Created: Tue Feb 16 14:14:33 1993 too
 * Last modified: Tue Jan 11 21:35:30 1994 too
 * 
 * HISTORY 
 * $Log: amiga_libtables.c,v $
 * Revision 3.3  1994/01/11  19:36:40  too
 * Replaced SetDtableSize with getdtablesize.
 * Removed some functions now in SocketBaseTagList
 *
 * Revision 3.2  1994/01/08  17:40:09  too
 * Added sendmsg and recvmsg
 *
 * Revision 3.1  1994/01/04  14:26:29  too
 * Added new, release 3 functions (getdtablesize, gethostname,
 * gethostid, GetHErrno, SetNetError and SocketBaseTagList)
 *
 * Revision 1.20  1993/08/12  07:32:27  jraja
 * Changed ioctl to IoctlSocket (too).
 *
 * Revision 1.19  1993/06/12  08:57:05  too
 * Added Du2Socket()
 *
 * Revision 1.18  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 */

/*
 * amiga_libtables.c --- LibVectors[]: the library's function vector table (the ABI).
 *
 * This is the single most ABI-critical file in the stack. LibVectors[] is the
 * ordered array of function pointers that MakeLibrary() (api/amiga_api.c) turns
 * into the jump table applications reach via `jsr a6@(-LVO)`. The ORDER is the
 * binary interface: socket first (LVO -30), then bind (-36), listen (-42), ... in
 * exactly the sequence given by the bsdsocket SFD (roadshow-ref/bsdsocket_lib.sfd).
 * Reorder an entry and every existing network binary calls the wrong function.
 * Adding a NEW function means appending it at the end (and taking a reserved slot),
 * never inserting. Read this beside the SFD and docs/ARCHITECTURE.md section 5.
 * The table ends with (f_void)-1, the sentinel MakeLibrary expects.
 */

#include <conf.h>

#include <exec/types.h>
#include <sys/param.h>
#include <api/amiga_raf.h>

typedef VOID (* REGARGFUN f_void)();

/*
 * Null used in both function tables
 */
extern VOID Null(VOID);

/*
 * "declarations" for ExecLibraryList_funcTable functions.
 */ 

extern REGARGFUN VOID ELL_Open();
extern REGARGFUN VOID ELL_Expunge();

f_void ExecLibraryList_funcTable[] = {
  ELL_Open,
  Null,		/* ELL_Close() is never called */
  ELL_Expunge,
  Null,		/* ELL_Reserved() */
  (f_void)-1
};

/*
 * "declarations" for userLibrary_funcTable functions.
 */ 
extern REGARGFUN VOID UL_Close();

extern REGARGFUN VOID _socket();
extern REGARGFUN VOID _bind();
extern REGARGFUN VOID _listen();
extern REGARGFUN VOID _accept();
extern REGARGFUN VOID _connect();
extern REGARGFUN VOID _sendto();
extern REGARGFUN VOID _send();
extern REGARGFUN VOID _recvfrom();
extern REGARGFUN VOID _recv();
extern REGARGFUN VOID _shutdown();
extern REGARGFUN VOID _setsockopt();
extern REGARGFUN VOID _getsockopt();
extern REGARGFUN VOID _getsockname();
extern REGARGFUN VOID _getpeername();

extern REGARGFUN VOID _IoctlSocket();
extern REGARGFUN VOID _CloseSocket();
extern REGARGFUN VOID _WaitSelect();
extern REGARGFUN VOID _SetSocketSignals();
extern REGARGFUN VOID _getdtablesize();  /* from V3 on */
/*extern REGARGFUN VOID _SetDTableSize(); */
extern REGARGFUN VOID _ObtainSocket();
extern REGARGFUN VOID _ReleaseSocket();
extern REGARGFUN VOID _ReleaseCopyOfSocket();
extern REGARGFUN VOID _Errno();
extern REGARGFUN VOID _SetErrnoPtr();

extern REGARGFUN VOID _Inet_NtoA();
extern REGARGFUN VOID _inet_addr();
extern REGARGFUN VOID _Inet_LnaOf();
extern REGARGFUN VOID _Inet_NetOf();
extern REGARGFUN VOID _Inet_MakeAddr();
extern REGARGFUN VOID _inet_network();

extern REGARGFUN VOID _gethostbyname();
extern REGARGFUN VOID _gethostbyaddr();
extern REGARGFUN VOID _getnetbyname();
extern REGARGFUN VOID _getnetbyaddr();
extern REGARGFUN VOID _getservbyname();
extern REGARGFUN VOID _getservbyport();
extern REGARGFUN VOID _getprotobyname();
extern REGARGFUN VOID _getprotobynumber();
extern REGARGFUN VOID _setnetent();
extern REGARGFUN VOID _endnetent();
extern REGARGFUN VOID _getnetent();
extern REGARGFUN VOID _setprotoent();
extern REGARGFUN VOID _endprotoent();
extern REGARGFUN VOID _getprotoent();
extern REGARGFUN VOID _setservent();
extern REGARGFUN VOID _endservent();
extern REGARGFUN VOID _getservent();

extern REGARGFUN VOID _Syslog();

/* Berkeley Packet Filter vectors (net/bpf.c via api/amiga_bpf.c) */
extern REGARGFUN VOID _bpf_open();
extern REGARGFUN VOID _bpf_close();
extern REGARGFUN VOID _bpf_read();
extern REGARGFUN VOID _bpf_write();
extern REGARGFUN VOID _bpf_set_notify_mask();
extern REGARGFUN VOID _bpf_set_interrupt_mask();
extern REGARGFUN VOID _bpf_ioctl();
extern REGARGFUN VOID _bpf_data_waiting();

/* bsdsocket.library 2 extensions */
extern REGARGFUN VOID _Dup2Socket();

/* bsdsocket.library 3 extensions */
extern REGARGFUN VOID _sendmsg();
extern REGARGFUN VOID _recvmsg();
extern REGARGFUN VOID _gethostname();
extern REGARGFUN VOID _gethostid();
extern REGARGFUN VOID _SocketBaseTagList();

/*
 * Roadshow (bsdsocket.library 3+) extensions --- entry points in
 * api/amiga_roadshow.c. Every vector below occupies its exact SFD offset;
 * unimplemented ones share a clean-fail stub (by return type). As tranches land,
 * a stub in the table is swapped for its real function name -- the OFFSET never
 * moves. See api/amiga_roadshow.c and ref/NDK3.2/.../bsdsocket_lib.sfd.
 */
extern REGARGFUN VOID _GetSocketEvents();
extern REGARGFUN VOID _RoadshowStubErr();	/* -1 + errno ENOSYS (int/void) */
extern REGARGFUN VOID _RoadshowStubNull();	/* NULL + errno ENOSYS (pointer) */
extern REGARGFUN VOID _inet_aton();
extern REGARGFUN VOID _inet_ntop();
extern REGARGFUN VOID _inet_pton();
extern REGARGFUN VOID _In_LocalAddr();
extern REGARGFUN VOID _In_CanForward();
extern REGARGFUN VOID _AddDomainNameServer();
extern REGARGFUN VOID _RemoveDomainNameServer();
extern REGARGFUN VOID _ConfigureInterfaceTagList();
extern REGARGFUN VOID _AddInterfaceTagList();
extern REGARGFUN VOID _ObtainInterfaceList();
extern REGARGFUN VOID _ReleaseInterfaceList();
extern REGARGFUN VOID _QueryInterfaceTagList();
extern REGARGFUN VOID _ObtainDomainNameServerList();
extern REGARGFUN VOID _ReleaseDomainNameServerList();
extern REGARGFUN VOID _freeaddrinfo();
extern REGARGFUN VOID _getaddrinfo();
extern REGARGFUN VOID _gai_strerror();
extern REGARGFUN VOID _getnameinfo();
extern REGARGFUN VOID _gethostbyname_r();
extern REGARGFUN VOID _gethostbyaddr_r();
extern REGARGFUN VOID _AddRouteTagList();
extern REGARGFUN VOID _DeleteRouteTagList();
extern REGARGFUN VOID _GetRouteInfo();
extern REGARGFUN VOID _FreeRouteInfo();
extern REGARGFUN VOID _GetDefaultDomainName();
extern REGARGFUN VOID _SetDefaultDomainName();
extern REGARGFUN VOID _GetNetworkStatistics();
extern REGARGFUN VOID _RemoveInterface();
extern REGARGFUN VOID _CreateAddrAllocMessageA();
extern REGARGFUN VOID _DeleteAddrAllocMessage();
extern REGARGFUN VOID _BeginInterfaceConfig();
extern REGARGFUN VOID _AbortInterfaceConfig();
extern REGARGFUN VOID _ObtainRoadshowData();
extern REGARGFUN VOID _ReleaseRoadshowData();
extern REGARGFUN VOID _ChangeRoadshowData();
extern REGARGFUN VOID _mbuf_get();
extern REGARGFUN VOID _mbuf_gethdr();
extern REGARGFUN VOID _mbuf_free();
extern REGARGFUN VOID _mbuf_freem();
extern REGARGFUN VOID _mbuf_prepend();
extern REGARGFUN VOID _mbuf_pullup();
extern REGARGFUN VOID _mbuf_copym();
extern REGARGFUN VOID _mbuf_copyback();
extern REGARGFUN VOID _mbuf_copydata();
extern REGARGFUN VOID _mbuf_cat();
extern REGARGFUN VOID _mbuf_adj();

f_void UserLibrary_funcTable[] = {
  (f_void)Null,		/* Open() */
  UL_Close,
  (f_void)Null,		/* Expunge() */
  (f_void)Null,		/* Reserved() */

  _socket,
  _bind,
  _listen,
  _accept,
  _connect,
  _sendto,
  _send,
  _recvfrom,
  _recv,
  _shutdown,
  _setsockopt,
  _getsockopt,
  _getsockname,
  _getpeername,

  _IoctlSocket,
  _CloseSocket,
  _WaitSelect,
  _SetSocketSignals,
  _getdtablesize,	/* from V3 on */
/*  _SetDTableSize, */
  _ObtainSocket,
  _ReleaseSocket,
  _ReleaseCopyOfSocket,
  _Errno,
  _SetErrnoPtr,

  _Inet_NtoA,
  _inet_addr,
  _Inet_LnaOf,
  _Inet_NetOf,
  _Inet_MakeAddr,
  _inet_network,

  _gethostbyname,
  _gethostbyaddr,
  _getnetbyname,
  _getnetbyaddr,
  _getservbyname,
  _getservbyport,
  _getprotobyname,
  _getprotobynumber,
  _Syslog,
  
  /* bsdsocket.library 2 extensions */
  _Dup2Socket,

  /* bsdsocket.library 3 extensions */
  _sendmsg,
  _recvmsg,
  _gethostname,
  _gethostid,
  _SocketBaseTagList,

  /* ---------------------------------------------------------------------- *
   * Roadshow bsdsocket.library EXTENSIONS. ORDER IS THE ABI -- it follows the
   * Roadshow SFD exactly; each slot must keep its offset forever. Implemented
   * vectors name their real function; not-yet-done vectors point at the shared
   * clean-fail stub matching their return type (_RoadshowStubErr for int/void/
   * BOOL, _RoadshowStubNull for pointer). Offsets noted for cross-checking
   * against the SFD. See api/amiga_roadshow.c.
   * ---------------------------------------------------------------------- */
  _GetSocketEvents,			/* -300 */

  /* ==reserve 10 (SFD): future expansion -- hold the offsets */
  _RoadshowStubErr, _RoadshowStubErr, _RoadshowStubErr, _RoadshowStubErr,
  _RoadshowStubErr, _RoadshowStubErr, _RoadshowStubErr, _RoadshowStubErr,
  _RoadshowStubErr, _RoadshowStubErr,	/* -306 .. -360 */

  /* Berkeley Packet Filter (net/bpf.c via api/amiga_bpf.c) */
  _bpf_open,				/* bpf_open               -366 */
  _bpf_close,				/* bpf_close              -372 */
  _bpf_read,				/* bpf_read               -378 */
  _bpf_write,				/* bpf_write              -384 */
  _bpf_set_notify_mask,			/* bpf_set_notify_mask    -390 */
  _bpf_set_interrupt_mask,		/* bpf_set_interrupt_mask -396 */
  _bpf_ioctl,				/* bpf_ioctl              -402 */
  _bpf_data_waiting,			/* bpf_data_waiting       -408 */

  /* Route management (Add/Delete IMPLEMENTED; GetRouteInfo pair pending) */
  _AddRouteTagList,			/* -414 */
  _DeleteRouteTagList,			/* -420 */
  _RoadshowStubErr,			/* ChangeRouteTagList     -426 (private, unimpl in Roadshow too) */
  _FreeRouteInfo,			/* -432 */
  _GetRouteInfo,			/* -438 */

  /* Interface management (deferred to config-mgmt tranche) */
  _AddInterfaceTagList,			/* -444 */
  _ConfigureInterfaceTagList,		/* -450 */
  _ReleaseInterfaceList,		/* -456 */
  _ObtainInterfaceList,			/* -462 */
  _QueryInterfaceTagList,		/* -468 */
  _CreateAddrAllocMessageA,		/* CreateAddrAllocMessageA    -474 */
  _DeleteAddrAllocMessage,		/* DeleteAddrAllocMessage     -480 (void) */
  _BeginInterfaceConfig,		/* BeginInterfaceConfig       -486 (void) */
  _AbortInterfaceConfig,		/* AbortInterfaceConfig       -492 (void) */

  /* Monitor management (deferred) */
  _RoadshowStubErr,			/* AddNetMonitorHookTagList   -498 */
  _RoadshowStubErr,			/* RemoveNetMonitorHook       -504 (void) */

  /* Status query (deferred to config-mgmt tranche) */
  _GetNetworkStatistics,		/* -510 */

  /* Domain name server management (Add/Remove IMPLEMENTED; list pair pending
   * the Roadshow node layout) */
  _AddDomainNameServer,			/* -516 */
  _RemoveDomainNameServer,		/* -522 */
  _ReleaseDomainNameServerList,		/* -528 */
  _ObtainDomainNameServerList,		/* -534 */

  /* Local database iterators (deferred) */
  _setnetent,				/* setnetent    -540 (void) */
  _endnetent,				/* endnetent    -546 (void) */
  _getnetent,				/* getnetent    -552 (ptr) */
  _setprotoent,				/* setprotoent  -558 (void) */
  _endprotoent,				/* endprotoent  -564 (void) */
  _getprotoent,				/* getprotoent  -570 (ptr) */
  _setservent,				/* setservent   -576 (void) */
  _endservent,				/* endservent   -582 (void) */
  _getservent,				/* getservent   -588 (ptr) */

  /* Address conversion (IMPLEMENTED) */
  _inet_aton,				/* -594 */
  _inet_ntop,				/* -600 */
  _inet_pton,				/* -606 */
  _In_LocalAddr,			/* -612 */
  _In_CanForward,			/* -618 */

  /* Kernel mbuf access -- thin forwards to the BSD m_* routines */
  _mbuf_copym,				/* mbuf_copym     -624 (ptr) */
  _mbuf_copyback,			/* mbuf_copyback  -630 */
  _mbuf_copydata,			/* mbuf_copydata  -636 */
  _mbuf_free,				/* mbuf_free      -642 (ptr) */
  _mbuf_freem,				/* mbuf_freem     -648 (void) */
  _mbuf_get,				/* mbuf_get       -654 (ptr) */
  _mbuf_gethdr,				/* mbuf_gethdr    -660 (ptr) */
  _mbuf_prepend,			/* mbuf_prepend   -666 (ptr) */
  _mbuf_cat,				/* mbuf_cat       -672 */
  _mbuf_adj,				/* mbuf_adj       -678 */
  _mbuf_pullup,				/* mbuf_pullup    -684 (ptr) */

  /* Internet servers (deferred) */
  _RoadshowStubErr,			/* ProcessIsServer    -690 (BOOL) */
  _RoadshowStubErr,			/* ObtainServerSocket -696 */

  /* Default domain name (IMPLEMENTED) */
  _GetDefaultDomainName,		/* -702 */
  _SetDefaultDomainName,		/* -708 */

  /* Global data access (deferred to config-mgmt tranche) */
  _ObtainRoadshowData,			/* ObtainRoadshowData  -714 (ptr) */
  _ReleaseRoadshowData,			/* ReleaseRoadshowData -720 (void) */
  _ChangeRoadshowData,			/* ChangeRoadshowData  -726 (BOOL) */

  /* Counterpart to AddInterfaceTagList (deferred) */
  _RemoveInterface,			/* -732 */

  /* Reentrant gethostby* (IMPLEMENTED) */
  _gethostbyname_r,			/* -738 */
  _gethostbyaddr_r,			/* -744 */

  /* ==reserve 2 (SFD) */
  _RoadshowStubErr, _RoadshowStubErr,	/* -750 .. -756 */

  /* IP filter (private, deferred) */
  _RoadshowStubErr,			/* ipf_open               -762 */
  _RoadshowStubErr,			/* ipf_close              -768 */
  _RoadshowStubErr,			/* ipf_ioctl              -774 */
  _RoadshowStubErr,			/* ipf_log_read           -780 */
  _RoadshowStubErr,			/* ipf_log_data_waiting   -786 */
  _RoadshowStubErr,			/* ipf_set_notify_mask    -792 */
  _RoadshowStubErr,			/* ipf_set_interrupt_mask -798 */

  /* Node/service name translation, rfc3493 (getnameinfo still pending) */
  _freeaddrinfo,			/* -804 */
  _getaddrinfo,				/* -810 */
  _gai_strerror,			/* -816 */
  _getnameinfo,				/* -822 */

  /* ==reserve 6 (SFD) */
  _RoadshowStubErr, _RoadshowStubErr, _RoadshowStubErr,
  _RoadshowStubErr, _RoadshowStubErr, _RoadshowStubErr,	/* -828 .. -858 */

  (f_void)-1
};
