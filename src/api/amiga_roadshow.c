/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 *
 * ABI REFERENCE / ATTRIBUTION: the Roadshow bsdsocket.library extension ABI
 * implemented in this file (function offsets, tag values, structure layouts and
 * documented behaviour) was matched using Olaf Barthel's Roadshow SDK as the
 * authoritative reference -- its autodoc, SFD and headers. With thanks to Olaf
 * Barthel. The Roadshow SDK is Copyright (C) Olaf Barthel / APC&TCP, All Rights
 * Reserved, and is NOT included in this repository; obtain it from the author:
 *   Roadshow:      http://roadshow.apc-tcp.de/
 *   Roadshow SDK:  https://www.amigafuture.de/app.php/dlext/details?df_id=3658
 * AmiTCP_NG is an independent, open implementation of the same published ABI and
 * includes no Roadshow code.
 */

/*
 * amiga_roadshow.c --- Roadshow bsdsocket.library EXTENSION entry points (the Roadshow-compat shim).
 *
 * PORT (AmiTCP_NG): this file is entirely new. AmiTCP 3.0b2 exported 45 library
 * vectors (socket() at LVO -30 ... SocketBaseTagList() at -294). Olaf Barthel's
 * Roadshow grew the same bsdsocket.library ABI to 133 vectors: GetSocketEvents,
 * then (after 10 reserved slots) whole families of EXTENSION functions -- BPF,
 * route/interface configuration, DHCP, the netdb iterators, address conversion,
 * mbuf access, getaddrinfo, ... For AmiTCP_NG to be a genuine drop-in for
 * Roadshow's library -- so that Roadshow's OWN config tools (AddNetInterface,
 * ShowNetStatus, ...) drive our stack -- every one of those vectors must exist at
 * the EXACT same offset the Roadshow SFD assigns it (ref/NDK3.2/.../bsdsocket_lib.sfd).
 *
 * The whole extension table is wired up here in one go so the ABI is complete and
 * no Roadshow client can jump through an empty vector into hyperspace. Functions
 * we have not implemented yet route to a shared stub that fails cleanly (errno
 * ENOSYS, -1 / NULL) rather than crashing; they are then filled in tranche by
 * tranche. See docs/ARCHITECTURE.md section 5 and amiga_libtables.c (which places
 * these in LibVectors[] at their fixed offsets).
 *
 * Calling convention: like every other library entry these are register-argument
 * functions (see amiga_raf.h). The library base -- our struct SocketBase * -- is
 * always in A6; the remaining arguments sit in the registers the SFD names.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/ioctl.h>
#include <sys/synch.h>

/* strcmp() is not declared by any reachable header in the GNUC/-noixemul
 * build (kern/amiga_includes.h only pulls in <string.h> for __SASC; the
 * GNUC branch relies on amiga_subr.h's own inline strlen()/strcpy(), and
 * <string.h> itself cannot be included here -- it redeclares those with
 * incompatible signatures). strcmp() is still provided by libnix at link
 * time, so just declare it. */
extern int strcmp(const char *, const char *);

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_types.h>
#include <net/if_sana.h>
#include <net/sana2arp.h>	/* RFC 3927 link-local ARP primitives (ng_ll_*) */

#include <utility/tagitem.h>

#include <kern/amiga_includes.h>
#include <kern/amiga_netdb.h>

#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <api/amiga_raf.h>

#include <net/if_protos.h>
#include <kern/uipc_socket_protos.h>

/*
 * Internal helpers we thinly re-export. inet_aton() lives in amiga_libcalls.c;
 * in_localaddr()/in_canforward() live in netinet/in.c. None had a shared
 * prototype, so declare them here (matching their definitions exactly).
 */
extern int inet_aton(const char *cp, struct in_addr *addr);
extern int in_localaddr(struct in_addr in);
extern int in_canforward(struct in_addr in);

/* ------------------------------------------------------------------------- *
 *  Shared "not implemented yet" stubs.
 *
 *  A6 always holds the SocketBase, whatever the vector, so a single one-argument
 *  entry can stand in for ANY unimplemented function: it ignores the call's other
 *  register arguments and fails cleanly. Two flavours are needed only because the
 *  natural "failure" value differs by return type: an integer/BOOL/void vector
 *  wants -1 in D0, a pointer-returning vector wants NULL. Both set errno = ENOSYS
 *  ("function not implemented") on the caller's own SocketBase.
 * ------------------------------------------------------------------------- */

LONG SAVEDS RAF1(_RoadshowStubErr,
		 struct SocketBase *,	libPtr,	a6)
#if 0
{
#endif
  writeErrnoValue(libPtr, ENOSYS);
  return (-1);
}

APTR SAVEDS RAF1(_RoadshowStubNull,
		 struct SocketBase *,	libPtr,	a6)
#if 0
{
#endif
  writeErrnoValue(libPtr, ENOSYS);
  return (NULL);
}

/* ------------------------------------------------------------------------- *
 *  GetSocketEvents (LVO -300) -- the one "standard" vector AmiTCP 3.0b2 lacked.
 *
 *  Roadshow apps poll this for per-socket event flags (accept-ready, connect
 *  complete, closed, ...) as a lighter alternative to WaitSelect(). We do not yet
 *  track that event state, so report "no socket currently has events" (return -1,
 *  the documented empty answer) and clear the caller's flag word. TODO: real event
 *  bookkeeping wired to the socket state changes in uipc_socket2.c.
 * ------------------------------------------------------------------------- */

LONG SAVEDS RAF2(_GetSocketEvents,
		 struct SocketBase *,	libPtr,		a6,
		 ULONG *,		eventsp,	a0)
#if 0
{
#endif
  (void)libPtr;
  if (eventsp != NULL)
    *eventsp = 0;
  return (-1);
}

/* ------------------------------------------------------------------------- *
 *  Address conversion -- thin, self-contained, safe to expose now.
 * ------------------------------------------------------------------------- */

/*
 * inet_aton (LVO -594): dotted-decimal string -> struct in_addr. Straight
 * re-export of the internal parser; returns non-zero on success, 0 on a
 * malformed address (BSD semantics).
 */
LONG SAVEDS RAF3(_inet_aton,
		 struct SocketBase *,	libPtr,	a6,
		 STRPTR,		cp,	a0,
		 struct in_addr *,	addr,	a1)
#if 0
{
#endif
  (void)libPtr;
  return (LONG)inet_aton((const char *)cp, addr);
}

/*
 * inet_pton (LVO -606): presentation string -> network address, address-family
 * aware. Only AF_INET is meaningful on this stack (no IPv6); for AF_INET it is
 * exactly inet_aton with the RFC-3493 return convention (1 ok, 0 unparsable,
 * -1 unsupported family).
 */
LONG SAVEDS RAF4(_inet_pton,
		 struct SocketBase *,	libPtr,	a6,
		 LONG,			af,	d0,
		 STRPTR,		src,	a0,
		 APTR,			dst,	a1)
#if 0
{
#endif
  if (af != AF_INET) {
    writeErrnoValue(libPtr, EAFNOSUPPORT);
    return (-1);
  }
  return inet_aton((const char *)src, (struct in_addr *)dst) ? 1 : 0;
}

/*
 * Format the four network-order bytes of an IPv4 address as "a.b.c.d".
 * Returns the string length (not counting the terminating NUL). dst must hold
 * at least 16 bytes ("255.255.255.255\0").
 */
static int
ntop4(const UBYTE *b, char *dst)
{
  int i, n = 0;

  for (i = 0; i < 4; i++) {
    UBYTE v = b[i];

    if (i)
      dst[n++] = '.';
    if (v >= 100) {
      dst[n++] = '0' + v / 100; v %= 100;
      dst[n++] = '0' + v / 10;  v %= 10;
      dst[n++] = '0' + v;
    } else if (v >= 10) {
      dst[n++] = '0' + v / 10;
      dst[n++] = '0' + v % 10;
    } else {
      dst[n++] = '0' + v;
    }
  }
  dst[n] = '\0';
  return n;
}

/*
 * inet_ntop (LVO -600): network address -> presentation string, bounded by the
 * caller's buffer size. Returns dst on success, NULL (errno set) on a too-small
 * buffer or unsupported family. AF_INET only.
 */
STRPTR SAVEDS RAF5(_inet_ntop,
		   struct SocketBase *,	libPtr,	a6,
		   LONG,		af,	d0,
		   APTR,		src,	a0,
		   STRPTR,		dst,	a1,
		   LONG,		size,	d1)
#if 0
{
#endif
  char tmp[16];
  int len;

  if (af != AF_INET) {
    writeErrnoValue(libPtr, EAFNOSUPPORT);
    return (NULL);
  }
  len = ntop4((const UBYTE *)&((struct in_addr *)src)->s_addr, tmp);
  if (size < (LONG)(len + 1)) {
    writeErrnoValue(libPtr, EINVAL);	/* no ENOSPC in this errno set */
    return (NULL);
  }
  bcopy(tmp, (char *)dst, len + 1);
  return dst;
}

/* ------------------------------------------------------------------------- *
 *  Address classification -- direct re-exports of the internal predicates.
 * ------------------------------------------------------------------------- */

/* In_LocalAddr (LVO -612): is this address on a directly-attached subnet? */
LONG SAVEDS RAF2(_In_LocalAddr,
		 struct SocketBase *,	libPtr,		a6,
		 ULONG,			address,	d0)
#if 0
{
#endif
  struct in_addr ia;

  (void)libPtr;
  ia.s_addr = address;
  return (LONG)in_localaddr(ia);
}

/* In_CanForward (LVO -618): is this a routable (non-broadcast/-local) address? */
LONG SAVEDS RAF2(_In_CanForward,
		 struct SocketBase *,	libPtr,		a6,
		 ULONG,			address,	d0)
#if 0
{
#endif
  struct in_addr ia;

  (void)libPtr;
  ia.s_addr = address;
  return (LONG)in_canforward(ia);
}

/* ------------------------------------------------------------------------- *
 *  Domain name server management -- the string-based half of the DNS API.
 *
 *  The resolver (api/res_send.c) walks NDB->ndb_NameServers directly when it
 *  sends queries, so adding/removing a node here immediately changes which
 *  servers DNS lookups use. Roadshow's convention (confirmed from its config
 *  tools) is 0 = success, non-zero = failure with errno set. The list-returning
 *  half (Obtain/ReleaseDomainNameServerList) is still stubbed pending the exact
 *  Roadshow list-node layout, so SBTC_HAVE_DNS_API stays 0 for now.
 * ------------------------------------------------------------------------- */

/* AddDomainNameServer (LVO -516): add a DNS server by dotted-decimal address. */
LONG SAVEDS RAF2(_AddDomainNameServer,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		address,	a0)
#if 0
{
#endif
  struct in_addr ns_addr;
  struct NameserventNode *nsn;

  if (address == NULL || !inet_aton((const char *)address, &ns_addr)) {
    writeErrnoValue(libPtr, EINVAL);
    return (-1);
  }
  if ((nsn = bsd_malloc(sizeof(*nsn), M_NETDB, M_WAITOK)) == NULL) {
    writeErrnoValue(libPtr, ENOBUFS);
    return (-1);
  }
  nsn->nsn_EntSize = sizeof(nsn->nsn_Ent);
  nsn->nsn_Dynamic = 1;		/* runtime-added (DHCP / a tool) -- cleared on offline */
  nsn->nsn_Ent.ns_addr = ns_addr;

  LOCK_W_NDB(NDB);
  AddTail((struct List *)&NDB->ndb_NameServers, (struct Node *)nsn);
  UNLOCK_NDB(NDB);
  return (0);
}

/* RemoveDomainNameServer (LVO -522): remove the first server matching address. */
LONG SAVEDS RAF2(_RemoveDomainNameServer,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		address,	a0)
#if 0
{
#endif
  struct in_addr ns_addr;
  struct NameserventNode *nsn, *next;
  int found = 0;

  if (address == NULL || !inet_aton((const char *)address, &ns_addr)) {
    writeErrnoValue(libPtr, EINVAL);
    return (-1);
  }

  LOCK_W_NDB(NDB);
  for (nsn = (struct NameserventNode *)NDB->ndb_NameServers.mlh_Head;
       nsn->nsn_Node.mln_Succ != NULL;
       nsn = next) {
    next = (struct NameserventNode *)nsn->nsn_Node.mln_Succ;
    if (nsn->nsn_Ent.ns_addr.s_addr == ns_addr.s_addr) {
      Remove((struct Node *)nsn);
      bsd_free(nsn, M_NETDB);
      found = 1;
      break;
    }
  }
  UNLOCK_NDB(NDB);

  if (!found) {
    writeErrnoValue(libPtr, EINVAL);
    return (-1);
  }
  return (0);
}

/*
 * Remove every runtime-added (dynamic) DNS server, leaving statically configured
 * ones (loaded from the config file, nsn_Dynamic == 0) untouched. Called when an
 * interface goes offline (net/if_sana.c) so a stale DHCP-supplied resolver config
 * does not linger past the link it came from. Not a public library vector -- an
 * internal helper -- so it takes no SocketBase and sets no errno.
 */
void
ng_flush_dynamic_nameservers(void)
{
  struct NameserventNode *nsn, *next;

  if (NDB == NULL)
    return;

  LOCK_W_NDB(NDB);
  for (nsn = (struct NameserventNode *)NDB->ndb_NameServers.mlh_Head;
       nsn->nsn_Node.mln_Succ != NULL;
       nsn = next) {
    next = (struct NameserventNode *)nsn->nsn_Node.mln_Succ;
    if (nsn->nsn_Dynamic) {
      Remove((struct Node *)nsn);
      bsd_free(nsn, M_NETDB);
    }
  }
  UNLOCK_NDB(NDB);
}

/* ------------------------------------------------------------------------- *
 *  Interface configuration -- ConfigureInterfaceTagList().
 *
 *  This is the heart of the Roadshow interface-management API: it configures an
 *  EXISTING interface (address, netmask, point-to-point/broadcast peers, metric,
 *  up/down state) from a tag list. Roadshow's own ConfigureNetInterface tool --
 *  and AddNetInterface, once the interface exists -- drive the stack entirely
 *  through this call. We translate each IFC_* tag into the corresponding classic
 *  BSD ifioctl (SIOCSIFADDR, SIOCSIFNETMASK, SIOCSIFFLAGS, ...) issued through a
 *  throwaway privileged UDP socket -- exactly the path an application takes with
 *  IoctlSocket(), and the same one tmp/udptest.c proved end to end. Address tag
 *  data is a dotted-decimal STRPTR (confirmed from Roadshow's config-tool source).
 *
 *  Roadshow's IFC_* interface-config tags, from <libraries/bsdsocket.h>. Defined
 *  locally so the shim does not depend on the Roadshow NDK headers at build time.
 * ------------------------------------------------------------------------- */
#define IFC_BASE		(TAG_USER + 1800)
#define IFC_Address		(IFC_BASE + 1)	/* STRPTR dotted-decimal   */
#define IFC_NetMask		(IFC_BASE + 2)	/* STRPTR dotted-decimal   */
#define IFC_DestinationAddress	(IFC_BASE + 3)	/* STRPTR (point-to-point) */
#define IFC_BroadcastAddress	(IFC_BASE + 4)	/* STRPTR                  */
#define IFC_Metric		(IFC_BASE + 5)	/* LONG routing metric     */
#define IFC_State		(IFC_BASE + 8)	/* LONG, one of SM_*       */

/* IFC_State values (Roadshow SM_* interface-state machine). */
#define NG_SM_Offline	0
#define NG_SM_Online	1
#define NG_SM_Down	2
#define NG_SM_Up	3

/*
 * Minimal, self-contained TagItem walker (equivalent to utility.library's
 * NextTagItem): follows TAG_MORE chains, honours TAG_IGNORE/TAG_SKIP, stops at
 * TAG_END. Returns the next real tag or NULL. Kept local so the shim has no
 * hard dependency on UtilityBase being valid in the caller's context.
 */
static struct TagItem *
ng_nexttag(struct TagItem **tstate)
{
  struct TagItem *t = *tstate;

  if (t == NULL)
    return NULL;
  for (;;) {
    switch (t->ti_Tag) {
    case TAG_DONE:			/* == TAG_END */
      *tstate = NULL;
      return NULL;
    case TAG_MORE:
      t = (struct TagItem *)t->ti_Data;
      if (t == NULL) { *tstate = NULL; return NULL; }
      continue;
    case TAG_IGNORE:
      t++;
      continue;
    case TAG_SKIP:
      t += 1 + t->ti_Data;
      continue;
    default:
      *tstate = t + 1;
      return t;
    }
  }
}

/* Zero an ifreq and copy in the (NUL-terminated, bounded) interface name. */
static void
ng_ifr_init(struct ifreq *ifr, const char *name)
{
  int i;

  bzero((caddr_t)ifr, sizeof(*ifr));
  for (i = 0; i < IFNAMSIZ - 1 && name[i] != '\0'; i++)
    ifr->ifr_name[i] = name[i];
  ifr->ifr_name[i] = '\0';
}

/* Parse a dotted-decimal address string and issue an address-setting ioctl. */
static int
ng_set_ifaddr(struct socket *so, const char *name, int cmd, const char *addrstr)
{
  struct ifreq ifr;
  struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
  struct in_addr ia;

  if (addrstr == NULL || !inet_aton(addrstr, &ia))
    return EINVAL;
  ng_ifr_init(&ifr, name);
  sin->sin_len    = sizeof(*sin);
  sin->sin_family = AF_INET;
  sin->sin_addr   = ia;
  return ifioctl(so, cmd, (caddr_t)&ifr);
}

/*
 * Apply an IFC_* tag list to an interface that must already exist. Returns an
 * errno (0 on success). Shared by ConfigureInterfaceTagList (configure existing)
 * and AddInterfaceTagList (create then configure). The caller holds the syscall
 * semaphore. The address ioctls route through the socket protocol's PRU_CONTROL
 * to in_control(), which demands a privileged socket (SS_PRIV) -- so a throwaway
 * UDP socket (every socket here is SS_PRIV) is exactly what we need;
 * socreate()/soclose() are the internals behind socket()/CloseSocket().
 */
