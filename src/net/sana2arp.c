/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: sana2arp.c,v 3.1 1994/02/03 04:06:52 ppessi Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * sana2arp.c - ARP for Sana-II Interfaces
 *
 * Last modified: Thu Feb  3 03:06:35 1994 ppessi
 *
 * HISTORY
 * $Log: sana2arp.c,v $
 * Revision 3.1  1994/02/03  04:06:52  ppessi
 * Removed OSIOCGARP (COMPAT_43); cleaned up history
 *
 * Revision 1.16  1994/01/23  21:58:01  jraja
 * Fixed one void return for SAS/C 6.51.
 *
 * Revision 1.15  1993/10/29  02:00:50  ppessi
 * Fixed ARP table entry allocation policy.
 */

/*
 * Address Resolution Protocol.
 * TODO:
 *	add "inuse/lock" bit (or ref. count) along with valid bit
 */

/*
 * sana2arp.c --- Address Resolution Protocol (RFC 826) for SANA-II Ethernet.
 *
 * ARP answers "I want to send IP to 192.168.0.5 -- what Ethernet (hardware)
 * address do I put on the frame?". This is the standard BSD ARP, adapted to talk
 * to SANA-II interfaces (net/if_sana.c) instead of raw Ethernet drivers.
 * docs/ARCHITECTURE.md sections 7-8.
 *
 * The two entry points to understand:
 *   arpresolve()  called by sana_output() on the TRANSMIT path. If the IP->hw
 *                 mapping is known it fills in the destination hw address and
 *                 returns TRUE (send now). If NOT known, it QUEUES the packet on
 *                 the pending ARP entry, broadcasts an ARP request, and returns
 *                 FALSE (send later) -- which is why the first packet to a new
 *                 host is delayed until the ARP reply arrives.
 *   arpinput()    called on the RECEIVE path when an ARP frame arrives. It learns
 *                 the sender's mapping (updating the table), answers ARP requests
 *                 for our own address, and flushes any packets that were waiting
 *                 on the now-resolved entry.
 *
 * State lives in a `struct arptable` of `struct arptab` entries (arptnew/arptfree
 * manage them); entries expire on a timer unless marked permanent. Mostly stock
 * 4.4BSD ARP -- compare TCP/IP Illustrated Vol 2 ch. 21.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/synch.h>

#include <net/if.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>

#include <net/if_sana.h>
#include <net/sana2arp.h>

#include <net/if_loop_protos.h>

/*
 * Internet to hardware address resolution table entry
 */
struct	arptab {
  struct arptab *at_succ;	/* doubly linked list */
  struct arptab *at_pred;
  struct in_addr at_iaddr;	/* internet address */
  u_char         at_hwaddr[MAXADDRSANA]; /* hardware address */
  u_char         at_timer;	/* minutes since last reference */
  u_char         at_flags;	/* flags */
  struct mbuf   *at_hold;	/* last packet until resolved/timeout */
};

/*
 * Global constant for ARP entry allocation
 */
unsigned long arpentries = ARPENTRIES;

/*
 * General per interface hash table
 */
struct arptable {
  struct SignalSemaphore atb_lock;
  struct arptab         *atb_free;
  struct MinList         atb_entries[ARPTAB_HSIZE];
};

#define ARPTAB_LOCK(atb) (ObtainSemaphore(&atb->atb_lock))
#define ARPTAB_UNLOCK(atb) (ReleaseSemaphore(&atb->atb_lock)) 
#define	ARPTAB_HASH(a) ((u_long)(a) % ARPTAB_HSIZE)

extern struct ifnet loif;

int	useloopback = 0;	/* use loopback interface for local traffic */

static void arpwhohas(register struct sana_softc *ssc, struct in_addr * addr);
static void in_arpinput(register struct sana_softc *ssc, struct mbuf *m);
static char *sana_sprintf(register u_char *ap, int len);

/*
 * Initialization routine. Allocate ARP entries.
 * MUST BE CALLED AT SPLIMP.
 */
