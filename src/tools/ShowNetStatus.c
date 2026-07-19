/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ShowNetStatus -- report the state of the TCP/IP stack. Our own name-and-behaviour-
 * compatible replacement for Roadshow's ShowNetStatus; it uses the same ReadArgs
 * template and, for the no-argument "network status summary" (the form front-ends such
 * as Roadie run and then parse -- `ShowNetStatus >RAM:shownetstatus`), the same wording
 * so those tools work against the AmiTCP_NG stack unchanged. It drives the stack solely
 * through the public bsdsocket.library extension API (ObtainInterfaceList /
 * QueryInterfaceTagList / GetRouteInfo / ObtainDomainNameServerList).
 *
 * Implemented views: the default status summary, INTERFACES (the compact table),
 * INTERFACE <name> (verbose per-interface detail), ROUTES (the routing table) and DNS.
 * The per-protocol statistics switches (ICMP/IP/TCP/UDP/...) are accepted for argument
 * compatibility but not dumped by this build (a one-line note is shown); see the
 * project docs. NetMon and Roadie do not rely on those dumps.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/ShowNetStatus.c -o ShowNetStatus
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <utility/tagitem.h>

#include "ng_lvo.h"

struct Library *SocketBase = 0;

/* A minimal sockaddr_in mirror (we only read family + address). */
/* A full 16-byte sockaddr_in mirror. The sin_zero[8] tail is REQUIRED: the library's
 * IFQ_Address/IFQ_NetMask handlers bcopy a whole sizeof(struct sockaddr_in)==16 bytes
 * into the buffer we pass, so an 8-byte struct here would overrun onto the adjacent
 * stack locals (MTU, unit, packet counts) and print garbage for them. */
struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE sin_zero[8]; };
/* COMPILE-TIME GUARD: any buffer handed to an IFQ_Address/NetMask/Dest/Broadcast query
 * MUST be at least a full sockaddr_in (16 bytes) -- the library bcopy's that many into
 * it. If a future edit shrinks ng_sin below 16 bytes, this breaks the build instead of
 * silently overrunning the adjacent stack locals (which is the bug this once was). */
typedef char ng_sin_must_hold_a_sockaddr_in[(sizeof(struct ng_sin) >= 16) ? 1 : -1];

/* IFQ tags used here (see amiga_roadshow.c / the Roadshow SDK). */
#define IFQ_DeviceName_      (IFQ_BASE + 1)
#define IFQ_DeviceUnit_      (IFQ_BASE + 2)
#define IFQ_HardwareType_    (IFQ_BASE + 7)
#define IFQ_PacketsReceived_ (IFQ_BASE + 8)
#define IFQ_PacketsSent_     (IFQ_BASE + 9)
#define IFQ_NetMask_         (IFQ_BASE + 17)

static const struct { long n; const char *s; } hw_types[] = {
  { 1, "Ethernet" }, { 6, "IEEE 802" }, { 7, "Arcnet" }, { 11, "LocalTalk" },
  { 254, "PPP" }, { 255, "SLIP" }, { 256, "CSLIP" }, { 257, "PLIP" },
};

static const char *hw_name(long t, char *buf)
{
  int i;
  for (i = 0; i < (int)(sizeof(hw_types)/sizeof(hw_types[0])); i++)
    if (hw_types[i].n == t) return hw_types[i].s;
  { /* "Unknown (N)" */
    static const char *u = "Unknown";
    char tmp[12]; int k = 0, j; unsigned long v = (unsigned long)t;
    const char *p = "Unknown (";
    for (j = 0; p[j]; j++) buf[k++] = p[j];
    if (v == 0) tmp[0] = '0', j = 1; else for (j = 0; v; v /= 10) tmp[j++] = '0' + (v % 10);
    while (j--) buf[k++] = tmp[j];
    buf[k++] = ')'; buf[k] = 0; (void)u;
  }
  return buf;
}

