/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * Shared inline bsdsocket.library vector wrappers + Roadshow tag constants for the
 * AmiTCP_NG command-line tools (our own name/behaviour-compatible versions of the
 * Roadshow commands). Each tool #defines nothing special and just includes this after
 * declaring `struct Library *SocketBase;`. Register-argument LVO calls (bias 30, a6 =
 * SocketBase); scratch address/data registers a caller might reuse are marked "+r".
 */
#ifndef NG_LVO_H
#define NG_LVO_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <utility/tagitem.h>

extern struct Library *SocketBase;

/* ---- Roadshow extension-API tag values ------------------------------------- */
#define NG_TU			0x80000000UL		/* TAG_USER */
#define RTA_BASE		(NG_TU + 1600)
#define RTA_Destination		(RTA_BASE + 1)
#define RTA_Gateway		(RTA_BASE + 2)
#define RTA_DefaultGateway	(RTA_BASE + 3)
#define RTA_DestinationHost	(RTA_BASE + 4)
#define RTA_DestinationNet	(RTA_BASE + 5)

#define IFC_BASE		(NG_TU + 1800)
#define IFC_Address		(IFC_BASE + 1)
#define IFC_NetMask		(IFC_BASE + 2)
#define IFC_DestinationAddress	(IFC_BASE + 3)
#define IFC_BroadcastAddress	(IFC_BASE + 4)
#define IFC_Metric		(IFC_BASE + 5)
#define IFC_MTU			(IFC_BASE + 6)
#define IFC_State		(IFC_BASE + 8)
#define NG_SM_Offline		0
#define NG_SM_Online		1
#define NG_SM_Down		2
#define NG_SM_Up		3

/* Interface-query (QueryInterfaceTagList) tags. IMPORTANT BUFFER CONTRACT: the address
 * tags below (IFQ_Address / IFQ_NetMask / IFQ_DestinationAddress / IFQ_BroadcastAddress)
 * make the library bcopy a WHOLE `struct sockaddr_in` (16 bytes) into the ti_Data buffer
 * you supply -- so that buffer MUST be at least 16 bytes. Passing a smaller struct
 * overruns onto whatever follows it in memory (there is no MMU). Use a 16-byte
 * sockaddr_in mirror and, ideally, a compile-time size assert (see ShowNetStatus.c). */
#define IFQ_BASE		(NG_TU + 1900)
#define IFQ_DeviceName		(IFQ_BASE + 1)
#define IFQ_MTU			(IFQ_BASE + 5)
#define IFQ_BPS			(IFQ_BASE + 6)
#define IFQ_HardwareType	(IFQ_BASE + 7)
#define IFQ_PacketsReceived	(IFQ_BASE + 8)
#define IFQ_PacketsSent		(IFQ_BASE + 9)
#define IFQ_Address		(IFQ_BASE + 14)
#define IFQ_NetMask		(IFQ_BASE + 17)
#define IFQ_State		(IFQ_BASE + 19)

/* ---- vector wrappers ------------------------------------------------------- */
static long __attribute__((unused)) ng_errno(void) {						/* Errno -162 */
  register long _d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static long __attribute__((unused)) ng_addroute(void *tags) {					/* AddRouteTagList -414 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=tags;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-414)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}
static long __attribute__((unused)) ng_delroute(void *tags) {					/* DeleteRouteTagList -420 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=tags;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-420)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}
static long __attribute__((unused)) ng_removeif(void *name, long force) {			/* RemoveInterface -732 (a0,d0) */
  register long _d0 __asm("d0")=force; register void *_a0 __asm("a0")=name;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-732)":"+r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}
static long __attribute__((unused)) ng_configif(void *name, void *tags) {			/* ConfigureInterfaceTagList -450 (a0,a1) */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=name;
  register void *_a1 __asm("a1")=tags; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-450)":"=r"(_d0),"+r"(_a0),"+r"(_a1):"r"(_a6):"d1","memory"); return _d0;
}
static __attribute__((unused)) struct List *ng_obtainiflist(void) {				/* ObtainInterfaceList -462 */
  register struct List *_d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-462)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static void __attribute__((unused)) ng_releaseiflist(struct List *l) {				/* ReleaseInterfaceList -456 */
  register struct List *_a0 __asm("a0")=l; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-456)":"+r"(_a0):"r"(_a6):"d0","d1","a1","memory");
}
static long __attribute__((unused)) ng_queryif(void *name, void *tags) {			/* QueryInterfaceTagList -468 (a0,a1) */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=name;
  register void *_a1 __asm("a1")=tags; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-468)":"=r"(_d0),"+r"(_a0),"+r"(_a1):"r"(_a6):"d1","memory"); return _d0;
}
static long __attribute__((unused)) ng_addif(void *nm, void *dev, long unit, void *tags) {	/* AddInterfaceTagList -444 */
  register long _d0 __asm("d0")=unit; register void *_a0 __asm("a0")=nm;
  register void *_a1 __asm("a1")=dev; register void *_a2 __asm("a2")=tags;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-444)":"+r"(_d0),"+r"(_a0),"+r"(_a1),"+r"(_a2):"r"(_a6):"d1","memory"); return _d0;
}