void
alloc_arptable(struct sana_softc* ssc, int to_allocate)
{
  struct arptab *at;
  struct arptable *atab;
  int i;
  
  if (ssc->ss_arp.table) 
    return /* (void)ssc->ss_arp.table */;

#if 0
  if (to_allocate < arpentries)
#endif
      to_allocate = arpentries;
    
  atab = bsd_malloc(sizeof(*atab), M_ARPENT, M_WAITOK);
  at = bsd_malloc(sizeof(*at) * to_allocate, M_ARPENT, M_WAITOK);

  if (atab && at) {
    InitSemaphore(&atab->atb_lock);
    for (i = 0; i < ARPTAB_HSIZE; i++)
      NewList((struct List *)(atab->atb_entries + i));

    aligned_bzero(at, sizeof(*at) * to_allocate);

    at[0].at_succ = NULL;
    for (i = 1; i < to_allocate; i++) {
      at[i].at_succ = &at[i - 1];
    }
    atab->atb_free = &at[to_allocate - 1];
  } else {
    if (atab) bsd_free(atab, M_ARPENT);
    if (at) bsd_free(at, M_ARPENT);
    log(LOG_ERR, "Could not allocate ARP table for %s\n", ssc->ss_name);
  }

  ssc->ss_arp.table = atab;
}

/* 
 * Notification function for arp entries 
 */
LONG 
arpentries_notify(void *dummy, LONG value)
{
  return (ULONG)value > ARPENTRIES_MIN;
}

/*
 * Free an arptab entry. ARP TABLE MUST BE LOCKED
 */
static void
arptfree(register struct arptable *atb, register struct arptab *at)
{
  if (at->at_hold)
    m_freem(at->at_hold);

  Remove((struct Node *)at);

  if (at->at_flags & ATF_PERM) {
    bsd_free(at, M_ARPENT);
  } else {
    at->at_hold = NULL;
    at->at_timer = at->at_flags = 0;
    at->at_iaddr.s_addr = 0;
    at->at_succ = atb->atb_free;
    atb->atb_free = at;
  }
}

/*
 * Enter a new address in arptab. ARP TABLE MUST BE LOCKED
 */
static struct arptab *
arptnew(u_long addr, struct arptable *atb, int permanent)
{
  struct arptab *at = NULL;

  if (permanent) {
    at = bsd_malloc(sizeof(*at), M_ARPENT, M_WAITOK);
    bzero((caddr_t)at, sizeof(*at));
  } else {
    at = atb->atb_free;
    if (at) {
      atb->atb_free = at->at_succ;
    } else {
      /*
       * The the oldest entry is pushed out from the 
       * interface table if there is no free entry.
       * This should always succeed since all 
       * entries can not be permanent
       */
      struct arptab *oldest = NULL;
      int i;

      for (i = 0; i < ARPTAB_HSIZE; i++) {
	for (at = (struct arptab *)atb->atb_entries[i].mlh_Head; 
	     at->at_succ; 
	     at = at->at_succ) {
	  if (at->at_flags == 0 || (at->at_flags & ATF_PERM))
	    continue;
	  if (!oldest || oldest->at_timer < at->at_timer)
	    oldest = at;
	}
      }
      if (oldest) {
	Remove((struct Node *)oldest);
	at = oldest;
      } else {
	at = NULL;
      }
    }
  }
  if (at) {
    at->at_iaddr.s_addr = addr;
    at->at_flags = ATF_INUSE;
    AddHead((struct List *)(&atb->atb_entries[ARPTAB_HASH(addr)]), 
	    (struct Node *)at);
  }
  return (at);
}

/*
 * Locate an IP address in the ARP table
 * Assume looker have locked the table
 */
static struct arptab*
arptab_look(struct arptable *table, u_long addr)
{
  register struct arptab *at = (struct arptab *)
    table->atb_entries[ARPTAB_HASH(addr)].mlh_Head; 

  for(;at->at_succ; at = at->at_succ) 
    if (at->at_iaddr.s_addr == addr) 
      return at;

  return NULL;
}

/*
 * Timeout routine.  Age arp_tab entries once a minute.
 */
void
arptimer()
{
  struct sana_softc *ssc;
  register struct arptable *atab;
  register struct arptab *at;
  register int i;

  for (ssc = ssq; ssc; ssc = ssc->ss_next) {
    if (!(atab = ssc->ss_arp.table))
      continue;
    /* Lock the table */
    ARPTAB_LOCK(atab);
    for (i = 0; i < ARPTAB_HSIZE; i++) {
      for (at = (struct arptab *)atab->atb_entries[i].mlh_Head; 
	   at->at_succ; 
	   at = at->at_succ) {
	if (at->at_flags == 0 || (at->at_flags & ATF_PERM))
	  continue;
	if (++at->at_timer < ((at->at_flags & ATF_COM) ?
			      ARPT_KILLC : ARPT_KILLI))
	  continue;
	/* timer has expired, clear entry */
	arptfree(atab, at);
      }
    }
    ARPTAB_UNLOCK(atab);
  }
}