static int
ng_apply_iface_config(char *ifname, struct TagItem *tags)
{
  struct socket *so;
  struct ifnet *ifp;
  struct TagItem *tstate, *ti;
  int error = 0, e;

  if ((ifp = ifunit(ifname)) == NULL)
    return ENXIO;
  if ((e = socreate(AF_INET, &so, SOCK_DGRAM, 0)) != 0)
    return e;

  /*
   * Apply the netmask BEFORE the address. If the address is set first, in_ifinit()
   * keys the interface's connected route with the address's CLASSFUL mask; the
   * later SIOCSIFNETMASK changes the mask but does NOT re-key that route, so
   * teardown (which deletes using the new mask) misses it and leaves an orphaned
   * route -- and re-adding it on the next AddNetInterface fails EEXIST, surfacing
   * as AAMR_AddrChangeFailed on a DHCP re-lease. Setting the mask first makes the
   * connected-route add and its later delete use one consistent key.
   */
  for (tstate = tags; (ti = ng_nexttag(&tstate)) != NULL; ) {
    if (ti->ti_Tag == IFC_NetMask) {
      if ((e = ng_set_ifaddr(so, ifname, SIOCSIFNETMASK, (char *)ti->ti_Data)) != 0
	  && error == 0)
	error = e;
      break;				/* one netmask expected */
    }
  }

  for (tstate = tags; (ti = ng_nexttag(&tstate)) != NULL; ) {
    e = 0;
    switch (ti->ti_Tag) {
    case IFC_Address:
      e = ng_set_ifaddr(so, ifname, SIOCSIFADDR, (char *)ti->ti_Data);
      break;
    case IFC_NetMask:
      break;				/* already applied first, above */
    case IFC_DestinationAddress:
      e = ng_set_ifaddr(so, ifname, SIOCSIFDSTADDR, (char *)ti->ti_Data);
      break;
    case IFC_BroadcastAddress:
      e = ng_set_ifaddr(so, ifname, SIOCSIFBRDADDR, (char *)ti->ti_Data);
      break;
    case IFC_Metric: {
      struct ifreq ifr;
      ng_ifr_init(&ifr, ifname);
      ifr.ifr_metric = (int)ti->ti_Data;
      e = ifioctl(so, SIOCSIFMETRIC, (caddr_t)&ifr);
      break;
    }
    case IFC_State: {
      struct ifreq ifr;
      short flags = ifp->if_flags;

      ng_ifr_init(&ifr, ifname);
      if (ti->ti_Data == NG_SM_Up || ti->ti_Data == NG_SM_Online)
	flags |= IFF_UP;			/* bring the interface up */
      else
	flags &= ~IFF_UP;			/* take it down */
      ifr.ifr_flags = flags;
      e = ifioctl(so, SIOCSIFFLAGS, (caddr_t)&ifr);
      break;
    }
    default:
      /*
       * Unhandled interface tag (aliases, MTU limit, DHCP release, debug mode,
       * IFC_Complete, ...). Ignore rather than fail, so a tag list that mixes
       * supported and not-yet-supported items still applies the parts we do
       * handle. These land as later tranches.
       */
      break;
    }
    if (e != 0 && error == 0)
      error = e;			/* remember the first failure, keep going */
  }

  soclose(so);
  return error;
}

/* ConfigureInterfaceTagList (LVO -450): configure an EXISTING interface. */
LONG SAVEDS RAF3(_ConfigureInterfaceTagList,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		interface_name,	a0,
		 struct TagItem *,	tags,		a1)
#if 0
{
#endif
  int error;

  CHECK_TASK();

  if (interface_name == NULL) {
    writeErrnoValue(libPtr, EINVAL);
    return (-1);
  }

  ObtainSyscallSemaphore(libPtr);
  error = ng_apply_iface_config((char *)interface_name, tags);
  ReleaseSyscallSemaphore(libPtr);

  if (error != 0) {
    writeErrnoValue(libPtr, error);
    return (-1);
  }
  return (0);
}

/*
 * AddInterfaceTagList (LVO -444): CREATE a SANA-II interface bound to an exec
 * network device, then apply the same IFC_* configuration. This is the call
 * Roadshow's AddNetInterface tool makes to bring up a real NIC. We create the
 * interface via sana_add_interface() (net/if_sana.c, which opens the SANA-II
 * device and if_attach()es it), then reuse the tag loop above. Returns 0 on
 * success, -1 + errno (EADDRINUSE if the name is taken, ENXIO if the device
 * could not be opened).
 */
extern struct ifnet *sana_add_interface(char *ifname, char *devname, long devunit);

LONG SAVEDS RAF5(_AddInterfaceTagList,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		interface_name,	a0,
		 STRPTR,		device_name,	a1,
		 LONG,			unit,		d0,
		 struct TagItem *,	tags,		a2)
#if 0
{
#endif
  int error;

  CHECK_TASK();

  if (interface_name == NULL || device_name == NULL) {
    writeErrnoValue(libPtr, EINVAL);
    return (-1);
  }

  ObtainSyscallSemaphore(libPtr);

  if (ifunit((char *)interface_name) != NULL) {
    ReleaseSyscallSemaphore(libPtr);
    writeErrnoValue(libPtr, EADDRINUSE);	/* interface already exists */
    return (-1);
  }
  if (sana_add_interface((char *)interface_name, (char *)device_name,
			 (long)unit) == NULL) {
    ReleaseSyscallSemaphore(libPtr);
    writeErrnoValue(libPtr, ENXIO);		/* could not open the device */
    return (-1);
  }

  error = ng_apply_iface_config((char *)interface_name, tags);
  ReleaseSyscallSemaphore(libPtr);

  if (error != 0) {
    writeErrnoValue(libPtr, error);
    return (-1);
  }
  return (0);
}

/* ------------------------------------------------------------------------- *
 *  Route management -- AddRouteTagList() / DeleteRouteTagList().
 *
 *  Adds or deletes an entry in the kernel routing table from an RTA_* tag list,
 *  translating straight onto the classic BSD rtrequest() the way rtioctl() does.
 *  Roadshow's route tags all carry dotted-decimal address STRINGS (confirmed from
 *  its config-tool source): RTA_DefaultGateway sets the 0.0.0.0/0 route, RTA_
 *  Destination/DestinationNet a network route (natural mask via in_sockmaskof),
 *  RTA_DestinationHost a host route, RTA_Gateway the next hop. This is what
 *  AddNetInterface uses to install the default route once an interface is up, so
 *  completing Add/Delete lets Roadshow's route setup drive our table.
 * ------------------------------------------------------------------------- */
#define RTA_BASE		(TAG_USER + 1600)
#define RTA_Destination		(RTA_BASE + 1)	/* STRPTR network/host dest */
#define RTA_Gateway		(RTA_BASE + 2)	/* STRPTR next-hop gateway  */
#define RTA_DefaultGateway	(RTA_BASE + 3)	/* STRPTR default route gw  */
#define RTA_DestinationHost	(RTA_BASE + 4)	/* STRPTR host route        */
#define RTA_DestinationNet	(RTA_BASE + 5)	/* STRPTR network route     */

extern int rtrequest(int req, struct sockaddr *dst, struct sockaddr *gateway,
		     struct sockaddr *netmask, int flags,
		     struct rtentry **ret_nrt);
extern void in_sockmaskof(struct in_addr in, struct sockaddr_in *sockmask);

/* Parse an RTA_* tag list and add/delete the route via rtrequest(). errno or 0. */
static int
ng_route_op(int req, struct TagItem *tags)
{
  struct TagItem *tstate, *ti;
  struct in_addr dst, gw;
  int have_dst = 0, is_host = 0, flags, error;
  struct sockaddr_in sa_dst, sa_gw, sa_mask;
  struct sockaddr *nm;

  dst.s_addr = 0;
  gw.s_addr = 0;

  for (tstate = tags; (ti = ng_nexttag(&tstate)) != NULL; ) {
    switch (ti->ti_Tag) {
    case RTA_Destination:
    case RTA_DestinationNet:
      if (!inet_aton((const char *)ti->ti_Data, &dst)) return EINVAL;
      have_dst = 1; is_host = 0;
      break;
    case RTA_DestinationHost:
      if (!inet_aton((const char *)ti->ti_Data, &dst)) return EINVAL;
      have_dst = 1; is_host = 1;
      break;
    case RTA_Gateway:
      if (!inet_aton((const char *)ti->ti_Data, &gw)) return EINVAL;
      break;
    case RTA_DefaultGateway:
      if (!inet_aton((const char *)ti->ti_Data, &gw)) return EINVAL;
      dst.s_addr = 0; have_dst = 1; is_host = 0;	/* 0.0.0.0/0 */
      break;
    default:
      break;
    }
  }

  if (!have_dst)
    return EINVAL;

  bzero((caddr_t)&sa_dst, sizeof sa_dst);
  sa_dst.sin_len = sizeof sa_dst; sa_dst.sin_family = AF_INET; sa_dst.sin_addr = dst;
  bzero((caddr_t)&sa_gw, sizeof sa_gw);
  sa_gw.sin_len = sizeof sa_gw; sa_gw.sin_family = AF_INET; sa_gw.sin_addr = gw;

  flags = RTF_UP;
  if (gw.s_addr != 0)
    flags |= RTF_GATEWAY;
  if (is_host)
    flags |= RTF_HOST;

  nm = NULL;
  if (!is_host) {
    bzero((caddr_t)&sa_mask, sizeof sa_mask);
    in_sockmaskof(dst, &sa_mask);		/* natural mask; 0.0.0.0 -> default */
    nm = (struct sockaddr *)&sa_mask;
  }

  {
    /* Hold splnet() across the add AND the EEXIST recheck as one atomic section.
     * rtrequest() and rtalloc1() each take splnet() internally, but the routing
     * table must not change in the gap between them (e.g. AmiTCP_Task processing
     * an ICMP redirect via rtredirect()). splnet() nests safely on this port. */
    spl_t ns = splnet();

    error = rtrequest(req, (struct sockaddr *)&sa_dst, (struct sockaddr *)&sa_gw,
		      nm, flags, (struct rtentry **)0);

    /* Idempotent add. rtrequest() returns EEXIST when a route with this exact
     * (destination, mask) key is already in the table. If that route is truly the
     * same one we tried to add, the caller is merely re-adding an identical route
     * -- e.g. a config tool (Roadshow's AddNetInterface, or ours) installing the
     * DHCP-provided default route that our own DHCP client already installed.
     * Report success so the caller does not fail; that duplicate add was the cause
     * of "AddNetInterface: Could not add route to <gw> (file exists)" / rc 20. A
     * DIFFERENT gateway for the same destination is a genuine conflict and keeps
     * its EEXIST.
     *
     * rtalloc1() does a longest-prefix rn_match(), which could in principle return
     * a more-specific covering route, so confirm the destination key and the
     * host/net nature match what we intended before trusting the gateway. Key +
     * RTF_HOST + gateway are independently sufficient to identify the route; we
     * skip comparing the netmask value only to avoid depending on the radix mask
     * table's compressed sa_len representation (not a safety issue -- rn_addmask()
     * zero-fills the trimmed trailing bytes -- just extra complexity for no gain).
     * For the /0 default route, the case this fix exists for, a more-specific route
     * also keyed on 0.0.0.0 is structurally impossible (in_sockmaskof(0.0.0.0)
     * always yields mask 0), so the match is exact. For arbitrary RTA_Destination/
     * RTA_DestinationNet networks the residual gap (a coincidental more-specific
     * sibling sharing base address and gateway) is narrower but not structurally
     * impossible -- still with no real-world trigger in this stack today. */
    if (req == RTM_ADD && error == EEXIST) {
      struct rtentry *rt = rtalloc1((struct sockaddr *)&sa_dst, 0);
      if (rt != NULL) {
	struct sockaddr_in *exkey = (struct sockaddr_in *)rt_key(rt);
	struct sockaddr_in *exgw  = (struct sockaddr_in *)rt->rt_gateway;
	int    ex_is_host = (rt->rt_flags & RTF_HOST) != 0;
	ULONG  want_key   = is_host ? sa_dst.sin_addr.s_addr
				    : (sa_dst.sin_addr.s_addr & sa_mask.sin_addr.s_addr);
	if (exkey != NULL &&
	    exkey->sin_addr.s_addr == want_key &&
	    ex_is_host == (is_host != 0) &&
	    exgw != NULL && exgw->sin_family == AF_INET &&
	    exgw->sin_addr.s_addr == sa_gw.sin_addr.s_addr)
	  error = 0;			/* identical route already present */
	rtfree(rt);			/* rtalloc1() took a reference */
      }
    }

    splx(ns);
  }
  return error;
}

/* AddRouteTagList (LVO -414). 0 on success, -1 + errno. */
LONG SAVEDS RAF2(_AddRouteTagList,
		 struct SocketBase *,	libPtr,	a6,
		 struct TagItem *,	tags,	a0)
#if 0
{
#endif
  int error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);
  error = ng_route_op(RTM_ADD, tags);
  ReleaseSyscallSemaphore(libPtr);
  if (error != 0) { writeErrnoValue(libPtr, error); return (-1); }
  return (0);
}

/* DeleteRouteTagList (LVO -420). 0 on success, -1 + errno. */
LONG SAVEDS RAF2(_DeleteRouteTagList,
		 struct SocketBase *,	libPtr,	a6,
		 struct TagItem *,	tags,	a0)
#if 0
{
#endif
  int error;

  CHECK_TASK();
  ObtainSyscallSemaphore(libPtr);
  error = ng_route_op(RTM_DELETE, tags);
  ReleaseSyscallSemaphore(libPtr);
  if (error != 0) { writeErrnoValue(libPtr, error); return (-1); }
  return (0);
}

/* ------------------------------------------------------------------------- *
 *  Interface enumeration -- ObtainInterfaceList() / ReleaseInterfaceList().
 *
 *  ObtainInterfaceList() returns a struct List of plain Exec Nodes, one per
 *  configured interface, each ln_Name'd with the interface name ("lo0", "eth0",
 *  ...). This is the standard Roadshow convention: the list names the interfaces,
 *  and QueryInterfaceTagList(name, ...) fetches the details of any one. The list
 *  (header + every node, each carrying its own name string) is allocated with
 *  AllocVec so the caller can hold it across calls; ReleaseInterfaceList() frees
 *  the lot. Tools like ShowNetStatus walk this to enumerate interfaces.
 * ------------------------------------------------------------------------- */

extern struct ifnet *ifnet;		/* head of the interface list (net/if.c) */

/* Build "name+unit" (e.g. "lo" + 0 -> "lo0") into buf; returns its length. */
static int
ng_ifname(char *buf, struct ifnet *ifp)
{
  int i = 0;
  unsigned u = (unsigned)ifp->if_unit;

  while (i < IFNAMSIZ && ifp->if_name[i] != '\0') {
    buf[i] = ifp->if_name[i];
    i++;
  }
  if (u == 0) {
    buf[i++] = '0';
  } else {
    char rev[10];
    int r = 0;
    while (u != 0) { rev[r++] = '0' + (u % 10); u /= 10; }
    while (r != 0) buf[i++] = rev[--r];
  }
  buf[i] = '\0';
  return i;
}

/* ObtainInterfaceList (LVO -462). Returns a struct List *, or NULL + errno. */
struct List * SAVEDS RAF1(_ObtainInterfaceList,
			  struct SocketBase *,	libPtr,	a6)