/* ---- DHCP address-allocation (CONFIGURE=dhcp) ------------------------------ */
#define CAAMTA_BASE		(NG_TU + 2000)
#define CAAMTA_RouterTableSize	(CAAMTA_BASE + 6)
#define CAAMTA_DNSTableSize	(CAAMTA_BASE + 7)
#define CAAMTA_ReplyPort	(CAAMTA_BASE + 13)
#define NG_AAMP_DHCP		1
#define NG_AAM_VERSION		2
struct ng_aamx {			/* struct Message (20) prefix, then the fields we use */
  char mn[20];
  long aam_Reserved, aam_Result, aam_Version, aam_Protocol;
  char aam_InterfaceName[16];
  long aam_Timeout;
  unsigned long aam_LeaseTime, aam_RequestedAddress;
  char *aam_ClientIdentifier;
  unsigned long aam_Address, aam_ServerAddress, aam_SubnetMask;
};
static long __attribute__((unused)) ng_createaam(long ver, long proto, const char *ifn, void **res, void *tags) { /* -474 */
  register long _d0 __asm("d0")=ver; register long _d1 __asm("d1")=proto;
  register const char *_a0 __asm("a0")=ifn; register void **_a1 __asm("a1")=res;
  register void *_a2 __asm("a2")=tags; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-474)":"+r"(_d0),"+r"(_d1),"+r"(_a0),"+r"(_a1):"r"(_a2),"r"(_a6):"memory"); return _d0;
}
static void __attribute__((unused)) ng_begincfg(void *aam) {					/* BeginInterfaceConfig -486 (a0) */
  register void *_a0 __asm("a0")=aam; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-486)":"+r"(_a0):"r"(_a6):"d0","d1","a1","memory");
}

/* ---- SocketBaseTagList + SBTC_SYSTEM_STATUS (GetNetStatus) ------------------ */
#define NG_SBTB_CODE		1
#define NG_SBTC_SYSTEM_STATUS	56
#define NG_SBTM_GETVAL(code)	(NG_TU | (((code) & 0x3FFF) << NG_SBTB_CODE))
#define SBSYSSTAT_Interfaces		(1L<<0)
#define SBSYSSTAT_PTP_Interfaces	(1L<<1)
#define SBSYSSTAT_BCast_Interfaces	(1L<<2)
#define SBSYSSTAT_Resolver		(1L<<3)
#define SBSYSSTAT_Routes		(1L<<4)
#define SBSYSSTAT_DefaultRoute		(1L<<5)
static long __attribute__((unused)) ng_sbtaglist(void *tags) {					/* SocketBaseTagList -294 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=tags;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-294)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}

/* ---- route table info + DNS server list (ShowNetStatus) --------------------- */
#define NG_RTA_DST	0x1
#define NG_RTA_GATEWAY	0x2
#define NG_RTA_NETMASK	0x4
#define NG_RTF_UP	0x1
#define NG_RTF_GATEWAY	0x2
#define NG_RT_ROUNDUP(a) (((a) > 0) ? (1 + ((((long)(a)) - 1) | 3)) : 4)

/* Prefix of Roadshow's rt_msghdr (version 3) as our GetRouteInfo emits it, followed
 * by the ROUNDUP'd dst/gateway/netmask sockaddrs. Terminated by rtm_msglen == 0. */
struct ng_rtm {
  unsigned short rtm_msglen;
  unsigned char  rtm_version, rtm_type;
  unsigned short rtm_index;
  long rtm_flags, rtm_addrs, rtm_pid, rtm_seq, rtm_errno, rtm_use;
  unsigned long  rtm_inits;
  unsigned long  rtm_rmx[10];		/* rt_metrics: 10 ULONGs */
};

/* Roadshow's DNS list node (<libraries/bsdsocket.h>). */
struct ng_dnsnode {
  struct MinNode dnsn_MinNode;
  long   dnsn_Size;
  char  *dnsn_Address;			/* dotted-decimal string */
  long   dnsn_UseCount;
};

static __attribute__((unused)) void *ng_getrouteinfo(long af, long flags) {			/* GetRouteInfo -438 (d0,d1) */
  register void *_d0 __asm("d0"); register long _d0i __asm("d0")=af;
  register long _d1 __asm("d1")=flags; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-438)":"=r"(_d0):"r"(_d0i),"r"(_d1),"r"(_a6):"a0","a1","memory");
  return _d0;
}
static void __attribute__((unused)) ng_freerouteinfo(void *p) {					/* FreeRouteInfo -432 (a0) */
  register void *_a0 __asm("a0")=p; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-432)":"+r"(_a0):"r"(_a6):"d0","d1","a1","memory");
}
static __attribute__((unused)) struct List *ng_obtaindnslist(void) {				/* ObtainDomainNameServerList -534 */
  register struct List *_d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-534)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static void __attribute__((unused)) ng_releasednslist(struct List *l) {				/* ReleaseDomainNameServerList -528 */
  register struct List *_a0 __asm("a0")=l; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-528)":"+r"(_a0):"r"(_a6):"d0","d1","a1","memory");
}

#endif /* NG_LVO_H */