/* Format a network-order IPv4 address (as stored in sin_addr) as dotted decimal. */
static void fmt_ip(ULONG a, char *buf)
{
  unsigned parts[4];
  int k = 0, i;
  parts[0] = (a >> 24) & 0xFF; parts[1] = (a >> 16) & 0xFF;
  parts[2] = (a >> 8) & 0xFF;  parts[3] = a & 0xFF;
  for (i = 0; i < 4; i++) {
    unsigned v = parts[i]; char t[4]; int n = 0;
    if (v == 0) t[n++] = '0'; else for (; v; v /= 10) t[n++] = '0' + (v % 10);
    while (n--) buf[k++] = t[n];
    if (i < 3) buf[k++] = '.';
  }
  buf[k] = 0;
}

/* -- default status summary (the Roadie-parsed form) ------------------------- */
static void show_summary(void)
{
  struct List *iflist, *dnslist;
  struct Node *node;
  char ip[24];
  const char *hostif = 0;
  ULONG hostaddr = 0;

  Printf((STRPTR)"Network status summary\n");

  /* Local host address: the first configured interface whose address is not loopback.
   * The interface list now includes lo0 (127.0.0.1), so skip any 127/8 address here --
   * the host address is a real, routable one. */
  iflist = ng_obtainiflist();
  if (iflist) {
    for (node = iflist->lh_Head; node->ln_Succ != NULL; node = node->ln_Succ) {
      struct ng_sin sin; struct TagItem tg[2];
      sin.sin_addr = 0;
      tg[0].ti_Tag = IFQ_Address; tg[0].ti_Data = (ULONG)&sin;
      tg[1].ti_Tag = TAG_END;     tg[1].ti_Data = 0;
      if (ng_queryif(node->ln_Name, tg) == 0 && sin.sin_addr != 0 &&
          ((sin.sin_addr >> 24) & 0xFF) != 127) {
        hostaddr = sin.sin_addr;
        hostif   = node->ln_Name;
        break;
      }
    }
  }
  if (hostif) {
    fmt_ip(hostaddr, ip);
    Printf((STRPTR)"Local host address         = %s (on interface '%s')\n", (LONG)ip, (LONG)hostif);
  } else {
    Printf((STRPTR)"Local host address         = (Not configured)\n");
  }
  if (iflist) ng_releaseiflist(iflist);

  /* Default gateway: the default route's gateway address. */
  {
    char *base = (char *)ng_getrouteinfo(0 /*AF_UNSPEC*/, 0);
    struct ng_rtm *rtm;
    int found = 0;
    if (base) {
      int guard = 0;
      for (rtm = (struct ng_rtm *)base; rtm->rtm_msglen > 0 && guard < 4096;
           rtm = (struct ng_rtm *)((char *)rtm + rtm->rtm_msglen), guard++) {
        char *cp = (char *)(rtm + 1);
        struct ng_sin *dst, *gw;
        if (rtm->rtm_msglen < sizeof(struct ng_rtm)) break;	/* malformed entry */
        if ((rtm->rtm_addrs & (NG_RTA_DST | NG_RTA_GATEWAY)) != (NG_RTA_DST | NG_RTA_GATEWAY))
          continue;
        if ((rtm->rtm_flags & NG_RTF_GATEWAY) == 0)
          continue;
        dst = (struct ng_sin *)cp;
        cp += NG_RT_ROUNDUP(dst->sin_len ? dst->sin_len : 16);
        gw  = (struct ng_sin *)cp;
        if (dst->sin_addr == 0) {			/* 0.0.0.0 -> the default route */
          fmt_ip(gw->sin_addr, ip);
          Printf((STRPTR)"Default gateway address    = %s\n", (LONG)ip);
          found = 1;
          break;
        }
      }
      ng_freerouteinfo(base);
    }
    if (!found)
      Printf((STRPTR)"Default gateway address    = (Not configured)\n");
  }

  /* Domain name servers. */
  Printf((STRPTR)"Domain name system servers = ");
  dnslist = ng_obtaindnslist();
  if (dnslist && dnslist->lh_Head->ln_Succ != NULL) {
    struct ng_dnsnode *dn; int first = 1;
    for (dn = (struct ng_dnsnode *)dnslist->lh_Head;
         dn->dnsn_MinNode.mln_Succ != NULL;
         dn = (struct ng_dnsnode *)dn->dnsn_MinNode.mln_Succ) {
      if (!first) Printf((STRPTR)", ");
      Printf((STRPTR)"%s", (LONG)dn->dnsn_Address);
      first = 0;
    }
    Printf((STRPTR)"\n");
  } else {
    Printf((STRPTR)"(Not configured)\n");
  }
  if (dnslist) ng_releasednslist(dnslist);
}

