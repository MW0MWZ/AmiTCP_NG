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
#define IFQ_BadData		(IFQ_BASE + 10)	/* input errors (if_ierrors)          */
#define IFQ_OutputDrops		(IFQ_BASE + 35)	/* output-queue-full drops (if_snd.ifq_drops) */
#define IFQ_InputDrops		(IFQ_BASE + 36)	/* input-queue-full drops (if_iqdrops)*/
/* Byte counters. NOTE THE BUFFER CONTRACT: these take a pointer to a 64-bit
 * SBQUAD_T -- {ULONG high; ULONG low;}, high word FIRST -- not a plain ULONG.
 * Passing a 32-bit target leaves the low word as caller-stack garbage, which is
 * exactly how ShowNetStatus once printed impossible byte figures. */
#define IFQ_GetBytesIn		(IFQ_BASE + 28)
#define IFQ_GetBytesOut		(IFQ_BASE + 29)
/* SANA-II buffer-management copy-callback counts -> struct SANA2CopyStats. A driver
 * that does not use the byte-copy hooks reports 0; that is the driver's choice. */
#define IFQ_GetSANA2CopyStats	(IFQ_BASE + 31)
/* AmiTCP_NG-private (match amiga_roadshow_compat.c): split the TX failure modes. */
#define NGIFQ_OutErrors		(NG_TU + 0x004E4731)	/* if_oerrors: media/device TX errors  */
#define NGIFQ_OutNoBuf		(NG_TU + 0x004E4732)	/* TX drops: send-tag mbuf alloc failed */
#define NGIFQ_InNoBuf		(NG_TU + 0x004E4733)	/* RX drops: read re-post mbuf alloc fail */
/* SANA-II R4 32-bit-aligned copy-callback counts (0 on a driver that only knows
 * the original two functions -- which is the measurement). */
#define NGIFQ_Copy32In		(NG_TU + 0x004E4734)
#define NGIFQ_Copy32Out		(NG_TU + 0x004E4735)
/* AmiTCP_NG-private: the stack-computed effective TCP MSS for the interface
 * (override-aware). MUST match NGIFQ_TcpMss in src/api/amiga_roadshow_compat.c. */
#define NGIFQ_TcpMss		(NG_TU + 0x004E4730)

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
/* ---- statistics + routing-table enumeration (netstat) ---------------------- */
/* GetNetworkStatistics type codes. Only these four are served; the rest
 * (igmp/mbuf/multicast-routing/route/socket lists) return EINVAL today. */
#define NG_NS_ICMP	0
#define NG_NS_IP	2
#define NG_NS_TCP	6
#define NG_NS_UDP	7

/* GetNetworkStatistics (-510). destination NULL asks for the required size.
 * NOTE the buffer must be at least that size: the library bcopies into it. */
static long __attribute__((unused)) ng_netstats(long type, long ver, void *dst, long size) {
  register long _d0 __asm("d0")=type; register long _d1 __asm("d1")=ver;
  register void *_a0 __asm("a0")=dst; register long _d2 __asm("d2")=size;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-510)":"+r"(_d0),"+r"(_d1),"+r"(_a0),"+r"(_d2)
                       :"r"(_a6):"a1","memory");
  return _d0;
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
#define NG_SBTC_HERRNO		7	/* SBTC_HERRNO: the resolver's h_errno */
#define NG_SBTM_GETVAL(code)	(NG_TU | (((code) & 0x3FFF) << NG_SBTB_CODE))

/* AmiTCP_NG-private GET-only diagnostic codes (mirror of SBTC_NG_* in the library's
 * amitcp/socketbasetags.h -- keep the values in step). Report the running stack's live
 * tuning for GetNetStatus DEBUG. An older library returns 0 (unknown-code default). */
#define NG_SBTC_DETECTED_RAM	0x2000	/* installed RAM the stack detected */
#define NG_SBTC_TCP_SENDSPACE	0x2001	/* effective global tcp_sendspace   */
#define NG_SBTC_TCP_RECVSPACE	0x2002	/* effective global tcp_recvspace   */
#define NG_SBTC_SB_MAX		0x2003	/* effective global sb_max          */
#define NG_SBTC_LINK_SPEED	0x2004	/* last interface's if_baudrate (bps) */
#define NG_SBTC_TCP_PREDACK	0x2005	/* TCP fast-path hits, pure ACKs      */
#define NG_SBTC_TCP_PREDDAT	0x2006	/* TCP fast-path hits, in-seq data    */
#define NG_SBTC_TCP_RCVTOTAL	0x2007	/* all TCP segments received          */
#define NG_SBTC_TCP_PCBMISS	0x2008	/* one-entry PCB cache misses         */
#define NG_SBTC_TCP_PREDWIN	0x2009	/* fast-path hits, window updates     */
#define NG_SBTC_SOWK_CALLS	0x2020
#define NG_SBTC_SOWK_RCV	0x2021
#define NG_SBTC_SOWK_WAIT	0x2022
#define NG_SBTC_SOWK_SEL	0x2023
#define NG_SBTC_SOWK_ASYNC	0x2024
/* Header-prediction miss attribution -- one bucket per segment, but they do NOT
 * sum to the slow path (segments dropped before prediction are never attributed). */
#define NG_SBTC_TPM_STATE	0x2010
#define NG_SBTC_TPM_FLAGS	0x2011
#define NG_SBTC_TPM_TSTAMP	0x2012
#define NG_SBTC_TPM_SEQ		0x2013
#define NG_SBTC_TPM_WIN		0x2014
#define NG_SBTC_TPM_REXMIT	0x2015
#define NG_SBTC_TPM_DUPACK	0x2016
#define NG_SBTC_TPM_SACK	0x2017
#define NG_SBTC_TPM_ACK		0x2018
#define NG_SBTC_TPM_CWND	0x2019
#define NG_SBTC_TPM_ACKDATA	0x201A
#define NG_SBTC_TPM_REASS	0x201B
#define NG_SBTC_TPM_SPACE	0x201C
#define NG_SBTC_TPM_ZEROWIN	0x201D
#define NG_SBTC_TPM_ACKDUP	0x201E
#define NG_SBTC_TPM_WINONLY	0x201F
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