/*
 * Broadcast an ARP REQUEST with an explicit sender/target protocol address.
 * arpwhohas() (sender = our IP), the RFC 3927 probe (sender 0.0.0.0), and the
 * RFC 3927 announcement (sender == target == the claimed address) differ only
 * in these two addresses, so they all funnel through here. sender/target are
 * passed by value (struct in_addr is a 4-byte scalar), keeping the caller from
 * having to hold storage for the 0.0.0.0 probe source.
 */
static void
arp_request_out(register struct sana_softc *ssc,
		struct in_addr sender, struct in_addr target)
{
  register struct mbuf *m;
  register struct s2_arppkt *s2a;
  struct sockaddr_sana2 ss2;

  if ((m = m_gethdr(M_DONTWAIT, MT_DATA)) == NULL)
    return;
  m->m_len = sizeof(*s2a);
  m->m_pkthdr.len = sizeof(*s2a);
  MH_ALIGN(m, sizeof(*s2a));
  s2a = mtod(m, struct s2_arppkt *);
  aligned_bzero_const((caddr_t)s2a, sizeof (*s2a));
  m->m_flags |= M_BCAST;

  /* fill in header depending of the interface */
  s2a->arp_hrd = ssc->ss_arp.hrd;
  s2a->arp_pro = htons(ssc->ss_ip.type);
  s2a->arp_pln = sizeof(struct in_addr); /* protocol address length */
  s2a->arp_hln = ssc->ss_if.if_addrlen;  /* hardware address length */
  s2a->arp_op = htons(ARPOP_REQUEST);

  /* Copy source hardware address */
  bcopy((caddr_t)ssc->ss_hwaddr,
	(caddr_t)&s2a->arpdata,
	s2a->arp_hln);
  /* Copy source protocol address */
  bcopy((caddr_t)&sender,
	(caddr_t)&s2a->arpdata + s2a->arp_hln,
	s2a->arp_pln);
  /* Zero target hardware address */
  bzero((caddr_t)&s2a->arpdata + s2a->arp_hln + s2a->arp_pln,
	s2a->arp_hln);
  /* Copy target protocol address */
  bcopy((caddr_t)&target,
	(caddr_t)&s2a->arpdata + 2 * s2a->arp_hln + s2a->arp_pln,
	s2a->arp_pln);

  /* Send an ARP packet */
  ss2.ss2_len = sizeof(ss2);
  ss2.ss2_family = AF_UNSPEC;
  ss2.ss2_type = ssc->ss_arp.type;
  (*ssc->ss_if.if_output)(&ssc->ss_if, m, (struct sockaddr *)&ss2,
			  (struct rtentry *)0);
}

/*
 * Broadcast an ARP packet, asking who has addr on interface ssc.
 */
static void
arpwhohas(register struct sana_softc *ssc, struct in_addr *addr)
{
  arp_request_out(ssc, ssc->ss_ipaddr, *addr);
}

/*
 * RFC 3927 IPv4 link-local support (used by the ZeroConf fallback path).
 *
 * ng_arp_probe() asks whether anyone already owns `target`, using a sender
 * protocol address of 0.0.0.0 exactly as RFC 3927 s2.2 requires -- so a peer
 * that hears the probe does NOT cache a mapping for an address we do not yet
 * own. ng_arp_announce() is the gratuitous ARP (sender == target == addr) that
 * claims the address once probing found it free, refreshing every peer's cache.
 */
void
ng_arp_probe(struct sana_softc *ssc, struct in_addr target)
{
  struct in_addr any;
  any.s_addr = 0;			/* the probe's 0.0.0.0 sender */
  arp_request_out(ssc, any, target);
}

void
ng_arp_announce(struct sana_softc *ssc, struct in_addr addr)
{
  arp_request_out(ssc, addr, addr);
}

/*
 * RFC 3927 link-local acquisition helpers, driven by the DHCP-failure fallback
 * path in the library (amiga_roadshow.c). That path runs in its own Process, so
 * every one of these takes splimp() around the instant it touches interface
 * state or the output path -- serialising with the network task exactly as the
 * socket API does. The caller releases the CPU (Delay()) only OUTSIDE these
 * calls, i.e. with spl dropped, so the network task can run in_arpinput() and
 * flag a conflict between probes. The interface is resolved by name every call
 * (cheap; interfaces are effectively static), so the caller never has to hold a
 * raw softc pointer across a Delay(). Addresses are network byte order, as on
 * the wire and in in_arpinput's comparisons.
 */
extern struct ifnet *ifunit(char *name);

static struct sana_softc *
ll_softc(const char *ifname)
{
  struct ifnet *ifp;
  struct sana_softc *ssc;

  ifp = ifunit((char *)ifname);
  if (ifp == NULL)
    return NULL;
  for (ssc = ssq; ssc; ssc = ssc->ss_next)
    if (&ssc->ss_if == ifp)		/* a real SANA interface, not lo0 */
      return ssc;
  return NULL;
}