/* -- compact interface table (INTERFACES) ------------------------------------ */
static void show_interfaces(void)
{
  struct List *iflist = ng_obtainiflist();
  struct Node *node;
  int n = 0;

  if (!iflist) { Printf((STRPTR)"No interfaces are currently available.\n"); return; }

  for (node = iflist->lh_Head; node->ln_Succ != NULL; node = node->ln_Succ) {
    struct ng_sin sin; LONG mtu = 0, hwt = 0, state = 0; ULONG rx = 0, tx = 0;
    struct TagItem tg[8]; char ip[24], hb[24];
    sin.sin_addr = 0;
    tg[0].ti_Tag = IFQ_MTU;             tg[0].ti_Data = (ULONG)&mtu;
    tg[1].ti_Tag = IFQ_HardwareType_;   tg[1].ti_Data = (ULONG)&hwt;
    tg[2].ti_Tag = IFQ_PacketsReceived_;tg[2].ti_Data = (ULONG)&rx;
    tg[3].ti_Tag = IFQ_PacketsSent_;    tg[3].ti_Data = (ULONG)&tx;
    tg[4].ti_Tag = IFQ_Address;         tg[4].ti_Data = (ULONG)&sin;
    tg[5].ti_Tag = IFQ_State;           tg[5].ti_Data = (ULONG)&state;
    tg[6].ti_Tag = TAG_END;             tg[6].ti_Data = 0;
    if (ng_queryif(node->ln_Name, tg) != 0) continue;

    if (n++ == 0)
      Printf((STRPTR)"%-16s %5s %-12s %-16s %8s %8s %s\n",
             (LONG)"Name", (LONG)"MTU", (LONG)"Type", (LONG)"Address",
             (LONG)"Received", (LONG)"Sent", (LONG)"Status");
    if (sin.sin_addr) fmt_ip(sin.sin_addr, ip); else { ip[0] = '-'; ip[1] = 0; }
    Printf((STRPTR)"%-16s %5ld %-12s %-16s %8ld %8ld %s\n",
           (LONG)node->ln_Name, mtu, (LONG)hw_name(hwt, hb), (LONG)ip,
           (LONG)rx, (LONG)tx, (LONG)((state == NG_SM_Up) ? "Up" : "Down"));
  }
  if (n == 0) Printf((STRPTR)"No interfaces are currently available.\n");
  ng_releaseiflist(iflist);
}