#if 0
{
#endif
  struct List *list;
  struct ifnet *ifp;

  CHECK_TASK_NULL();

  if ((list = AllocVec(sizeof(struct List), MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
    writeErrnoValue(libPtr, ENOBUFS);
    return (NULL);
  }
  NewList(list);

  ObtainSyscallSemaphore(libPtr);
  for (ifp = ifnet; ifp != NULL; ifp = ifp->if_next) {
    char namebuf[IFNAMSIZ + 12];
    int len;
    struct Node *n;

    /*
     * Every interface is listed, INCLUDING the loopback (lo0) -- matching Roadshow,
     * whose ShowNetStatus shows lo0 as well. Tools that must act only on real network
     * interfaces filter the loopback themselves: ShowNetStatus skips a 127/8 address
     * when choosing the host address, and NetShutdown skips lo0 when removing
     * interfaces (so it never tears down, or loops forever on, the loopback).
     */
    len = ng_ifname(namebuf, ifp);
    n = AllocVec((ULONG)(sizeof(struct Node) + len + 1),
		 MEMF_PUBLIC | MEMF_CLEAR);
    if (n == NULL)
      continue;				/* best effort: skip on low memory */
    n->ln_Name = (char *)(n + 1);
    { int i; for (i = 0; i <= len; i++) n->ln_Name[i] = namebuf[i]; }
    AddTail(list, n);
  }
  ReleaseSyscallSemaphore(libPtr);

  return (list);
}

/* ReleaseInterfaceList (LVO -456): free a list from ObtainInterfaceList(). */
VOID SAVEDS RAF2(_ReleaseInterfaceList,
		 struct SocketBase *,	libPtr,	a6,
		 struct List *,		list,	a0)
#if 0
{
#endif
  struct Node *n;

  CHECK_TASK_VOID();

  if (list == NULL)
    return;
  while ((n = RemHead(list)) != NULL)
    FreeVec(n);
  FreeVec(list);
}

/* ------------------------------------------------------------------------- *
 *  Interface query -- QueryInterfaceTagList().
 *
 *  Retrieves per-interface properties into caller storage. Per the Roadshow
 *  autodoc, each IFQ_ tag's ti_Data is a POINTER to where the result is written:
 *  scalar tags are LONG or ULONG pointers, IFQ_DeviceName returns a STRPTR
 *  pointer set to the device name, IFQ_HardwareAddress copies up to 16 raw
 *  bytes (not a
 *  string), IFQ_HardwareAddressSize is in BITS, and the address tags fill a
 *  struct sockaddr(_in). SANA-II-specific values come from the sana_softc; generic
 *  values (address, mtu, metric, state, packet counts) come from the ifnet/ifaddr,
 *  so they work for lo0 too. SANA-only tags are answered only for SANA interfaces
 *  (if_type == IFT_SANA); tags we do not track are left untouched (not failed).
 *  ShowNetStatus uses this to display interface details.
 * ------------------------------------------------------------------------- */
#define IFQ_BASE		(TAG_USER + 1900)
#define IFQ_DeviceName		(IFQ_BASE + 1)
#define IFQ_DeviceUnit		(IFQ_BASE + 2)
#define IFQ_HardwareAddressSize	(IFQ_BASE + 3)
#define IFQ_HardwareAddress	(IFQ_BASE + 4)
#define IFQ_MTU			(IFQ_BASE + 5)
#define IFQ_BPS			(IFQ_BASE + 6)
#define IFQ_HardwareType	(IFQ_BASE + 7)
#define IFQ_PacketsReceived	(IFQ_BASE + 8)
#define IFQ_PacketsSent		(IFQ_BASE + 9)
#define IFQ_BadData		(IFQ_BASE + 10)
#define IFQ_Address		(IFQ_BASE + 14)
#define IFQ_DestinationAddress	(IFQ_BASE + 15)
#define IFQ_BroadcastAddress	(IFQ_BASE + 16)
#define IFQ_NetMask		(IFQ_BASE + 17)
#define IFQ_Metric		(IFQ_BASE + 18)
#define IFQ_State		(IFQ_BASE + 19)
#define IFQ_HardwareMTU		(IFQ_BASE + 34)
/* Per-interface I/O counters a monitor (NetMon) queries. This stack's ifnet/SANA softc
 * do not track most of them; we still answer with a plausible value (0, or the I/O
 * request-pool size) so a tool never reads its own uninitialised buffer as the result. */
#define IFQ_Overruns			(IFQ_BASE + 11)
#define IFQ_UnknownTypes		(IFQ_BASE + 12)
#define IFQ_NumReadRequests		(IFQ_BASE + 24)
#define IFQ_MaxReadRequests		(IFQ_BASE + 25)
#define IFQ_NumWriteRequests		(IFQ_BASE + 26)
#define IFQ_MaxWriteRequests		(IFQ_BASE + 27)
#define IFQ_GetBytesIn			(IFQ_BASE + 28)
#define IFQ_GetBytesOut			(IFQ_BASE + 29)
#define IFQ_GetSANA2CopyStats		(IFQ_BASE + 31)

/* SANA-II buffer-management copy-function call counters (mirrors the SDK's
 * struct SANA2CopyStats). We provide only the byte-wide copy hooks, so the DMA
 * and word-wide counts are always 0. */
struct SANA2CopyStats {
	ULONG	s2cs_DMAIn;
	ULONG	s2cs_DMAOut;
	ULONG	s2cs_ByteIn;
	ULONG	s2cs_ByteOut;
	ULONG	s2cs_WordOut;
};
#define IFQ_NumReadRequestsPending	(IFQ_BASE + 32)
#define IFQ_NumWriteRequestsPending	(IFQ_BASE + 33)
#define IFQ_OutputDrops			(IFQ_BASE + 35)
#define IFQ_InputDrops			(IFQ_BASE + 36)
#define IFQ_IPDrops			(IFQ_BASE + 41)
#define IFQ_ARPDrops			(IFQ_BASE + 42)

/* First AF_INET address record on an interface (== its in_ifaddr). */
static struct ifaddr *
ng_ifa_inet(struct ifnet *ifp)
{
  struct ifaddr *ifa;

  for (ifa = ifp->if_addrlist; ifa != NULL; ifa = ifa->ifa_next)
    if (ifa->ifa_addr != NULL && ifa->ifa_addr->sa_family == AF_INET)
      return ifa;
  return NULL;
}

/* SBSYSSTAT_* system-status flags (see amitcp/socketbasetags.h; kept local here as
 * this file carries its own copies of the Roadshow extension constants). */
#define SBSYSSTAT_Interfaces		(1L<<0)
#define SBSYSSTAT_PTP_Interfaces	(1L<<1)
#define SBSYSSTAT_BCast_Interfaces	(1L<<2)
#define SBSYSSTAT_Resolver		(1L<<3)
#define SBSYSSTAT_Routes		(1L<<4)
#define SBSYSSTAT_DefaultRoute		(1L<<5)

/*
 * SBTC_SYSTEM_STATUS: compute the SBSYSSTAT_* bitmask describing what the stack
 * currently has configured. Roadshow's GetNetStatus tool reads this (a GET on tag 56)
 * to report whether the machine is "online" and which facilities are up; SocketBase-
 * TagList() in api/amiga_generic2.c calls this. Loopback interfaces are ignored (they
 * are always present and must not read as "the network is up"). Called with no locks
 * held; takes the syscall semaphore itself.
 */
ULONG
ng_system_status(struct SocketBase *libPtr)
{
  struct ifnet *ifp;
  ULONG status = 0;

  ObtainSyscallSemaphore(libPtr);

  for (ifp = ifnet; ifp != NULL; ifp = ifp->if_next) {
    if (ifp->if_flags & IFF_LOOPBACK)
      continue;
    if ((ifp->if_flags & IFF_UP) == 0)
      continue;
    if (ng_ifa_inet(ifp) == NULL)
      continue;				/* up but unaddressed -> not "configured" */
    status |= SBSYSSTAT_Interfaces;
    if (ifp->if_flags & IFF_POINTOPOINT)
      status |= SBSYSSTAT_PTP_Interfaces;
    if (ifp->if_flags & IFF_BROADCAST)
      status |= SBSYSSTAT_BCast_Interfaces;
    status |= SBSYSSTAT_Routes;		/* an up, addressed interface has a subnet route */
  }

  /* Resolver: any configured domain name server. */
  LOCK_R_NDB(NDB);
  if (((struct MinNode *)NDB->ndb_NameServers.mlh_Head)->mln_Succ != NULL)
    status |= SBSYSSTAT_Resolver;
  UNLOCK_NDB(NDB);

  /* Default route: is there a route for 0.0.0.0? */
  {
    struct sockaddr_in sin;
    struct rtentry *rt;

    bzero((caddr_t)&sin, sizeof(sin));
    sin.sin_len	   = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;		/* INADDR_ANY -> the default route */
    if ((rt = rtalloc1((struct sockaddr *)&sin, 0)) != NULL) {
      if (rt->rt_flags & RTF_GATEWAY)
	status |= SBSYSSTAT_DefaultRoute;
      status |= SBSYSSTAT_Routes;
      rtfree(rt);			/* rtalloc1() took a reference */
    }
  }

  ReleaseSyscallSemaphore(libPtr);
  return status;
}

/* QueryInterfaceTagList (LVO -468). 0 on success, -1 + errno. */
LONG SAVEDS RAF3(_QueryInterfaceTagList,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		interface_name,	a0,
		 struct TagItem *,	tags,		a1)
#if 0
{
#endif
  struct ifnet *ifp;
  struct ifaddr *ifa;
  struct sana_softc *ssc = NULL;
  struct TagItem *tstate, *ti;

  CHECK_TASK();

  if (interface_name == NULL) {
    writeErrnoValue(libPtr, EINVAL);
    return (-1);
  }

  ObtainSyscallSemaphore(libPtr);

  if ((ifp = ifunit((char *)interface_name)) == NULL) {
    ReleaseSyscallSemaphore(libPtr);
    writeErrnoValue(libPtr, ENXIO);
    return (-1);
  }
  ifa = ng_ifa_inet(ifp);
  if (ifp->if_type == IFT_SANA)
    ssc = (struct sana_softc *)ifp;	/* ss_if is sana_softc's first member */

  for (tstate = tags; (ti = ng_nexttag(&tstate)) != NULL; ) {
    APTR d = (APTR)ti->ti_Data;

    if (d == NULL)
      continue;
    switch (ti->ti_Tag) {
    /* --- generic (any interface, incl. lo0) --- */
    case IFQ_MTU:              *(LONG *)d  = (LONG)ifp->if_mtu;      break;
    case IFQ_BPS:              *(LONG *)d  = (LONG)ifp->if_baudrate; break;
    case IFQ_Metric:           *(LONG *)d  = (LONG)ifp->if_metric;   break;
    case IFQ_State:
      *(LONG *)d = (ifp->if_flags & IFF_UP) ? NG_SM_Up : NG_SM_Down;
      break;
    case IFQ_PacketsReceived:  *(ULONG *)d = (ULONG)ifp->if_ipackets; break;
    case IFQ_PacketsSent:      *(ULONG *)d = (ULONG)ifp->if_opackets; break;
    case IFQ_BadData:          *(ULONG *)d = (ULONG)ifp->if_ierrors;  break;
    /* Byte counters. IMPORTANT: these tags take a pointer to a 64-bit SBQUAD_T
     * ({ULONG sbq_High; ULONG sbq_Low;}, big-endian high word first), NOT a plain
     * ULONG. Writing only 32 bits leaves sbq_Low as caller-stack garbage, which the
     * tool reads back as an impossible figure (e.g. "16,375,418" bytes sent). We
     * track 32 bits of octets, so the high word is always 0. */
    case IFQ_GetBytesIn:
      ((ULONG *)d)[0] = 0;			/* sbq_High */
      ((ULONG *)d)[1] = (ULONG)ifp->if_ibytes;	/* sbq_Low  */
      break;
    case IFQ_GetBytesOut:
      ((ULONG *)d)[0] = 0;			/* sbq_High */
      ((ULONG *)d)[1] = (ULONG)ifp->if_obytes;	/* sbq_Low  */
      break;
    case IFQ_GetSANA2CopyStats:
      /* Buffer-management copy-function call counts (Roadshow ShowNetStatus's
       * "Transfer statistics" line). We register only the byte-wide copy hooks
       * (m_copy_to_mbuf / m_copy_from_mbuf), so ByteIn/ByteOut carry the RX/TX
       * call counts and the DMA/word fields are honestly 0. */
      {
	struct SANA2CopyStats *cs = (struct SANA2CopyStats *)d;
	cs->s2cs_DMAIn   = 0;
	cs->s2cs_DMAOut  = 0;
	cs->s2cs_ByteIn  = ssc ? ssc->ss_copyin  : 0;
	cs->s2cs_ByteOut = ssc ? ssc->ss_copyout : 0;
	cs->s2cs_WordOut = 0;
      }
      break;
    /* Counters this stack does not track. Answer 0 (a plausible figure) rather than
     * leaving the caller's buffer untouched -- an unwritten buffer reads back as
     * whatever garbage the tool left there ("impossible" byte/request numbers). */
    case IFQ_InputDrops:
      /* Packets dropped on input because the protocol input queue was full
       * (bumped as if_iqdrops on the SANA receive path). */
      *(ULONG *)d = (ULONG)ifp->if_iqdrops;
      break;
    case IFQ_Overruns:
    case IFQ_UnknownTypes:
    case IFQ_NumReadRequestsPending:
    case IFQ_NumWriteRequestsPending:
    case IFQ_OutputDrops:
    case IFQ_IPDrops:
    case IFQ_ARPDrops:
      *(ULONG *)d = 0;
      break;
    /* I/O request counters: the best real figure we have is the request-pool size. */
    case IFQ_NumReadRequests:
    case IFQ_MaxReadRequests:
    case IFQ_NumWriteRequests:
    case IFQ_MaxWriteRequests:
      *(ULONG *)d = ssc ? (ULONG)ssc->ss_reqno : 0;
      break;
    case IFQ_HardwareAddressSize:
      *(LONG *)d = (LONG)ifp->if_addrlen * 8;		/* size in BITS */
      break;
    case IFQ_Address:
      if (ifa != NULL && ifa->ifa_addr != NULL)
	bcopy((caddr_t)ifa->ifa_addr, (caddr_t)d, sizeof(struct sockaddr_in));
      break;
    case IFQ_NetMask:
      if (ifa != NULL && ifa->ifa_netmask != NULL)
	bcopy((caddr_t)ifa->ifa_netmask, (caddr_t)d, sizeof(struct sockaddr_in));
      break;
    case IFQ_DestinationAddress:
    case IFQ_BroadcastAddress:			/* ifa_broadaddr == ifa_dstaddr */
      if (ifa != NULL && ifa->ifa_dstaddr != NULL)
	bcopy((caddr_t)ifa->ifa_dstaddr, (caddr_t)d, sizeof(struct sockaddr_in));
      break;

    /* --- SANA-II specific (only meaningful for a SANA interface) --- */
    case IFQ_DeviceName:
      if (ssc != NULL) *(STRPTR *)d = (STRPTR)ssc->ss_execname;
      break;
    case IFQ_DeviceUnit:
      if (ssc != NULL) *(LONG *)d = (LONG)ssc->ss_execunit;
      break;
    case IFQ_HardwareAddress:
      if (ssc != NULL) {
	int n = (int)ifp->if_addrlen;
	if (n > MAXADDRSANA) n = MAXADDRSANA;	/* copy raw bytes, not a string */
	bcopy((caddr_t)ssc->ss_hwaddr, (caddr_t)d, n);
      }
      break;
    case IFQ_HardwareType:
      if (ssc != NULL) *(LONG *)d = (LONG)ssc->ss_hwtype;
      break;
    case IFQ_HardwareMTU:
      if (ssc != NULL) *(LONG *)d = (LONG)ssc->ss_maxmtu;
      break;

    default:
      /* Tag we do not track (DHCP lease, DNS, req counts, byte quads, debug,
       * SANA2CopyStats, drop/error/multicast counters). Leave the caller's
       * storage untouched rather than failing the whole query. */
      break;
    }
  }

  ReleaseSyscallSemaphore(libPtr);
  return (0);
}

/* ------------------------------------------------------------------------- *
 *  Domain name server list -- completes the DNS management API.
 *
 *  ObtainDomainNameServerList() returns a struct List of DomainNameServerNodes,
 *  one per configured DNS server. Unlike the interface list, Roadshow DOES define
 *  the node layout (<libraries/bsdsocket.h>): a MinNode plus the struct size, a
 *  pointer to the dotted-decimal address string, and a use count (negative =
 *  statically configured). We build it from NDB->ndb_NameServers -- the same list
 *  the resolver and AddDomainNameServer() use -- so it reflects the live config.
 *  Each node carries its own address string; ReleaseDomainNameServerList() frees
 *  the lot. With this the DNS family is complete, so SBTC_HAVE_DNS_API now reports 1.
 * ------------------------------------------------------------------------- */

/* Roadshow's DNS list node, mirrored from <libraries/bsdsocket.h>. */
struct DomainNameServerNode {
  struct MinNode dnsn_MinNode;
  LONG           dnsn_Size;	/* size of this data structure */
  STRPTR         dnsn_Address;	/* NUL-terminated dotted-decimal IP */
  LONG           dnsn_UseCount;	/* negative => statically configured */
};

/* ObtainDomainNameServerList (LVO -534). Returns a struct List *, or NULL. */
struct List * SAVEDS RAF1(_ObtainDomainNameServerList,
			  struct SocketBase *,	libPtr,	a6)
#if 0
{
#endif
  struct List *list;
  struct NameserventNode *nsn;

  CHECK_TASK_NULL();

  if ((list = AllocVec(sizeof(struct List), MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
    writeErrnoValue(libPtr, ENOBUFS);
    return (NULL);
  }
  NewList(list);

  LOCK_R_NDB(NDB);
  for (nsn = (struct NameserventNode *)NDB->ndb_NameServers.mlh_Head;
       nsn->nsn_Node.mln_Succ != NULL;
       nsn = (struct NameserventNode *)nsn->nsn_Node.mln_Succ) {
    char addr[16];
    int len;
    struct DomainNameServerNode *dn;

    len = ntop4((const UBYTE *)&nsn->nsn_Ent.ns_addr.s_addr, addr);
    dn = AllocVec((ULONG)(sizeof(*dn) + len + 1), MEMF_PUBLIC | MEMF_CLEAR);
    if (dn == NULL)
      continue;				/* best effort: skip on low memory */
    dn->dnsn_Size    = sizeof(*dn);
    dn->dnsn_Address = (STRPTR)(dn + 1);
    { int i; for (i = 0; i <= len; i++) dn->dnsn_Address[i] = addr[i]; }
    /* Roadshow's convention: negative UseCount = statically configured. Report
     * config-file servers as static (-1) and DHCP/runtime-added ones as dynamic
     * (a non-negative count), matching nsn_Dynamic, so tools can tell them apart. */
    dn->dnsn_UseCount = nsn->nsn_Dynamic ? 1 : -1;
    AddTail(list, (struct Node *)dn);
  }
  UNLOCK_NDB(NDB);

  return (list);
}

/* ReleaseDomainNameServerList (LVO -528): free an ObtainDomainNameServerList(). */
VOID SAVEDS RAF2(_ReleaseDomainNameServerList,
		 struct SocketBase *,	libPtr,	a6,
		 struct List *,		list,	a0)
#if 0
{
#endif
  struct Node *n;

  CHECK_TASK_VOID();

  if (list == NULL)
    return;
  while ((n = RemHead(list)) != NULL)
    FreeVec(n);
  FreeVec(list);
}

/* ------------------------------------------------------------------------- *
 *  getaddrinfo family (RFC 3493 node/service name translation).
 *
 *  The modern, protocol-independent replacement for gethostbyname()+getservbyname().
 *  getaddrinfo() resolves a host name (or numeric address, or NULL for
 *  loopback/wildcard) and a service name (or numeric port) into a linked list of
 *  `struct addrinfo`, each ready to hand straight to socket()/connect()/bind().
 *  This stack is IPv4-only, so we produce AF_INET results. We OWN the result
 *  nodes end to end (allocate here, free in freeaddrinfo), so the struct layout is
 *  entirely under our control -- matched to Roadshow's <netdb.h>. Host resolution
 *  reuses ng_gethostbyname_impl() (full local-db + DNS); service resolution reuses
 *  the services database via findServentNode().
 *
 *  struct addrinfo and the AI_/EAI_ codes mirror Roadshow's <netdb.h>. Defined
 *  locally (AmiTCP 3.0b2's netdb.h predates getaddrinfo) so the shim needs no NDK.
 * ------------------------------------------------------------------------- */
struct addrinfo {
  int              ai_flags;
  int              ai_family;
  int              ai_socktype;
  int              ai_protocol;
  LONG             ai_addrlen;		/* socklen_t */
  struct sockaddr *ai_addr;
  char            *ai_canonname;
  struct addrinfo *ai_next;
};

#define NG_AI_PASSIVE		1
#define NG_AI_CANONNAME		2
#define NG_AI_NUMERICHOST	4
#define NG_AI_NUMERICSERV	16

#define NG_EAI_NONAME		-2	/* name or service not known */
#define NG_EAI_FAIL		-4	/* non-recoverable failure */
#define NG_EAI_FAMILY		-6	/* ai_family not supported */
#define NG_EAI_SOCKTYPE		-7	/* ai_socktype not supported */
#define NG_EAI_SERVICE		-8	/* service not supported for socktype */
#define NG_EAI_MEMORY		-10	/* memory allocation failure */

#define NG_INADDR_LOOPBACK	0x7f000001UL	/* 127.0.0.1 (m68k: net order == this) */

extern struct hostent *ng_gethostbyname_impl(struct SocketBase *libPtr,
					     const char *name);
extern struct ServentNode *findServentNode(struct NetDataBase *ndb,
					   const char *name, const char *proto);

static int
ng_all_digits(const char *s)
{
  if (s == NULL || *s == '\0')
    return 0;
  while (*s != '\0') {
    if (*s < '0' || *s > '9')
      return 0;
    s++;
  }
  return 1;
}

static void
ng_freeaddrinfo(struct addrinfo *ai)
{
  struct addrinfo *next;

  while (ai != NULL) {
    next = ai->ai_next;
    FreeVec(ai);
    ai = next;
  }
}

/* Resolve a service (name or numeric) to a network-order port. 0 or an EAI_. */
static int
ng_resolve_serv(const char *servname, int socktype, int flags, UWORD *portp)
{
  if (servname == NULL) {
    *portp = 0;
    return 0;
  }
  if (ng_all_digits(servname)) {
    long p = 0;
    const char *c = servname;
    while (*c != '\0') {
      p = p * 10 + (*c++ - '0');
      if (p > 65535)
	return NG_EAI_SERVICE;
    }
    *portp = htons((UWORD)p);
    return 0;
  }
  if (flags & NG_AI_NUMERICSERV)
    return NG_EAI_NONAME;
  {
    const char *proto = (socktype == SOCK_DGRAM)  ? "udp"
		      : (socktype == SOCK_STREAM) ? "tcp" : NULL;
    struct ServentNode *sn;

    LOCK_R_NDB(NDB);
    sn = findServentNode(NDB, servname, proto);
    if (sn == NULL) {
      UNLOCK_NDB(NDB);
      return NG_EAI_SERVICE;
    }
    *portp = (UWORD)sn->sn_Ent.s_port;	/* already network order */
    UNLOCK_NDB(NDB);
    return 0;
  }
}

/* Allocate one addrinfo node (with its embedded sockaddr_in and, if requested,
 * canonical-name string) for the given socktype/protocol/address/port. */
static struct addrinfo *
ng_make_ai(int socktype, int proto, ULONG addr, UWORD port, const char *canon)
{
  int clen = (canon != NULL) ? (strlen(canon) + 1) : 0;
  struct addrinfo *ai;
  struct sockaddr_in *sin;

  ai = AllocVec((ULONG)(sizeof(*ai) + sizeof(struct sockaddr_in) + clen),
		MEMF_PUBLIC | MEMF_CLEAR);
  if (ai == NULL)
    return NULL;
  sin = (struct sockaddr_in *)(ai + 1);
  sin->sin_len          = sizeof(*sin);
  sin->sin_family       = AF_INET;
  sin->sin_port         = port;
  sin->sin_addr.s_addr  = addr;
  ai->ai_family   = AF_INET;
  ai->ai_socktype = socktype;
  ai->ai_protocol = proto;
  ai->ai_addrlen  = sizeof(struct sockaddr_in);
  ai->ai_addr     = (struct sockaddr *)sin;
  if (clen != 0) {
    ai->ai_canonname = (char *)(sin + 1);
    strcpy(ai->ai_canonname, canon);
  }
  return ai;
}

/* freeaddrinfo (LVO -804). */
VOID SAVEDS RAF2(_freeaddrinfo,
		 struct SocketBase *,	libPtr,	a6,
		 struct addrinfo *,	ai,	a0)
#if 0
{
#endif
  (void)libPtr;
  ng_freeaddrinfo(ai);
}

/* getaddrinfo (LVO -810). Returns 0 on success, an EAI_ code on failure. */
LONG SAVEDS RAF5(_getaddrinfo,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		hostname,	a0,
		 STRPTR,		servname,	a1,
		 struct addrinfo *,	hints,		a2,
		 struct addrinfo **,	res,		a3)
#if 0
{
#endif
  struct addrinfo *head = NULL, **pnext = &head, *ai;
  int flags = 0, family = 0, socktype = 0, want_canon;
  int socktypes[2], nst = 0, i;
  struct in_addr numaddr;
  int host_is_numeric = 0;
  struct hostent *he = NULL;
  const char *canon = NULL;

  CHECK_TASK();		/* returns -1 on the wrong task (generic failure) */

  if (res == NULL)
    return NG_EAI_FAIL;
  *res = NULL;
  if (hostname == NULL && servname == NULL)
    return NG_EAI_NONAME;

  if (hints != NULL) {
    flags    = hints->ai_flags;
    family   = hints->ai_family;
    socktype = hints->ai_socktype;
  }
  if (family != 0 && family != AF_INET)		/* IPv4 only */
    return NG_EAI_FAMILY;
  want_canon = (flags & NG_AI_CANONNAME) != 0;

  if (socktype == SOCK_STREAM || socktype == SOCK_DGRAM) {
    socktypes[nst++] = socktype;
  } else if (socktype == 0) {
    socktypes[nst++] = SOCK_STREAM;
    socktypes[nst++] = SOCK_DGRAM;
  } else {
    return NG_EAI_SOCKTYPE;
  }

  /* Resolve the host part into either a single numeric address or a hostent. */
  if (hostname == NULL) {
    numaddr.s_addr = (flags & NG_AI_PASSIVE) ? INADDR_ANY : NG_INADDR_LOOPBACK;
    host_is_numeric = 1;
  } else if (inet_aton((const char *)hostname, &numaddr)) {
    host_is_numeric = 1;
    if (want_canon)
      canon = (const char *)hostname;
  } else if (flags & NG_AI_NUMERICHOST) {
    return NG_EAI_NONAME;
  } else {
    he = ng_gethostbyname_impl(libPtr, (const char *)hostname);
    if (he == NULL)
      return NG_EAI_NONAME;
    if (want_canon)
      canon = he->h_name;
  }

  /* For each requested socket type, resolve the service and emit a node per
   * address. A service that is invalid for one socktype (e.g. a tcp-only name
   * with socktype 0) simply omits that socktype rather than failing outright. */
  for (i = 0; i < nst; i++) {
    int st = socktypes[i];
    int proto = (st == SOCK_STREAM) ? IPPROTO_TCP
	      : (st == SOCK_DGRAM)  ? IPPROTO_UDP : 0;
    UWORD port;
    const char *thiscanon;

    if (ng_resolve_serv((const char *)servname, st, flags, &port) != 0)
      continue;

    if (host_is_numeric) {
      thiscanon = (head == NULL) ? canon : NULL;	/* canon on first node only */
      ai = ng_make_ai(st, proto, numaddr.s_addr, port, thiscanon);
      if (ai == NULL) { ng_freeaddrinfo(head); return NG_EAI_MEMORY; }
      *pnext = ai; pnext = &ai->ai_next;
    } else {
      char **ap;
      for (ap = he->h_addr_list; *ap != NULL; ap++) {
	ULONG a;
	bcopy(*ap, (caddr_t)&a, sizeof(a));	/* h_addr = 4 bytes, net order */
	thiscanon = (head == NULL) ? canon : NULL;
	ai = ng_make_ai(st, proto, a, port, thiscanon);
	if (ai == NULL) { ng_freeaddrinfo(head); return NG_EAI_MEMORY; }
	*pnext = ai; pnext = &ai->ai_next;
      }
    }
  }

  if (head == NULL)		/* nothing resolved (service failed for all types) */
    return NG_EAI_SERVICE;
  *res = head;
  return 0;
}

/* gai_strerror (LVO -816): map an EAI_ code to a readable string. */
STRPTR SAVEDS RAF2(_gai_strerror,
		   struct SocketBase *,	libPtr,	a6,
		   LONG,		errnum,	a0)
#if 0
{
#endif
  static const char * const tbl[] = {
    "Success",					/*  0 */
    "Invalid value for ai_flags",		/*  1 EAI_BADFLAGS */
    "Name or service not known",		/*  2 EAI_NONAME */
    "Temporary failure in name resolution",	/*  3 EAI_AGAIN */
    "Non-recoverable failure in name resolution",/* 4 EAI_FAIL */
    "No address associated with name",		/*  5 EAI_NODATA */
    "ai_family not supported",			/*  6 EAI_FAMILY */
    "ai_socktype not supported",		/*  7 EAI_SOCKTYPE */
    "Service not supported for ai_socktype",	/*  8 EAI_SERVICE */
    "Address family for name not supported",	/*  9 EAI_ADDRFAMILY */
    "Memory allocation failure",		/* 10 EAI_MEMORY */
    "System error",				/* 11 EAI_SYSTEM */
    "Invalid value for hints",			/* 12 EAI_BADHINTS */
    "Resolved protocol is unknown"		/* 13 EAI_PROTOCOL */
  };
  int idx;

  (void)libPtr;
  if (errnum == 0)
    return (STRPTR)tbl[0];
  idx = (int)(-errnum);
  if (idx >= 1 && idx <= 13)
    return (STRPTR)tbl[idx];
  return (STRPTR)"Unknown error";
}

/* ------------------------------------------------------------------------- *
 *  getnameinfo (LVO -822): the reverse of getaddrinfo -- turn a socket address
 *  into host and service NAME strings (or numeric forms). Host resolution reuses
 *  ng_gethostbyaddr_impl() (local db + reverse DNS); service names are looked up
 *  in the services database by port. Every write into the caller's host/serv
 *  buffers is bounded by the supplied lengths. NI_* flags mirror <netdb.h>.
 *
 *  NOTE: this takes 8 register arguments (A6 base + 7), one more than the RAFn
 *  macros provide, so the register bindings are written out by hand exactly as
 *  the RAF macro would expand them.
 * ------------------------------------------------------------------------- */
#define NG_NI_NUMERICHOST	1
#define NG_NI_NUMERICSERV	2
#define NG_NI_NOFQDN		4
#define NG_NI_NAMEREQD		8
#define NG_NI_DGRAM		16

extern struct hostent *ng_gethostbyaddr_impl(struct SocketBase *libPtr,
					     const UBYTE *addr, int len, int type);

/* Bounded string copy: always NUL-terminates within size (unless size == 0). */
static void
ng_strlcpy(char *dst, const char *src, ULONG size)
{
  ULONG i;

  if (size == 0)
    return;
  for (i = 0; i + 1 < size && src[i] != '\0'; i++)
    dst[i] = src[i];
  dst[i] = '\0';
}

/* Format an unsigned port number as decimal into a bounded buffer. */
static void
ng_format_port(char *buf, ULONG size, unsigned port)
{
  char tmp[8];
  int t = 0;

  if (port == 0) {
    tmp[t++] = '0';
  } else {
    char rev[8];
    int r = 0;
    while (port != 0) { rev[r++] = '0' + (port % 10); port /= 10; }
    while (r != 0) tmp[t++] = rev[--r];
  }
  tmp[t] = '\0';
  ng_strlcpy(buf, tmp, size);
}

/* Look up a service NAME by network-order port + proto, copying into buf.
 * Returns 1 if found, 0 otherwise. */
static int
ng_servname(UWORD port, const char *proto, char *buf, ULONG bufsize)
{
  struct ServentNode *sn;
  int found = 0;

  LOCK_R_NDB(NDB);
  for (sn = (struct ServentNode *)NDB->ndb_Services.mlh_Head;
       sn->sn_Node.mln_Succ != NULL;
       sn = (struct ServentNode *)sn->sn_Node.mln_Succ) {
    if ((UWORD)sn->sn_Ent.s_port == port &&
	(proto == NULL || strcmp(sn->sn_Ent.s_proto, proto) == 0)) {
      ng_strlcpy(buf, sn->sn_Ent.s_name, bufsize);
      found = 1;
      break;
    }
  }
  UNLOCK_NDB(NDB);
  return found;
}

LONG SAVEDS _getnameinfo(VOID)
{
  register struct SocketBase * a6 __asm("a6");
  struct SocketBase *libPtr = a6;
  register struct sockaddr * a0 __asm("a0");
  struct sockaddr *sa = a0;
  register ULONG d0 __asm("d0");
  ULONG salen = d0;
  register char * a1 __asm("a1");
  char *host = a1;
  register ULONG d1 __asm("d1");
  ULONG hostlen = d1;
  register char * a2 __asm("a2");
  char *serv = a2;
  register ULONG d2 __asm("d2");
  ULONG servlen = d2;
  register ULONG d3 __asm("d3");
  ULONG flags = d3;

  struct sockaddr_in *sin = (struct sockaddr_in *)sa;

  CHECK_TASK();

  if (sa == NULL || salen < sizeof(struct sockaddr_in) ||
      sin->sin_family != AF_INET)
    return (NG_EAI_FAMILY);

  /* Host part. */
  if (host != NULL && hostlen > 0) {
    char numeric[16];

    if (flags & NG_NI_NUMERICHOST) {
      ntop4((const UBYTE *)&sin->sin_addr.s_addr, numeric);
      ng_strlcpy(host, numeric, hostlen);
    } else {
      struct hostent *he = ng_gethostbyaddr_impl(libPtr,
			     (const UBYTE *)&sin->sin_addr.s_addr,
			     sizeof(struct in_addr), AF_INET);
      if (he != NULL && he->h_name != NULL) {
	ng_strlcpy(host, he->h_name, hostlen);
	if (flags & NG_NI_NOFQDN) {		/* keep only the first label */
	  char *d = host;
	  while (*d != '\0' && *d != '.') d++;
	  *d = '\0';
	}
      } else if (flags & NG_NI_NAMEREQD) {
	return (NG_EAI_NONAME);
      } else {
	ntop4((const UBYTE *)&sin->sin_addr.s_addr, numeric);
	ng_strlcpy(host, numeric, hostlen);
      }
    }
  }

  /* Service part. */
  if (serv != NULL && servlen > 0) {
    if (flags & NG_NI_NUMERICSERV) {
      ng_format_port(serv, servlen, (unsigned)ntohs(sin->sin_port));
    } else {
      const char *proto = (flags & NG_NI_DGRAM) ? "udp" : "tcp";
      if (!ng_servname(sin->sin_port, proto, serv, servlen))
	ng_format_port(serv, servlen, (unsigned)ntohs(sin->sin_port));
    }
  }

  return (0);
}

/* ------------------------------------------------------------------------- *
 *  Reentrant host lookups -- gethostbyname_r() / gethostbyaddr_r().
 *
 *  BSD-style reentrant resolvers: instead of returning a pointer into a shared
 *  per-SocketBase buffer (as gethostbyname/gethostbyaddr do), the caller supplies
 *  its own `struct hostent *hp` plus a scratch buffer, so results are private and
 *  concurrency-safe. We reuse the resolution cores factored out earlier
 *  (ng_gethostbyname_impl / ng_gethostbyaddr_impl), then SERIALIZE the result --
 *  the name, alias array + strings, and address array + bytes -- into the caller's
 *  buffer with all internal pointers fixed up to point inside it. Everything is
 *  bounded by buflen. On failure the h_errno-style code is written through *he.
 *  Completing these flips SBTC_HAVE_GETHOSTADDR_R_API to 1.
 * ------------------------------------------------------------------------- */
#define NG_HERR_NO_RECOVERY	3	/* h_errno: non-recoverable (buf too small) */

extern struct hostent *ng_gethostbyname_impl(struct SocketBase *libPtr,
					     const char *name);

/* Pack src's hostent into caller-provided hp + buf. 0 ok, -1 if buf too small. */
static int
ng_serialize_hostent(struct hostent *src, struct hostent *hp,
		     char *buf, ULONG buflen)
{
  char *cur = buf;
  char *end = buf + buflen;
  int n_aliases = 0, n_addrs = 0, i, len;
  char **ap;

  if (src->h_aliases != NULL)
    for (ap = src->h_aliases; *ap != NULL; ap++) n_aliases++;
  if (src->h_addr_list != NULL)
    for (ap = src->h_addr_list; *ap != NULL; ap++) n_addrs++;

  hp->h_addrtype = src->h_addrtype;
  hp->h_length   = src->h_length;

  /* pointer arrays first (4-byte aligned so the char* / address longs are safe) */
  cur = (char *)(((ULONG)cur + 3) & ~3UL);
  if (cur + (n_aliases + 1) * sizeof(char *) > end) return (-1);
  hp->h_aliases = (char **)cur;
  cur += (n_aliases + 1) * sizeof(char *);

  cur = (char *)(((ULONG)cur + 3) & ~3UL);
  if (cur + (n_addrs + 1) * sizeof(char *) > end) return (-1);
  hp->h_addr_list = (char **)cur;
  cur += (n_addrs + 1) * sizeof(char *);

  len = strlen(src->h_name) + 1;
  if (cur + len > end) return (-1);
  bcopy(src->h_name, cur, len);
  hp->h_name = cur;
  cur += len;

  for (i = 0; i < n_aliases; i++) {
    len = strlen(src->h_aliases[i]) + 1;
    if (cur + len > end) return (-1);
    bcopy(src->h_aliases[i], cur, len);
    hp->h_aliases[i] = cur;
    cur += len;
  }
  hp->h_aliases[n_aliases] = NULL;

  cur = (char *)(((ULONG)cur + 3) & ~3UL);
  for (i = 0; i < n_addrs; i++) {
    if (cur + src->h_length > end) return (-1);
    bcopy(src->h_addr_list[i], cur, src->h_length);
    hp->h_addr_list[i] = cur;
    cur += src->h_length;
  }
  hp->h_addr_list[n_addrs] = NULL;

  return (0);
}

/* gethostbyname_r (LVO -738). */
struct hostent * SAVEDS RAF6(_gethostbyname_r,
			     struct SocketBase *,	libPtr,	a6,
			     STRPTR,			name,	a0,
			     struct hostent *,		hp,	a1,
			     APTR,			buf,	a2,
			     ULONG,			buflen,	d0,
			     LONG *,			he,	a3)
#if 0
{
#endif
  struct hostent *src;

  CHECK_TASK_NULL();

  if (hp == NULL || buf == NULL) {
    if (he != NULL) *he = NG_HERR_NO_RECOVERY;
    return (NULL);
  }
  src = ng_gethostbyname_impl(libPtr, (const char *)name);
  if (src == NULL) {
    if (he != NULL) *he = (LONG)*libPtr->hErrnoPtr;
    return (NULL);
  }
  if (ng_serialize_hostent(src, hp, (char *)buf, buflen) != 0) {
    if (he != NULL) *he = NG_HERR_NO_RECOVERY;
    return (NULL);
  }
  if (he != NULL) *he = 0;			/* NETDB_SUCCESS */
  return (hp);
}

/*
 * gethostbyaddr_r (LVO -744). 7 arguments + the A6 base = 8 registers, one past
 * RAF7, so the register bindings are hand-rolled (as in getnameinfo above).
 */
struct hostent * SAVEDS _gethostbyaddr_r(VOID)
{
  register struct SocketBase * a6 __asm("a6");
  struct SocketBase *libPtr = a6;
  register const UBYTE * a0 __asm("a0");
  const UBYTE *addr = a0;
  register LONG d0 __asm("d0");
  LONG len = d0;
  register LONG d1 __asm("d1");
  LONG type = d1;
  register struct hostent * a1 __asm("a1");
  struct hostent *hp = a1;
  register APTR a2 __asm("a2");
  APTR buf = a2;
  register ULONG d2 __asm("d2");
  ULONG buflen = d2;
  register LONG * a3 __asm("a3");
  LONG *he = a3;

  struct hostent *src;

  CHECK_TASK_NULL();

  if (hp == NULL || buf == NULL) {
    if (he != NULL) *he = NG_HERR_NO_RECOVERY;
    return (NULL);
  }
  src = ng_gethostbyaddr_impl(libPtr, addr, (int)len, (int)type);
  if (src == NULL) {
    if (he != NULL) *he = (LONG)*libPtr->hErrnoPtr;
    return (NULL);
  }
  if (ng_serialize_hostent(src, hp, (char *)buf, buflen) != 0) {
    if (he != NULL) *he = NG_HERR_NO_RECOVERY;
    return (NULL);
  }
  if (he != NULL) *he = 0;
  return (hp);
}

/* ------------------------------------------------------------------------- *
 *  Default domain name -- GetDefaultDomainName() / SetDefaultDomainName().
 *
 *  The resolver appends the default domain to unqualified host names and searches
 *  it first. AmiTCP keeps the search domains in NDB->ndb_Domains (a list of
 *  DomainentNode); the first entry is the primary/default. GetDefaultDomainName
 *  copies that primary name into the caller's buffer (bounded); SetDefaultDomainName
 *  makes the given name THE default by replacing the list with a single entry.
 * ------------------------------------------------------------------------- */

/* GetDefaultDomainName (LVO -702): TRUE if a default domain exists. */
BOOL SAVEDS RAF3(_GetDefaultDomainName,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		buffer,		a0,
		 LONG,			buffer_size,	d0)
#if 0
{
#endif
  struct DomainentNode *dn;
  int found = 0;

  (void)libPtr;
  if (buffer == NULL || buffer_size <= 0)
    return (FALSE);

  LOCK_R_NDB(NDB);
  dn = (struct DomainentNode *)NDB->ndb_Domains.mlh_Head;
  if (dn->dn_Node.mln_Succ != NULL && dn->dn_Ent.d_name != NULL) {
    ng_strlcpy((char *)buffer, dn->dn_Ent.d_name, (ULONG)buffer_size);
    found = 1;
  }
  UNLOCK_NDB(NDB);

  return found ? TRUE : FALSE;
}

/* SetDefaultDomainName (LVO -708): make `buffer` the sole default domain. */
VOID SAVEDS RAF2(_SetDefaultDomainName,
		 struct SocketBase *,	libPtr,	a6,
		 STRPTR,		buffer,	a0)
#if 0
{
#endif
  struct DomainentNode *dn, *next;
  int nodesize;

  (void)libPtr;
  if (buffer == NULL)
    return;

  LOCK_W_NDB(NDB);

  /* Drop any existing search/default domains. */
  for (dn = (struct DomainentNode *)NDB->ndb_Domains.mlh_Head;
       dn->dn_Node.mln_Succ != NULL; dn = next) {
    next = (struct DomainentNode *)dn->dn_Node.mln_Succ;
    Remove((struct Node *)dn);
    bsd_free(dn, M_NETDB);
  }

  /* Install the new default (node carries its own name string, as adddomainent). */
  nodesize = sizeof(*dn) + strlen((char *)buffer) + 1;
  if ((dn = bsd_malloc(nodesize, M_NETDB, M_WAITOK)) != NULL) {
    dn->dn_EntSize = nodesize - sizeof(struct GenentNode);
    dn->dn_Ent.d_name = (char *)(dn + 1);
    strcpy((char *)(dn + 1), (char *)buffer);
    AddTail((struct List *)&NDB->ndb_Domains, (struct Node *)dn);
  }

  UNLOCK_NDB(NDB);
}

/* ------------------------------------------------------------------------- *
 *  Network statistics -- GetNetworkStatistics().
 *
 *  Copies the protocol stack's internal counters into caller memory. Per the SDK
 *  autodoc: destination==NULL returns the required byte count; otherwise up to
 *  'size' bytes are copied. Roadshow (4.4BSD-Lite2) APPENDED counters to the BSD
 *  stat structs, so ours are byte-prefixes of Roadshow's for ip/tcp/udp -- we
 *  copy our struct and zero-fill the trailing Roadshow-only fields, which is
 *  memory-safe (bounded by 'size') and value-correct for everything we track.
 *  EXCEPTION: icmpstat was REORDERED (Roadshow puts icps_outhist[] right after
 *  icps_oldicmp; ours has it near the end), so icmp is remapped field-by-field
 *  into the Roadshow layout rather than block-copied. Roadshow struct sizes are
 *  the verified field counts x 4 (all counters are 32-bit). Completing this flips
 *  SBTC_HAVE_STATUS_API to 1. ShowNetStatus reads these to display the counters.
 * ------------------------------------------------------------------------- */
#define NG_NETSTATUS_icmp	0
#define NG_NETSTATUS_ip		2
#define NG_NETSTATUS_tcp	6
#define NG_NETSTATUS_udp	7

/* Roadshow struct byte sizes (field count x 4); ip/tcp appended, udp identical. */
#define NG_STAT_IP_OUR		80	/* our ipstat  = 20 longs */
#define NG_STAT_IP_FULL		96	/* Roadshow    = 24 longs */
#define NG_STAT_TCP_OUR		184	/* our tcpstat = 46 longs */
#define NG_STAT_TCP_FULL	208	/* Roadshow    = 52 longs */
#define NG_STAT_UDP_FULL	32	/* both        =  8 longs */

/* our ipstat/tcpstat/udpstat globals (address only -- incomplete types suffice) */
struct ipstat;   extern struct ipstat  ipstat;
struct tcpstat;  extern struct tcpstat tcpstat;
struct udpstat;  extern struct udpstat udpstat;

/* Roadshow's icmpstat layout (from the autodoc): outhist sits mid-struct. */
struct ng_rs_icmpstat {
  ULONG icps_error, icps_oldshort, icps_oldicmp;
  ULONG icps_outhist[ICMP_MAXTYPE + 1];
  ULONG icps_badcode, icps_tooshort, icps_checksum, icps_badlen, icps_reflect;
  ULONG icps_inhist[ICMP_MAXTYPE + 1];
};

/* Remap our (reordered) icmpstat into the Roadshow field layout. */
static void
ng_map_icmpstat(struct ng_rs_icmpstat *r)
{
  int i;

  r->icps_error    = icmpstat.icps_error;
  r->icps_oldshort = icmpstat.icps_oldshort;
  r->icps_oldicmp  = icmpstat.icps_oldicmp;
  for (i = 0; i <= ICMP_MAXTYPE; i++)
    r->icps_outhist[i] = icmpstat.icps_outhist[i];
  r->icps_badcode  = icmpstat.icps_badcode;
  r->icps_tooshort = icmpstat.icps_tooshort;
  r->icps_checksum = icmpstat.icps_checksum;
  r->icps_badlen   = icmpstat.icps_badlen;
  r->icps_reflect  = icmpstat.icps_reflect;
  for (i = 0; i <= ICMP_MAXTYPE; i++)
    r->icps_inhist[i] = icmpstat.icps_inhist[i];
}

/* GetNetworkStatistics (LVO -510). Returns the full data length, or -1 + errno. */
LONG SAVEDS RAF5(_GetNetworkStatistics,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			type,		d0,
		 LONG,			version,	d1,
		 APTR,			destination,	a0,
		 LONG,			size,		d2)
#if 0
{
#endif
  struct ng_rs_icmpstat ricmp;
  const void *src;
  ULONG our_size, full_size, n, real;

  CHECK_TASK();

  (void)version;			/* only version 1 exists; serve it */

  switch (type) {
  case NG_NETSTATUS_ip:
    src = (const void *)&ipstat;  our_size = NG_STAT_IP_OUR;  full_size = NG_STAT_IP_FULL;
    break;
  case NG_NETSTATUS_tcp:
    src = (const void *)&tcpstat; our_size = NG_STAT_TCP_OUR; full_size = NG_STAT_TCP_FULL;
    break;
  case NG_NETSTATUS_udp:
    src = (const void *)&udpstat; our_size = NG_STAT_UDP_FULL; full_size = NG_STAT_UDP_FULL;
    break;
  case NG_NETSTATUS_icmp:
    ng_map_icmpstat(&ricmp);
    src = (const void *)&ricmp;   our_size = sizeof(ricmp);  full_size = sizeof(ricmp);
    break;
  default:
    /* igmp/mb/mrt/rt/tcp_sockets/udp_sockets not yet provided */
    writeErrnoValue(libPtr, EINVAL);
    return (-1);
  }

  if (destination == NULL)
    return (LONG)full_size;		/* required buffer size */

  if (size < 0)
    size = 0;
  n = ((ULONG)size < full_size) ? (ULONG)size : full_size;	/* bytes to write */
  real = (our_size < n) ? our_size : n;				/* real bytes we have */
  bcopy((caddr_t)src, (caddr_t)destination, real);
  if (n > real)
    bzero((caddr_t)destination + real, n - real);		/* trailing zeros */

  return (LONG)full_size;
}

/* ------------------------------------------------------------------------- *
 *  Route table info -- GetRouteInfo() / FreeRouteInfo().
 *
 *  GetRouteInfo() returns an AllocVec'd copy of the routing table. Per the SDK
 *  autodoc each entry is a `struct rt_msghdr` followed by the sockaddrs named in
 *  rtm_addrs (dst, gateway, netmask), each ROUNDUP'd to a longword; entries are
 *  walked by rtm_msglen and the list is TERMINATED BY A DUMMY ENTRY whose
 *  rtm_msglen is zero. We walk the radix tree with rt_walk() (net/rtsock.c), once
 *  to size the buffer and once to fill it (both under splnet so the tree can't
 *  change mid-walk). IMPORTANT: Roadshow's rt_msghdr/rt_metrics layout DIFFERS
 *  from ours (rtm_flags/rtm_pid are swapped and rt_metrics gained rmx_pksent), so
 *  we emit a local Roadshow-layout struct (version 3), NOT our own. FreeRouteInfo()
 *  releases the buffer. Completing these flips SBTC_HAVE_ROUTING_API to 1.
 * ------------------------------------------------------------------------- */
#define NG_RTM_GET		4
#define NG_RTM_VERSION_RS	3	/* Roadshow rt_msghdr layout version */
#define NG_RTA_DST		0x1
#define NG_RTA_GATEWAY		0x2
#define NG_RTA_NETMASK		0x4
#define NG_RT_ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : (int)sizeof(long))

/* Roadshow's exact rt_metrics / rt_msghdr byte layout (from its <net/route.h>). */
struct ng_rs_rt_metrics {
  ULONG rmx_locks, rmx_mtu, rmx_hopcount, rmx_expire, rmx_recvpipe,
	rmx_sendpipe, rmx_ssthresh, rmx_rtt, rmx_rttvar, rmx_pksent;
};
struct ng_rs_rt_msghdr {
  UWORD rtm_msglen;
  UBYTE rtm_version, rtm_type;
  UWORD rtm_index;
  LONG  rtm_flags, rtm_addrs, rtm_pid, rtm_seq, rtm_errno, rtm_use;
  ULONG rtm_inits;
  struct ng_rs_rt_metrics rtm_rmx;
};

extern struct radix_node_head *radix_node_head;	/* head of the af head list */
struct walkarg;					/* opaque; rt_walk only passes it */
extern int rt_walk(struct radix_node *rn,
		   int (*f)(struct radix_node *, struct walkarg *),
		   struct walkarg *w);

/* Walk context, passed through rt_walk's opaque walkarg pointer. */
struct ng_rtwalk {
  int    flags;		/* rt_flags that must ALL be set (0 = every route) */
  caddr_t buf;		/* output buffer, or NULL in the sizing pass */
  ULONG  size;		/* buffer capacity */
  ULONG  used;		/* bytes emitted / needed so far */
};

/* rt_walk callback: size or emit one route (and any dupedkey siblings). */
static int
ng_rt_dump(struct radix_node *rn, struct walkarg *wa)
{
  struct ng_rtwalk *w = (struct ng_rtwalk *)wa;

  for (; rn != NULL; rn = rn->rn_dupedkey) {
    struct rtentry *rt = (struct rtentry *)rn;
    struct sockaddr *dst, *gw, *nm;
    int addrs = 0, msgsize = sizeof(struct ng_rs_rt_msghdr);

    if (rn->rn_flags & RNF_ROOT)
      continue;
    if (w->flags && (rt->rt_flags & w->flags) != w->flags)
      continue;

    dst = rt_key(rt);
    gw  = rt->rt_gateway;
    nm  = rt_mask(rt);
    if (dst) { msgsize += NG_RT_ROUNDUP(dst->sa_len); addrs |= NG_RTA_DST; }
    if (gw)  { msgsize += NG_RT_ROUNDUP(gw->sa_len);  addrs |= NG_RTA_GATEWAY; }
    if (nm)  { msgsize += NG_RT_ROUNDUP(nm->sa_len);  addrs |= NG_RTA_NETMASK; }

    if (w->buf != NULL) {
      struct ng_rs_rt_msghdr *rtm;
      caddr_t cp;

      if (w->used + (ULONG)msgsize > w->size)
	return 0;			/* buffer full -- stop safely */
      rtm = (struct ng_rs_rt_msghdr *)(w->buf + w->used);
      /* header (buffer is zeroed by AllocVec, so unset fields stay 0) */
      rtm->rtm_msglen  = (UWORD)msgsize;
      rtm->rtm_version = NG_RTM_VERSION_RS;
      rtm->rtm_type    = NG_RTM_GET;
      rtm->rtm_index   = rt->rt_ifp ? rt->rt_ifp->if_index : 0;
      rtm->rtm_flags   = rt->rt_flags;
      rtm->rtm_addrs   = addrs;
      rtm->rtm_use     = (LONG)rt->rt_use;
      /* our rt_metrics is a 9-field prefix of Roadshow's; rmx_pksent stays 0 */
      bcopy((caddr_t)&rt->rt_rmx, (caddr_t)&rtm->rtm_rmx, sizeof(rt->rt_rmx));
      /* appended sockaddrs (padding to ROUNDUP already zero) */
      cp = (caddr_t)(rtm + 1);
      if (dst) { bcopy((caddr_t)dst, cp, dst->sa_len); cp += NG_RT_ROUNDUP(dst->sa_len); }
      if (gw)  { bcopy((caddr_t)gw,  cp, gw->sa_len);  cp += NG_RT_ROUNDUP(gw->sa_len); }
      if (nm)  { bcopy((caddr_t)nm,  cp, nm->sa_len);  cp += NG_RT_ROUNDUP(nm->sa_len); }
    }
    w->used += msgsize;
  }
  return 0;
}

/* GetRouteInfo (LVO -438). Returns the table copy, or NULL + errno. */
struct ng_rs_rt_msghdr * SAVEDS RAF3(_GetRouteInfo,
				     struct SocketBase *,	libPtr,		a6,
				     LONG,			address_family,	d0,
				     LONG,			flags,		d1)
#if 0
{
#endif
  struct ng_rtwalk w;
  struct radix_node_head *rnh;
  caddr_t buf;
  spl_t s;

  CHECK_TASK_NULL();

  ObtainSyscallSemaphore(libPtr);

  /* Pass 1: size the buffer. */
  w.flags = (int)flags;
  w.buf = NULL; w.size = 0; w.used = 0;
  s = splnet();
  for (rnh = radix_node_head; rnh != NULL; rnh = rnh->rnh_next) {
    if (rnh->rnh_af == 0)
      continue;
    if (address_family != 0 && (LONG)rnh->rnh_af != address_family)
      continue;
    rt_walk(rnh->rnh_treetop, ng_rt_dump, (struct walkarg *)&w);
  }
  splx(s);

  /* +1 dummy header for the zero-rtm_msglen terminator; MEMF_CLEAR zeroes it. */
  buf = AllocVec((ULONG)(w.used + sizeof(struct ng_rs_rt_msghdr)),
		 MEMF_PUBLIC | MEMF_CLEAR);
  if (buf == NULL) {
    ReleaseSyscallSemaphore(libPtr);
    writeErrnoValue(libPtr, ENOBUFS);
    return (NULL);
  }

  /* Pass 2: fill (bounded by w.size in case the table grew between passes). */
  w.buf = buf; w.size = w.used; w.used = 0;
  s = splnet();
  for (rnh = radix_node_head; rnh != NULL; rnh = rnh->rnh_next) {
    if (rnh->rnh_af == 0)
      continue;
    if (address_family != 0 && (LONG)rnh->rnh_af != address_family)
      continue;
    rt_walk(rnh->rnh_treetop, ng_rt_dump, (struct walkarg *)&w);
  }
  splx(s);

  ReleaseSyscallSemaphore(libPtr);
  return ((struct ng_rs_rt_msghdr *)buf);
}

/* FreeRouteInfo (LVO -432): release a GetRouteInfo() buffer. */
VOID SAVEDS RAF2(_FreeRouteInfo,
		 struct SocketBase *,	libPtr,	a6,
		 struct ng_rs_rt_msghdr *, buf,	a0)
#if 0
{
#endif
  (void)libPtr;
  if (buf != NULL)
    FreeVec((APTR)buf);
}

/* ------------------------------------------------------------------------- *
 *  RemoveInterface() -- the counterpart to AddInterfaceTagList().
 *
 *  Finds the named interface and hands it to sana_remove_interface() (if_sana.c),
 *  which does the actual teardown (offline, free request buffers, scrub addresses,
 *  unlink from the ifnet/softc lists, close the device, free the softc). Refuses
 *  a non-SANA interface (EINVAL) or one still up unless `force` is TRUE (EBUSY).
 *  Completing this makes the interface family whole -> SBTC_HAVE_INTERFACE_API=1.
 * ------------------------------------------------------------------------- */
extern int sana_remove_interface(struct ifnet *ifp, int force);

/* RemoveInterface (LVO -732). TRUE on success, FALSE + errno on failure. */
BOOL SAVEDS RAF3(_RemoveInterface,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		interface_name,	a0,
		 LONG,			force,		d0)
#if 0
{
#endif
  struct ifnet *ifp;
  int error;

  if (interface_name == NULL) {
    writeErrnoValue(libPtr, EINVAL);
    return (FALSE);
  }

  ObtainSyscallSemaphore(libPtr);

  if ((ifp = ifunit((char *)interface_name)) == NULL) {
    ReleaseSyscallSemaphore(libPtr);
    writeErrnoValue(libPtr, ENXIO);
    return (FALSE);
  }
  error = sana_remove_interface(ifp, (int)force);

  ReleaseSyscallSemaphore(libPtr);

  if (error != 0) {
    writeErrnoValue(libPtr, error);
    return (FALSE);
  }
  return (TRUE);
}

/* ------------------------------------------------------------------------- *
 *  DHCP / BOOTP address allocation -- CreateAddrAllocMessage / Delete.
 *
 *  These manage the `struct AddressAllocationMessage` that drives the
 *  BeginInterfaceConfig() DHCP/BOOTP process. CreateAddrAllocMessageA() does a
 *  single AllocVec of the message plus every result buffer the caller asked for
 *  via CAAMTA_* tags (NAK text, router/DNS/static-route tables, host/domain name,
 *  BOOTP message, lease-expiry DateStamp), pointing the aam_* members into that
 *  tail; DeleteAddrAllocMessage() frees the lot. Layout mirrored from Roadshow's
 *  <libraries/bsdsocket.h>. (BeginInterfaceConfig itself -- the async BOOTP/DHCP
 *  protocol exchange -- is a separate, larger piece; see the stub note.)
 * ------------------------------------------------------------------------- */
#define AAM_VERSION_NG		2
#define AAM_VERSION_MIN_NG	1
#define CAAMTA_BASE_NG		(TAG_USER + 2000)
#define CAAMTA_Timeout_NG		(CAAMTA_BASE_NG + 1)
#define CAAMTA_LeaseTime_NG		(CAAMTA_BASE_NG + 2)
#define CAAMTA_RequestedAddress_NG	(CAAMTA_BASE_NG + 3)
#define CAAMTA_ClientIdentifier_NG	(CAAMTA_BASE_NG + 4)
#define CAAMTA_NAKMessageSize_NG	(CAAMTA_BASE_NG + 5)
#define CAAMTA_RouterTableSize_NG	(CAAMTA_BASE_NG + 6)
#define CAAMTA_DNSTableSize_NG		(CAAMTA_BASE_NG + 7)
#define CAAMTA_StaticRouteTableSize_NG	(CAAMTA_BASE_NG + 8)
#define CAAMTA_HostNameSize_NG		(CAAMTA_BASE_NG + 9)
#define CAAMTA_DomainNameSize_NG	(CAAMTA_BASE_NG + 10)
#define CAAMTA_BOOTPMessageSize_NG	(CAAMTA_BASE_NG + 11)
#define CAAMTA_RecordLeaseExpiration_NG	(CAAMTA_BASE_NG + 12)
#define CAAMTA_ReplyPort_NG		(CAAMTA_BASE_NG + 13)
#define CAAMTA_RequestUnicast_NG	(CAAMTA_BASE_NG + 14)

/* Roadshow's AddressAllocationMessage byte layout (from <libraries/bsdsocket.h>). */
struct ng_aam {
  struct Message aam_Message;
  LONG    aam_Reserved;
  LONG    aam_Result;
  LONG    aam_Version;
  LONG    aam_Protocol;
  char    aam_InterfaceName[16];
  LONG    aam_Timeout;
  ULONG   aam_LeaseTime;
  ULONG   aam_RequestedAddress;
  STRPTR  aam_ClientIdentifier;
  ULONG   aam_Address;
  ULONG   aam_ServerAddress;
  ULONG   aam_SubnetMask;
  STRPTR  aam_NAKMessage;
  LONG    aam_NAKMessageSize;
  ULONG  *aam_RouterTable;
  LONG    aam_RouterTableSize;
  ULONG  *aam_DNSTable;
  LONG    aam_DNSTableSize;
  ULONG  *aam_StaticRouteTable;
  LONG    aam_StaticRouteTableSize;
  STRPTR  aam_HostName;
  LONG    aam_HostNameSize;
  STRPTR  aam_DomainName;
  LONG    aam_DomainNameSize;
  UBYTE  *aam_BOOTPMessage;
  LONG    aam_BOOTPMessageSize;
  struct DateStamp *aam_LeaseExpires;
  BOOL    aam_Unicast;
};

#define NG_A4(n) (((ULONG)(n) + 3) & ~3UL)

/* CreateAddrAllocMessageA (LVO -474). Returns 0 on success, else errno. */
LONG SAVEDS RAF6(_CreateAddrAllocMessageA,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			version,	d0,
		 LONG,			protocol,	d1,
		 STRPTR,		interface_name,	a0,
		 struct ng_aam **,	result_ptr,	a1,
		 struct TagItem *,	tags,		a2)
#if 0
{
#endif
  struct TagItem *tstate, *ti;
  struct ng_aam *aam;
  char *base;
  ULONG total;
  LONG timeout = 10, leasetime = 0, reqaddr = 0, unicast = 0;
  STRPTR clientid = NULL;
  LONG naksz = 0, routersz = 0, dnssz = 0, staticsz = 0, hostsz = 0, domainsz = 0, bootpsz = 0;
  LONG recordlease = 0, cidlen;
  struct MsgPort *replyport = NULL;

  if (result_ptr == NULL || interface_name == NULL) {
    writeErrnoValue(libPtr, EINVAL);
    return (EINVAL);
  }
  *result_ptr = NULL;
  if (version < AAM_VERSION_MIN_NG || version > AAM_VERSION_NG) {
    writeErrnoValue(libPtr, EINVAL);		/* AAMR_VersionUnknown territory */
    return (EINVAL);
  }

  for (tstate = tags; (ti = ng_nexttag(&tstate)) != NULL; ) {
    switch (ti->ti_Tag) {
    case CAAMTA_Timeout_NG:            timeout = (LONG)ti->ti_Data; break;
    case CAAMTA_LeaseTime_NG:          leasetime = (LONG)ti->ti_Data; break;
    case CAAMTA_RequestedAddress_NG:   reqaddr = (LONG)ti->ti_Data; break;
    case CAAMTA_ClientIdentifier_NG:   clientid = (STRPTR)ti->ti_Data; break;
    case CAAMTA_NAKMessageSize_NG:     naksz = (LONG)ti->ti_Data; break;
    case CAAMTA_RouterTableSize_NG:    routersz = (LONG)ti->ti_Data; break;
    case CAAMTA_DNSTableSize_NG:       dnssz = (LONG)ti->ti_Data; break;
    case CAAMTA_StaticRouteTableSize_NG: staticsz = (LONG)ti->ti_Data; break;
    case CAAMTA_HostNameSize_NG:       hostsz = (LONG)ti->ti_Data; break;
    case CAAMTA_DomainNameSize_NG:     domainsz = (LONG)ti->ti_Data; break;
    case CAAMTA_BOOTPMessageSize_NG:   bootpsz = (LONG)ti->ti_Data; break;
    case CAAMTA_RecordLeaseExpiration_NG: recordlease = (LONG)ti->ti_Data; break;
    case CAAMTA_ReplyPort_NG:          replyport = (struct MsgPort *)ti->ti_Data; break;
    case CAAMTA_RequestUnicast_NG:     unicast = (LONG)ti->ti_Data; break;
    default: break;
    }
  }
  if (timeout < 10)				/* enforced minimum per autodoc */
    timeout = 10;
  cidlen = clientid ? (strlen((char *)clientid) + 1) : 0;

  /* Single allocation: message + every requested result buffer, 4-byte aligned. */
  total = NG_A4(sizeof(struct ng_aam));
#define NG_RESV(field_off, bytes) do { field_off = total; total += NG_A4(bytes); } while (0)
  { ULONG cid_o, nak_o, rt_o, dns_o, st_o, hn_o, dn_o, bp_o, le_o;
    NG_RESV(cid_o, cidlen);
    NG_RESV(nak_o, naksz);
    NG_RESV(rt_o,  (ULONG)routersz * sizeof(ULONG));
    NG_RESV(dns_o, (ULONG)dnssz * sizeof(ULONG));
    NG_RESV(st_o,  (ULONG)staticsz * sizeof(ULONG));
    NG_RESV(hn_o,  hostsz);
    NG_RESV(dn_o,  domainsz);
    NG_RESV(bp_o,  bootpsz);
    NG_RESV(le_o,  recordlease ? sizeof(struct DateStamp) : 0);

    aam = AllocVec(total, MEMF_PUBLIC | MEMF_CLEAR);
    if (aam == NULL) {
      writeErrnoValue(libPtr, ENOMEM);
      return (ENOMEM);
    }
    base = (char *)aam;
    aam->aam_Message.mn_Node.ln_Type = NT_MESSAGE;
    aam->aam_Message.mn_Length = sizeof(struct ng_aam);
    aam->aam_Message.mn_ReplyPort = replyport;
    aam->aam_Version = version;
    aam->aam_Protocol = protocol;
    ng_strlcpy(aam->aam_InterfaceName, (char *)interface_name, sizeof(aam->aam_InterfaceName));
    aam->aam_Timeout = timeout;
    aam->aam_LeaseTime = (ULONG)leasetime;
    aam->aam_RequestedAddress = (ULONG)reqaddr;
    aam->aam_Unicast = (BOOL)(unicast != 0);
    if (cidlen)   { aam->aam_ClientIdentifier = (STRPTR)(base + cid_o); bcopy((caddr_t)clientid, base + cid_o, cidlen); }
    if (naksz)    { aam->aam_NAKMessage = (STRPTR)(base + nak_o); aam->aam_NAKMessageSize = naksz; }
    if (routersz) { aam->aam_RouterTable = (ULONG *)(base + rt_o); aam->aam_RouterTableSize = routersz; }
    if (dnssz)    { aam->aam_DNSTable = (ULONG *)(base + dns_o); aam->aam_DNSTableSize = dnssz; }
    if (staticsz) { aam->aam_StaticRouteTable = (ULONG *)(base + st_o); aam->aam_StaticRouteTableSize = staticsz; }
    if (hostsz)   { aam->aam_HostName = (STRPTR)(base + hn_o); aam->aam_HostNameSize = hostsz; }
    if (domainsz) { aam->aam_DomainName = (STRPTR)(base + dn_o); aam->aam_DomainNameSize = domainsz; }
    if (bootpsz)  { aam->aam_BOOTPMessage = (UBYTE *)(base + bp_o); aam->aam_BOOTPMessageSize = bootpsz; }
    if (recordlease) aam->aam_LeaseExpires = (struct DateStamp *)(base + le_o);
  }
#undef NG_RESV

  *result_ptr = aam;
  return (0);
}

/* DeleteAddrAllocMessage (LVO -480): free a CreateAddrAllocMessage() message. */
VOID SAVEDS RAF2(_DeleteAddrAllocMessage,
		 struct SocketBase *,	libPtr,	a6,
		 struct ng_aam *,	aam,	a0)
#if 0
{
#endif
  (void)libPtr;
  if (aam != NULL)
    FreeVec((APTR)aam);
}

/* ------------------------------------------------------------------------- *
 *  Roadshow internal configuration data -- Obtain / Release / Change.
 *
 *  Roadshow exposes a set of named stack tunables (ip.forwarding, tcp.mssdflt,
 *  udp.cksum, ...) as a read-only exec List of `struct RoadshowDataNode`, each
 *  node's rdn_Data pointing at the live variable. ObtainRoadshowData() builds
 *  that list, ChangeRoadshowData() writes through a node to the real variable,
 *  ReleaseRoadshowData() frees the list.
 *
 *  Every option below is wired to the ACTUAL AmiTCP global that governs the
 *  behaviour, so a config tool reading or tuning "tcp.sendspace" reads/writes
 *  the stack's real default socket buffer size. All are RDNT_Integer (signed
 *  32-bit). Options Roadshow documents but this stack has no variable for
 *  (ip.defttl, tcp.do_rfc1323, bpf.bufsize, ...) are simply absent from the
 *  list -- ChangeRoadshowData() then correctly reports ENOENT for them.
 *
 *  Concurrency: the autodoc says "only one caller can modify at a time." Each
 *  write is made atomic here with Forbid()/Permit() (safe on the cooperative,
 *  single-CPU stack); cross-caller write exclusivity is best-effort.
 * ------------------------------------------------------------------------- */
#define ORD_ReadAccess_NG	0
#define ORD_WriteAccess_NG	1
#define RDNT_Integer_NG		0
#define RDNF_ReadOnly_NG	(1 << 0)

struct RoadshowDataNode {
  struct MinNode rdn_MinNode;
  STRPTR  rdn_Name;
  UWORD   rdn_Flags;
  WORD    rdn_Type;
  ULONG   rdn_Length;
  APTR    rdn_Data;
};

/* Live stack tunables (defined across the BSD core). */
extern int    ipforwarding, ipsendredirects, subnetsarelocal, tcp_mssdflt, udpcksum;
extern u_long tcp_recvspace, tcp_sendspace, udp_recvspace, udp_sendspace;

struct ng_rsd_opt { const char *name; UWORD flags; void *data; };
static const struct ng_rsd_opt ng_rsd_opts[] = {
  { "ip.forwarding",      0, &ipforwarding    },
  { "ip.sendredirects",   0, &ipsendredirects },
  { "ip.subnetsarelocal", 0, &subnetsarelocal },
  { "tcp.mssdflt",        0, &tcp_mssdflt     },
  { "tcp.recvspace",      0, &tcp_recvspace   },
  { "tcp.sendspace",      0, &tcp_sendspace   },
  { "udp.cksum",          0, &udpcksum        },
  { "udp.recvspace",      0, &udp_recvspace   },
  { "udp.sendspace",      0, &udp_sendspace   },
};
#define NG_RSD_COUNT (sizeof(ng_rsd_opts) / sizeof(ng_rsd_opts[0]))

/* Returned pointer IS &rh_List (rh_List first) so Release/Change recover us. */
struct ng_rsd_handle {
  struct List rh_List;
  LONG        rh_Access;
  struct RoadshowDataNode rh_Nodes[NG_RSD_COUNT];
};

/* ASCII case-insensitive compare (option names are not case-sensitive). */
static int ng_rsd_casecmp(const char *a, const char *b)
{
  unsigned char ca, cb;
  for (;;) {
    ca = (unsigned char)*a++; cb = (unsigned char)*b++;
    if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    if (ca != cb) return (int)ca - (int)cb;
    if (ca == 0) return 0;
  }
}

/* ObtainRoadshowData (LVO -714): access in d0, returns struct List * in d0. */
struct List * SAVEDS RAF2(_ObtainRoadshowData,
			  struct SocketBase *,	libPtr,	a6,
			  LONG,			access,	d0)
#if 0
{
#endif
  struct ng_rsd_handle *h;
  struct List *l;
  ULONG i;

  if (access != ORD_ReadAccess_NG && access != ORD_WriteAccess_NG) {
    writeErrnoValue(libPtr, EINVAL);
    return (NULL);
  }
  h = AllocVec(sizeof(*h), MEMF_PUBLIC | MEMF_CLEAR);
  if (h == NULL) {
    writeErrnoValue(libPtr, ENOMEM);
    return (NULL);
  }
  h->rh_Access = access;
  l = &h->rh_List;
  l->lh_Head     = (struct Node *)&l->lh_Tail;		/* NewList() */
  l->lh_Tail     = NULL;
  l->lh_TailPred = (struct Node *)&l->lh_Head;

  for (i = 0; i < NG_RSD_COUNT; i++) {
    struct RoadshowDataNode *n = &h->rh_Nodes[i];
    struct Node *nd = (struct Node *)&n->rdn_MinNode;
    n->rdn_Name   = (STRPTR)ng_rsd_opts[i].name;
    n->rdn_Flags  = ng_rsd_opts[i].flags;
    n->rdn_Type   = RDNT_Integer_NG;
    n->rdn_Length = 4;
    n->rdn_Data   = ng_rsd_opts[i].data;
    nd->ln_Pred = l->lh_TailPred;			/* AddTail() */
    nd->ln_Succ = (struct Node *)&l->lh_Tail;
    l->lh_TailPred->ln_Succ = nd;
    l->lh_TailPred = nd;
  }
  return (l);
}

/* ReleaseRoadshowData (LVO -720): list in d0 (per autodoc, NOT a0). */
VOID SAVEDS RAF2(_ReleaseRoadshowData,
		 struct SocketBase *,	libPtr,	a6,
		 struct List *,		list,	d0)
#if 0
{
#endif
  (void)libPtr;
  if (list != NULL)
    FreeVec((APTR)list);			/* list == &handle->rh_List */
}

/* ChangeRoadshowData (LVO -726): list a0, name a1, length d0, data a2 -> BOOL. */
BOOL SAVEDS RAF5(_ChangeRoadshowData,
		 struct SocketBase *,	libPtr,	a6,
		 struct List *,		list,	a0,
		 STRPTR,		name,	a1,
		 ULONG,			length,	d0,
		 APTR,			data,	a2)
#if 0
{
#endif
  struct ng_rsd_handle *h = (struct ng_rsd_handle *)list;
  struct RoadshowDataNode *n = NULL;
  ULONG i;

  if (list == NULL || name == NULL || data == NULL) {
    writeErrnoValue(libPtr, EINVAL);
    return (FALSE);
  }
  if (h->rh_Access != ORD_WriteAccess_NG) {
    writeErrnoValue(libPtr, EACCES);		/* obtained read-only */
    return (FALSE);
  }
  for (i = 0; i < NG_RSD_COUNT; i++) {
    if (ng_rsd_casecmp((char *)h->rh_Nodes[i].rdn_Name, (char *)name) == 0) {
      n = &h->rh_Nodes[i];
      break;
    }
  }
  if (n == NULL) {
    writeErrnoValue(libPtr, ENOENT);		/* no such option */
    return (FALSE);
  }
  if (n->rdn_Flags & RDNF_ReadOnly_NG) {
    writeErrnoValue(libPtr, EPERM);
    return (FALSE);
  }
  if (length != n->rdn_Length) {
    writeErrnoValue(libPtr, ENOSPC);		/* wrong size for this option */
    return (FALSE);
  }
  Forbid();
  bcopy((caddr_t)data, (caddr_t)n->rdn_Data, length);
  Permit();
  return (TRUE);
}

/* ------------------------------------------------------------------------- *
 *  Kernel mbuf access -- mbuf_get/gethdr/free/freem/copym/copyback/copydata/
 *  cat/adj/prepend/pullup.
 *
 *  Roadshow exposes the BSD kernel mbuf routines so an IP-filter hook can build
 *  and edit packet buffers. Each vector is documented as "functionally identical
 *  to the BSD kernel routine m_*()", so every one here is a thin forward to the
 *  corresponding AmiTCP m_* function -- allocations use M_DONTWAIT/MT_DATA (the
 *  "returns NULL on no memory" semantic rules out the blocking M_WAIT).
 *
 *  We forward-declare the m_* prototypes locally rather than #include <sys/mbuf.h>
 *  (which is unguarded and re-drags <sys/malloc.h>, already included above); the
 *  wrappers only ever pass mbufs by opaque pointer, never touching their fields.
 *
 *  CAVEAT (matches the autodoc): these are meant to run in kernel/IP-filter-hook
 *  context. The success/failure LONG results follow the documented convention
 *  (0 == ok); mbuf_copyback/copydata forward to void kernel routines, so a rare
 *  OOM mid-copyback is not surfaced as an error here (the underlying m_copyback
 *  is itself void) -- documented rather than papered over.
 * ------------------------------------------------------------------------- */
#define NG_M_DONTWAIT	1		/* == M_NOWAIT */
#define NG_MT_DATA	1		/* == MT_DATA  */
struct mbuf;
extern struct mbuf *m_get(int, int);
extern struct mbuf *m_gethdr(int, int);
extern struct mbuf *m_free(struct mbuf *);
extern void         m_freem(struct mbuf *);
extern struct mbuf *m_prepend(struct mbuf *, int, int);
extern struct mbuf *m_copym(struct mbuf *, int, int, int);
extern void         m_copydata(struct mbuf *, int, int, caddr_t);
extern void         m_copyback(struct mbuf *, int, int, caddr_t);
extern void         m_cat(struct mbuf *, struct mbuf *);
extern void         m_adj(struct mbuf *, int);
extern struct mbuf *m_pullup(struct mbuf *, int);

/* mbuf_get (LVO -654). */
struct mbuf * SAVEDS RAF1(_mbuf_get,
			  struct SocketBase *, libPtr, a6)
#if 0
{
#endif
  (void)libPtr;
  return (m_get(NG_M_DONTWAIT, NG_MT_DATA));
}

/* mbuf_gethdr (LVO -660). */
struct mbuf * SAVEDS RAF1(_mbuf_gethdr,
			  struct SocketBase *, libPtr, a6)
#if 0
{
#endif
  (void)libPtr;
  return (m_gethdr(NG_M_DONTWAIT, NG_MT_DATA));
}

/* mbuf_free (LVO -642): free one mbuf, return its successor. */
struct mbuf * SAVEDS RAF2(_mbuf_free,
			  struct SocketBase *,	libPtr,	a6,
			  struct mbuf *,	m,	a0)
#if 0
{
#endif
  (void)libPtr;
  if (m == NULL)
    return (NULL);
  return (m_free(m));
}

/* mbuf_freem (LVO -648): free an entire chain. */
VOID SAVEDS RAF2(_mbuf_freem,
		 struct SocketBase *,	libPtr,	a6,
		 struct mbuf *,		m,	a0)
#if 0
{
#endif
  (void)libPtr;
  if (m != NULL)
    m_freem(m);
}

/* mbuf_prepend (LVO -666): prepend space; on OOM the chain is freed, NULL back. */
struct mbuf * SAVEDS RAF3(_mbuf_prepend,
			  struct SocketBase *,	libPtr,	a6,
			  struct mbuf *,	m,	a0,
			  LONG,			len,	d0)
#if 0
{
#endif
  (void)libPtr;
  if (m == NULL)
    return (NULL);
  return (m_prepend(m, (int)len, NG_M_DONTWAIT));
}

/* mbuf_pullup (LVO -684): make len bytes contiguous at the chain head. */
struct mbuf * SAVEDS RAF3(_mbuf_pullup,
			  struct SocketBase *,	libPtr,	a6,
			  struct mbuf *,	m,	a0,
			  LONG,			len,	d0)
#if 0
{
#endif
  (void)libPtr;
  if (m == NULL)
    return (NULL);
  return (m_pullup(m, (int)len));
}

/* mbuf_copym (LVO -624): copy len bytes starting at offset into a new chain. */
struct mbuf * SAVEDS RAF4(_mbuf_copym,
			  struct SocketBase *,	libPtr,	a6,
			  struct mbuf *,	m,	a0,
			  LONG,			off,	d0,
			  LONG,			len,	d1)
#if 0
{
#endif
  (void)libPtr;
  if (m == NULL)
    return (NULL);
  return (m_copym(m, (int)off, (int)len, NG_M_DONTWAIT));
}

/* mbuf_adj (LVO -678): trim length bytes (head if +ve, tail if -ve). */
LONG SAVEDS RAF3(_mbuf_adj,
		 struct SocketBase *,	libPtr,	a6,
		 struct mbuf *,		m,	a0,
		 LONG,			len,	d0)
#if 0
{
#endif
  (void)libPtr;
  if (m == NULL)
    return (-1L);
  m_adj(m, (int)len);
  return (0L);
}

/* mbuf_cat (LVO -672): append second_chain onto first_chain (consumes second). */
LONG SAVEDS RAF3(_mbuf_cat,
		 struct SocketBase *,	libPtr,	a6,
		 struct mbuf *,		first,	a0,
		 struct mbuf *,		second,	a1)
#if 0
{
#endif
  (void)libPtr;
  if (first == NULL || second == NULL)
    return (-1L);
  m_cat(first, second);
  return (0L);
}

/* mbuf_copyback (LVO -630): copy length bytes from data into the chain at offset,
 * extending it if needed. (Underlying m_copyback is void -> OOM not surfaced.) */
LONG SAVEDS RAF5(_mbuf_copyback,
		 struct SocketBase *,	libPtr,	a6,
		 struct mbuf *,		m,	a0,
		 LONG,			off,	d0,
		 LONG,			len,	d1,
		 caddr_t,		data,	a1)
#if 0
{
#endif
  (void)libPtr;
  if (m == NULL || data == NULL)
    return (-1L);
  m_copyback(m, (int)off, (int)len, data);
  return (0L);
}

/* mbuf_copydata (LVO -636): copy length bytes at offset out of the chain to data. */
LONG SAVEDS RAF5(_mbuf_copydata,
		 struct SocketBase *,	libPtr,	a6,
		 struct mbuf *,		m,	a0,
		 LONG,			off,	d0,
		 LONG,			len,	d1,
		 caddr_t,		data,	a1)
#if 0
{
#endif
  (void)libPtr;
  if (m == NULL || data == NULL)
    return (-1L);
  m_copydata(m, (int)off, (int)len, data);
  return (0L);
}

/* ------------------------------------------------------------------------- *
 *  DHCP / BOOTP client -- BeginInterfaceConfig() / AbortInterfaceConfig().
 *
 *  The asynchronous half of the address-allocation API. BeginInterfaceConfig()
 *  spawns a helper Process that runs the BOOTP (RFC 951) / DHCP (RFC 2131)
 *  exchange over an already-created SANA-II interface, applies the obtained
 *  address/mask/routers/DNS to the stack, fills the AddressAllocationMessage
 *  result fields, and ReplyMsg()s it back to the caller's port. It is a real
 *  client: the helper opens bsdsocket.library and drives our own public API
 *  (socket/bind/sendto/recvfrom + Configure/AddRoute/AddDomainNameServer),
 *  i.e. it dogfoods the stack exactly as a third-party DHCP tool would.
 *  AbortInterfaceConfig() flags an in-flight exchange to stop early.
 *
 *  DHCP on an as-yet-unnumbered interface: the interface is brought up at
 *  0.0.0.0, and the client sends its DISCOVER to the limited broadcast address
 *  255.255.255.255:67 with the BOOTP broadcast flag set, so the server's reply
 *  comes back as a broadcast our 0.0.0.0 interface can receive (this also dodges
 *  the "unicast OFFER not seen in promiscuous mode" class of problems).
 * ------------------------------------------------------------------------- */
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/dos.h>	/* CreateNewProcTags(), Delay() -- see amiga_main.c/amiga_log.c */

/* AddressAllocationMessage result codes / protocols (libraries/bsdsocket.h). */
#define AAMR_Success		0
#define AAMR_Aborted		1
#define AAMR_InterfaceNotKnown	2
#define AAMR_InterfaceWrongType	3
#define AAMR_AddressKnown	4
#define AAMR_VersionUnknown	5
#define AAMR_NoMemory		6
#define AAMR_Timeout		7
#define AAMR_AddressInUse	8
#define AAMR_AddrChangeFailed	9
#define AAMR_MaskChangeFailed	10
#define AAMR_Busy		11
#define AAMP_BOOTP		0
#define AAMP_DHCP		1

/* BOOTP/DHCP wire format. */
#define DHCP_SERVER_PORT	67
#define DHCP_CLIENT_PORT	68
#define BOOTREQUEST		1
#define BOOTREPLY		2
#define HTYPE_ETHER		1
#define DHCP_MAGIC		0x63825363UL
#define BOOTP_BCAST_FLAG	0x8000
/* DHCP message types (option 53). */
#define DHCPDISCOVER	1
#define DHCPOFFER	2
#define DHCPREQUEST	3
#define DHCPACK		5
#define DHCPNAK		6
/* DHCP options. */
#define DHO_SUBNET_MASK		1
#define DHO_ROUTERS		3
#define DHO_DNS			6
#define DHO_HOSTNAME		12
#define DHO_DOMAIN		15
#define DHO_REQUESTED_ADDR	50
#define DHO_LEASE_TIME		51
#define DHO_MSG_TYPE		53
#define DHO_SERVER_ID		54
#define DHO_PARAM_REQ		55
#define DHO_CLIENT_ID		61
#define DHO_END			255

struct dhcp_pkt {
  UBYTE  op, htype, hlen, hops;
  ULONG  xid;
  UWORD  secs, flags;
  ULONG  ciaddr, yiaddr, siaddr, giaddr;
  UBYTE  chaddr[16];
  UBYTE  sname[64];
  UBYTE  file[128];
  UBYTE  options[312];		/* magic cookie + TLV options */
};

struct ng_dhcp_ctx {
  struct ng_aam *		dc_aam;
  struct Task *			dc_parent;	/* signalled when helper has ctx */
  volatile LONG			dc_abort;
};

/* Socket-level constants the helper needs (public bsdsocket.library values). */
#define NG_AF_INET	2
#define NG_SOCK_DGRAM	2
#define NG_SO_BROADCAST	0x0020
#define NG_SOL_SOCKET	0xffff
#define NG_FIONBIO	0x8004667EUL
#define IFC_State_Up	3

/* --- inline public-API callers (helper runs as an ordinary client task) --- */
static long d_socket(struct Library *sb, long d, long t, long p) {
  register long _d0 __asm("d0")=d; register long _d1 __asm("d1")=t;
  register long _d2 __asm("d2")=p; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-30)":"=r"(_d0):"r"(_d0),"r"(_d1),"r"(_d2),"r"(_a6):"a0","a1","memory");
  return _d0;
}
static long d_bind(struct Library *sb, long s, void *n, long l) {
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=n;
  register long _d1 __asm("d1")=l; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-36)":"=r"(_d0):"r"(_d0),"r"(_a0),"r"(_d1),"r"(_a6):"a1","memory");
  return _d0;
}
static long d_sendto(struct Library *sb, long s, void *b, long l, long f, void *to, long tl) {
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=b;
  register long _d1 __asm("d1")=l; register long _d2 __asm("d2")=f;
  register void *_a1 __asm("a1")=to; register long _d3 __asm("d3")=tl;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-60)":"=r"(_d0):"r"(_d0),"r"(_a0),"r"(_d1),"r"(_d2),"r"(_a1),"r"(_d3),"r"(_a6):"memory");
  return _d0;
}
static long d_recvfrom(struct Library *sb, long s, void *b, long l, long f, void *a, void *al) {
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=b;
  register long _d1 __asm("d1")=l; register long _d2 __asm("d2")=f;
  register void *_a1 __asm("a1")=a; register void *_a2 __asm("a2")=al;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-72)":"=r"(_d0):"r"(_d0),"r"(_a0),"r"(_d1),"r"(_d2),"r"(_a1),"r"(_a2),"r"(_a6):"memory");
  return _d0;
}
static long d_setsockopt(struct Library *sb, long s, long lv, long on, void *v, long vl) {
  register long _d0 __asm("d0")=s; register long _d1 __asm("d1")=lv;
  register long _d2 __asm("d2")=on; register void *_a0 __asm("a0")=v;
  register long _d3 __asm("d3")=vl; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-90)":"=r"(_d0):"r"(_d0),"r"(_d1),"r"(_d2),"r"(_a0),"r"(_d3),"r"(_a6):"a1","memory");
  return _d0;
}
static long d_ioctl(struct Library *sb, long s, unsigned long r, void *a) {
  register long _d0 __asm("d0")=s; register unsigned long _d1 __asm("d1")=r;
  register void *_a0 __asm("a0")=a; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-114)":"=r"(_d0):"r"(_d0),"r"(_d1),"r"(_a0),"r"(_a6):"a1","memory");
  return _d0;
}
static void d_closesocket(struct Library *sb, long s) {
  register long _d0 __asm("d0")=s; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_a6):"d1","a0","a1","memory");
}
static long d_configiface(struct Library *sb, void *name, void *tags) {  /* ConfigureInterfaceTagList -450 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=name;
  register void *_a1 __asm("a1")=tags; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-450)":"=r"(_d0),"+r"(_a0),"+r"(_a1):"r"(_a6):"d1","memory");
  return _d0;
}
static long d_addroute(struct Library *sb, void *tags) {  /* AddRouteTagList -414 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=tags;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-414)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
static long d_adddns(struct Library *sb, void *addrstr) {  /* AddDomainNameServer -516 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=addrstr;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-516)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
static long d_queryiface(struct Library *sb, void *name, void *tags) {  /* QueryInterfaceTagList -468 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=name;
  register void *_a1 __asm("a1")=tags; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-468)":"=r"(_d0),"+r"(_a0),"+r"(_a1):"r"(_a6):"d1","memory");
  return _d0;
}

/* Append a TLV option; returns the advanced write pointer. */
static UBYTE *dhcp_put(UBYTE *p, UBYTE code, UBYTE len, const void *val)
{
  int i;
  *p++ = code; *p++ = len;
  for (i = 0; i < len; i++) *p++ = ((const UBYTE *)val)[i];
  return p;
}

/* Find option `code` in a received packet; returns value ptr + sets *len, or NULL. */
static UBYTE *dhcp_find(struct dhcp_pkt *pkt, UBYTE code, int *len)
{
  UBYTE *p = pkt->options + 4;			/* skip magic cookie */
  UBYTE *end = pkt->options + sizeof(pkt->options);
  while (p < end && *p != DHO_END) {
    UBYTE c = *p++;
    UBYTE l;
    if (c == 0) continue;			/* pad */
    if (p >= end) break;
    l = *p++;
    if (c == code) { if (len) *len = l; return p; }
    p += l;
  }
  return NULL;
}

/* Build a sockaddr_in (network order == native on m68k). */
struct ng_sain { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE pad[8]; };
static void ng_sain_set(struct ng_sain *s, ULONG addr, UWORD port)
{
  int i; for (i = 0; i < (int)sizeof(*s); i++) ((char *)s)[i] = 0;
  s->sin_len = sizeof(*s); s->sin_family = NG_AF_INET;
  s->sin_port = port; s->sin_addr = addr;
}

/* Format a dotted-quad into buf (for the string-based config vectors). */
static void ng_ip2str(ULONG a, char *buf)
{
  UBYTE o[4]; int i, n = 0;
  o[0]=(a>>24)&0xff; o[1]=(a>>16)&0xff; o[2]=(a>>8)&0xff; o[3]=a&0xff;
  for (i = 0; i < 4; i++) {
    UBYTE v = o[i]; char t[3]; int k = 0;
    if (v >= 100) t[k++] = '0'+v/100;
    if (v >= 10)  t[k++] = '0'+(v/10)%10;
    t[k++] = '0'+v%10;
    { int j; for (j = 0; j < k; j++) buf[n++] = t[j]; }
    buf[n++] = (i < 3) ? '.' : 0;
  }
}

#ifdef NG_DHCP_DEBUG
/* TEMP DIAGNOSTIC (build with -DNG_DHCP_DEBUG): append "<label> <decimal>\n" to
 * SYS:dhcp.log so the DORA exchange can be traced on real hardware. The helper is a
 * Process (it uses Delay(), so DOS is available) -- direct file I/O is safe here. */
static void d_dbg(const char *label, long v)
{
  BPTR f = Open((STRPTR)"SYS:dhcp.log", MODE_READWRITE);
  if (f) {
    char b[16]; int n = 0; unsigned long u; int neg = 0;
    long len = 0; while (label[len]) len++;
    Seek(f, 0, OFFSET_END);
    Write(f, (APTR)label, len);
    Write(f, (APTR)" ", 1);
    if (v < 0) { neg = 1; u = (unsigned long)(-v); } else u = (unsigned long)v;
    { char t[12]; int k = 0;
      if (u == 0) t[k++] = '0';
      while (u) { t[k++] = '0' + (int)(u % 10); u /= 10; }
      if (neg) b[n++] = '-';
      while (k) b[n++] = t[--k]; }
    b[n++] = '\n';
    Write(f, (APTR)b, n);
    Close(f);
  }
}
#define D_LOG(label, v) d_dbg((label), (long)(v))
#else
#define D_LOG(label, v)
#endif

/* --- RFC 3927 IPv4 link-local (ZeroConf) acquisition --- */
#define LL_NET_BASE      0xA9FE0000UL	/* 169.254.0.0, network == host order on 68k */
#define LL_PROBE_NUM     3
#define LL_PROBE_WAIT    1		/* s: initial random delay 0..PROBE_WAIT */
#define LL_PROBE_MIN     1		/* s: min gap between probes */
#define LL_PROBE_MAX     2		/* s: max gap between probes */
#define LL_ANNOUNCE_NUM  2
#define LL_ANNOUNCE_WAIT 2		/* s: after last clean probe, before claiming */
#define LL_ANNOUNCE_INT  2		/* s: gap between announcements */
#define LL_MAX_CONFLICTS 10		/* attempts before giving up (background retry takes over) */
#define LL_TICKS         50		/* Amiga Delay() ticks per second */
#define LL_RETRY_MIN     60		/* s: first background DHCP-retry interval */
#define LL_RETRY_MAX     1800		/* s: retry backoff cap (30 min) */
#define LL_RETRY_DORA    8		/* s: DORA budget per background retry */

/* Advance a small LCG (the classic glibc constants). Its low bits are weak, so
 * callers take from the middle/high bits. */
static ULONG ng_ll_rand(ULONG *seed)
{
  return (*seed = *seed * 1103515245UL + 12345UL);
}

/*
 * When no DHCP server answers, self-assign a link-local address in
 * 169.254.1.0 - 169.254.254.255. Probe a MAC-seeded pseudo-random candidate by
 * ARP (sender 0.0.0.0); if a peer already owns it, pick another; once one
 * probes clean, assign it as a /16 and announce it with gratuitous ARPs.
 *
 * Runs in the DHCP helper's own Process. All splimp()-sensitive work is inside
 * the ng_ll_* primitives, and we never hold a lock across a Delay() -- that is
 * what lets the network task run in_arpinput() and set the conflict flag we
 * poll for between probes. Returns TRUE and writes *out (network order) on
 * success; honours ctx->dc_abort between waits. Bounded at LL_MAX_CONFLICTS
 * attempts (a persistent background retry is a later phase).
 */
static BOOL
ng_linklocal_acquire(struct Library *sb, const char *ifname,
		     const UBYTE *mac, struct ng_dhcp_ctx *ctx, ULONG *out)
{
  ULONG seed, rnd, cand;
  int conflicts, p;
  struct TagItem ctags[4];
  char ipstr[16];

  /* Seed from the MAC so the first pick is stable across reboots (RFC 3927
   * s2.1); MACs are unique, so two hosts do not share a seed. */
  seed = ((ULONG)mac[0] << 24) ^ ((ULONG)mac[1] << 16) ^ ((ULONG)mac[2] << 8)
       ^  (ULONG)mac[3] ^ ((ULONG)mac[4] << 13) ^ ((ULONG)mac[5] << 5);
  if (seed == 0)
    seed = LL_NET_BASE | 1;		/* never seed the LCG with 0 */

  for (conflicts = 0; conflicts <= LL_MAX_CONFLICTS; conflicts++) {
    if (ctx->dc_abort)
      return FALSE;

    rnd = ng_ll_rand(&seed);
    cand = LL_NET_BASE
	 | ((ULONG)(1 + ((rnd >> 16) % 254)) << 8)	/* host 1..254 */
	 |  (ULONG)((rnd >> 8) & 0xff);			/* host 0..255 */

    if (ng_ll_arm(ifname, cand) != 0)			/* interface vanished */
      return FALSE;
    /* Initial random delay 0..PROBE_WAIT, spl dropped so ARP can be received. */
    Delay(ng_ll_rand(&seed) % (LL_PROBE_WAIT * LL_TICKS + 1));

    for (p = 0; p < LL_PROBE_NUM; p++) {
      if (ctx->dc_abort) { ng_ll_disarm(ifname); return FALSE; }
      if (p)						/* arm already sent probe #0 */
	ng_ll_send_probe(ifname);
      Delay(LL_PROBE_MIN * LL_TICKS
	    + (ng_ll_rand(&seed) % ((LL_PROBE_MAX - LL_PROBE_MIN) * LL_TICKS + 1)));
      if (ng_ll_conflicted(ifname))
	break;
    }
    if (ng_ll_conflicted(ifname)) {			/* someone owns it */
      ng_ll_disarm(ifname);
      continue;						/* try a new address */
    }

    /* Probed clean. Wait ANNOUNCE_WAIT, re-checking for a late conflict. */
    Delay(LL_ANNOUNCE_WAIT * LL_TICKS);
    if (ng_ll_conflicted(ifname)) { ng_ll_disarm(ifname); continue; }

    /* Claim it: assign 169.254.x.y/16 and bring the interface up. */
    ng_ip2str(cand, ipstr);
    ctags[0].ti_Tag = IFC_Address; ctags[0].ti_Data = (ULONG)ipstr;
    ctags[1].ti_Tag = IFC_NetMask; ctags[1].ti_Data = (ULONG)"255.255.0.0";
    ctags[2].ti_Tag = IFC_State;   ctags[2].ti_Data = IFC_State_Up;
    ctags[3].ti_Tag = 0;
    if (d_configiface(sb, (void *)ifname, ctags) != 0) { ng_ll_disarm(ifname); return FALSE; }

    /* Record the bound address and announce it (commit sends the first). */
    ng_ll_commit(ifname, cand);
    for (p = 1; p < LL_ANNOUNCE_NUM; p++) {
      Delay(LL_ANNOUNCE_INT * LL_TICKS);
      ng_ll_send_announce(ifname);
    }
    *out = cand;
    return TRUE;
  }
  return FALSE;			/* all candidates conflicted (background retry: later phase) */
}

/* One DHCP exchange outcome. */
#define NG_DHCP_TIMEOUT 0
#define NG_DHCP_GOT     1
#define NG_DHCP_ABORT   2

/* What a won lease carries (all network byte order, 0 = absent). */
struct ng_lease { ULONG addr, serverid, mask, router, dns0, lease; };

/*
 * Run one DISCOVER/OFFER/REQUEST/ACK exchange on the already-bound socket s,
 * for up to `deadline` ~0.2s poll ticks. On DHCPACK, fills *L and returns
 * NG_DHCP_GOT (rx holds the ACK on return); on ctx->dc_abort returns
 * NG_DHCP_ABORT; otherwise NG_DHCP_TIMEOUT. Touches neither the interface nor
 * the aam -- the caller applies the result. Factored out of ng_dhcp_task so the
 * background DHCP-retry loop can reuse the identical exchange.
 */
static int
ng_dhcp_exchange(struct Library *sb, long s, struct dhcp_pkt *tx,
		 struct dhcp_pkt *rx, const UBYTE *mac, ULONG xid,
		 long deadline, struct ng_dhcp_ctx *ctx, struct ng_lease *L)
{
  struct ng_sain to;
  long i, r, sent, msgtype;

  ng_sain_set(&to, 0xFFFFFFFFUL, DHCP_SERVER_PORT);
  L->addr = L->serverid = L->mask = L->router = L->dns0 = L->lease = 0;
  sent = 0; msgtype = DHCPDISCOVER;
  for (i = 0; i < deadline; i++) {
    if (ctx->dc_abort) return NG_DHCP_ABORT;
    if ((i % 20) == 0) {			/* (re)transmit every ~4s */
      UBYTE *o; UBYTE bt;
      for (r = 0; r < (long)sizeof(*tx); r++) ((char *)tx)[r] = 0;
      tx->op = BOOTREQUEST; tx->htype = HTYPE_ETHER; tx->hlen = 6;
      tx->xid = xid; tx->flags = BOOTP_BCAST_FLAG;
      if (msgtype == DHCPREQUEST) tx->ciaddr = 0;
      for (r = 0; r < 6; r++) tx->chaddr[r] = mac[r];
      o = tx->options;
      *o++ = (DHCP_MAGIC>>24)&0xff; *o++ = (DHCP_MAGIC>>16)&0xff;
      *o++ = (DHCP_MAGIC>>8)&0xff;  *o++ = DHCP_MAGIC&0xff;
      bt = (UBYTE)msgtype; o = dhcp_put(o, DHO_MSG_TYPE, 1, &bt);
      { UBYTE cid[7]; cid[0]=HTYPE_ETHER; for(r=0;r<6;r++) cid[1+r]=mac[r];
        o = dhcp_put(o, DHO_CLIENT_ID, 7, cid); }
      if (msgtype == DHCPREQUEST) {
        o = dhcp_put(o, DHO_REQUESTED_ADDR, 4, &L->addr);
        o = dhcp_put(o, DHO_SERVER_ID, 4, &L->serverid);
      }
      { UBYTE prl[4]; prl[0]=DHO_SUBNET_MASK; prl[1]=DHO_ROUTERS; prl[2]=DHO_DNS; prl[3]=DHO_DOMAIN;
        o = dhcp_put(o, DHO_PARAM_REQ, 4, prl); }
      *o++ = DHO_END;
      r = d_sendto(sb, s, tx, sizeof(*tx), 0, &to, sizeof(to));
      D_LOG((msgtype == DHCPDISCOVER) ? "tx_discover_r" : "tx_request_r", r);
      sent++;
    }
    r = d_recvfrom(sb, s, rx, sizeof(*rx), 0, (void*)0, (void*)0);
    if (r >= 0) D_LOG("rx_r", r);
    if (r >= 240 && rx->xid == xid && rx->op == BOOTREPLY) {
      UBYTE *mt = dhcp_find(rx, DHO_MSG_TYPE, (int*)0);
      int t = mt ? *mt : 0;
      if (msgtype == DHCPDISCOVER && t == DHCPOFFER) {
        UBYTE *sid = dhcp_find(rx, DHO_SERVER_ID, (int*)0);
        L->addr = rx->yiaddr;
        if (sid) L->serverid = ((ULONG)sid[0]<<24)|((ULONG)sid[1]<<16)|((ULONG)sid[2]<<8)|sid[3];
        msgtype = DHCPREQUEST; i = -1;		/* restart timing for REQUEST */
        continue;
      }
      if (msgtype == DHCPREQUEST && t == DHCPACK) {
        UBYTE *op; int ol;
        L->addr = rx->yiaddr;
        if ((op = dhcp_find(rx, DHO_SUBNET_MASK, &ol)) && ol>=4)
          L->mask = ((ULONG)op[0]<<24)|((ULONG)op[1]<<16)|((ULONG)op[2]<<8)|op[3];
        if ((op = dhcp_find(rx, DHO_ROUTERS, &ol)) && ol>=4)
          L->router = ((ULONG)op[0]<<24)|((ULONG)op[1]<<16)|((ULONG)op[2]<<8)|op[3];
        if ((op = dhcp_find(rx, DHO_DNS, &ol)) && ol>=4)
          L->dns0 = ((ULONG)op[0]<<24)|((ULONG)op[1]<<16)|((ULONG)op[2]<<8)|op[3];
        if ((op = dhcp_find(rx, DHO_LEASE_TIME, &ol)) && ol>=4)
          L->lease = ((ULONG)op[0]<<24)|((ULONG)op[1]<<16)|((ULONG)op[2]<<8)|op[3];
        return NG_DHCP_GOT;
      }
      if (msgtype == DHCPREQUEST && t == DHCPNAK) { msgtype = DHCPDISCOVER; L->addr = 0; i = -1; continue; }
    }
    Delay(10);					/* ~0.2s */
  }
  D_LOG("timeout_sent", sent);
  return NG_DHCP_TIMEOUT;
}

/*
 * Apply a won lease to the interface: address (+ mask), default route, DNS.
 * Returns 0 on success, -1 if the address change itself failed (route/DNS are
 * then skipped, matching the original inline behaviour).
 */
static int
ng_apply_lease(struct Library *sb, const char *ifname, const struct ng_lease *L)
{
  struct TagItem ctags[4], rtags[2];
  char ipstr[16], maskstr[16], gwstr[16], dnsstr[16];
  int i;

  ng_ip2str(L->addr, ipstr);
  ctags[0].ti_Tag = IFC_Address; ctags[0].ti_Data = (ULONG)ipstr;
  i = 1;
  if (L->mask) { ng_ip2str(L->mask, maskstr); ctags[i].ti_Tag = IFC_NetMask; ctags[i].ti_Data = (ULONG)maskstr; i++; }
  ctags[i].ti_Tag = IFC_State; ctags[i].ti_Data = IFC_State_Up; i++;
  ctags[i].ti_Tag = 0;
  if (d_configiface(sb, (void *)ifname, ctags) != 0)
    return -1;
  if (L->router) {
    ng_ip2str(L->router, gwstr);
    rtags[0].ti_Tag = RTA_DefaultGateway; rtags[0].ti_Data = (ULONG)gwstr;
    rtags[1].ti_Tag = 0;
    (void)d_addroute(sb, rtags);
  }
  /*
   * REPLACE (not append) the dynamic DNS servers on every lease. Each DHCP
   * configuration -- initial, renewal, or a fresh one after Online/Offline --
   * flushes the previously DHCP/runtime-added servers first, then installs this
   * lease's. Without this, repeated Online/Offline cycles pile up a duplicate DNS
   * server every time (and however many the server hands out). Statically
   * configured servers (from the config file, nsn_Dynamic == 0) are preserved.
   */
  ng_flush_dynamic_nameservers();
  if (L->dns0) { ng_ip2str(L->dns0, dnsstr); (void)d_adddns(sb, dnsstr); }
  return 0;
}

/* TRUE only if the interface is still Up AND still carries `addr` (network
 * order). Used to detect the operator reconfiguring the interface out from
 * under the background retry loop -- whether by changing its address OR by
 * taking it Offline (down) -- so the loop bows out instead of fighting them
 * (its restore step would otherwise re-Up an interface the operator downed). */
static BOOL
ng_iface_has_addr(struct Library *sb, const char *ifname, ULONG addr)
{
  struct sockaddr_in sin;
  LONG state = 0;
  struct TagItem q[3];
  int i;

  for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
  q[0].ti_Tag = IFQ_Address; q[0].ti_Data = (ULONG)&sin;
  q[1].ti_Tag = IFQ_State;   q[1].ti_Data = (ULONG)&state;
  q[2].ti_Tag = 0;
  if (d_queryiface(sb, (void *)ifname, q) != 0)
    return FALSE;
  return (BOOL)(sin.sin_addr.s_addr == addr && state == IFC_State_Up);
}

/*
 * Background DHCP-retry loop, entered after link-local has been assigned and
 * the caller has already been told the interface is up (see ng_dhcp_task). Keep
 * retrying DHCP with exponential backoff; each attempt briefly unnumbers the
 * interface to 0.0.0.0 (RFC 2131 requires a DISCOVER's source be 0.0.0.0) and,
 * on failure, re-restores the SAME link-local address (we still own it, so no
 * re-probe). On a lease it applies it and returns -- DHCP has won. Exits
 * promptly on stack shutdown (ng_stack_running) or if the operator has
 * reconfigured the interface away from our link-local address.
 *
 * Runs detached (the aam is already replied), so it can no longer be reached by
 * AbortInterfaceConfig; ng_stack_running is the shutdown signal, polled each 1 s
 * sleep chunk. The OpenLibrary() reference held in sb keeps bsdsocket.library
 * from expunging while we still have work in flight.
 */
static void
ng_dhcp_background(struct Library *sb, long s, struct dhcp_pkt *tx,
		   struct dhcp_pkt *rx, const char *ifname, const UBYTE *mac,
		   ULONG llad, struct ng_dhcp_ctx *ctx)
{
  extern volatile BOOL ng_stack_running;
  ULONG backoff = LL_RETRY_MIN;
  char ipstr[16];
  struct TagItem ctags[4], rtags[3];

  while (ng_stack_running && !ctx->dc_abort) {
    ULONG waited;

    /* Sleep `backoff` seconds in 1 s chunks so shutdown is noticed fast. */
    for (waited = 0; waited < backoff; waited++) {
      Delay(LL_TICKS);
      if (!ng_stack_running || ctx->dc_abort) return;
    }
    /* If the operator reconfigured the interface, stop -- don't fight them. */
    if (!ng_iface_has_addr(sb, ifname, llad))
      return;

    /* Unnumber to 0.0.0.0 for a compliant DISCOVER source; stop defending the
     * link-local address while we are not using it, and re-post the limited
     * broadcast route the exchange needs. */
    ng_ll_disarm(ifname);
    ctags[0].ti_Tag = IFC_Address; ctags[0].ti_Data = (ULONG)"0.0.0.0";
    ctags[1].ti_Tag = IFC_State;   ctags[1].ti_Data = IFC_State_Up;
    ctags[2].ti_Tag = 0;
    (void)d_configiface(sb, (void *)ifname, ctags);
    rtags[0].ti_Tag = RTA_DestinationHost; rtags[0].ti_Data = (ULONG)"255.255.255.255";
    rtags[1].ti_Tag = RTA_Gateway;         rtags[1].ti_Data = (ULONG)"0.0.0.0";
    rtags[2].ti_Tag = 0;
    (void)d_addroute(sb, rtags);

    {
      struct ng_lease L;
      int rc = ng_dhcp_exchange(sb, s, tx, rx, mac, (ULONG)ctx ^ 0x52455452UL,
				(long)LL_RETRY_DORA * 5, ctx, &L);
      if (rc == NG_DHCP_GOT) {
	D_LOG("bg_dhcp_ok", L.addr & 0xffff);
	(void)ng_apply_lease(sb, ifname, &L);	/* upgraded to a real lease -- done */
	return;
      }
    }

    /* Still no server: restore the SAME link-local address (no re-probe -- we
     * already own it) and announce it again, then back off further. */
    ng_ip2str(llad, ipstr);
    ctags[0].ti_Tag = IFC_Address; ctags[0].ti_Data = (ULONG)ipstr;
    ctags[1].ti_Tag = IFC_NetMask; ctags[1].ti_Data = (ULONG)"255.255.0.0";
    ctags[2].ti_Tag = IFC_State;   ctags[2].ti_Data = IFC_State_Up;
    ctags[3].ti_Tag = 0;
    (void)d_configiface(sb, (void *)ifname, ctags);
    (void)ng_ll_commit(ifname, llad);		/* re-arm defense + announce */

    backoff <<= 1;
    if (backoff > LL_RETRY_MAX)
      backoff = LL_RETRY_MAX;
  }
}

/* The helper Process: run the exchange, apply the result, ReplyMsg the aam. */
static SAVEDS void ng_dhcp_task(void)
{
  struct Process *me = (struct Process *)FindTask(NULL);
  struct ng_dhcp_ctx *ctx = (struct ng_dhcp_ctx *)me->pr_Task.tc_UserData;
  struct ng_aam *aam;
  struct Library *sb;
  struct dhcp_pkt *tx = NULL, *rx = NULL;
  UBYTE mac[16], macbuf[16];
  char ifname[16];
  long s = -1, i, one = 1, deadline;
  ULONG xid;
  struct ng_sain from;
  struct TagItem qtags[3], ctags[4], rtags[3];

  Signal(ctx->dc_parent, SIGBREAKF_CTRL_F);	/* parent may release the spawn lock */
  aam = ctx->dc_aam;

  sb = OpenLibrary((STRPTR)"bsdsocket.library", 3L);
  if (sb == NULL) { aam->aam_Result = AAMR_NoMemory; goto reply; }

  if (aam->aam_Version < 1 || aam->aam_Version > 2) { aam->aam_Result = AAMR_VersionUnknown; goto reply; }
  for (i = 0; i < 15 && aam->aam_InterfaceName[i]; i++) ifname[i] = aam->aam_InterfaceName[i];
  ifname[i] = 0;

  tx = AllocVec(sizeof(*tx), MEMF_PUBLIC | MEMF_CLEAR);
  rx = AllocVec(sizeof(*rx), MEMF_PUBLIC | MEMF_CLEAR);
  if (tx == NULL || rx == NULL) { aam->aam_Result = AAMR_NoMemory; goto reply; }

  /* Hardware address for chaddr. */
  for (i = 0; i < 16; i++) mac[i] = macbuf[i] = 0;
  qtags[0].ti_Tag = IFQ_HardwareAddress; qtags[0].ti_Data = (ULONG)macbuf;
  qtags[1].ti_Tag = 0;
  if (d_queryiface(sb, ifname, qtags) != 0) { aam->aam_Result = AAMR_InterfaceNotKnown; goto reply; }
  for (i = 0; i < 6; i++) mac[i] = macbuf[i];
  D_LOG("start_mac", ((long)mac[0]<<8)|mac[5]);   /* first+last MAC byte, non-zero => got a MAC */

  /* Bring the interface up, unnumbered (0.0.0.0), so we can broadcast. */
  ctags[0].ti_Tag = IFC_Address; ctags[0].ti_Data = (ULONG)"0.0.0.0";
  ctags[1].ti_Tag = IFC_State;   ctags[1].ti_Data = IFC_State_Up;
  ctags[2].ti_Tag = 0;
  (void)d_configiface(sb, ifname, ctags);	/* best effort; may already be up */

  s = d_socket(sb, NG_AF_INET, NG_SOCK_DGRAM, 0);
  if (s < 0) { aam->aam_Result = AAMR_NoMemory; goto reply; }
  d_setsockopt(sb, s, NG_SOL_SOCKET, NG_SO_BROADCAST, &one, sizeof(one));
  ng_sain_set(&from, 0, DHCP_CLIENT_PORT);
  if (d_bind(sb, s, &from, sizeof(from)) < 0) { aam->aam_Result = AAMR_AddrChangeFailed; goto reply; }
  d_ioctl(sb, s, NG_FIONBIO, &one);		/* non-blocking AFTER bind (as udptest) */
  /* Route the limited broadcast (255.255.255.255) out this interface. Gateway
   * 0.0.0.0 == the unnumbered interface itself, i.e. a link route. */
  rtags[0].ti_Tag = RTA_DestinationHost; rtags[0].ti_Data = (ULONG)"255.255.255.255";
  rtags[1].ti_Tag = RTA_Gateway;         rtags[1].ti_Data = (ULONG)"0.0.0.0";
  rtags[2].ti_Tag = 0;
  (void)d_addroute(sb, rtags);

  xid = (ULONG)ctx ^ 0x414d4954UL;		/* 'AMIT' ^ ctx -- unique enough */
  deadline = (long)aam->aam_Timeout; if (deadline < 10) deadline = 10;
  deadline *= 5;				/* poll ticks of ~0.2s */

  /* Initial DHCP attempt. */
  {
    struct ng_lease L;
    int rc = ng_dhcp_exchange(sb, s, tx, rx, mac, xid, deadline, ctx, &L);
    if (rc == NG_DHCP_ABORT) { aam->aam_Result = AAMR_Aborted; goto reply; }
    if (rc == NG_DHCP_GOT) {
      if (ng_apply_lease(sb, ifname, &L) != 0) { aam->aam_Result = AAMR_AddrChangeFailed; goto reply; }
      aam->aam_Address = L.addr;
      aam->aam_ServerAddress = L.serverid;
      aam->aam_SubnetMask = L.mask;
      aam->aam_LeaseTime = L.lease;
      aam->aam_RequestedAddress = 0;
      if (aam->aam_RouterTable && aam->aam_RouterTableSize >= 1) aam->aam_RouterTable[0] = L.router;
      if (aam->aam_DNSTable && aam->aam_DNSTableSize >= 1) aam->aam_DNSTable[0] = L.dns0;
      if (aam->aam_BOOTPMessage && aam->aam_BOOTPMessageSize > 0) {
        long n = aam->aam_BOOTPMessageSize; if (n > (long)sizeof(*rx)) n = sizeof(*rx);
        bcopy((caddr_t)rx, (caddr_t)aam->aam_BOOTPMessage, n);
      }
      aam->aam_Result = AAMR_Success;
      goto reply;
    }
  }

  /* No DHCP server answered -> RFC 3927 link-local (ZeroConf) fallback. */
  {
    ULONG llad = 0;
    if (ng_linklocal_acquire(sb, ifname, mac, ctx, &llad)) {
      D_LOG("linklocal_ok", llad & 0xffff);
      aam->aam_Address = llad;
      aam->aam_ServerAddress = 0;		/* self-assigned, no server */
      aam->aam_SubnetMask = 0xFFFF0000UL;	/* 255.255.0.0 */
      aam->aam_LeaseTime = 0;			/* link-local has no lease */
      aam->aam_RequestedAddress = 0;
      if (aam->aam_RouterTable && aam->aam_RouterTableSize >= 1)
	aam->aam_RouterTable[0] = 0;		/* link-local has no default route */
      if (aam->aam_DNSTable && aam->aam_DNSTableSize >= 1)
	aam->aam_DNSTable[0] = 0;
      aam->aam_Result = AAMR_Success;
      /* The interface is up on link-local; tell the caller now, then keep
       * running to retry DHCP in the background and upgrade if a server
       * appears. Detach first so AbortInterfaceConfig can no longer reach us. */
      Forbid();
      aam->aam_Reserved = 0;
      ReplyMsg((struct Message *)aam);
      Permit();
      ng_dhcp_background(sb, s, tx, rx, ifname, mac, llad, ctx);
      goto cleanup;
    }
  }
  aam->aam_Result = ctx->dc_abort ? AAMR_Aborted : AAMR_Timeout;

reply:
  Forbid();
  aam->aam_Reserved = 0;			/* detach: Abort can no longer reach us */
  ReplyMsg((struct Message *)aam);
  Permit();
cleanup:
  if (s >= 0) d_closesocket(sb, s);
  if (tx) FreeVec(tx);
  if (rx) FreeVec(rx);
  if (sb) CloseLibrary(sb);
  FreeVec(ctx);
}

/* BeginInterfaceConfig (LVO -486): kick off the async exchange. */
VOID SAVEDS RAF2(_BeginInterfaceConfig,
		 struct SocketBase *,	libPtr,	a6,
		 struct ng_aam *,	aam,	a0)
#if 0
{
#endif
  struct ng_dhcp_ctx *ctx;
  struct Process *proc;

  (void)libPtr;
  if (aam == NULL)
    return;

  ctx = AllocVec(sizeof(*ctx), MEMF_PUBLIC | MEMF_CLEAR);
  if (ctx == NULL) {			/* cannot even run: reply NoMemory */
    aam->aam_Result = AAMR_NoMemory;
    ReplyMsg((struct Message *)aam);
    return;
  }
  ctx->dc_aam = aam;
  ctx->dc_parent = FindTask(NULL);
  ctx->dc_abort = 0;

  Forbid();				/* hand the ctx to the helper before it runs */
  proc = CreateNewProcTags(NP_Entry, (LONG)&ng_dhcp_task,
			   NP_Name, (LONG)"AmiTCP_NG DHCP",
			   NP_Priority, 0,
			   NP_StackSize, 8192,
			   TAG_DONE, 0);
  if (proc != NULL)
    proc->pr_Task.tc_UserData = (APTR)ctx;	/* safe: child is blocked by Forbid */
  else
    aam->aam_Reserved = 0;
  aam->aam_Reserved = proc ? (LONG)ctx : 0;	/* Abort finds ctx here */
  Permit();

  if (proc == NULL) {			/* spawn failed: reply NoMemory now */
    FreeVec(ctx);
    aam->aam_Result = AAMR_NoMemory;
    ReplyMsg((struct Message *)aam);
    return;
  }
  /* Wait for the helper to take the ctx (it signals CTRL_F). */
  Wait(SIGBREAKF_CTRL_F);
}

/* AbortInterfaceConfig (LVO -492): request an in-flight exchange to stop. */
VOID SAVEDS RAF2(_AbortInterfaceConfig,
		 struct SocketBase *,	libPtr,	a6,
		 struct ng_aam *,	aam,	a0)
#if 0
{
#endif
  (void)libPtr;
  if (aam == NULL)
    return;
  Forbid();
  if (aam->aam_Reserved != 0)		/* still running -> flag the helper */
    ((struct ng_dhcp_ctx *)aam->aam_Reserved)->dc_abort = 1;
  Permit();
}