/* Arm probing of `cand` on ifname: record the candidate, clear the conflict
 * flag and any old bound address, and send the first probe. */
int
ng_ll_arm(const char *ifname, ULONG cand)
{
  struct sana_softc *ssc;
  spl_t s;
  int ok = -1;

  s = splimp();
  if ((ssc = ll_softc(ifname)) != NULL) {
    ssc->ss_ll_probe.s_addr = cand;
    ssc->ss_ll_addr.s_addr = 0;
    ssc->ss_ll_conflict = 0;
    ng_arp_probe(ssc, ssc->ss_ll_probe);
    ok = 0;
  }
  splx(s);
  return ok;
}

/* Send one more probe for the armed candidate. */
int
ng_ll_send_probe(const char *ifname)
{
  struct sana_softc *ssc;
  spl_t s;
  int ok = -1;

  s = splimp();
  if ((ssc = ll_softc(ifname)) != NULL && ssc->ss_ll_probe.s_addr) {
    ng_arp_probe(ssc, ssc->ss_ll_probe);
    ok = 0;
  }
  splx(s);
  return ok;
}

/* Non-zero if a conflicting ARP has been seen for the armed candidate. */
int
ng_ll_conflicted(const char *ifname)
{
  struct sana_softc *ssc;
  spl_t s;
  int c = 0;

  s = splimp();
  if ((ssc = ll_softc(ifname)) != NULL)
    c = ssc->ss_ll_conflict;
  splx(s);
  return c;
}

/* Commit `addr` as the bound link-local address: record it, stop probing, and
 * send the first gratuitous announcement. */
int
ng_ll_commit(const char *ifname, ULONG addr)
{
  struct sana_softc *ssc;
  spl_t s;
  int ok = -1;

  s = splimp();
  if ((ssc = ll_softc(ifname)) != NULL) {
    ssc->ss_ll_addr.s_addr = addr;
    ssc->ss_ll_probe.s_addr = 0;
    ssc->ss_ll_conflict = 0;
    ng_arp_announce(ssc, ssc->ss_ll_addr);
    ok = 0;
  }
  splx(s);
  return ok;
}

/* Send one more announcement for the bound address. */
int
ng_ll_send_announce(const char *ifname)
{
  struct sana_softc *ssc;
  spl_t s;
  int ok = -1;

  s = splimp();
  if ((ssc = ll_softc(ifname)) != NULL && ssc->ss_ll_addr.s_addr) {
    ng_arp_announce(ssc, ssc->ss_ll_addr);
    ok = 0;
  }
  splx(s);
  return ok;
}

/* Clear all link-local state (probe aborted, or address given up). */
void
ng_ll_disarm(const char *ifname)
{
  struct sana_softc *ssc;
  spl_t s;

  s = splimp();
  if ((ssc = ll_softc(ifname)) != NULL) {
    ssc->ss_ll_probe.s_addr = 0;
    ssc->ss_ll_addr.s_addr = 0;
    ssc->ss_ll_conflict = 0;
  }
  splx(s);
}

/*
 * Resolve an IP address into an SANA-II address.  If success, 
 * desten is filled in.  If there is no entry in arptab,
 * set one up and broadcast a request for the IP address.
 * Hold onto this mbuf and resend it once the address
 * is finally resolved.  A return value of 1 indicates
 * that desten has been filled in and the packet should be sent
 * normally; a 0 return indicates that the packet has been
 * taken over here, either now or for later transmission.
 *
 * We do some (conservative) locking here at splimp, since
 * arptab is also altered from sana poll routine 
 */