/* -- verbose per-interface detail (INTERFACE <name>) ------------------------- */
static int show_interface_info(const char *name)
{
  struct ng_sin sin, mask; LONG mtu = 0, hwt = 0, state = 0, unit = 0; ULONG rx = 0, tx = 0;
  struct TagItem tg[12]; char ip[24], hb[24]; char *dev = 0;   /* IFQ_DeviceName -> STRPTR */
  sin.sin_addr = 0; mask.sin_addr = 0;
  tg[0].ti_Tag = IFQ_DeviceName_;     tg[0].ti_Data = (ULONG)&dev;
  tg[1].ti_Tag = IFQ_DeviceUnit_;     tg[1].ti_Data = (ULONG)&unit;
  tg[2].ti_Tag = IFQ_MTU;             tg[2].ti_Data = (ULONG)&mtu;
  tg[3].ti_Tag = IFQ_HardwareType_;   tg[3].ti_Data = (ULONG)&hwt;
  tg[4].ti_Tag = IFQ_PacketsReceived_;tg[4].ti_Data = (ULONG)&rx;
  tg[5].ti_Tag = IFQ_PacketsSent_;    tg[5].ti_Data = (ULONG)&tx;
  tg[6].ti_Tag = IFQ_Address;         tg[6].ti_Data = (ULONG)&sin;
  tg[7].ti_Tag = IFQ_NetMask_;        tg[7].ti_Data = (ULONG)&mask;
  tg[8].ti_Tag = IFQ_State;           tg[8].ti_Data = (ULONG)&state;
  tg[9].ti_Tag = TAG_END;             tg[9].ti_Data = 0;
  if (ng_queryif((void *)name, tg) != 0) return 0;

  Printf((STRPTR)"Interface \"%s\"\n", (LONG)name);
  if (dev) Printf((STRPTR)"Device name                  = %s\n", (LONG)dev);
  Printf((STRPTR)"Device unit number           = %ld\n", unit);
  Printf((STRPTR)"Hardware type                = %s\n", (LONG)hw_name(hwt, hb));
  Printf((STRPTR)"Maximum transmission unit    = %ld Bytes\n", mtu);
  if (sin.sin_addr)  { fmt_ip(sin.sin_addr, ip);  Printf((STRPTR)"Address                      = %s\n", (LONG)ip); }
  else                                             Printf((STRPTR)"Address                      = (Not configured)\n");
  if (mask.sin_addr) { fmt_ip(mask.sin_addr, ip); Printf((STRPTR)"Network mask                 = %s\n", (LONG)ip); }
  Printf((STRPTR)"Packets received             = %ld\n", (LONG)rx);
  Printf((STRPTR)"Packets sent                 = %ld\n", (LONG)tx);
  Printf((STRPTR)"Link status                  = %s\n", (LONG)((state == NG_SM_Up) ? "Up" : "Down"));
  return 1;
}

/* -- routing table (ROUTES) -------------------------------------------------- */
static void show_routes(void)
{
  char *base = (char *)ng_getrouteinfo(0, 0);
  struct ng_rtm *rtm;
  char d[24], g[24];

  Printf((STRPTR)"Routing tables\n");
  Printf((STRPTR)"%-18s %-18s %s\n", (LONG)"Destination", (LONG)"Gateway", (LONG)"Flags");
  if (!base) return;
  { int guard = 0;
  for (rtm = (struct ng_rtm *)base; rtm->rtm_msglen > 0 && guard < 4096;
       rtm = (struct ng_rtm *)((char *)rtm + rtm->rtm_msglen), guard++) {
    char *cp = (char *)(rtm + 1);
    struct ng_sin *dst, *gw = 0;
    char flags[8]; int fi = 0;
    if (rtm->rtm_msglen < sizeof(struct ng_rtm)) break;		/* malformed entry */
    if ((rtm->rtm_addrs & NG_RTA_DST) == 0) continue;
    dst = (struct ng_sin *)cp;
    cp += NG_RT_ROUNDUP(dst->sin_len ? dst->sin_len : 16);
    if (rtm->rtm_addrs & NG_RTA_GATEWAY) gw = (struct ng_sin *)cp;

    if (dst->sin_addr == 0) { d[0]='d';d[1]='e';d[2]='f';d[3]='a';d[4]='u';d[5]='l';d[6]='t';d[7]=0; }
    else fmt_ip(dst->sin_addr, d);
    if (gw && (rtm->rtm_flags & NG_RTF_GATEWAY)) fmt_ip(gw->sin_addr, g);
    else { g[0]='*'; g[1]=0; }

    if (rtm->rtm_flags & NG_RTF_UP)      flags[fi++] = 'U';
    if (rtm->rtm_flags & NG_RTF_GATEWAY) flags[fi++] = 'G';
    flags[fi] = 0;
    Printf((STRPTR)"%-18s %-18s %s\n", (LONG)d, (LONG)g, (LONG)flags);
  }
  }
  ng_freerouteinfo(base);
}

