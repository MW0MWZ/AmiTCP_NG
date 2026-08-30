/* $Id: if_sana.h,v 3.1 1994/02/03 03:50:38 ppessi Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * if_sana.h --- Sana-II Interface 
 *
 * Created      : Thu Feb 11 15:57:16 1993 ppessi
 * Last modified: Tue Feb  1 18:25:02 1994 ppessi
 *
 * HISTORY
 * $Log: if_sana.h,v $
 * Revision 3.1  1994/02/03  03:50:38  ppessi
 * Initially tested version
 *
 * Revision 1.15  1993/12/21  22:11:33  jraja
 * Added ss_cflags field to softsana struct.
 * Defined configuration flags. (ppessi)
 *
 * Revision 1.14  1993/11/06  23:39:15  ppessi
 * Added ss_eventsent to record sent event requests.
 *
 * Revision 1.13  1993/08/04  22:14:27  ppessi
 * Restored sockaddr_sana2
 *
 * Revision 1.12  1993/08/01  19:31:48  ppessi
 * Defined prefix "networks/" for SANA-II device names.
 * Moved sockaddr_sana2 structure definition to public header net/if.h
 *
 * Revision 1.11  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.10  1993/05/16  21:09:43  ppessi
 * RCS version changed.
 *
 * Revision 1.9  1993/05/14  15:54:47  ppessi
 * Minor prototype fixes.
 *
 * Revision 1.8  93/05/04  10:54:02  10:54:02  ppessi (Pekka Pessi)
 * Sanitized soft_sanac structure
 * 
 * Revision 1.7  93/04/19  02:12:36  02:12:36  ppessi (Pekka Pessi)
 * Minor fixes for ARP support.
 * Now also supports sockaddr_sana2 interface (AF_UNSPEC).
 * 
 * Revision 1.6  93/04/11  22:15:54  22:15:54  jraja (Jarno Tapio Rajahalme)
 * Changed first argument of the sana_output back to ifnet *.
 * 
 * Revision 1.5  93/03/10  21:15:10  21:15:10  ppessi (Pekka Pessi)
 * Includes arp prototypes
 * 
 * Revision 1.4  93/03/05  03:12:25  03:12:25  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version
 * 
 * Revision 1.3  93/02/28  22:34:12  22:34:12  ppessi (Pekka Pessi)
 * Revised with jraja. 
 * 
 */

#ifndef IF_SANA_H
#define IF_SANA_H

#ifndef DEVICES_SANA_H
#include <devices/sana2.h>
#endif

#ifndef IF_ARP_H
#include <net/if_arp.h>
#endif

/* A prefix added to the SANA-II device name if needed */
#define NAME_PREFIX "networks/"

/*
 * Our Special SANA-II request
 */
struct IOIPReq {
  struct IOSana2Req  ioip_s2;	
#define ioip_ReplyPort  ioip_s2.ios2_Req.io_Message.mn_ReplyPort
#define ioip_Command ioip_s2.ios2_Req.io_Command
#define ioip_Error   ioip_s2.ios2_Req.io_Error
  struct sana_softc *ioip_if;	      /* pointer to network interface */
                     /* request dispatch routine */
  void             (*ioip_dispatch)(struct sana_softc *, struct IOIPReq *); 
  struct mbuf       *ioip_reserved;   /* reserved for packet */
  struct mbuf       *ioip_packet;     /* packet */
  struct IOIPReq    *ioip_next;	      /* allocation queue */
};

/*
 * A socket address for a generic SANA-II host
 */
#define MAXADDRSANA 16

struct sockaddr_sana2 {
  u_char  ss2_len;
  u_char  ss2_family;
  u_long  ss2_type;
  u_char  ss2_host[MAXADDRSANA];
};

/*
 * Interface descriptor
 *	NOTE: most of the code outside will believe this to be simply
 *	a "struct ifnet". The other information is, on the other hand,
 *	our own business.
 */