int
arpresolve(register struct sana_softc *ssc,
	   struct mbuf *m,
	   register struct in_addr *destip,
	   register u_char *desten,
	   int *error)
{
  register struct arptab *at;
  register struct arptable *atb;
  struct sockaddr_in sin;
  register struct in_ifaddr *ia;

  if (m->m_flags & M_BCAST) {	/* broadcast */
    return 1;
  }
  /* if for us, use software loopback driver if up */
  for (ia = in_ifaddr; ia; ia = ia->ia_next)
    if ((ia->ia_ifp == &ssc->ss_if) &&
	(destip->s_addr == ia->ia_addr.sin_addr.s_addr)) {
      /*
       * This test used to be
       *	if (loif.if_flags & IFF_UP)
       * It allowed local traffic to be forced
       * through the hardware by configuring the loopback down.
       * However, it causes problems during network configuration
       * for boards that can't receive packets they send.
       * It is now necessary to clear "useloopback"
       * to force traffic out to the hardware.
       */
      if (useloopback) {
	sin.sin_family = AF_INET;
	sin.sin_addr = *destip;
	(void) looutput(&loif, m, (struct sockaddr *)&sin, 0);
	/*
	 * The packet has already been sent and freed.
	 */
	return (0);
      } else {
	bcopy((caddr_t)ssc->ss_hwaddr, (caddr_t)desten, ssc->ss_if.if_addrlen);
	return (1);
      }
    }

  if (ssc->ss_if.if_flags & IFF_NOARP) {
    /* No arp */
    log(LOG_ERR, 
	"arpresolve: can't resolve address for if %s/%ld\n", 
	ssc->ss_if.if_name, ssc->ss_if.if_unit);
    *error = ENETUNREACH;
    m_freem(m);
    return (0);
  } 

  /* Try to locate ARP table */ 
  if (!(atb = ssc->ss_arp.table)) {
    alloc_arptable(ssc, 0);
    if (!(atb = ssc->ss_arp.table)) {
      log(LOG_ERR, "arpresolve: memory exhausted");
      *error = ENOBUFS; 
      m_free(m); 
      return 0;
    }
  }

  ARPTAB_LOCK(atb);
  at = arptab_look(atb, destip->s_addr);
  if (at == 0) {		/* not found */
    at = arptnew(destip->s_addr, atb, FALSE);
    if (at) {
      at->at_hold = m;
      arpwhohas(ssc, destip);
    } else {
      log(LOG_ERR, "arpresolve: no free entry");
      *error = ENETUNREACH;
      m_free(m); 
    } 
    ARPTAB_UNLOCK(atb);
    return 0;
  }

  at->at_timer = 0;		/* restart the timer */
  if (at->at_flags & ATF_COM) {	/* entry IS complete */
    bcopy((caddr_t)at->at_hwaddr, (caddr_t)desten, ssc->ss_if.if_addrlen);
    ARPTAB_UNLOCK(atb);
    return 1;
  }
  /*
   * There is an arptab entry, but no address response yet.  
   * Replace the held mbuf with this latest one.
   */
  if (at->at_hold)
    m_freem(at->at_hold);
  at->at_hold = m;
  arpwhohas(ssc, destip);	/* ask again */
  ARPTAB_UNLOCK(atb);
  return 0;
}

/*
 * Called from the sana poll routine
 * when ARP type packet is received.  
 * Common length and type checks are done here,
 * then the protocol-specific routine is called.
 * In addition, a sanity check is performed on the sender
 * protocol address, to catch impersonators.
 */
void
arpinput(struct sana_softc *ssc,
	struct mbuf *m,
        caddr_t srcaddr)
{
  register struct arphdr *ar;
  int proto;

  if (ssc->ss_if.if_flags & IFF_NOARP)
    goto out;
  if (m->m_len < sizeof(struct arphdr))
    goto out;
  ar = mtod(m, struct arphdr *);
  if (ntohs(ar->ar_hrd) != ssc->ss_arp.hrd)
    goto out;
  if (m->m_len < sizeof(struct arphdr) + 2 * ar->ar_hln + 2 * ar->ar_pln)
    goto out;
  if (ar->ar_hln != ssc->ss_if.if_addrlen) 
    goto out;

#ifdef paranoid_arp_mode
  /* Sanity check */
  if (bcmp(srcaddr, (UBYTE*)ar + sizeof(*ar), ar->ar_hln)) {
    log(LOG_ERR, "An ARP packet sent as %s", 
	sana_sprintf(srcaddr, ar->ar_hln));
    log(LOG_ERR, " from address: %s!!\n", 
	sana_sprintf((UBYTE*)ar + sizeof(*ar), ar->ar_hln));
    goto out;
  }
#endif

  proto = ntohs(ar->ar_pro);

  if (proto == ssc->ss_ip.type) {
    in_arpinput(ssc, m);
    return;
  } 

 out:
  m_freem(m);
}

/* RFC 3927 s2.5: minimum seconds between successive defenses of a bound
 * link-local address, so a persistent conflict cannot make us flood the wire. */
#define NG_LL_DEFEND_INTERVAL 10

/*
 * ARP for Internet protocols on SANA-II interfaces.
 * Algorithm is that given in RFC 826.
 */
static void
in_arpinput(register struct sana_softc *ssc,
	    struct mbuf *m)
{
  register struct s2_arppkt *s2a;
  struct sockaddr_in sin;
  struct in_addr isaddr, itaddr, myaddr;
  int op;
  caddr_t sha, spa, tha, tpa;
  size_t  len = ssc->ss_if.if_addrlen;

