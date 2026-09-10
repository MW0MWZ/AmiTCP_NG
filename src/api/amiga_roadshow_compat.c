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
 * amiga_roadshow_compat.c --- Roadshow bsdsocket.library COMPATIBILITY layer.
 *
 * This is our OWN, clean-room code that implements Roadshow's PUBLISHED extension
 * ABI so Roadshow's config tools can drive our stack. It contains NO Roadshow
 * source -- the "roadshow" in the name means "compatible with", not "copied from".
 * (Roadshow itself is Olaf Barthel's closed-source product; see the attribution above.)
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

#include <ng_hostname.h>
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
#include <net/ng_ifconfig.h>
#include <net/sana2arp.h>	/* RFC 3927 link-local ARP primitives (ng_ll_*) */

#include <utility/tagitem.h>

#include <kern/amiga_includes.h>
#include <kern/amiga_netdb.h>

#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <api/amiga_raf.h>
#include <api/dns_cache.h>		/* ng_dnscache_flush() on a name-server change */

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

/*
 * Interface configuration memory and DNS ownership. The configuring paths record
 * what they applied; when a SANA-II device reports itself back online,
 * net/if_sana.c calls ng_reconfigure_interface() to apply it again. Defined
 * further down, beside the DHCP client they share machinery with.
 */
void ng_reconfigure_interface(const char *ifname);
void ng_iface_reconfig_done(const char *ifname);
void ng_flush_dynamic_nameservers_for(const char *ifname);
static int  ng_ifname(char *buf, struct ifnet *ifp);	/* defined further down */
static void ng_canon_ifname(const char *in, char *out);
static int  ng_add_owned_nameserver(const char *address, const char *owner);

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
 *  Returns the next socket with asynchronous events pending, removes those
 *  events from it, and stores them through eventsp. -1 means "nothing pending",
 *  which is how an application knows to stop draining.
 *
 *  The events themselves are recorded by soraise_event() (kern/uipc_socket2.c)
 *  on the same wakeup paths select() uses, for sockets that opted in with
 *  setsockopt(SO_EVENTMASK); the application is told to look by the signal it
 *  named with SocketBaseTags(SBTC_SIGEVENTMASK).
 *
 *  Scanning the descriptor table rather than keeping a ready-queue is deliberate.
 *  The event is recorded against a `struct socket`, but this call must return a
 *  DESCRIPTOR, and descriptors are per-SocketBase -- the same socket can be fd 5
 *  in the task that opened it and absent from every other base. The table is the
 *  only place that mapping exists, it is small (dTableSize, 64 by default), and
 *  the scan happens once per delivered signal rather than per packet.
 *
 *  FD_ACCEPT is deliberately NOT re-armed here. A bitmask cannot hold two
 *  pending connections, and re-arming at collection time while so_qlen stayed
 *  non-zero would mean an application that drains "until -1" before it calls
 *  accept() never sees -1 -- a hang. The documented per-connection accounting
 *  is honoured in _accept() instead (api/amiga_syscalls.c), which re-raises
 *  FD_ACCEPT once if the queue is still non-empty after a connection is taken
 *  off it. That is exactly one re-arm per accept() call, so it satisfies both
 *  the accept-once-per-signal client and the drain-in-a-loop client, with no
 *  way to spin.
 * ------------------------------------------------------------------------- */

LONG SAVEDS RAF2(_GetSocketEvents,
		 struct SocketBase *,	libPtr,		a6,
		 ULONG *,		eventsp,	a0)
#if 0
{
#endif
  register struct socket *so;
  register int fd;
  ULONG events;
  LONG result = -1;
  spl_t s;

  CHECK_TASK();

  /*
   * eventsp is written on every success. A caller that passes NULL cannot be
   * told WHICH events fired, so consuming them would destroy the only copy --
   * refuse instead of silently discarding. (EFAULT rather than EINVAL: there is
   * no MMU here, so an unusable pointer is exactly what this reports.)
   */
  if (eventsp == NULL) {
    writeErrnoValue(libPtr, EFAULT);
    return (-1);
  }

  ObtainSyscallSemaphore(libPtr);
  /*
   * splnet() against the net task: so_events is written by soraise_event() from
   * interrupt/net context, and this reads-and-clears it.
   */
  s = splnet();

  for (fd = 0; fd < libPtr->dTableSize; fd++) {
    so = libPtr->dTable[fd];
    if (so == NULL || so->so_events == 0)
      continue;

    events = so->so_events;
    so->so_events = 0;
    *eventsp = events;
    result = (LONG)fd;
    break;
  }

  splx(s);
  ReleaseSyscallSemaphore(libPtr);

  if (result < 0)
    *eventsp = 0;		/* documented empty answer */
  return (result);
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
  NG_ENSURE_STACK();
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
  NG_ENSURE_STACK();
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
  NG_ENSURE_STACK();
  char tmp[16];
  int len;

  /* Guard NULL pointers before any access: on this no-MMU 68k a NULL dst would
   * make the bcopy below a wild write into the CPU exception-vector table at
   * address 0, and a NULL src a garbage read of the same. Fail cleanly instead. */
  if (src == NULL || dst == NULL) {
    writeErrnoValue(libPtr, EINVAL);
    return (NULL);
  }
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
  NG_CHECK_DEAD(0);
  NG_ENSURE_STACK();
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
  NG_CHECK_DEAD(0);
  NG_ENSURE_STACK();
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
  NG_CHECK_DEAD(-1);
  NG_ENSURE_STACK();
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
  /* Unowned: this vector says nothing about which interface the server belongs
   * to, so nothing withdraws it automatically. Callers that DO know use the
   * NGCT_NameServer configuration tag instead. */
  nsn->nsn_Owner[0] = '\0';
  nsn->nsn_Ent.ns_addr = ns_addr;

  LOCK_W_NDB(NDB);
  AddTail((struct List *)&NDB->ndb_NameServers, (struct Node *)nsn);
  UNLOCK_NDB(NDB);

  /* A new server may answer differently from the ones already listed, so what is
   * cached is no longer necessarily what would be resolved now. Outside the NDB
   * lock: the flush takes the cache's own lock, and the two are never nested. */
  ng_dnscache_flush();
  return (0);
}


/*
 * One canonical spelling of an interface name, so ownership can be compared.
 *
 * The same interface is called different things by different callers: a
 * DEVS:NetInterfaces file (and so the AddressAllocationMessage) says "smoke",
 * while a name built from the ifnet says "smoke0". ifunit() resolves both to the
 * same interface, plain string comparison does not -- and that mismatch made the
 * offline teardown find none of its own name servers, leaving them behind and
 * then duplicating them on the next lease. An unknown name is copied through
 * unchanged: it simply matches nothing, which is the safe direction.
 */
static void
ng_canon_ifname(const char *in, char *out)
{
  struct ifnet *ifp;
  int i = 0;
  spl_t s;

  out[0] = '\0';
  if (in == NULL)
    return;
  s = splimp();
  if ((ifp = ifunit((char *)in)) != NULL) {
    ng_ifname(out, ifp);
    splx(s);
    return;
  }
  splx(s);
  for (; i < IFNAMSIZ - 1 && in[i]; i++) out[i] = in[i];
  out[i] = '\0';
}

/*
 * Add a dynamic name server OWNED by `owner`.
 *
 * The public AddDomainNameServer() vector has no interface argument, so a server
 * added through it belongs to nobody and is never withdrawn automatically --
 * which is its documented behaviour. This is the internal form, used by the
 * NGCT_NameServer configuration tag, so that ownership is established by the same
 * locked call that configures the interface rather than by a global read back
 * across separate library calls.
 */
static int
ng_add_owned_nameserver(const char *address, const char *owner)
{
  struct in_addr ns_addr;
  struct NameserventNode *nsn;
  char canon[IFNAMSIZ];
  int i;

  if (address == NULL || !inet_aton(address, &ns_addr))
    return EINVAL;
  if ((nsn = bsd_malloc(sizeof(*nsn), M_NETDB, M_WAITOK)) == NULL)
    return ENOBUFS;

  ng_canon_ifname(owner, canon);
  nsn->nsn_EntSize = sizeof(nsn->nsn_Ent);
  nsn->nsn_Dynamic = 1;
  for (i = 0; i < (int)sizeof(nsn->nsn_Owner) - 1 && canon[i]; i++)
    nsn->nsn_Owner[i] = canon[i];
  nsn->nsn_Owner[i] = '\0';
  nsn->nsn_Ent.ns_addr = ns_addr;

  LOCK_W_NDB(NDB);
  AddTail((struct List *)&NDB->ndb_NameServers, (struct Node *)nsn);
  UNLOCK_NDB(NDB);

  /* This is the DHCP / interface-configuration route, so it is the one that
   * matters most: it fires exactly when the machine has just joined a network
   * whose resolver may answer the same names differently. */
  ng_dnscache_flush();
  return 0;
}

/* RemoveDomainNameServer (LVO -522): remove the first server matching address. */
LONG SAVEDS RAF2(_RemoveDomainNameServer,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		address,	a0)
#if 0
{
#endif
  NG_CHECK_DEAD(-1);
  NG_ENSURE_STACK();
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

  /* Only when something actually went: a failed removal changed nothing, and
   * throwing the cache away for it would let a caller repeatedly asking to remove
   * a server that is not there keep the cache permanently empty. */
  ng_dnscache_flush();
  return (0);
}

/*
 * Remove every runtime-added (dynamic) DNS server, leaving statically configured
 * ones (loaded from the config file, nsn_Dynamic == 0) untouched. Called when an
 * interface goes offline (net/if_sana.c) so a stale DHCP-supplied resolver config
 * does not linger past the link it came from. Not a public library vector -- an
 * internal helper -- so it takes no SocketBase and sets no errno.
 */

/* Case-sensitive name compare; interface names are generated, not typed. */
static int
ng_name_eq(const char *a, const char *b)
{
  int i;
  if (a == NULL || b == NULL) return 0;
  for (i = 0; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
  return a[i] == b[i];
}

void
ng_flush_dynamic_nameservers_for(const char *ifname)
{
  struct NameserventNode *nsn, *next;
  char want[IFNAMSIZ];			/* the name spelled the one canonical way */
  int  removed = 0;

  ng_canon_ifname(ifname, want);

  if (NDB == NULL)
    return;

  LOCK_W_NDB(NDB);
  for (nsn = (struct NameserventNode *)NDB->ndb_NameServers.mlh_Head;
       nsn->nsn_Node.mln_Succ != NULL;
       nsn = next) {
    next = (struct NameserventNode *)nsn->nsn_Node.mln_Succ;
    /* Only this interface's servers when a name is given. Flushing every dynamic
     * server on any interface going offline took name resolution away from the
     * interfaces still up. A NULL name keeps the old whole-list behaviour for the
     * paths that genuinely mean it (a lease REPLACING its own servers, shutdown). */
    if (nsn->nsn_Dynamic &&
	(ifname == NULL || ng_name_eq(nsn->nsn_Owner, want))) {
      Remove((struct Node *)nsn);
      bsd_free(nsn, M_NETDB);
      removed++;
    }
  }
  UNLOCK_NDB(NDB);

  /*
   * This is the "last interface came down" case as well as the per-interface one:
   * answers obtained through servers that have just been withdrawn must not
   * survive them. Only when something was actually removed -- this is called on
   * every offline, including for interfaces that never supplied a server, and
   * flushing on those would empty the cache for the interfaces still up.
   */
  if (removed)
    ng_dnscache_flush();
}

/* Flush EVERY dynamic server, whoever owns it. For a lease replacing its own set
 * and for shutdown -- not for one interface going offline. */
void
ng_flush_dynamic_nameservers(void)
{
  ng_flush_dynamic_nameservers_for(NULL);
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
 *  IoctlSocket(), and the same one the UDP round-trip test proves end to end.
 *  Address tag
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
#define IFC_MTU			(IFC_BASE + 6)	/* LONG interface MTU (B)  */
#define IFC_State		(IFC_BASE + 8)	/* LONG, one of SM_*       */
/* Ownership declarations. Values from the SDK header, not guessed. A caller sets
 * these to say the interface owns its default route / its name servers, so they
 * are removed when it goes down or offline (bsdsocket.h: IFC_BASE+12, +13). */
#define IFC_AssociatedRoute	(IFC_BASE + 12)	/* BOOL */
#define IFC_AssociatedDNS	(IFC_BASE + 13)	/* BOOL */

/*
 * AmiTCP_NG-private creation tags for AddInterfaceTagList(): the SANA-II receive /
 * transmit request-pool sizes, from the interface config's iprequests= /
 * writerequests= keys. A private TAG_USER range well outside Roadshow's
 * IFC_/CAAMTA_/IFA_/... ranges -- these only pass between AmiTCP_NG's own
 * AddNetInterface command and this library. Absent / 0 = use the RAM-tiered
 * default (net/sana2config.c). MUST match the definitions in src/tools/ng_lvo.h.
 */
#define NGCT_IPRequests		(TAG_USER + 0x004E4701)	/* LONG receive requests */
#define NGCT_WriteRequests	(TAG_USER + 0x004E4702)	/* LONG send requests    */
#define NGCT_TcpSendspace	(TAG_USER + 0x004E4703)	/* LONG TCP send buffer  */
#define NGCT_TcpRecvspace	(TAG_USER + 0x004E4704)	/* LONG TCP recv buffer  */
#define NGCT_TcpMssdflt		(TAG_USER + 0x004E4705)	/* LONG off-subnet MSS cap */
#define NGCT_LinkSpeed		(TAG_USER + 0x004E4706)	/* LONG bits/sec, overrides S2_DEVICEQUERY BPS */
/*
 * A name server this interface provides, dotted-decimal STRPTR, repeatable.
 * Exists because AddDomainNameServer() has no interface argument: ownership has
 * to be established by the call that configures the interface, as ONE locked
 * operation. Carrying it in a global set by one library call and read by later
 * ones -- what this replaced -- misattributes servers the moment two interfaces
 * are configured at once, and leaks the attribution to unrelated callers after.
 */
#define NGCT_NameServer		(TAG_USER + 0x004E4707)	/* STRPTR, repeatable */

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
    case IFC_MTU: {
      /*
       * Set the interface MTU. The SANA-II read buffers were posted at the device's
       * hardware MTU when the interface attached, so the MTU may only be LOWERED from
       * there (a smaller MTU just fragments sooner; the device cannot carry larger
       * frames). Clamp to the device's reported maximum and to the IPv4 minimum (68);
       * ignore anything sillier. if_mtu is a short.
       *
       * The ceiling is the SANA softc's ss_maxmtu -- cached once at attach and NEVER
       * mutated -- NOT the live ifp->if_mtu: this call is shared with the public
       * ConfigureInterfaceTagList LVO, so using the live (possibly already-lowered)
       * if_mtu as its own ceiling would ratchet the MTU down permanently and make a
       * later "raise it back to 1500" silently clamp to the lowered value. For a
       * non-SANA interface (loopback) there is no device ceiling, so honour as given.
       */
      long m = (long)ti->ti_Data;
      /*
       * PORT (AmiTCP_NG) security fix: the non-SANA ceiling was 0, and the
       * clamp below is gated on `ceil > 0`, so for the loopback interface it
       * was skipped ENTIRELY and any value up to 0x7FFFFFFF reached the naked
       * (short) cast -- ti_Data is a ULONG under the caller's control, so
       * ConfigureInterfaceTagList("lo0", {IFC_MTU, 100000}) stored
       * (short)100000 == -31072. if_mtu is a SIGNED short, and a negative MTU
       * propagates into the MSS and fragmentation arithmetic. Use SHRT_MAX as
       * the ceiling when the interface has no device limit of its own.
       */
      long ceil = (ifp->if_type == IFT_SANA)
		  ? (long)((struct sana_softc *)ifp)->ss_maxmtu : 32767;
      if (ceil > 0 && m > ceil) m = ceil;
      if (m >= 68)
	ifp->if_mtu = (short)m;
      break;
    }
    case NGCT_NameServer:
      /* Owned by this interface, added inside the same call that configures it,
       * so the owner never depends on what another task is doing meanwhile. */
      if (ti->ti_Data)
	e = ng_add_owned_nameserver((const char *)ti->ti_Data, ifname);
      break;
    case IFC_AssociatedRoute:
    case IFC_AssociatedDNS: {
      /* Ownership is a property of the interface, so it is recorded on the softc
       * rather than applied through an ioctl. Only SANA interfaces have one; the
       * loopback cannot own a default route or a name server. */
      if (ifp->if_type == IFT_SANA) {
	struct sana_softc *ssc = (struct sana_softc *)ifp;
	if (ti->ti_Tag == IFC_AssociatedRoute) {
	  ssc->ss_assoc_route = ti->ti_Data ? 1 : 0;	/* informational: nothing reads this now --
					 * offline purges an interface's routes
					 * unconditionally, because it frees the
					 * ifaddrs they point at. Kept because the
					 * Roadshow tag must still be accepted. */
	} else {
	  /* Purely a declaration. The servers themselves arrive as NGCT_NameServer
	   * tags in this same call, so there is no window in which "who owns what"
	   * depends on ordering between separate library calls. */
	  ssc->ss_assoc_dns = ti->ti_Data ? 1 : 0;
	}
      }
      break;
    }
    case IFC_State: {
      struct ifreq ifr;
      short flags = ifp->if_flags;
      extern int sana_device_online(struct ifnet *ifp, int online);

      /*
       * SM_Online and SM_Offline are SM_Up and SM_Down plus the device command,
       * per the SDK -- and the order matters in each direction.
       *
       * Going up, the device command comes FIRST and a failure stops everything:
       * "if the command succeeds, the other necessary configuration operations
       * will take place. If it fails, then this function will return with an
       * error code set and no further configuration will have been done." An
       * interface marked up over a device that refused to come online would just
       * be a lie.
       *
       * Going down, the interface is taken down FIRST and the device offlined
       * after, so nothing is still trying to transmit through a device on its way
       * out.
       *
       * Driving the device from here is also what makes this path independent of
       * whether the driver reports anything: we know it worked because we did it.
       */
      if (ti->ti_Data == NG_SM_Online && ifp->if_type == IFT_SANA) {
	if ((e = sana_device_online(ifp, 1)) != 0)
	  break;				/* refused: change nothing */
      }

      ng_ifr_init(&ifr, ifname);
      if (ti->ti_Data == NG_SM_Up || ti->ti_Data == NG_SM_Online)
	flags |= IFF_UP;			/* bring the interface up */
      else
	flags &= ~IFF_UP;			/* take it down */
      ifr.ifr_flags = flags;
      e = ifioctl(so, SIOCSIFFLAGS, (caddr_t)&ifr);

      if (ti->ti_Data == NG_SM_Offline && ifp->if_type == IFT_SANA) {
	(void)sana_device_online(ifp, 0);	/* down first, then offline */

	/*
	 * Deconfigure it, exactly as a device that vanished on its own is
	 * deconfigured.
	 *
	 * These two ways of going offline used to leave the interface in
	 * visibly different states. A device that dropped out had its address
	 * scrubbed, its associated route and name servers withdrawn, the DNS
	 * cache emptied and its blocked sockets woken; an operator asking for
	 * the same thing got none of it -- the interface went down still
	 * advertising an address it could not use, still offering name servers
	 * that could not be reached, with applications waiting on it. The
	 * interface ends up in the same place either way, so it should get there
	 * the same way.
	 *
	 * OFFLINE ONLY, not DOWN, and the SDK's own wording is why. It defines
	 * the four states as: Offline "not ready to receive and transmit data";
	 * Down "not ready ... but might still be ONLINE"; Up "ready ... but not
	 * necessarily online" (libraries/bsdsocket.h). Down/Up are about the
	 * interface, Offline/Online about the device underneath it -- so tearing
	 * the configuration down belongs to the device-level pair. It is also the
	 * only pair that can put it back: SM_Online reconfigures the interface
	 * from its config file, where SM_Up does not, so scrubbing on Down would
	 * leave a Down/Up cycle unable to restore itself. (What the SDK does NOT say anywhere is when
	 * an IFC_AssociatedRoute / IFC_AssociatedDNS claim is redeemed; the tags
	 * are documented only as "that interface is associated with a route/DNS".
	 * Doing it here is our reading, not a citation.)
	 *
	 * Done through ss_offcleanup, the same flag the involuntary path sets,
	 * rather than by calling the teardown here: it must run in the network
	 * task, because it touches the routing table and address lists and then
	 * blocks on the NetDataBase semaphore to withdraw the name servers.
	 * Sharing the flag also means the two paths cannot drift apart again.
	 *
	 * Only when the interface actually went down (e == 0). Deconfiguring an
	 * interface that is still up would be the worse half of the old bug.
	 * ss_wantback is deliberately NOT set: that flag means "the device left
	 * against our wishes, watch for its return", and this is the opposite.
	 * Coming back is SM_Online's job, and it reconfigures from the config file.
	 */
	if (e == 0) {
	  spl_t sp = splimp();
	  ((struct sana_softc *)ifp)->ss_offcleanup = 1;
	  splx(sp);
	}
      }

      /* Coming up through this path, put the configuration back ourselves rather
       * than waiting for an event the driver may never send. */
      if (e == 0 && ti->ti_Data == NG_SM_Online && ifp->if_type == IFT_SANA)
	((struct sana_softc *)ifp)->ss_reconfig = 1;
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
extern struct ifnet *sana_add_interface(char *ifname, char *devname, long devunit,
					long ipreq, long wreq, long bps);

/*
 * ng_speed_window -- the TCP window we WANT for a link of the given speed, in bytes.
 *
 * The window that fills a link without overshooting is its bandwidth-delay product
 * (bandwidth x RTT); more than that just queues data in the path and hurts loss
 * recovery. RAM only sets the CEILING (how big a window the mbuf pool can back, via
 * ng_ram_tier); the LINK sets the target, and the caller clamps target down to the
 * ceiling. Values are anchored on real-hardware testing (512 KB tested fastest on a
 * ~100 Mbit PiStorm WiFi link). Returns 0 for an unknown/zero baud rate, which tells
 * the caller "I can't tell -- keep the RAM default". if_baudrate comes from the SANA-II
 * S2_DEVICEQUERY BPS field; a driver that leaves it 0 simply falls back to the RAM tier.
 */
static u_long
ng_speed_window(u_long bps)
{
  /* MSS-aligned (n x 1460), matching the RAM tiers: keeps a full number of segments and
   * lets the ~64 KB value stay under the 16-bit window (44*1460=64240) so it needs no
   * window-scale shift, while 512 KB / 1 MB coincide exactly with the RAM-tier values. */
  if (bps == 0)                  return 0;		/* unknown -> keep RAM default */
  if (bps <=  12000000UL)        return  44UL * 1460;	/* 64240  (~64 KB):  ~10 Mbit (A2065, PLIP) */
  if (bps <= 128000000UL)        return 359UL * 1460;	/* 524140 (~512 KB): ~100 Mbit (wifipi)     */
  return 718UL * 1460;					/* 1048280 (~1 MB):  gigabit+ (genet)       */
}

/*
 * Sticky: set once any interface supplies an explicit tcp.sendspace=/recvspace=. It
 * makes an explicit override beat the link-speed auto-tune on EVERY subsequent interface
 * (not just the last one configured), matching the documented "a config override always
 * wins". Persists until the stack is torn down (overrides persist until reboot anyway).
 */
static BOOL ng_tcp_user_override = FALSE;

/*
 * The if_baudrate of the most recently added interface, published so GetNetStatus DEBUG
 * can show what link speed the auto-tune actually read from the driver (0 = the driver
 * reports none, so the RAM ceiling was used). Queried via SBTC_NG_LINK_SPEED.
 */
u_long ng_last_if_baudrate = 0;

/*
 * ng_effective_window -- the effective default TCP window (bytes) for a link of the
 * given baud, applying the SAME policy as the global auto-tune below (the sndsp/rcvsp
 * block in AddInterfaceTagList): an explicit user tcp.sendspace/recvspace override
 * wins; otherwise the link-speed target (ng_speed_window) clamped to the FIXED RAM
 * ceiling captured at boot; otherwise (unknown/zero baud) the current RAM-tier default.
 *
 * Exported so net/sana2config.c can size the SANA-II request rings (read + write) on
 * the SAME window the sockets get -- the rings are sized in ssconfig() (inside
 * iface_make()) which runs BEFORE the global auto-tune has stored its value, so the
 * rings must derive the window from THIS interface's if_baudrate directly rather than
 * reading the not-yet-updated tcp_sendspace/recvspace globals.
 */
u_long
ng_effective_window(u_long baud)
{
  extern u_long tcp_sendspace, tcp_recvspace, ng_ram_ceiling;
  u_long want, ceil;
  u_long def = (tcp_sendspace > tcp_recvspace) ? tcp_sendspace : tcp_recvspace;

  if (ng_tcp_user_override)		/* a config override forces the window */
    return def;
  want = ng_speed_window(baud);
  if (want == 0)			/* driver reports no speed -> keep the RAM default */
    return def;
  ceil = ng_ram_ceiling ? ng_ram_ceiling : def;
  if (want > ceil) want = ceil;		/* RAM stays the ceiling */
  return want;
}

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
  long ipreq, wreq, sndsp, rcvsp;	/* sndsp/rcvsp needed again for the auto-tune below */
  long mssd;				/* tcp.mssdflt= off-subnet MSS cap (0 = keep global) */
  long lspeed;				/* bps= link-speed override (0 = keep the driver's) */

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
  /*
   * Pull the SANA-II request-pool sizes out of the tag list before creating the
   * interface (they size the read/write ring at creation). Absent = 0 = the
   * RAM-tiered default. ng_apply_iface_config() below ignores these private tags.
   */
  { struct TagItem *ti, *tstate = tags;
    ipreq = wreq = sndsp = rcvsp = mssd = lspeed = 0;
    while ((ti = ng_nexttag(&tstate)) != NULL) {
      if (ti->ti_Tag == NGCT_IPRequests)         ipreq  = (long)ti->ti_Data;
      else if (ti->ti_Tag == NGCT_WriteRequests) wreq   = (long)ti->ti_Data;
      else if (ti->ti_Tag == NGCT_TcpSendspace)  sndsp  = (long)ti->ti_Data;
      else if (ti->ti_Tag == NGCT_TcpRecvspace)  rcvsp  = (long)ti->ti_Data;
      else if (ti->ti_Tag == NGCT_TcpMssdflt)    mssd   = (long)ti->ti_Data;
      else if (ti->ti_Tag == NGCT_LinkSpeed)     lspeed = (long)ti->ti_Data;
    }
    /*
     * Honour an explicit tcp.sendspace= / tcp.recvspace= from the interface config by
     * overriding the RAM-tiered socket-buffer defaults (absent = 0 = keep the tier).
     *
     * IMPORTANT -- these are GLOBAL, not per-interface: tcp_sendspace/tcp_recvspace and
     * sb_max are process-wide and read at socket attach (soreserve in tcp_attach) for
     * EVERY socket, whichever interface it is later routed over (a BSD socket is not
     * bound to an interface at creation). So this key on one interface's config sets
     * the default for the whole stack. Semantics, matching Roadshow's model: LAST
     * writer wins if several interfaces set it, and the value PERSISTS until reboot
     * (RemoveInterface does not restore the tiered default). It also lifts the RAM-tier
     * safety cap for the whole machine -- deliberately, since it is an explicit user
     * override -- so document "only set this if you mean it" in the config (we do).
     *
     * Raise sb_max to cover the larger of the two so soreserve()'s sb_max ceiling does
     * not clip the reservation, and so the window-scale shift (grown from the socket's
     * receive high-water) can match a larger recvspace. The AddNetInterface tool clamps
     * the values (<= 1 MB), so this cannot drive sb_max to an absurd size; a request an
     * mbuf pool cannot satisfy fails cleanly at soreserve (ENOBUFS), it does not crash.
     */
    if (sndsp > 0 || rcvsp > 0) {
      extern u_long tcp_sendspace, tcp_recvspace, sb_max;
      ng_tcp_user_override = TRUE;	/* explicit override now beats auto-tune everywhere */
      if (sndsp > 0) tcp_sendspace = (u_long)sndsp;
      if (rcvsp > 0) tcp_recvspace = (u_long)rcvsp;
      /*
       * sb_max must be at least ~2x the buffer, NOT merely equal to it: sbreserve()
       * rejects any reservation larger than sb_max * MCLBYTES / (MSIZE + MCLBYTES)
       * (~94% of sb_max), so setting sb_max == the buffer makes every socket's
       * soreserve() fail with ENOBUFS -- which breaks ALL TCP (incl. DNS-over-TCP).
       * The RAM-tier defaults already use sb_max = 2 * buffer; match that here.
       */
      if (2 * tcp_sendspace > sb_max) sb_max = 2 * tcp_sendspace;
      if (2 * tcp_recvspace > sb_max) sb_max = 2 * tcp_recvspace;
    }
    /*
     * tcp.mssdflt= off-subnet MSS cap. GLOBAL and whole-stack (the clamp in
     * tcp_mss() reads this global at connection time, not per-interface), same
     * model as tcp_sendspace/recvspace above: last writer wins, persists until
     * reboot, interface config overrides the AmiTCP.config/default value. 0 here
     * means the key was absent -> leave the global (auto MTU-40, or a prior set).
     * Clamp to <=65535: t_maxseg is a u_short, a larger value truncates.
     */
    if (mssd > 0) {
      extern int tcp_mssdflt;
      tcp_mssdflt = (mssd > 65535) ? 65535 : (int)mssd;
    }
  }
  {
    struct ifnet *newif = sana_add_interface((char *)interface_name, (char *)device_name,
					     (long)unit, ipreq, wreq, lspeed);
    if (newif == NULL) {
      ReleaseSyscallSemaphore(libPtr);
      writeErrnoValue(libPtr, ENXIO);		/* could not open the device */
      return (-1);
    }
    ng_last_if_baudrate = (u_long)newif->if_baudrate;	/* what the auto-tune sees (GetNetStatus DEBUG) */
    /*
     * Link-speed auto-tune. When neither this nor any earlier interface gave an explicit
     * tcp.sendspace/recvspace (ng_tcp_user_override), size the default window to this
     * interface's link speed instead of leaving it at the RAM-tier value (which, on a
     * big-RAM machine, is a 1 MB ceiling a ~100 Mbit link cannot use and that measurably
     * hurts -- see ng_speed_window). RAM stays the CEILING: we clamp the speed target down
     * to the FIXED ng_ram_ceiling captured at boot (not the live, already-mutated window),
     * so a small-RAM machine keeps its safe small window and, with several NICs, a faster
     * one added later can still reach the full ceiling rather than being stuck at a slower
     * NIC's value. A driver reporting baud 0 yields want==0 -> keep the RAM default. This
     * is GLOBAL (last interface configured wins) and persists until reboot; iface_make()
     * has already run S2_DEVICEQUERY so if_baudrate is populated by now.
     */
    if (sndsp <= 0 && rcvsp <= 0 && !ng_tcp_user_override) {
      extern u_long tcp_sendspace, tcp_recvspace, ng_ram_ceiling;
      u_long ceil = ng_ram_ceiling ? ng_ram_ceiling : tcp_recvspace;
      u_long want = ng_speed_window((u_long)newif->if_baudrate);
      if (want > 0) {
	if (want > ceil) want = ceil;			/* clamp to the FIXED RAM ceiling */
	tcp_sendspace = tcp_recvspace = want;
      }
    }
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
    /*
     * Every address tag below carries a STRING pointer. Reject a NULL one up
     * front: inet_aton() dereferences its argument on the first line of its
     * parse loop with no guard of its own, and with no MMU that read does not
     * trap -- it just returns whatever address 0 holds, so a tag list
     * containing { RTA_Destination, 0 } could quietly parse as 0.0.0.0 and be
     * ACCEPTED as a route rather than cleanly refused. ng_set_ifaddr() already
     * guards the identical class of input for the IFC_* tags. These vectors
     * (AddRouteTagList/DeleteRouteTagList) take a caller-supplied tag list from
     * arbitrary local callers.
     */
    switch (ti->ti_Tag) {
    case RTA_Destination:
    case RTA_DestinationNet:
    case RTA_DestinationHost:
    case RTA_Gateway:
    case RTA_DefaultGateway:
      if (ti->ti_Data == 0) return EINVAL;
      break;
    default:
      break;
    }
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
  /* If that was a default route, remember its gateway against the interface it
   * exits through, so a static configuration can be restored complete after the
   * device goes offline and returns. Roadshow calls this the interface's
   * associated route (IFC_AssociatedRoute). */
  { struct TagItem *tstate = tags, *ti;
    while ((ti = ng_nexttag(&tstate)) != NULL)
      if (ti->ti_Tag == RTA_DefaultGateway && ti->ti_Data) {
	struct in_addr g;
	if (inet_aton((char *)ti->ti_Data, &g))
	break;
      }
  }
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
/* AmiTCP_NG-private interface query (NOT a Roadshow IFQ_ tag): the effective TCP
 * MSS the stack computes for this interface -- interface MTU minus IP+TCP headers,
 * clamped by any tcp.mssdflt override. The stack is the single source of truth so
 * a tool never recomputes (and mis-reports) it. MUST match src/tools/ng_lvo.h. */
#define NGIFQ_TcpMss		(TAG_USER + 0x004E4730)
/* AmiTCP_NG-private transmit drop/error breakdown (see if_sana.c). MUST match ng_lvo.h. */
#define NGIFQ_OutErrors		(TAG_USER + 0x004E4731)	/* if_oerrors: media/device TX errors   */
#define NGIFQ_OutNoBuf		(TAG_USER + 0x004E4732)	/* TX drops: send-tag mbuf alloc failed */
#define NGIFQ_InNoBuf		(TAG_USER + 0x004E4733)	/* RX drops: read re-post mbuf alloc fail */
/* How many of the SANA-II copy callbacks came through the R4 32-bit-aligned
 * variants. The spec's SANA2CopyStats has no field for these (it predates R4),
 * so they get private tags rather than being crammed into DMAIn/DMAOut, which
 * mean something else and which this stack honestly reports as 0. */
#define NGIFQ_Copy32In		(TAG_USER + 0x004E4734)
#define NGIFQ_Copy32Out		(TAG_USER + 0x004E4735)
/* MUST match tools/ng_lvo.h -- the tools include no library headers, so these
 * numbers are deliberately duplicated and a change here needs one there. */
#define NGIFQ_DmaIn		(TAG_USER + 0x004E4736)
#define NGIFQ_DmaOut		(TAG_USER + 0x004E4737)
#define NGIFQ_DmaAsk		(TAG_USER + 0x004E4738)
#define NGIFQ_DmaAskOut		(TAG_USER + 0x004E4739)
#define NGIFQ_DmaNoBuf		(TAG_USER + 0x004E473A)
#define NGIFQ_DmaNoLen		(TAG_USER + 0x004E473B)
#define NGIFQ_DmaNoAlign		(TAG_USER + 0x004E473C)
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
    case NGIFQ_TcpMss: {
      extern int ng_iface_mss(struct ifnet *);
      *(LONG *)d = (LONG)ng_iface_mss(ifp);	/* stack computes it, override-aware */
      break;
    }
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
    case IFQ_OutputDrops:
      /* Output drops: packets dropped because the transmit queue was full. */
      *(ULONG *)d = (ULONG)ifp->if_snd.ifq_drops;
      break;
    case NGIFQ_OutErrors:
      /* Output errors: genuine media/device transmit failures (if_oerrors). */
      *(ULONG *)d = (ULONG)ifp->if_oerrors;
      break;
    case NGIFQ_OutNoBuf:
      /* TX drops from send-tag mbuf exhaustion -- RX pressure starving TX. */
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_txnobuf : 0;
      break;
    case NGIFQ_InNoBuf:
      /* RX drops from read re-post mbuf exhaustion -- the receive-side twin of
       * NGIFQ_OutNoBuf; the true mbuf-starvation signal on the download path. */
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_rxnobuf : 0;
      break;
    case NGIFQ_Copy32In:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_copyin32 : 0;
      break;
    case NGIFQ_Copy32Out:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_copyout32 : 0;
      break;
    case NGIFQ_DmaIn:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_dmato32 : 0;
      break;
    case NGIFQ_DmaOut:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_dmafrom32 : 0;
      break;
    case NGIFQ_DmaAsk:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_dmaask : 0;
      break;
    case NGIFQ_DmaAskOut:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_dmaaskout : 0;
      break;
    case NGIFQ_DmaNoBuf:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_dmano_buf : 0;
      break;
    case NGIFQ_DmaNoLen:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_dmano_len : 0;
      break;
    case NGIFQ_DmaNoAlign:
      *(ULONG *)d = (ssc != NULL) ? (ULONG)ssc->ss_dmano_align : 0;
      break;
    case IFQ_Overruns:
    case IFQ_UnknownTypes:
    case IFQ_NumReadRequestsPending:
    case IFQ_NumWriteRequestsPending:
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
  /* Deliberately NOT gated on a dead base: this frees memory the CALLER
   * owns and touches no stack state. Returning early here would leak that
   * buffer on every call through an abandoned base -- a fault that does not
   * exist today. Do the work regardless of what happened to the stack. */
  NG_ENSURE_STACK();
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
  NG_CHECK_DEAD(FALSE);
  NG_ENSURE_STACK();
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

/* Make `name` the sole default search domain: drop any existing domains and
 * install this one. Shared by SetDefaultDomainName (the LVO) and the DHCP
 * initial-config path (so a DHCP-provided domain populates the resolver's search
 * list). A no-op on NULL/empty. */
void
ng_set_default_domain(const char *name)
{
  struct DomainentNode *dn, *next;
  int nodesize;

  if (name == NULL || *name == '\0')
    return;

  /*
   * dn_EntSize is a signed 16-bit field. Refuse an oversized name BEFORE
   * anything is dropped or allocated: SetDefaultDomainName (LVO -708) takes a
   * bare NUL-terminated string with no length argument at all, so any local
   * caller controls this length completely, and a ~32KB name would truncate
   * the stored size. Rejecting up front also means the existing search domains
   * below are not discarded on a call that was never going to succeed.
   */
  nodesize = sizeof(*dn) + strlen(name) + 1;
  if (nodesize - (int)sizeof(struct GenentNode) > 32767) {
    log(LOG_ERR, "netdb: refusing oversized domain name (%ld bytes).",
	(long)nodesize);
    return;
  }

  LOCK_W_NDB(NDB);

  /* Drop any existing search/default domains. */
  for (dn = (struct DomainentNode *)NDB->ndb_Domains.mlh_Head;
       dn->dn_Node.mln_Succ != NULL; dn = next) {
    next = (struct DomainentNode *)dn->dn_Node.mln_Succ;
    Remove((struct Node *)dn);
    bsd_free(dn, M_NETDB);
  }

  /* Install the new default (node carries its own name string, as adddomainent).
   * nodesize was computed and bounds-checked above. */
  if ((dn = bsd_malloc(nodesize, M_NETDB, M_WAITOK)) != NULL) {
    dn->dn_EntSize = nodesize - sizeof(struct GenentNode);
    dn->dn_Ent.d_name = (char *)(dn + 1);
    strcpy((char *)(dn + 1), name);
    AddTail((struct List *)&NDB->ndb_Domains, (struct Node *)dn);
  }

  UNLOCK_NDB(NDB);
}

/*
 * (ng_set_default_domain_if_empty() lived here. It installed a search domain only
 * when none was set, so a lease won late by the background retry would not
 * displace an explicit domain=. That was correct while domain= outranked DHCP.
 * It is not correct now the lease outranks domain=, and keeping it meant the rule
 * held only for servers that answered promptly -- one config resolving two ways
 * depending on how long the server took. Its single caller now uses
 * ng_set_default_domain(), and a helper whose only purpose was the old precedence
 * is worse than absent: it is an invitation to reintroduce it.)
 */

/* SetDefaultDomainName (LVO -708): make `buffer` the sole default domain. */
VOID SAVEDS RAF2(_SetDefaultDomainName,
		 struct SocketBase *,	libPtr,	a6,
		 STRPTR,		buffer,	a0)
#if 0
{
#endif
  NG_CHECK_DEAD();
  NG_ENSURE_STACK();
  (void)libPtr;
  ng_set_default_domain((const char *)buffer);
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

/* Roadshow struct byte sizes (field count x 4); ip/tcp appended, udp identical.
 *
 * THESE MUST MATCH THE REAL STRUCTS. They were both one field short -- tcpstat
 * is 47 longs and udpstat 9, not 46 and 8 -- so the LAST field of each was
 * silently truncated and read back as 0 forever: tcps_rcvwinupd (which the
 * header-prediction fast path increments) and udps_opackets (every UDP packet
 * this machine has ever sent). Nothing reported these stats, so nobody noticed.
 * A compile-time assert now sits next to each struct definition, where the type
 * is complete; this file only has incomplete types, deliberately, so it cannot
 * check them itself. If you add a field, the build breaks -- fix both places. */
#define NG_STAT_IP_OUR		80	/* our ipstat  = 20 longs */
#define NG_STAT_IP_FULL		96	/* Roadshow    = 24 longs */
#define NG_STAT_TCP_OUR		188	/* our tcpstat = 47 longs */
#define NG_STAT_TCP_FULL	208	/* Roadshow    = 52 longs */
#define NG_STAT_UDP_FULL	36	/* both        =  9 longs */

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
  /* Deliberately NOT gated on a dead base: this frees memory the CALLER
   * owns and touches no stack state. Returning early here would leak that
   * buffer on every call through an abandoned base -- a fault that does not
   * exist today. Do the work regardless of what happened to the stack. */
  NG_ENSURE_STACK();
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
  NG_CHECK_DEAD(FALSE);
  NG_ENSURE_STACK();
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
  NG_CHECK_DEAD(ENETDOWN);
  NG_ENSURE_STACK();
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

  /*
   * Validate the caller-supplied size tags before they drive the allocation
   * arithmetic below: a negative value, or a count big enough to overflow the
   * offset math, would under-reserve (even shrink `total` below sizeof(ng_aam))
   * and cause a heap overflow when the fixed fields are written. Real DHCP
   * fields are far under these caps; an out-of-range request is a caller bug.
   */
  if (naksz < 0 || routersz < 0 || dnssz < 0 || staticsz < 0 ||
      hostsz < 0 || domainsz < 0 || bootpsz < 0 ||
      naksz > 0x10000 || hostsz > 0x10000 || domainsz > 0x10000 ||
      bootpsz > 0x10000 ||
      routersz > 0x4000 || dnssz > 0x4000 || staticsz > 0x4000) {
    writeErrnoValue(libPtr, EINVAL);
    return (EINVAL);
  }

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
  /* Deliberately NOT gated on a dead base: this frees memory the CALLER
   * owns and touches no stack state. Returning early here would leak that
   * buffer on every call through an abandoned base -- a fault that does not
   * exist today. Do the work regardless of what happened to the stack. */
  NG_ENSURE_STACK();
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
/*
 * OUR OWN marker for "this one is auto-tuned", kept OUT of rdn_Flags.
 *
 * These were once published as RDNF_ReadOnly, and that broke every script that
 * sets them: PiStorm/Emu68 pushes buffer sizes from SYS:PiStorm/RoadshowParameters
 * with `roadshowcontrol SET`, got EPERM, and gave up before bringing the network
 * up at all. Refusing was the wrong answer -- the stack sizes these from this
 * machine's RAM and link speed, so the RIGHT answer is to take the request, keep
 * the tuned value, and SAY SO, which leaves the caller informed and the script
 * running. See _ChangeRoadshowData().
 */
#define NG_RSD_AUTOTUNED	(1 << 8)

struct RoadshowDataNode {
  struct MinNode rdn_MinNode;
  STRPTR  rdn_Name;
  UWORD   rdn_Flags;
  WORD    rdn_Type;
  ULONG   rdn_Length;
  APTR    rdn_Data;
};

/* Live stack tunables (defined across the BSD core). */
extern int icmp_process_echo, icmp_process_tstamp;	/* netinet/ip_icmp.c */
extern int    ipforwarding, ipsendredirects, subnetsarelocal, tcp_mssdflt, tcp_iw, udpcksum;
extern int    tcp_do_sack, tcp_do_rfc3042;
extern int    ip_defttl, icmpmaskrepl, tcp_do_rfc1323, tcp_do_rfc1323_tstmp;
extern int    ng_netctl_grace_secs;			/* kern/amiga_netctl.c */
extern u_long tcp_recvspace, tcp_sendspace, udp_recvspace, udp_sendspace;

/*
 * PORT (AmiTCP_NG): READ-ONLY means "the stack works this out for itself".
 *
 * The socket buffer sizes come from ng_ram_tier() (installed RAM) and are then
 * re-derived per interface from the link speed; timestamps come from
 * ng_cpu_tune() (on for 68020+, off for a bare 68000, because the per-segment
 * cost is not worth it there). Those are not opinions a user should have to
 * hold: a value carried over from another stack's sizing model, or picked to
 * suit a different machine, is usually worse than what we compute here.
 *
 * They stay VISIBLE -- reading back what the stack actually chose is genuinely
 * useful, and is how you check the tiering did what you expected -- but
 * ChangeRoadshowData() refuses to write them (EPERM). AmiTCP.config keeps
 * TCP_SENDSPACE=/TCP_RECVSPACE= as a documented expert override for anyone who
 * really does know better; the point of the read-only flag is that the ordinary
 * discoverable surface steers away from breaking a good default by accident.
 *
 * Options Roadshow documents but this stack has no variable for (tcp.rttdflt,
 * tcp.random, if.*, bpf.bufsize, ...) are simply ABSENT rather than faked:
 * ChangeRoadshowData() then reports ENOENT, which is the honest answer.
 * tcp.do_rfc1323 is likewise absent -- we have no single master flag, only the
 * two independent halves below, and inventing an alias that silently wrote both
 * would misrepresent what the stack does.
 */
struct ng_rsd_opt { const char *name; UWORD flags; void *data; };
static const struct ng_rsd_opt ng_rsd_opts[] = {
  /*
   * The option names the Roadshow-compatible configuration tools ask for, limited
   * to those we can honestly back. Twenty-one names are in circulation and a script
   * may ask for any of them; one it cannot find is another way for it to give up. Only options with a real variable behind them are added --
   * publishing a knob that controls nothing would be worse than not having it.
   *
   * Still absent, for want of anything to point at: tcp.rttdflt, tcp.random,
   * tcp.use_mssdflt_for_remote, bpf.bufsize, task.controller.priority.
   */
  /*
   * OURS, not one of Roadshow's names -- how many seconds applications get to close
   * their sockets after NetShutdown breaks them, before the stack goes down anyway.
   * Publishing an extra name costs a script nothing (it asks for the ones it knows)
   * and this is the only way to lengthen the grace: the shutdown protocol has no
   * field for the caller's patience, so the stack cannot be told. See
   * kern/amiga_netctl.c for why the DEFAULT must stay at 4 -- raising it past a
   * caller's timeout makes that caller cancel a shutdown that was about to work.
   */
  { "net.shutdown_grace", 0, &ng_netctl_grace_secs },
  { "icmp.maskrepl",      0, &icmpmaskrepl        },
  { "icmp.processecho",   0, &icmp_process_echo   },
  { "icmp.procesststamp", 0, &icmp_process_tstamp },
  /* Roadshow exposes the RFC 1323 master switch as well as the window-scale name
   * below; both drive the same setting here. */
  { "tcp.do_rfc1323",     0, &tcp_do_rfc1323      },
  { "ip.defttl",          0, &ip_defttl       },
  { "ip.forwarding",      0, &ipforwarding    },
  { "ip.sendredirects",   0, &ipsendredirects },
  { "ip.subnetsarelocal", 0, &subnetsarelocal },
  { "tcp.do_win_scale",   0, &tcp_do_rfc1323  },
  { "tcp.iw",             0, &tcp_iw          },
  { "tcp.mssdflt",        0, &tcp_mssdflt     },
  { "tcp.rfc3042",        0, &tcp_do_rfc3042  },
  { "tcp.sack",           0, &tcp_do_sack     },
  { "udp.cksum",          0, &udpcksum        },
  /* Auto-tuned -- readable, not writable. See the note above. */
  { "tcp.do_timestamps",  NG_RSD_AUTOTUNED, &tcp_do_rfc1323_tstmp },
  { "tcp.recvspace",      NG_RSD_AUTOTUNED, &tcp_recvspace        },
  { "tcp.sendspace",      NG_RSD_AUTOTUNED, &tcp_sendspace        },
  { "udp.recvspace",      NG_RSD_AUTOTUNED, &udp_recvspace        },
  { "udp.sendspace",      NG_RSD_AUTOTUNED, &udp_sendspace        },
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
  NG_CHECK_DEAD(NULL);
  NG_ENSURE_STACK();
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
    /* Publish only real Roadshow flags; NG_RSD_AUTOTUNED is ours and private,
     * so no caller is told the option cannot be set. */
    n->rdn_Flags  = (UWORD)(ng_rsd_opts[i].flags & ~NG_RSD_AUTOTUNED);
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
  /* Deliberately NOT gated on a dead base: this frees memory the CALLER
   * owns and touches no stack state. Returning early here would leak that
   * buffer on every call through an abandoned base -- a fault that does not
   * exist today. Do the work regardless of what happened to the stack. */
  NG_ENSURE_STACK();
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
  NG_CHECK_DEAD(FALSE);
  NG_ENSURE_STACK();
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
  /*
   * Auto-tuned option: ACCEPT the request, do NOT apply it, and say so.
   *
   * Returning an error here is what stopped PiStorm/Emu68's Network.rexx dead --
   * it sets the socket buffer sizes from RoadshowParameters and bails out on a
   * failure, so the machine never came online at all. Nor do we want the value:
   * it is sized from this machine's RAM and link speed, and a number carried over
   * from another machine is worse. So the caller gets success and carries on, and
   * the user is told plainly why the number they asked for is not the one in use.
   */
  if (ng_rsd_opts[i].flags & NG_RSD_AUTOTUNED) {
    log(LOG_NOTICE, "%s is tuned automatically from this machine's RAM and link "
	"speed; the requested value was not applied (currently %ld).\n",
	(char *)n->rdn_Name, (long)*(LONG *)n->rdn_Data);
    return (TRUE);
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
/* Sanity-check a caller-supplied mbuf pointer before we dereference or free it
 * -- see the note on its definition in kern/uipc_mbuf.c. Declared here for the
 * same reason as the rest of this list. */
extern int          m_valid(struct mbuf *);

/* mbuf_get (LVO -654). */
struct mbuf * SAVEDS RAF1(_mbuf_get,
			  struct SocketBase *, libPtr, a6)
#if 0
{
#endif
  NG_CHECK_DEAD(NULL);
  NG_ENSURE_STACK();
  (void)libPtr;
  return (m_get(NG_M_DONTWAIT, NG_MT_DATA));
}

/* mbuf_gethdr (LVO -660). */
struct mbuf * SAVEDS RAF1(_mbuf_gethdr,
			  struct SocketBase *, libPtr, a6)
#if 0
{
#endif
  NG_CHECK_DEAD(NULL);
  NG_ENSURE_STACK();
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
  NG_CHECK_DEAD(NULL);
  NG_ENSURE_STACK();
  (void)libPtr;
  /*
   * PORT (AmiTCP_NG) security fix: every vector in this group that takes a
   * struct mbuf * from the caller now sanity-checks it with m_valid() rather
   * than merely testing for NULL. These vectors hand raw mbuf pointers out to
   * applications and accept them back, so the pointer is caller-controlled --
   * and MFREE() links whatever it is given straight into the free list
   * ((m)->m_next = mfree; mfree = (m)), after which the next allocation by ANY
   * task, including the stack's own receive path, hands that address out as an
   * mbuf and writes through it. With no MMU that is a write-what-where
   * primitive for any local task; a NULL check only excluded address 0, not
   * address 4 onwards in the exception-vector table. m_valid() confirms the
   * pointer is MSIZE-aligned and inside one of our pool chunks. It cannot
   * detect a pointer to an mbuf that was ours and has since been freed -- that
   * needs generation tags the pool does not have -- so a double free through
   * these vectors is still possible; this closes the "point anywhere" class.
   */
  if (!m_valid(m))
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
  NG_CHECK_DEAD();
  NG_ENSURE_STACK();
  (void)libPtr;
  if (m_valid(m))		/* see mbuf_free above */
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
  NG_CHECK_DEAD(NULL);
  NG_ENSURE_STACK();
  (void)libPtr;
  /* see mbuf_free above for m_valid(). len is bounded inside m_prepend(): an
   * oversized or negative value corrupts the new mbuf's m_len/m_data. */
  if (!m_valid(m))
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
  NG_CHECK_DEAD(NULL);
  NG_ENSURE_STACK();
  (void)libPtr;
  /* see mbuf_free above. A negative len is not rejected here: m_pullup()'s own
   * min(min(max(len, max_protohdr), space), n->m_len) clamp degrades it to
   * "pull up about max_protohdr bytes", matching upstream BSD behaviour. */
  if (!m_valid(m))
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
  NG_CHECK_DEAD(NULL);
  NG_ENSURE_STACK();
  (void)libPtr;
  /* see mbuf_free above. m_copym() rejects a negative off/len itself. */
  if (!m_valid(m))
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
  NG_CHECK_DEAD(-1);
  NG_ENSURE_STACK();
  (void)libPtr;
  /* see mbuf_free above. An over-large len can no longer drive pkthdr.len
   * negative -- m_adj() floors it. */
  if (!m_valid(m))
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
  NG_CHECK_DEAD(-1);
  NG_ENSURE_STACK();
  (void)libPtr;
  /* see mbuf_free above -- BOTH chains are caller-supplied, and m_cat() frees
   * mbufs of `second` as it splices, so an invalid `second` reaches MFREE. */
  if (!m_valid(first) || !m_valid(second))
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
  NG_CHECK_DEAD(-1);
  NG_ENSURE_STACK();
  (void)libPtr;
  /* see mbuf_free above. m_copyback() rejects a negative off/len itself. */
  if (!m_valid(m) || data == NULL)
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
  NG_CHECK_DEAD(-1);
  NG_ENSURE_STACK();
  (void)libPtr;
  /* see mbuf_free above. m_copydata() rejects a negative off/len itself. */
  if (!m_valid(m) || data == NULL)
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

/*
 * PORT (AmiTCP_NG): apply saved tunables from ENV: at startup.
 *
 * AmiTCPControl SAVE writes ENVARC:AmiTCP_NG/<group>/<name> (dots become
 * slashes), mirroring how Roadshow persists its own; a booted system copies
 * ENVARC: into ENV:, which is what we read here.
 *
 * PRECEDENCE. This runs AFTER ng_ram_tier()/ng_cpu_tune() and BEFORE
 * readconfig(), which puts it exactly where it belongs:
 *
 *   built-in default  <  RAM/CPU auto-tune  <  ENV:  <  AmiTCP.config  <  live SET
 *
 * so a saved setting beats the computed default, and an explicit line in
 * AmiTCP.config still beats the saved setting. The config file is the thing a
 * user can read, so it wins over invisible state.
 *
 * Read-only (auto-tuned) options are skipped here for the same reason
 * AmiTCPControl refuses to set them: a socket buffer size saved on another
 * machine, or under another stack's sizing model, is worse than what this
 * machine works out for itself. Being settable through the back door would
 * defeat the point of marking them read-only at the front.
 *
 * Nothing is logged from here -- log_init() has not run yet. What was applied
 * is recorded and reported later; see ng_env_report().
 */
static TEXT  ng_env_applied[160];
static ULONG ng_env_count;

void
ng_apply_env_tunables(void)
{
  ULONG i;

  ng_env_applied[0] = '\0';
  ng_env_count = 0;

  for (i = 0; i < NG_RSD_COUNT; i++) {
    TEXT name[96], val[32];
    LONG v = 0;
    int  j, k, neg = 0, digits = 0;

    if (ng_rsd_opts[i].flags & NG_RSD_AUTOTUNED)
      continue;				/* auto-tuned: never taken from ENV */

    /* "AmiTCP_NG/" + option name with '.' -> '/' */
    for (j = 0; "AmiTCP_NG/"[j] != '\0'; j++)
      name[j] = "AmiTCP_NG/"[j];
    for (k = 0; ng_rsd_opts[i].name[k] != '\0' && j < (int)sizeof(name) - 1; k++, j++)
      name[j] = (ng_rsd_opts[i].name[k] == '.') ? '/' : ng_rsd_opts[i].name[k];
    name[j] = '\0';

    if (GetVar((STRPTR)name, (STRPTR)val, sizeof(val) - 1, GVF_GLOBAL_ONLY) <= 0)
      continue;				/* not set -- leave the default alone */

    k = 0;
    while (val[k] == ' ' || val[k] == '\t') k++;
    if (val[k] == '-') { neg = 1; k++; }
    while (val[k] >= '0' && val[k] <= '9') {
      LONG digit = val[k++] - '0';
      /* Bounded: a hand-edited or corrupted ENVARC: file with a long digit
       * string would otherwise wrap silently and apply a value nobody chose. */
      if (v > (2147483647L - digit) / 10) { digits = 0; break; }
      v = v * 10 + digit;
      digits++;
    }
    if (!digits)
      continue;				/* not a number -- ignore, do not guess */
    if (neg) v = -v;

    *(LONG *)ng_rsd_opts[i].data = v;

    /* Remember the name for the startup report (bounded, oldest wins). */
    {
      ULONG l = 0;
      int   namelen = 0;

      while (ng_env_applied[l] != '\0') l++;
      while (ng_rsd_opts[i].name[namelen] != '\0') namelen++;
      /*
       * Measure the NAME we are about to append. This used to test the
       * leftover digit-parsing cursor `k`, which had nothing to do with the
       * name's length -- harmless, because the copy loop below has its own
       * correct bound, but it meant the "is there room?" question was being
       * asked about the wrong string.
       */
      if (l + 2 + (ULONG)namelen < sizeof(ng_env_applied) - 1) {
	int c;
	if (l) { ng_env_applied[l++] = ','; ng_env_applied[l++] = ' '; }
	for (c = 0; ng_rsd_opts[i].name[c] != '\0' &&
		    l < sizeof(ng_env_applied) - 1; c++)
	  ng_env_applied[l++] = ng_rsd_opts[i].name[c];
	ng_env_applied[l] = '\0';
      }
    }
    ng_env_count++;
  }
}

/* Called once logging is up (see amiga_main.c). Silent when nothing was set. */
void
ng_env_report(void)
{
  if (ng_env_count > 0)
    log(LOG_NOTICE, "config: %lu setting%s applied from ENV: -- %s",
	ng_env_count, (ng_env_count == 1) ? "" : "s", ng_env_applied);
}


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
  /*
   * Set when the stack started this exchange itself, rather than an application
   * calling BeginInterfaceConfig. There is no caller waiting on a reply port, so
   * the helper must dispose of the message instead of replying to it -- see the
   * reply: label in ng_dhcp_task(). Used when a device comes back online and its
   * interface has to acquire a lease again.
   */
  UBYTE				dc_internal;
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
  __asm__ __volatile__("jsr a6@(-30)"
      :"+r"(_d0),"+r"(_d1)
      :"r"(_d2),"r"(_a6):"a0","a1","memory");
  return _d0;
}
static long d_bind(struct Library *sb, long s, void *n, long l) {
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=n;
  register long _d1 __asm("d1")=l; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-36)"
      :"+r"(_d0),"+r"(_a0),"+r"(_d1)
      :"r"(_a6):"a1","memory");
  return _d0;
}
/*
 * PORT (AmiTCP_NG): d0/d1/a0/a1 are IN-OUT ("+r"), not inputs.
 *
 * Those four registers are unconditionally scratch across an AmigaOS library
 * call. Declaring one input-only tells gcc it survives the `jsr`, so the register
 * allocator is free to park an unrelated live value there across the call -- and
 * get back whatever the callee left. It is not a theoretical risk: `d_sendto` and
 * `d_recvfrom` were the only two stubs in this file with NO register clobber at
 * all, and they are the two called inside ng_dhcp_exchange()'s poll loop. At
 * -m68040 -O2 the allocator took that freedom and the exchange stopped completing
 * -- DISCOVER goes out, the OFFER comes back, and the reply is tested against a
 * register the library has since overwritten, so nothing ever matches and the
 * client falls back to a 169.254 link-local address.
 *
 * That is why every traced build "fixed" it: a trace call between the jsr and the
 * comparison forces the value to be reloaded. A bug that disappears when you add
 * logging is a codegen/register-allocation signature, not a race.
 *
 * d2/d3/a2 stay plain inputs deliberately -- those ARE callee-saved on this ABI,
 * and marking them in-out would only cost a needless reload. a6 likewise: the
 * library base is preserved by contract.
 */
static long d_sendto(struct Library *sb, long s, void *b, long l, long f, void *to, long tl) {
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=b;
  register long _d1 __asm("d1")=l; register long _d2 __asm("d2")=f;
  register void *_a1 __asm("a1")=to; register long _d3 __asm("d3")=tl;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-60)"
      :"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_a1)
      :"r"(_d2),"r"(_d3),"r"(_a6):"memory");
  return _d0;
}
static long d_recvfrom(struct Library *sb, long s, void *b, long l, long f, void *a, void *al) {
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=b;
  register long _d1 __asm("d1")=l; register long _d2 __asm("d2")=f;
  register void *_a1 __asm("a1")=a; register void *_a2 __asm("a2")=al;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-72)"
      :"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_a1)
      :"r"(_d2),"r"(_a2),"r"(_a6):"memory");
  return _d0;
}
static long d_setsockopt(struct Library *sb, long s, long lv, long on, void *v, long vl) {
  register long _d0 __asm("d0")=s; register long _d1 __asm("d1")=lv;
  register long _d2 __asm("d2")=on; register void *_a0 __asm("a0")=v;
  register long _d3 __asm("d3")=vl; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-90)"
      :"+r"(_d0),"+r"(_d1),"+r"(_a0)
      :"r"(_d2),"r"(_d3),"r"(_a6):"a1","memory");
  return _d0;
}
static long d_ioctl(struct Library *sb, long s, unsigned long r, void *a) {
  register long _d0 __asm("d0")=s; register unsigned long _d1 __asm("d1")=r;
  register void *_a0 __asm("a0")=a; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-114)"
      :"+r"(_d0),"+r"(_d1),"+r"(_a0)
      :"r"(_a6):"a1","memory");
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
static void d_setdomain(struct Library *sb, void *name) {  /* SetDefaultDomainName -708 */
  register void *_a0 __asm("a0")=name; register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-708)":"+r"(_a0):"r"(_a6):"d0","d1","a1","memory");
}
static long d_addroute(struct Library *sb, void *tags) {  /* AddRouteTagList -414 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=tags;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-414)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
static long d_delroute(struct Library *sb, void *tags) {  /* DeleteRouteTagList -420 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=tags;
  register struct Library *_a6 __asm("a6")=sb;
  __asm__ __volatile__("jsr a6@(-420)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
/*
 * Remove the transient limited-broadcast host route (255.255.255.255/32, gw
 * 0.0.0.0) that a DISCOVER needs to leave a chosen interface. It MUST NOT outlive
 * the exchange: while it exists, ip_broadcast_flood()'s directed-egress logic
 * (ip_output.c) pins EVERY limited broadcast on the box to that one interface, so
 * ordinary application broadcasts would stop flooding all segments. Called the
 * moment each DISCOVER window closes. Idempotent -- a missing route just yields
 * ESRCH, which we ignore.
 *
 * The radix table tracks no ownership, so this deletes by key alone: in the
 * pathological case where an operator manually pinned an identical
 * 255.255.255.255/32 (gw 0.0.0.0) route (e.g. via AddNetRoute), a closing DISCOVER
 * window would collaterally remove it. Accepted -- that is a far narrower footgun
 * than the permanent all-broadcast pinning this scoping fixes.
 */
static void ng_del_discover_route(struct Library *sb) {
  struct TagItem rtags[3];
  rtags[0].ti_Tag = RTA_DestinationHost; rtags[0].ti_Data = (ULONG)"255.255.255.255";
  rtags[1].ti_Tag = RTA_Gateway;         rtags[1].ti_Data = (ULONG)"0.0.0.0";
  rtags[2].ti_Tag = 0;
  (void)d_delroute(sb, rtags);
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

/* Find option `code` in a received packet; returns value ptr + sets *len, or NULL.
 *
 * `rlen` is the length d_recvfrom() actually returned for THIS datagram. It is
 * required: the options array is a fixed 312 bytes, but a datagram may be far
 * shorter, and the rx buffer is reused across every receive of the exchange.
 * Scanning to the fixed end would therefore read whatever an EARLIER datagram
 * left behind and hand it back as an option of the packet just received -- an
 * attacker who can reach UDP port 68 could prime the tail with one oversized
 * bogus datagram (wrong xid, ignored for message-type logic but still copied
 * into the buffer) and have a later short, xid-matching ACK inherit it as
 * DHO_ROUTERS/DHO_DNS, i.e. gateway and resolver hijack. Bound the scan to what
 * really arrived. (ng_dhcp_exchange also clears rx before each receive; either
 * measure alone would close this, both together are cheap.)
 */
static UBYTE *dhcp_find(struct dhcp_pkt *pkt, int rlen, UBYTE code, int *len)
{
  int hdr = (int)((UBYTE *)pkt->options - (UBYTE *)pkt);
  int avail = rlen - hdr;
  UBYTE *p, *end;

  if (avail < 4)				/* no magic cookie -> no options */
    return NULL;
  if (avail > (int)sizeof(pkt->options))
    avail = (int)sizeof(pkt->options);

  p = pkt->options + 4;				/* skip magic cookie */
  end = pkt->options + avail;
  while (p < end && *p != DHO_END) {
    UBYTE c = *p++;
    UBYTE l;
    if (c == 0) continue;			/* pad */
    if (p >= end) break;
    l = *p++;
    if (c == code) {
      /* Clamp the server-declared length to what's actually left in the buffer so
       * a malformed option cannot make a caller read past the packet. */
      if (p + l > end) l = (UBYTE)(end - p);
      if (len) *len = l;
      return p;
    }
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
#define NG_DHCP_MAXDNS 3	/* DNS servers captured from a DHCP DHO_DNS option */
struct ng_lease { ULONG addr, serverid, mask, router, dns[NG_DHCP_MAXDNS], lease; char domain[128]; char hostname[128]; };

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
  long i, r, msgtype;

  ng_sain_set(&to, 0xFFFFFFFFUL, DHCP_SERVER_PORT);
  L->addr = L->serverid = L->mask = L->router = L->lease = 0;
  L->domain[0] = 0;
  L->hostname[0] = 0;
  bzero((caddr_t)L->dns, sizeof(L->dns));
  msgtype = DHCPDISCOVER;
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
      { UBYTE prl[5]; prl[0]=DHO_SUBNET_MASK; prl[1]=DHO_ROUTERS; prl[2]=DHO_DNS; prl[3]=DHO_DOMAIN;
        prl[4]=DHO_HOSTNAME;
        o = dhcp_put(o, DHO_PARAM_REQ, 5, prl); }
      *o++ = DHO_END;
      r = d_sendto(sb, s, tx, sizeof(*tx), 0, &to, sizeof(to));
    }
    /* Clear rx before every receive: it is reused across the whole DORA
     * exchange and every background retry, so anything a previous (possibly
     * hostile) datagram left past this one's length must not survive into the
     * option scan. Belt and braces with dhcp_find's rlen bound. */
    for (r = 0; r < (long)sizeof(*rx); r++) ((char *)rx)[r] = 0;
    /* NOTE: from/fromlen are deliberately NULL. Filtering on the sender's IP
     * would break relayed DHCP (the reply legitimately arrives from the relay
     * agent, not the server). xid plus the server-id option is the standard
     * validation and is what this client uses -- do not "fix" this to a
     * source-address check. */
    r = d_recvfrom(sb, s, rx, sizeof(*rx), 0, (void*)0, (void*)0);
    if (r >= 240 && rx->xid == xid && rx->op == BOOTREPLY) {
      UBYTE *mt = dhcp_find(rx, (int)r, DHO_MSG_TYPE, (int*)0);
      int t = mt ? *mt : 0;
      if (msgtype == DHCPDISCOVER && t == DHCPOFFER) {
        UBYTE *sid = dhcp_find(rx, (int)r, DHO_SERVER_ID, (int*)0);
        L->addr = rx->yiaddr;
        if (sid) L->serverid = ((ULONG)sid[0]<<24)|((ULONG)sid[1]<<16)|((ULONG)sid[2]<<8)|sid[3];
        msgtype = DHCPREQUEST; i = -1;		/* restart timing for REQUEST */
        continue;
      }
      if (msgtype == DHCPREQUEST && t == DHCPACK) {
        UBYTE *op; int ol;
        L->addr = rx->yiaddr;
        if ((op = dhcp_find(rx, (int)r, DHO_SUBNET_MASK, &ol)) && ol>=4)
          L->mask = ((ULONG)op[0]<<24)|((ULONG)op[1]<<16)|((ULONG)op[2]<<8)|op[3];
        if ((op = dhcp_find(rx, (int)r, DHO_ROUTERS, &ol)) && ol>=4)
          L->router = ((ULONG)op[0]<<24)|((ULONG)op[1]<<16)|((ULONG)op[2]<<8)|op[3];
        /* DHO_DNS may carry several servers (4 bytes each); capture up to
         * NG_DHCP_MAXDNS of them. ol is already clamped to the packet by dhcp_find. */
        if ((op = dhcp_find(rx, (int)r, DHO_DNS, &ol)) && ol >= 4) {
          int di;
          for (di = 0; (di + 1) * 4 <= ol && di < NG_DHCP_MAXDNS; di++)
            L->dns[di] = ((ULONG)op[di*4]<<24)|((ULONG)op[di*4+1]<<16)|
                         ((ULONG)op[di*4+2]<<8)|op[di*4+3];
        }
        if ((op = dhcp_find(rx, (int)r, DHO_LEASE_TIME, &ol)) && ol>=4)
          L->lease = ((ULONG)op[0]<<24)|((ULONG)op[1]<<16)|((ULONG)op[2]<<8)|op[3];
        /* DHO_DOMAIN (option 15): the search domain. dhcp_find clamps `ol` to the
         * packet, and we cap at the buffer, so the copy is bounded both ways. */
        if ((op = dhcp_find(rx, (int)r, DHO_DOMAIN, &ol)) && ol > 0) {
          int k = (ol < (int)sizeof(L->domain) - 1) ? ol : (int)sizeof(L->domain) - 1;
          bcopy((caddr_t)op, (caddr_t)L->domain, k);
          L->domain[k] = 0;
        }
        /* DHO_HOSTNAME (option 12): the name the server has for us. Bounded the
         * same way as the domain above -- dhcp_find has already clamped `ol` to
         * the packet, and we clamp again to the field.
         *
         * KEPT WHOLE. This used to chop the name at the first dot, on the reasoning
         * that a host name store holds a host name and the search domain arrives
         * separately in DHO_DOMAIN. The effect was that a server handing out
         * "a3000.intra.sm41.de" left gethostname() answering "a3000", where other
         * stacks -- AmiTCP 4.6's DHCP client among them -- report the fully
         * qualified name. Throwing away information the server sent us is not this
         * layer's decision to make; a caller that wants only the first label can
         * still take it, and one that wants the whole name now can have it. */
        if ((op = dhcp_find(rx, (int)r, DHO_HOSTNAME, &ol)) && ol > 0) {
          int k = (ol < (int)sizeof(L->hostname) - 1) ? ol : (int)sizeof(L->hostname) - 1;
          bcopy((caddr_t)op, (caddr_t)L->hostname, k);
          L->hostname[k] = 0;
        }
        /*
         * Say what the server actually sent. Until this existed, "the domain is
         * wrong" could not be told apart from "the server never sent one" from
         * anywhere on the machine -- no tool reported the stack's domain, so an
         * empty answer was read as absence when it only meant nobody was asking.
         *
         * Two lines, not one. Each of these fields can be 127 characters on its
         * own, so a single combined line could reach ~290 -- past the smallest
         * log buffer the stack ever runs with (log_cnf.log_buf_len is RAM-tiered
         * in kern/amiga_main.c: 256 bytes on the leanest tier, 512 above it).
         * Split, each line is at most ~158 and always fits. Losing the tail of a
         * diagnostic is how a diagnostic starts lying.
         *
         * NB the old flat 127-character cut is long fixed -- do not "restore" that
         * number here; it describes behaviour this stack no longer has.
         */
        log(LOG_NOTICE, "dhcp: host-name (option 12) %s",
            L->hostname[0] ? L->hostname : (char *)"not sent by the server");
        log(LOG_NOTICE, "dhcp: domain-name (option 15) %s",
            L->domain[0] ? L->domain : (char *)"not sent by the server");
        return NG_DHCP_GOT;
      }
      if (msgtype == DHCPREQUEST && t == DHCPNAK) { msgtype = DHCPDISCOVER; L->addr = 0; i = -1; continue; }
    }
    Delay(10);					/* ~0.2s */
  }
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
  /* This lease REPLACES the servers this interface previously provided -- not
   * everybody's. Then attribute the new ones to it, so that taking this interface
   * offline later withdraws exactly these and leaves other interfaces' alone. */
  ng_flush_dynamic_nameservers_for(ifname);
  /* Install this lease's servers OWNED BY THIS INTERFACE, one configure call per
   * server. Going through the interface-configuration path rather than the bare
   * AddDomainNameServer vector is what records the owner. */
  { int di;
    struct TagItem dt[2];
    char dbuf[16];
    for (di = 0; di < NG_DHCP_MAXDNS; di++)
      if (L->dns[di]) {
	ng_ip2str(L->dns[di], dbuf);
	dt[0].ti_Tag = NGCT_NameServer; dt[0].ti_Data = (ULONG)dbuf;
	dt[1].ti_Tag = 0;              dt[1].ti_Data = 0;
	(void)d_configiface(sb, (void *)ifname, dt);
      } }
  (void)dnsstr;				/* no longer used for adding */
  /* A DHCP interface owns the default route and the name servers its lease gave
   * it, so both are withdrawn when the device goes offline. Declared here rather
   * than left to the caller: the lease is what created them. */
  { struct ifnet *ifp; spl_t sp = splimp();
    if ((ifp = ifunit((char *)ifname)) != NULL && ifp->if_type == IFT_SANA) {
      ((struct sana_softc *)ifp)->ss_assoc_route = 1;
      ((struct sana_softc *)ifp)->ss_assoc_dns   = 1;
    }
    splx(sp); }
  /*
   * Remember that this interface is DHCP-configured, so that when its device goes
   * offline and comes back the stack acquires a lease again instead of returning
   * an unnumbered, routeless interface. Recorded LAST: applying the lease above
   * goes through ConfigureInterfaceTagList, which records the static values, and
   * for a DHCP interface this flag is the one that matters.
   */
  return 0;
}


/*
 * A stack-initiated reconfigure has finished for this interface, so another one
 * may start. Cleared at the very END of the helper, not when it replies: after a
 * link-local fallback the helper keeps running, retrying DHCP in the background,
 * and starting a second client alongside it is exactly what this prevents.
 */
void
ng_iface_reconfig_done(const char *ifname)
{
  struct ifnet *ifp;
  spl_t s;

  if (ifname == NULL)
    return;
  s = splimp();
  if ((ifp = ifunit((char *)ifname)) != NULL && ifp->if_type == IFT_SANA)
    ((struct sana_softc *)ifp)->ss_reconfiguring = 0;
  splx(s);
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
      ng_del_discover_route(sb);	/* DISCOVER window closed: unpin the limited broadcast */
      if (rc == NG_DHCP_GOT) {
	(void)ng_apply_lease(sb, ifname, &L);	/* upgraded to a real lease -- done */
	/*
	 * The lease's search domain WINS, exactly as it does for an on-time lease.
	 *
	 * This used to fill it in only if nothing was set, so as not to clobber an
	 * explicit domain=. That was right when domain= outranked DHCP; it is wrong
	 * now that the lease does, and it made the rule true only for servers that
	 * answered promptly -- the same config resolving differently depending on
	 * how long the DHCP server took to appear.
	 *
	 * This is not a renewal re-pointing a running machine's resolver, which
	 * would be a different question: it is the FIRST real configuration this
	 * interface has had, arriving late after a spell on link-local. Nothing
	 * else in the renewal path touches the domain at all.
	 */
	if (L.domain[0]) ng_set_default_domain(L.domain);
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
/* NOT SAVEDS: this is a CreateNewProc()'d Process, entered with an arbitrary a6
 * -- not a library base. Under a compiler where SAVEDS expands to __saveds (the
 * __SASC branch of sys/cdefs.h) the prologue would store that garbage a6 into
 * the global SysBase and fault on the first library call, exactly as documented
 * for ng_stack_process() in kern/amiga_main.c. Harmless under the bebbo gcc
 * build this ships with, where SAVEDS expands to nothing -- removed so it does
 * not become a live bug if the SASC path is ever revived. log_task() in
 * kern/amiga_log.c, spawned the same way, already omits it. */
/*
 * Detach an AddressAllocationMessage from AbortInterfaceConfig and dispose of it.
 *
 * ONE routine because there are TWO exits that finish an exchange -- the normal
 * reply: label and the RFC 3927 link-local fallback, which replies early and then
 * keeps running in the background -- and they must not disagree. They did: the
 * link-local path replied unconditionally, so a stack-initiated exchange (no
 * caller, mn_ReplyPort NULL) that fell back to link-local did ReplyMsg() through a
 * NULL port and leaked the message. With no MMU that is a write through address 0.
 */
static void
ng_dhcp_finish_aam(struct ng_dhcp_ctx *ctx, struct ng_aam *aam)
{
  Forbid();
  aam->aam_Reserved = 0;		/* detach: Abort can no longer reach us */
  if (ctx->dc_internal)
    FreeVec(aam);			/* nobody is waiting; FreeVec does not block */
  else
    ReplyMsg((struct Message *)aam);
  Permit();
}

static void ng_dhcp_task(void)
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

  /* Tell the caller we have the context, so it can release the spawn lock. NOT on
   * the internal path: there the "parent" is the network task, which must never be
   * signalled or blocked by us -- it is the task that moves the packets this
   * exchange depends on, and CTRL_F means other things elsewhere in the stack. */
  if (!ctx->dc_internal)
    Signal(ctx->dc_parent, SIGBREAKF_CTRL_F);
  aam = ctx->dc_aam;

  /*
   * THE NAME FIRST, before anything that can fail.
   *
   * cleanup: releases the reconfigure interlock with ng_iface_reconfig_done(ifname)
   * on the internal path, and every `goto reply` below lands there. Copying the
   * name after the OpenLibrary() check meant a failed open -- entirely possible on
   * a memory-tight machine -- reached that call with ifname still holding whatever
   * was on the stack. Its only guard is `ifname == NULL`, which a stack address
   * always passes, so it would call ifunit() on garbage: the interface that needed
   * the flag cleared never gets it (locked out of reconfiguring until reboot,
   * silently), and if those bytes happened to spell a live interface's name --
   * quite possible in reused Process stack memory -- it clears the WRONG one.
   *
   * It only depends on aam, which is valid here, so there is no reason for it to
   * be anywhere else.
   */
  for (i = 0; i < 15 && aam->aam_InterfaceName[i]; i++) ifname[i] = aam->aam_InterfaceName[i];
  ifname[i] = 0;

  sb = OpenLibrary((STRPTR)"bsdsocket.library", 3L);
  if (sb == NULL) { aam->aam_Result = AAMR_NoMemory; goto reply; }

  if (aam->aam_Version < 1 || aam->aam_Version > 2) { aam->aam_Result = AAMR_VersionUnknown; goto reply; }

  tx = AllocVec(sizeof(*tx), MEMF_PUBLIC | MEMF_CLEAR);
  rx = AllocVec(sizeof(*rx), MEMF_PUBLIC | MEMF_CLEAR);
  if (tx == NULL || rx == NULL) { aam->aam_Result = AAMR_NoMemory; goto reply; }

  /* Hardware address for chaddr. */
  for (i = 0; i < 16; i++) mac[i] = macbuf[i] = 0;
  qtags[0].ti_Tag = IFQ_HardwareAddress; qtags[0].ti_Data = (ULONG)macbuf;
  qtags[1].ti_Tag = 0;
  if (d_queryiface(sb, ifname, qtags) != 0) { aam->aam_Result = AAMR_InterfaceNotKnown; goto reply; }
  for (i = 0; i < 6; i++) mac[i] = macbuf[i];

  /* Bring the interface up, unnumbered (0.0.0.0), so we can broadcast. */
  ctags[0].ti_Tag = IFC_Address; ctags[0].ti_Data = (ULONG)"0.0.0.0";
  ctags[1].ti_Tag = IFC_State;   ctags[1].ti_Data = IFC_State_Up;
  ctags[2].ti_Tag = 0;
  /* Best effort: the interface may already be up, and a failure here is not fatal --
   * the DISCOVER below is what actually decides whether this works. */
  (void)d_configiface(sb, ifname, ctags);

  s = d_socket(sb, NG_AF_INET, NG_SOCK_DGRAM, 0);
  if (s < 0) { aam->aam_Result = AAMR_NoMemory; goto reply; }
  { long br = d_setsockopt(sb, s, NG_SOL_SOCKET, NG_SO_BROADCAST, &one, sizeof(one));
    (void)br; }
  ng_sain_set(&from, 0, DHCP_CLIENT_PORT);
  if (d_bind(sb, s, &from, sizeof(from)) < 0) {
    aam->aam_Result = AAMR_AddrChangeFailed; goto reply;
  }
  d_ioctl(sb, s, NG_FIONBIO, &one);
  /* Route the limited broadcast (255.255.255.255) out this interface. Gateway
   * 0.0.0.0 == the unnumbered interface itself, i.e. a link route. */
  rtags[0].ti_Tag = RTA_DestinationHost; rtags[0].ti_Data = (ULONG)"255.255.255.255";
  rtags[1].ti_Tag = RTA_Gateway;         rtags[1].ti_Data = (ULONG)"0.0.0.0";
  rtags[2].ti_Tag = 0;
  { long rr = d_addroute(sb, rtags);
    (void)rr; }

  xid = (ULONG)ctx ^ 0x414d4954UL;		/* 'AMIT' ^ ctx -- unique enough */
  deadline = (long)aam->aam_Timeout; if (deadline < 10) deadline = 10;
  deadline *= 5;				/* poll ticks of ~0.2s */

  /* Initial DHCP attempt. */
  {
    struct ng_lease L;
    int rc = ng_dhcp_exchange(sb, s, tx, rx, mac, xid, deadline, ctx, &L);
    ng_del_discover_route(sb);	/* DISCOVER window closed: unpin the limited broadcast */
    /* The initial exchange is over either way, so the timing-critical window has
     * passed -- get what we have onto disk before anything else can wedge. */
    if (rc == NG_DHCP_ABORT) { aam->aam_Result = AAMR_Aborted; goto reply; }
    if (rc == NG_DHCP_GOT) {
      if (ng_apply_lease(sb, ifname, &L) != 0) { aam->aam_Result = AAMR_AddrChangeFailed; goto reply; }
      /*
       * Install the DHCP search domain. THE LEASE WINS: a server handing out a
       * domain is authoritative for the network just joined, the same rule the
       * host name below has always followed. An explicit domain= is the FALLBACK
       * and is applied BEFORE the exchange (by the tool, and by the reconfigure
       * helper), so it stands only when the server offers nothing.
       *
       * Priority: DHCP > explicit domain= > hostname-derived (res_search's own).
       *
       * INITIAL configuration only, like the host name: a background renewal must
       * not silently re-point a running machine's resolver.
       */
      if (L.domain[0]) ng_set_default_domain(L.domain);
      /*
       * PORT (AmiTCP_NG): a host name from DHCP SUPERSEDES HOSTNAME= in
       * AmiTCP.config. The config value is the fallback -- what a statically
       * addressed machine uses, and what we keep when the server offers nothing.
       * A server that hands out names is authoritative for this network, and
       * leaving the machine calling itself whatever the config file happened to
       * say defeats the point of asking. Validated with the same rule the config
       * path uses, so a malformed option is ignored rather than stored.
       *
       * Applied on the INITIAL configuration only, like the search domain above:
       * a background renewal must not silently rename a running machine.
       */
      {
        extern int ng_hostname_valid(const char *s, int len);
        extern int sethostname(const char *name, size_t namelen);
        extern char host_name[];
        extern size_t host_namelen;
        /* Sized from the host-name limit, not from literals. If `fq` cannot hold a
         * full-length joined name the join fails ng_hostname_valid() and degrades
         * to the bare name -- which looks exactly like the join never happening,
         * the very symptom this code was written to cure. `base` sources from
         * host_name, so it must be able to hold all of it. */
        char base[NG_MAXHOSTNAME + 1];
        char fq[NG_MAXHOSTNAME + 1];
        int  hl = 0, dotted = 0, i2, from_dhcp;

        /*
         * PICK THE BASE NAME FIRST, THEN QUALIFY IT. There are two independent
         * inputs and either may be absent:
         *
         *   option 12 (host-name)   -> L.hostname, authoritative, supersedes HOSTNAME=
         *   option 15 (domain-name) -> L.domain,   already installed just above
         *
         * This whole block used to sit inside `if (L.hostname[0])`, so a server
         * that sends a domain but NO host name -- a common, entirely normal
         * configuration -- left the machine calling itself the bare HOSTNAME= from
         * the config for ever, with a perfectly good domain sitting unused right
         * beside it. The domain was set, the name was never qualified, and the two
         * facts were impossible to tell apart from outside.
         */
        from_dhcp = (L.hostname[0] != 0);
        if (from_dhcp) {
          while (L.hostname[hl] && hl < (int)sizeof(base) - 1) { base[hl] = L.hostname[hl]; hl++; }
        } else {
          /*
           * No option 12: fall back to the name we already hold, which is
           * HOSTNAME= from AmiTCP.config. Copied under Forbid() because host_name
           * is process-global and a concurrent sethostname() from another task
           * would otherwise let us read it half-rewritten.
           *
           * BOUND BY host_namelen, NOT by sizeof(base). This is deliberate even
           * though the two buffers are now the SAME size (both NG_MAXHOSTNAME+1):
           * the right bound for a copy is how many bytes the source actually
           * holds, never how many the destination could take. Sizing them alike
           * is a coincidence of today's constants, not a guarantee -- and with no
           * MMU, a bound that is only correct by coincidence is one edit away from
           * reading off the end of a global. Taking the authoritative length under
           * the same Forbid() -- as _gethostname() also does -- removes the
           * question instead of answering it.
           * (sizeof(host_name) is not available here: it is an extern of
           * incomplete type, so sizeof on it will not compile.)
           *
           * The host_name[hl] test is belt and braces only: every sethostname()
           * caller passes strlen(name), so host_namelen always IS the length.
           */
          Forbid();
          {
            int cl = (int)host_namelen;
            if (cl > (int)sizeof(base) - 1)
              cl = (int)sizeof(base) - 1;
            while (hl < cl && host_name[hl]) { base[hl] = host_name[hl]; hl++; }
          }
          Permit();
        }
        base[hl] = 0;
        for (i2 = 0; i2 < hl; i2++)
          if (base[i2] == '.') { dotted = 1; break; }

        /*
         * Report a fully qualified name where we can, which is what other stacks do
         * and what programs asking gethostname() for "who am I on this network"
         * expect. In order:
         *
         *   1. no name at all anywhere    -> leave whatever is there
         *   2. already qualified          -> use it as it stands
         *   3. bare name AND a domain     -> join them (whichever the name came from)
         *   4. bare name and no domain    -> nothing to qualify it with
         *
         * If the joined name will not validate -- too long for MAXHOSTNAMELEN, or a
         * malformed domain -- fall back to the bare name rather than setting nothing.
         * A short name beats no name.
         */
        if (hl == 0) {
          /* nothing to name ourselves with */
        } else if (!dotted && L.domain[0]) {
          int k2 = 0;
          for (i2 = 0; base[i2] && k2 < (int)sizeof(fq) - 2; i2++)      fq[k2++] = base[i2];
          fq[k2++] = '.';
          for (i2 = 0; L.domain[i2] && k2 < (int)sizeof(fq) - 1; i2++)  fq[k2++] = L.domain[i2];
          fq[k2] = 0;
          if (ng_hostname_valid(fq, k2))
            sethostname(fq, (size_t)k2);
          else if (from_dhcp && ng_hostname_valid(base, hl))
            sethostname(base, (size_t)hl);
        } else if (from_dhcp && ng_hostname_valid(base, hl)) {
          /* Only worth storing when it came from the server -- if it came from the
           * config it is already the name we hold, and re-setting it is a no-op. */
          sethostname(base, (size_t)hl);
        }
      }
      aam->aam_Address = L.addr;
      aam->aam_ServerAddress = L.serverid;
      aam->aam_SubnetMask = L.mask;
      aam->aam_LeaseTime = L.lease;
      aam->aam_RequestedAddress = 0;
      if (aam->aam_RouterTable && aam->aam_RouterTableSize >= 1) aam->aam_RouterTable[0] = L.router;
      if (aam->aam_DNSTable) {
        long di;
        for (di = 0; di < aam->aam_DNSTableSize && di < NG_DHCP_MAXDNS; di++)
          aam->aam_DNSTable[di] = L.dns[di];
      }
      /* Report the domain in the aam too (Roadshow AAM protocol), if the caller
       * allocated the buffer. Bounded copy. */
      if (aam->aam_DomainName && aam->aam_DomainNameSize > 0) {
        long dl = (long)strlen(L.domain);
        if (dl > aam->aam_DomainNameSize - 1) dl = aam->aam_DomainNameSize - 1;
        bcopy((caddr_t)L.domain, (caddr_t)aam->aam_DomainName, dl);
        aam->aam_DomainName[dl] = 0;
      }
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
      ng_dhcp_finish_aam(ctx, aam);
      ng_dhcp_background(sb, s, tx, rx, ifname, mac, llad, ctx);
      goto cleanup;
    }
  }
  aam->aam_Result = ctx->dc_abort ? AAMR_Aborted : AAMR_Timeout;

reply:
  ng_dhcp_finish_aam(ctx, aam);
cleanup:
  if (ctx->dc_internal)
    ng_iface_reconfig_done(ifname);	/* another reconfigure may start now */
  if (s >= 0) d_closesocket(sb, s);
  if (tx) FreeVec(tx);
  if (rx) FreeVec(rx);
  if (sb) CloseLibrary(sb);
  FreeVec(ctx);
}

/* Context handed to the reconfigure helper. The helper frees it. */
struct ng_reconf_ctx {
  char rc_ifname[IFNAMSIZ + 8];
};

static void ng_reconfig_task(void);
static int  ng_reconfig_start_dhcp(const char *ifname);

/*
 * PORT (AmiTCP_NG): configure an interface again after its device came back online.
 *
 * Called by sana_poll() (net/if_sana.c) in the network task, once per interface
 * whose driver reported S2EVENT_ONLINE. Going offline scrubbed the address, the
 * routes and the dynamic name servers, so "online" alone leaves an interface that
 * is running but unnumbered; the Roadshow SDK describes SM_Online as performing
 * "the other necessary configuration operations", and this is them.
 *
 * THIS IS THE ONLY WAY AN INTERFACE COMES BACK. Online/Offline talk to the driver
 * with S2_ONLINE/S2_OFFLINE and never open bsdsocket.library, so an operator
 * cycling a link arrives here exactly as a replugged cable does. It is the normal
 * recovery path, not a rare corner case.
 *
 * It used to restore from a small snapshot the stack kept of what it had last been
 * told -- address, mask, default gateway -- which answered the wrong question. A
 * config file may also name extra routes, static name servers and a search domain,
 * and none of those were remembered, so an interface came back QUIETLY HALF
 * CONFIGURED: most visibly, with its name servers gone. A snapshot is also a "keep
 * it warm just in case" that has to be maintained in step with everything that can
 * ever change an interface.
 *
 * So the stack reads the file, exactly as the tool does at boot, and an interface
 * is either fully up or fully down. There is nothing left to keep in step.
 *
 * The read must NOT happen here. This is the network task; DOS blocks; and the
 * network task is what moves the packets the configuration itself depends on. All
 * this does is confirm the interface is real, mark it reconfiguring, and hand the
 * work to a helper Process -- the shape BeginInterfaceConfig() has always used.
 */
void
ng_reconfigure_interface(const char *ifname)
{
  struct ifnet *ifp;
  struct ng_reconf_ctx *ctx;
  struct Process *proc;
  spl_t s;
  int i;

  if (ifname == NULL || ifname[0] == '\0')
    return;

  /* Does it still exist? It may be removed the instant we drop splimp(), which is
   * why everything below works from the NAME and never from the softc. */
  s = splimp();
  if ((ifp = ifunit((char *)ifname)) == NULL || ifp->if_type != IFT_SANA) {
    splx(s);
    return;
  }
  ((struct sana_softc *)ifp)->ss_reconfiguring = 1;
  splx(s);

  ctx = AllocVec(sizeof(*ctx), MEMF_PUBLIC | MEMF_CLEAR);
  if (ctx == NULL) {
    log(LOG_ERR, "%s: back online but there was no memory to reconfigure it", ifname);
    ng_iface_reconfig_done(ifname);	/* or nothing will ever try again */
    return;
  }
  for (i = 0; i < (int)sizeof(ctx->rc_ifname) - 1 && ifname[i]; i++)
    ctx->rc_ifname[i] = ifname[i];
  ctx->rc_ifname[i] = '\0';

  /*
   * Under Forbid(), exactly as BeginInterfaceConfig() does it: the helper reads its
   * context out of tc_UserData as its first act, so it must not be allowed to run
   * until that field is set. Omitting this made a helper dereference whatever
   * tc_UserData happened to hold -- with no MMU, that wedged the machine the moment
   * a device came back online.
   */
  Forbid();
  proc = CreateNewProcTags(NP_Entry, (LONG)&ng_reconfig_task,
			   NP_Name, (LONG)"AmiTCP_NG reconfig",
			   NP_Priority, 0,
			   NP_StackSize, 8192,
			   TAG_DONE, 0);
  if (proc != NULL)
    proc->pr_Task.tc_UserData = (APTR)ctx;	/* safe: child is blocked by Forbid */
  Permit();

  if (proc == NULL) {
    FreeVec(ctx);
    log(LOG_ERR, "%s: back online but the reconfigure helper could not be started",
	ifname);
    ng_iface_reconfig_done(ifname);
    return;
  }

  /* No handshake, no Wait(): this is the network task. The helper owns ctx outright
   * from the moment it runs, and nothing here touches it again. */
}

/*
 * Start a DHCP exchange for an interface that has just come back, using the same
 * internal helper a device-initiated online has always used.
 *
 * Deliberately NOT through CreateAddrAllocMessage()/BeginInterfaceConfig(): those
 * spawn a helper of their own and expect a caller blocked on a reply port, so
 * going that way would nest a second Process and park this one on a port for the
 * whole lease timeout, to reach code we can call directly.
 *
 * Returns 1 if the helper was started (it then owns the reconfigure interlock and
 * clears it in its own cleanup), 0 if it could not be.
 */
static int
ng_reconfig_start_dhcp(const char *ifname)
{
  struct ng_aam      *aam;
  struct ng_dhcp_ctx *ctx;
  struct Process     *proc;
  int i;

  /* A message of our own. Every optional table stays NULL: the helper only writes
   * through those pointers when they are non-NULL, and there is no application
   * caller here to report a lease back to. */
  aam = AllocVec(sizeof(*aam), MEMF_PUBLIC | MEMF_CLEAR);
  if (aam == NULL)
    return 0;
  ctx = AllocVec(sizeof(*ctx), MEMF_PUBLIC | MEMF_CLEAR);
  if (ctx == NULL) { FreeVec(aam); return 0; }

  aam->aam_Message.mn_Node.ln_Type = NT_MESSAGE;
  aam->aam_Message.mn_Length       = sizeof(*aam);
  aam->aam_Message.mn_ReplyPort    = NULL;	/* nobody is waiting; see dc_internal */
  aam->aam_Version                 = AAM_VERSION_NG;
  aam->aam_Protocol                = AAMP_DHCP;
  for (i = 0; i < 15 && ifname[i]; i++)
    aam->aam_InterfaceName[i] = ifname[i];
  aam->aam_InterfaceName[i] = '\0';
  aam->aam_Timeout                 = 30;	/* seconds; the helper floors this */

  ctx->dc_aam      = aam;
  ctx->dc_parent   = FindTask(NULL);
  ctx->dc_internal = 1;				/* clears ss_reconfiguring on cleanup */

  Forbid();
  proc = CreateNewProcTags(NP_Entry, (LONG)&ng_dhcp_task,
			   NP_Name, (LONG)"AmiTCP_NG DHCP",
			   NP_Priority, 0,
			   NP_StackSize, 8192,
			   TAG_DONE, 0);
  if (proc != NULL)
    proc->pr_Task.tc_UserData = (APTR)ctx;	/* safe: child is blocked by Forbid */
  Permit();

  if (proc == NULL) {
    FreeVec(ctx);
    FreeVec(aam);
    log(LOG_ERR, "%s: back online but the DHCP helper could not be started", ifname);
    return 0;
  }
  log(LOG_NOTICE, "%s: device back online, requesting an address", ifname);
  return 1;
}

/*
 * The reconfigure helper Process: read this interface's config file and put the
 * interface back the way that file says it should be.
 *
 * EVERY EXIT MUST RELEASE THE INTERLOCK. sana_reconfig_poll() will not start
 * another reconfigure while ss_reconfiguring is set -- which is what stops a
 * flapping device stacking helpers -- so any path that returns without clearing it
 * leaves the interface unable to respond to a later online event, or to the
 * operator's Online, until the machine is rebooted. Silently. The single exception
 * is the DHCP hand-off, where ng_dhcp_task takes the flag over and clears it in its
 * own cleanup; that is why the hand-off is the last thing this function does.
 */
static void
ng_reconfig_task(void)
{
  struct Process *me = (struct Process *)FindTask(NULL);
  struct ng_reconf_ctx *rc = (struct ng_reconf_ctx *)me->pr_Task.tc_UserData;
  struct ng_ifcfg cfg;
  struct Library *sb;
  char ifname[IFNAMSIZ + 8];
  int  i;

  if (rc == NULL)
    return;
  for (i = 0; i < (int)sizeof(ifname) - 1 && rc->rc_ifname[i]; i++)
    ifname[i] = rc->rc_ifname[i];
  ifname[i] = '\0';
  FreeVec(rc);

  if (!ng_ifcfg_find(ifname, &cfg)) {
    /*
     * Nothing to bring it up from. Leave it exactly as offline left it --
     * registered, unnumbered, routeless -- rather than half configure it from
     * memory. Fully up or fully down, and this is down. Fixing the file and
     * cycling the link arrives back here.
     */
    log(LOG_ERR, "%s: back online, but no configuration was found in "
	"DEVS:NetInterfaces or SYS:Storage/NetInterfaces -- leaving it down", ifname);
    ng_iface_reconfig_done(ifname);
    return;
  }

  /*
   * Open our own library base and drive the PUBLIC vectors from here on.
   *
   * The internal appliers (ng_apply_iface_config, ng_route_op) document that the
   * caller holds the syscall semaphore, and this helper is a separate Process that
   * holds nothing. The old code called them straight because it ran ON the network
   * task, inside the stack, where opening a library would have re-entered the
   * lazy-start path; that reasoning does not survive the move into a Process. Going
   * through the vectors takes the semaphore properly and makes this branch match
   * the DHCP one, which has always opened its own base.
   */
  sb = OpenLibrary((STRPTR)"bsdsocket.library", 3L);
  if (sb == NULL) {
    log(LOG_ERR, "%s: back online but bsdsocket.library could not be opened to "
	"reconfigure it", ifname);
    ng_iface_reconfig_done(ifname);
    return;
  }

  /*
   * The search domain first, and on BOTH paths -- as the FALLBACK.
   *
   * Applied before the DHCP exchange precisely so a lease can supersede it: a
   * server handing out a domain is authoritative for the network just rejoined,
   * the same rule the host name follows. If the server offers none, this is what
   * the machine keeps. Priority: DHCP > explicit domain= > hostname-derived.
   *
   * This is also the only place domain= is read on this path at all -- the lease
   * applier cannot see the config file.
   */
  if (cfg.domain[0])
    d_setdomain(sb, cfg.domain);

  if (cfg.dhcp) {
    /*
     * A NEW lease, never the old address. The device may have been offline for a
     * minute or for months and the stack cannot tell, so the old address says
     * nothing about the network being rejoined -- it may be a different network
     * entirely.
     */
    CloseLibrary(sb);			/* the DHCP helper opens its own */
    if (!ng_reconfig_start_dhcp(ifname))
      ng_iface_reconfig_done(ifname);	/* spawn failed: release the interlock */
    return;				/* on success the helper owns the flag */
  }

  /* --- static --------------------------------------------------------------- */
  if (cfg.have_address && cfg.address[0]) {
    struct TagItem ctags[5];
    int n = 0;

    /* Mask BEFORE address: in_ifinit() keys the connected route with whatever mask
     * is in force when the address lands, and a later mask change does not re-key
     * it -- leaving a route that teardown cannot find by its own key. */
    if (cfg.netmask[0]) {
      ctags[n].ti_Tag = IFC_NetMask; ctags[n].ti_Data = (ULONG)cfg.netmask; n++;
    }
    ctags[n].ti_Tag = IFC_Address;   ctags[n].ti_Data = (ULONG)cfg.address; n++;
    if (cfg.mtu > 0) {
      ctags[n].ti_Tag = IFC_MTU;     ctags[n].ti_Data = (ULONG)cfg.mtu; n++;
    }
    ctags[n].ti_Tag = IFC_State;     ctags[n].ti_Data = IFC_State_Up; n++;
    ctags[n].ti_Tag = 0;             ctags[n].ti_Data = 0;

    if (d_configiface(sb, ifname, ctags) != 0) {
      log(LOG_ERR, "%s: back online but its address could not be restored", ifname);
      CloseLibrary(sb);
      ng_iface_reconfig_done(ifname);
      return;
    }
  }

  if (cfg.gateway[0]) {
    struct TagItem rt[2];
    rt[0].ti_Tag = RTA_DefaultGateway; rt[0].ti_Data = (ULONG)cfg.gateway;
    rt[1].ti_Tag = 0;                  rt[1].ti_Data = 0;
    if (d_addroute(sb, rt) != 0)
      log(LOG_NOTICE, "%s: its default route could not be restored", ifname);
  }

  /*
   * Name servers, ATTRIBUTED to this interface. Through the configuration path
   * rather than the bare AddDomainNameServer vector, which deliberately records no
   * owner: a server added that way is never withdrawn when this interface goes
   * away again. Going through here is what makes the withdrawal on the next
   * offline match what was installed on this online.
   */
  for (i = 0; i < cfg.nns; i++) {
    struct TagItem dt[2];
    dt[0].ti_Tag = NGCT_NameServer; dt[0].ti_Data = (ULONG)cfg.ns[i];
    dt[1].ti_Tag = 0;               dt[1].ti_Data = 0;
    if (d_configiface(sb, ifname, dt) != 0)
      log(LOG_NOTICE, "%s: name server %s could not be restored", ifname, cfg.ns[i]);
  }

  log(LOG_NOTICE, "%s: device back online, reconfigured from its config file", ifname);
  CloseLibrary(sb);
  ng_iface_reconfig_done(ifname);
}

/* BeginInterfaceConfig (LVO -486): kick off the async exchange. */
VOID SAVEDS RAF2(_BeginInterfaceConfig,
		 struct SocketBase *,	libPtr,	a6,
		 struct ng_aam *,	aam,	a0)
#if 0
{
#endif
  /*
   * DEAD BASE FIRST, before NG_ENSURE_STACK(), exactly as the other gated vectors
   * do it -- a call through a base the operator killed must not lazily start a
   * brand new stack. NG_ENSURE_STACK() carries its own sbDead guard too, so the
   * order is not load-bearing today; it is written this way so this function does
   * not quietly depend on that second guard still being there.
   *
   * It cannot use the plain macro. The caller is BLOCKED on this message's reply
   * port -- that is the whole protocol -- so returning without replying wedges it
   * for ever, a worse failure than the one being prevented. Reply as the
   * out-of-memory path below does, and do it before the helper is spawned: that
   * helper opens a FRESH base, whose sbDead is naturally FALSE, and would start a
   * whole new stack one call removed from the base just refused.
   */
  if (libPtr != NULL && libPtr->sbDead && aam != NULL) {
    writeErrnoValue(libPtr, ENETDOWN);
    aam->aam_Result = AAMR_InterfaceNotKnown;   /* no AAMR_* means "stack is gone" */
    ReplyMsg((struct Message *)aam);
    return;
  }

  NG_ENSURE_STACK();
  struct ng_dhcp_ctx *ctx;
  struct Process *proc;

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
  NG_CHECK_DEAD();
  NG_ENSURE_STACK();
  (void)libPtr;
  if (aam == NULL)
    return;
  Forbid();
  if (aam->aam_Reserved != 0)		/* still running -> flag the helper */
    ((struct ng_dhcp_ctx *)aam->aam_Reserved)->dc_abort = 1;
  Permit();
}