struct sana_softc {
  struct ifnet    ss_if;	      /* network-visible interface */
  struct in_addr  ss_ipaddr;	      /* copy of ip address */
  ULONG           ss_hwtype;	      /* wiretype */
  UBYTE           ss_hwaddr[MAXADDRSANA]; /* General hardware address */
  struct Device  *ss_dev;	      /* pointer to device */
  struct Unit    *ss_unit;	      /* pointer to unit */
  VOID           *ss_bufmgnt;	      /* magic cookie for buffer management */
  ULONG           ss_copyin;	      /* SANA2 byte CopyToBuff (RX) call count   */
  ULONG           ss_copyout;	      /* SANA2 byte CopyFromBuff (TX) call count  */
  /* Of the above, how many came through the SANA-II R4 32-bit-aligned variants.
   * Zero on every driver that only knows the original two functions -- which is
   * the number we are actually after: it says whether offering a driver more
   * than AmiTCP 3.0b2 did achieves anything on real hardware. */
  ULONG           ss_copyin32;
  ULONG           ss_copyout32;
  ULONG           ss_txnobuf;	      /* TX packets dropped: send-tag mbuf alloc failed */
  ULONG           ss_rxnobuf;	      /* RX packets dropped: read re-post mbuf alloc failed */
#if NG_RX_CSUM && NG_RX_CSUM_VERIFY
  /* Present only in a self-checking build (NG_RX_CSUM_VERIFY, off by default): the
   * fused receive checksum re-proved the slow way on every frame. Counters rather than
   * only a log line, because logging is off unless configured on, and a validation run
   * that reported success while silently checking nothing would be worse than no check
   * at all. */
  ULONG           ss_csumok;	      /* fused RX checksums confirmed correct */
  ULONG           ss_csumbad;	      /* fused RX checksums that DISAGREED -- must stay 0 */
#endif
  UWORD		  ss_reqno;	      /* # of requests to allocate */
  UWORD           ss_cflags;	      /* configuration flags */
  UBYTE           ss_offcleanup;      /* set when the driver went offline: sana_poll()
				       * must deconfigure this interface (route/addr/DNS) */
  /*
   * PORT (AmiTCP_NG): how this interface was configured, remembered so it can be
   * configured AGAIN when its device comes back online.
   *
   * Going offline scrubs the address, routes and dynamic DNS -- deliberately, so
   * nothing reports a configuration that is no longer real. The Roadshow SDK says
   * SM_Online "tries to send an S2_ONLINE command ... if the command succeeds, the
   * other necessary configuration operations will take place", and without these
   * fields there is nothing left to say what those operations are: the interface
   * came back up unnumbered, with no route and no name servers, and stayed that
   * way until someone re-ran AddNetInterface by hand.
   *
   * The stack READS THE CONFIG FILE to do it (ng_reconfig_task in
   * api/amiga_roadshow_compat.c), rather than keeping a snapshot of what it was
   * last told. A snapshot only ever held the address, mask and gateway, so extra
   * routes, static name servers and the search domain were silently lost on every
   * offline/online cycle -- and it had to be kept in step with everything that can
   * change an interface, for ever. An interface is fully up or fully down.
   *
   * A dynamic address is NOT reused: the device may have been offline for a minute
   * or for months, and a lease that old says nothing about the network it is
   * rejoining, so configure=dhcp means "acquire a lease again", never "restore the
   * old one".
   */
  UBYTE           ss_reconfig;	      /* device is back online: sana_poll() must reconfigure */
  /* A reconfigure is already running for this interface. A driver that flaps --
   * offline/online/offline in quick succession, which is exactly the wifipi
   * failure profile -- would otherwise spawn one DHCP client per online event,
   * each binding port 68 and each applying whatever lease it won, so the address
   * would flip between them depending on which finished last. */
  UBYTE           ss_reconfiguring;
  /*
   * Roadshow IFC_AssociatedRoute / IFC_AssociatedDNS: this interface owns the
   * default route / the name servers it made available.
   *
   * The tags are the SDK's (libraries/bsdsocket.h, IFC_BASE+12 and +13) and are
   * opt-in by design -- an interface that has not claimed them keeps its hands
   * off routes and servers somebody else configured. WHEN a claim is redeemed is
   * ours, though, not the SDK's: it documents the tags only as "that interface is
   * associated with a route / a DNS" and never says at what point the association
   * is dissolved. We withdraw them when the interface goes OFFLINE -- by an
   * operator or because its device vanished -- and not merely when it goes down,
   * since SM_Down explicitly "might still be online" and has no counterpart that
   * would put them back. Our own AddNetInterface and DHCP client set both.
   */
  /* The driver refused S2_ONEVENT, so no event will ever tell us it is back and
   * the watchdog probe is the only way to notice. */
  UBYTE           ss_noevents;
  UBYTE           ss_eventfails;	/* consecutive S2_ONEVENT failures */
  /*
   * Watchdog probe state -- how we notice a device that came back without saying
   * so. See the block comment above sana_probe_step() in if_sana.c.
   *
   * ss_wantback is the one that matters for safety: it means the DEVICE dropped
   * out from under a live interface, NOT that an operator took it offline. Only
   * an interface that lost its device against its will is ever brought back
   * automatically; one that was offlined deliberately stays down until it is
   * asked to come up.
   */
  UBYTE           ss_wantback;	      /* device dropped out on its own: watch for it */
  UBYTE           ss_probing;	      /* a probe read is outstanding on the device */
  UBYTE           ss_probe_abort;     /* we asked for that probe read back */
  UBYTE           ss_probe_stuck;     /* driver ignored the abort; said so once */
  UBYTE           ss_probe_wait;      /* if_slowtimo ticks until the next probe step */
  struct IOIPReq *ss_probe_req;	      /* the outstanding probe read, for AbortSanaIO */
  UBYTE           ss_assoc_route;
  UBYTE           ss_assoc_dns;
  UBYTE           ss_removing;	      /* teardown in progress: sana_up()/sana_rearm_reads()
				       * must NOT (re-)post reads or re-arm the watchdog, so a
				       * racing online event can't touch the freed request pool */
  struct IOIPReq *ss_reqs;	      /* allocated requests */
  struct MinList  ss_freereq;	      /* free requests */
#if	INET
  struct {
    UWORD reqno;	      /* for listening ip packets */
    UWORD sent;
    ULONG type;
  } ss_ip;
  struct {			/* for ARP */
    UWORD reqno;	      
    UWORD sent;
    ULONG type;			/* ARP packet type */
    ULONG hrd;			/* ARP header type */
    struct arptable *table;	/* ARP/IP table */
  } ss_arp;
#endif	/* INET */
#if	ISO
  UWORD           ss_isoreqno;	      /* for iso */
  UWORD           ss_isosent;
  ULONG           ss_isotype;
#endif	/* ISO */
#if	CCITT
  UWORD           ss_ccittreqno;      /* for ccitt */
  UWORD           ss_ccittsent;
  ULONG           ss_ccitttype;
#endif	/* CCITT */
#if	NS
  UWORD           ss_nsreqno;	      /* for ns */
  UWORD           ss_nssent;	
  ULONG           ss_nstype;
#endif	/* NS */
  UWORD           ss_rawreqno;	      /* for raw packets */
  UWORD           ss_rawsent;
  UWORD           ss_eventsent;	      /* sent event requests */
  UWORD           ss_maxmtu;	      /* limit given by device */
  UBYTE          *ss_execname;
  ULONG           ss_execunit;
  UBYTE           ss_name[IFNAMSIZ];
  /* RFC 3927 IPv4 link-local (ZeroConf) state. All zero when not in use. */
  struct in_addr  ss_ll_probe;	      /* candidate address being ARP-probed (0 = none) */
  struct in_addr  ss_ll_addr;	      /* bound link-local address (0 = none) */
  UBYTE           ss_ll_conflict;     /* a conflicting ARP for probe/addr was seen */
  ULONG           ss_ll_defend_time;  /* tick of last address defense (DEFEND_INTERVAL) */
  struct sana_softc *ss_next;
};

/*
 * Configuration flags
 */
#define SSF_TRACK (1<<0)	      /* Should we track packets? */
#define SSB_TRACK 0

/* Default configuration flags */
#define SS_CFLAGS (SSF_TRACK)

/*
 * Global functions defined in if_sana.c
 */
int sana_output(struct ifnet *ifp, struct mbuf *m0,
			struct sockaddr *dst, struct rtentry *rt);
int sana_ioctl(register struct ifnet *ifp, int cmd, caddr_t data);
/* queue for sana network interfaces */
extern struct sana_softc *ssq;
#endif /* of IF_SANA_H */