  s2a = mtod(m, struct s2_arppkt *);
  op = ntohs(s2a->arp_op);

  if (s2a->arp_pln != sizeof(struct in_addr))
    goto out;

  sha = (caddr_t)&(s2a->arpdata); /* other members must be calculated */
  bcopy(spa = sha + len, (caddr_t)&isaddr, sizeof (isaddr));
  tha = spa + sizeof(struct in_addr);
  bcopy(tpa = tha + len, (caddr_t)&itaddr, sizeof (itaddr));

  /*
   * RFC 3927 link-local conflict detection. This runs BEFORE the in_ifaddr
   * lookup below, because while we are probing a candidate the interface has
   * no address bound yet -- that lookup would `goto out` (maybe_ia == 0) and a
   * probe response would go unseen. We act only on ARPs from a DIFFERENT host
   * (sha != our hw). A conflict is: the ARP's sender IP is our candidate or
   * bound link-local address (someone else already owns it), or -- during
   * probing only -- its target IP is our candidate with a zero sender IP
   * (another host is probing the same address at the same time, RFC 3927
   * s2.2.1). The acquisition FSM and the defense path read ss_ll_conflict.
   */
  if ((ssc->ss_ll_probe.s_addr || ssc->ss_ll_addr.s_addr) &&
      bcmp(sha, (caddr_t)ssc->ss_hwaddr, len) != 0) {
    struct in_addr cand;
    cand = ssc->ss_ll_probe.s_addr ? ssc->ss_ll_probe : ssc->ss_ll_addr;
    if (isaddr.s_addr == cand.s_addr ||
	(ssc->ss_ll_probe.s_addr && isaddr.s_addr == 0 &&
	 itaddr.s_addr == cand.s_addr)) {
      ssc->ss_ll_conflict = 1;
      /*
       * RFC 3927 s2.5 defense: if another host is actively using our BOUND
       * link-local address (its sender IP == ours), broadcast one gratuitous
       * ARP to reassert the mapping -- but no more than once per
       * NG_LL_DEFEND_INTERVAL, so a persistent conflict can't make us storm
       * the wire. (A probe-phase conflict, by contrast, just leaves the flag
       * set so the acquisition FSM picks a different address.) We are already
       * in the network task here, where in_arpinput() also builds its normal
       * ARP replies, so calling ng_arp_announce() directly is in-context.
       */
      if (ssc->ss_ll_addr.s_addr &&
	  isaddr.s_addr == ssc->ss_ll_addr.s_addr) {
	struct timeval now;
	get_time(&now);
	if ((ULONG)now.tv_sec - ssc->ss_ll_defend_time >= NG_LL_DEFEND_INTERVAL) {
	  ssc->ss_ll_defend_time = (ULONG)now.tv_sec;
	  ng_arp_announce(ssc, ssc->ss_ll_addr);
	}
      }
    }
  }

  {
    register struct in_ifaddr *ia;
    struct in_ifaddr *maybe_ia = 0;

    /* Check for our own ARP packets. Skip any INADDR_ANY (0.0.0.0) address the
     * interface may carry: an unnumbered interface -- e.g. one brought up at
     * 0.0.0.0 for a DHCP DISCOVER -- owns NO IP, so it must never be treated as
     * the owner of address 0. Without this, a passing ARP whose sender IP is
     * 0.0.0.0 (every DHCP DISCOVER, every RFC 5227 probe) matched our 0.0.0.0
     * "address", tripping the "duplicate IP address 0" path below -- which logged
     * a bogus conflict AND made us ARP-reply defending 0.0.0.0, corrupting the
     * ARP exchange around the very lease we were trying to apply. */
    for (ia = in_ifaddr; ia; ia = ia->ia_next)
      if (ia->ia_ifp == &ssc->ss_if) {
	if (ia->ia_addr.sin_addr.s_addr == INADDR_ANY)
	  continue;			/* unnumbered: owns no address */
	maybe_ia = ia;
	if ((itaddr.s_addr == ia->ia_addr.sin_addr.s_addr) ||
	    (isaddr.s_addr == ia->ia_addr.sin_addr.s_addr))
	  break;
      }
    if (maybe_ia == 0)
      goto out;
    myaddr = ia ? ia->ia_addr.sin_addr : maybe_ia->ia_addr.sin_addr;
    if (!bcmp(sha, (caddr_t)ssc->ss_hwaddr, len))
      goto out;			/* it's from me, ignore it. */
  }
#ifdef AMITCP
  /*
   * PORT (AmiTCP_NG) hardening: reject an ARP packet whose CLAIMED sender
   * hardware address is the all-ones broadcast pattern -- a unicast sender
   * masquerading as the broadcast address is never legitimate and is a classic
   * spoof/misconfiguration signature. The stock 4.4BSD check for this lived in
   * an `#ifndef AMITCP` block (so it never compiled here) AND referenced
   * Ethernet-only globals (etherbroadcastaddr, ac->ac_if) that do not exist for
   * this SANA-II-generic interface -- it was dead, unbuildable leftover.
   * Rewritten against this interface's own address length `len`
   * (== ssc->ss_if.if_addrlen, already clamped to MAXADDRSANA by the driver
   * setup), so the fixed-size pattern comparison is in bounds. Distinct from
   * m_flags & M_BCAST, which merely means the FRAME was link-broadcast -- normal
   * for a real ARP request and not what is being screened here.
   */
  {
    static const UBYTE arp_bcast_pat[MAXADDRSANA] = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    if (len <= MAXADDRSANA && !bcmp(sha, (caddr_t)arp_bcast_pat, len)) {
      log(LOG_ERR,
	  "arp: sender hw address is broadcast for IP address %lx!\n",
	  ntohl(isaddr.s_addr));
      goto out;
    }
  }
#endif