int main(void)
{
  struct RDArgs *rda;
  /* INTERFACE/M, INTERFACES/S, ARP/S, ROUTES/S, DNS/S, ICMP/S, IGMP/S, IP/S, MEM/S,
   * MR/S, RT/S, TCP/S, UDP/S, TCPSOCKETS/S, UDPSOCKETS/S, NAMES/S, ALL/S, REPEAT/S,
   * QUIET/S  -- Roadshow's template, verbatim order. */
  LONG a[19];
  int  i, any_view, any_stats, rc = RETURN_OK;
  int  quiet;

  for (i = 0; i < 19; i++) a[i] = 0;
  rda = ReadArgs((STRPTR)
    "INTERFACE/M,INTERFACES/S,ARPCACHE=ARP/S,ROUTES/S,DNS=DOMAINNAMESERVERS/S,"
    "ICMP/S,IGMP/S,IP/S,MB=MEMORY/S,MR=MULTICASTROUTING/S,RT=ROUTING/S,TCP/S,UDP/S,"
    "TCPSOCKETS/S,UDPSOCKETS/S,NAMES/S,ALL/S,REPEAT/S,QUIET/S", a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)"ShowNetStatus"); return RETURN_ERROR; }
  quiet = a[18] ? 1 : 0;

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    if (!quiet) Printf((STRPTR)"ShowNetStatus: bsdsocket.library v4+ not available.\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  /* ALL selects the informational views. */
  if (a[16]) { a[1] = a[3] = a[4] = TRUE; }

  any_stats = a[2] || a[5] || a[6] || a[7] || a[8] || a[9] || a[10] || a[11] ||
              a[12] || a[13] || a[14];
  any_view  = (a[0] != 0) || a[1] || a[3] || a[4] || any_stats;

  if (!any_view) {
    show_summary();
  } else {
    int something = 0;
    if (a[0]) {			/* INTERFACE/M -> array of names */
      STRPTR *key = (STRPTR *)a[0]; STRPTR nm;
      while ((nm = *key++) != NULL) {
        if (something++) Printf((STRPTR)"\n");
        if (!show_interface_info((const char *)nm)) {
          if (!quiet) Printf((STRPTR)"ShowNetStatus: unable to obtain information on interface \"%s\".\n", (LONG)nm);
          rc = RETURN_WARN;
        }
      }
    }
    if (a[1]) { if (something++) Printf((STRPTR)"\n"); show_interfaces(); }
    if (a[3]) { if (something++) Printf((STRPTR)"\n"); show_routes(); }
    if (a[4]) {			/* DNS */
      struct List *dl = ng_obtaindnslist();
      if (something++) Printf((STRPTR)"\n");
      Printf((STRPTR)"Domain name servers\n");
      if (dl && dl->lh_Head->ln_Succ != NULL) {
        struct ng_dnsnode *dn;
        for (dn = (struct ng_dnsnode *)dl->lh_Head; dn->dnsn_MinNode.mln_Succ != NULL;
             dn = (struct ng_dnsnode *)dn->dnsn_MinNode.mln_Succ)
          Printf((STRPTR)"%s\n", (LONG)dn->dnsn_Address);
      } else Printf((STRPTR)"No domain name servers are configured.\n");
      if (dl) ng_releasednslist(dl);
    }
    if (any_stats && !quiet) {
      if (something++) Printf((STRPTR)"\n");
      Printf((STRPTR)"(Per-protocol statistics are not produced by this build of ShowNetStatus.)\n");
    }
  }

  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