  /* Check for duplicate IP addresses */
  if (isaddr.s_addr == myaddr.s_addr) {
    log(LOG_ERR,
	"duplicate IP address %lx!! sent from hardware address: %s\n",
	ntohl(isaddr.s_addr), 
	sana_sprintf((u_char *)sha, len));
    itaddr = myaddr;
    if (op == ARPOP_REQUEST)
      goto reply;
    goto out;
  }
  
  {
    struct arptable *atb;
    register struct arptab *at = NULL; /* same as "merge" flag */
    
    /* Try to locate ARP table */ 
    if (!(atb = ssc->ss_arp.table)) {
      goto reply;
    }

    ARPTAB_LOCK(atb);
    at = arptab_look(atb, isaddr.s_addr);

    if (at) {
      bcopy(sha, (caddr_t)at->at_hwaddr, len);
      at->at_flags |= ATF_COM;
      if (at->at_hold) {
	sin.sin_family = AF_INET;
	sin.sin_addr = isaddr;
	(*ssc->ss_if.if_output)(&ssc->ss_if, at->at_hold,
			       (struct sockaddr *)&sin, (struct rtentry *)0);
	at->at_hold = 0;
      }
    }
    if (at == 0 && itaddr.s_addr == myaddr.s_addr) {
      /* ensure we have a table entry */
      if ((at = arptnew(isaddr.s_addr, atb, FALSE))) {
	bcopy(sha, (caddr_t)at->at_hwaddr, len);
	at->at_flags |= ATF_COM;
      }
    }
    ARPTAB_UNLOCK(atb);
  }

 reply:
  /*
   * Reply if this is an IP request
   */
  if (op != ARPOP_REQUEST) 
    goto out;

  if (itaddr.s_addr == myaddr.s_addr) {
    /* I am the target */
    bcopy(sha, tha, len);
    bcopy((caddr_t)ssc->ss_hwaddr, sha, len);
  } else {
    /* Answer if we have a public entry */
    register struct arptab *at;
    
    /* Try to locate ARP table */ 
    if (!ssc->ss_arp.table)
      goto out;

    ARPTAB_LOCK(ssc->ss_arp.table);
    at = arptab_look(ssc->ss_arp.table, itaddr.s_addr);
    if (at && (at->at_flags & ATF_PUBL)) {
      bcopy(sha, tha, len);
      bcopy(at->at_hwaddr, sha, len);
    } else {
      at = NULL;
    }
    ARPTAB_UNLOCK(ssc->ss_arp.table);
    if (!at) 
      goto out;
  }
  {
    struct sockaddr_sana2 ss2;
    bcopy(spa, tpa, sizeof(struct in_addr));
    bcopy((caddr_t)&itaddr, spa, sizeof(struct in_addr));
    s2a->arp_op = htons(ARPOP_REPLY); 

    ss2.ss2_len = sizeof(ss2);
    ss2.ss2_family = AF_UNSPEC;
    ss2.ss2_type = ssc->ss_arp.type;
    bcopy(tha, ss2.ss2_host, len);

    m->m_flags &= ~(M_BCAST|M_MCAST);

    (*ssc->ss_if.if_output)(&ssc->ss_if, m, (struct sockaddr *)&ss2, 
			    (struct rtentry *)0);
    return;
  }
 out:
  m_freem(m);
  return;
}

int
arpioctl(cmd, data)
	int cmd;
	caddr_t data;
{
  register struct arpreq *ar = (struct arpreq *)data;
  register struct arptab *at;
  register struct sockaddr_in *sin;
  struct arptable *atb;
  struct ifaddr *ifa;
  struct sana_softc *ssc;
  spl_t s;

  sin = (struct sockaddr_in *)&ar->arp_pa;
  sin->sin_len = sizeof(ar->arp_pa);

  if (ar->arp_pa.sa_family != AF_INET ||
      ar->arp_ha.sa_family != AF_UNSPEC)
    return (EAFNOSUPPORT);

  s = splimp();
  if ((ifa = ifa_ifwithnet(&ar->arp_pa)) == NULL) {
    splx(s);
    return (ENETUNREACH);
  }

  ssc = (struct sana_softc *)ifa->ifa_ifp;
  splx(s);

  if (ssc->ss_if.if_type != IFT_SANA || !(atb = ssc->ss_arp.table)) {
    return (EAFNOSUPPORT);
  }

  ARPTAB_LOCK(atb);

  if (cmd != SIOCGARPT) {
    at = arptab_look(atb, sin->sin_addr.s_addr);
    if (at == NULL && cmd != SIOCSARP) {
      ARPTAB_UNLOCK(atb);
      return (ENXIO);
    }
  }

  switch (cmd) {

  case SIOCSARP:		/* set entry */
    if (ar->arp_ha.sa_len > sizeof(at->at_hwaddr) + 2 ||
	ar->arp_ha.sa_len != ssc->ss_if.if_addrlen + 2) {
      ARPTAB_UNLOCK(atb);
      return (EINVAL);
    }
    /*
     * Free if new entry should be allocated in a different way 
     */
    if (at != NULL && (at->at_flags ^ ar->arp_flags) & ATF_PERM) {
      arptfree(atb, at);
      at = NULL;
    }
    if (at == NULL) {
      at = arptnew(sin->sin_addr.s_addr, atb, ar->arp_flags & ATF_PERM);
      if (at == NULL) {
	ARPTAB_UNLOCK(atb);
	return (EADDRNOTAVAIL);
      }
    }

    bcopy((caddr_t)ar->arp_ha.sa_data, (caddr_t)at->at_hwaddr,
	  ar->arp_ha.sa_len - 2);
    at->at_flags = ATF_COM | ATF_INUSE | 
      (ar->arp_flags & (ATF_PERM|ATF_PUBL));
    at->at_timer = 0;
    break;

  case SIOCDARP:		/* delete entry */
    arptfree(atb, at);
    break;

  case SIOCGARP:		/* get entry */
    bcopy((caddr_t)at->at_hwaddr, (caddr_t)ar->arp_ha.sa_data,
	  ar->arp_ha.sa_len = ssc->ss_if.if_addrlen + 2);
    ar->arp_flags = at->at_flags;
    break;

  case SIOCGARPT:		/* get table */
    {
      int i, n; long siz;
      register struct arptabreq *atr = (struct arptabreq *)data;
      ar = atr->atr_table;
      siz = ar ? atr->atr_size : 0;

      for (n = i = 0; i < ARPTAB_HSIZE; i++) {
	for (at = (struct arptab *)atb->atb_entries[i].mlh_Head; 
	     at->at_succ; 
	     at = at->at_succ) {
	  n++;
	  if (siz > 0) {
	    struct sockaddr_in *sin = (struct sockaddr_in *)&ar->arp_pa;
	    sin->sin_len = sizeof(*sin);
	    sin->sin_family = AF_INET;
	    sin->sin_addr = at->at_iaddr;
	    bcopy((caddr_t)at->at_hwaddr, (caddr_t)ar->arp_ha.sa_data,
		  ar->arp_ha.sa_len = ssc->ss_if.if_addrlen + 2);
	    ar->arp_flags = at->at_flags;
	    siz--;
	    ar++;
	  }
	}
      }
      atr->atr_size -= siz;
      atr->atr_inuse = n;
    }
  }

  ARPTAB_UNLOCK(atb);
  return 0;
}

static const char *digits = "0123456789ABCDEF";
/*
 * Print Hardware Address
 */
static char *sana_sprintf(register u_char *ap, int len)
{
  register int i;
  static char addrbuf[17*3];
  register unsigned char *cp = (unsigned char *)addrbuf;

  for (i = 0; i < len; ) {
    *cp++ = digits[*ap >> 4];
    *cp++ = digits[*ap++ & 0xf];
    i++;
    if (i < len) 
      *cp++ = ':';
  }
  *cp = 0;
  return (addrbuf);
}
