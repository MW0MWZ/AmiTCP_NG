/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * netstat -- network status: interfaces, protocol statistics, routing table.
 *
 * AmiTCP 3.0b2 shipped a netstat (an ARexx script); Roadshow ships none. This is
 * a native command that keeps the AmiTCP column headings, so scripts that parsed
 * the old output keep working:
 *
 *     Destination          Gateway              Flags     Refs     Use  Interface
 *
 *   netstat            interfaces + routing table (the common case)
 *   netstat -i         interfaces only
 *   netstat -r         routing table only
 *   netstat -s         protocol statistics (ip / icmp / tcp / udp)
 *   netstat -a         listed for completeness; not available -- see below
 *
 * WHY THERE IS NO -a YET. The connection list needs the library to enumerate its
 * TCP/UDP protocol control blocks, and GetNetworkStatistics answers EINVAL for
 * the tcp_sockets/udp_sockets types today. That is a library gap, not something
 * this tool can work around, so -a says so plainly rather than printing an empty
 * table that looks like "no connections".
 *
 * -s IS THE INTERESTING ONE. Nothing in this stack has ever reported protocol
 * counters -- ShowNetStatus explicitly does not -- so retransmissions, duplicate
 * ACKs, out-of-order segments and checksum failures have been invisible. On a
 * link that looks slow for no reason, those four numbers usually say whether the
 * problem is loss, reordering, or neither.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase = 0;		/* ng_lvo.h's LVO stubs read this */

#include "ng_lvo.h"

/* A sockaddr_in mirror. MUST be >= 16 bytes: the library bcopies a whole
 * struct sockaddr_in into buffers handed to the IFQ_ address tags, and the
 * route records carry real sockaddrs. Same contract ShowNetStatus documents. */
struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE sin_zero[8]; };
typedef char ng_sin_must_hold_a_sockaddr_in[(sizeof(struct ng_sin) >= 16) ? 1 : -1];

/* Route flags we render. Values from net/route.h. */
#define NG_RTF_UP	0x1
#define NG_RTF_GATEWAY	0x2
#define NG_RTF_HOST	0x4
#define NG_RTF_DYNAMIC	0x10

/* ---------------------------------------------------------------- helpers -- */

static int ci_eq(const char *a, const char *b)
{
  while (*a && *b) {
    char ca = *a++, cb = *b++;
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
    if (ca != cb) return 0;
  }
  return *a == *b;
}

/* Dotted-quad into a caller buffer (>= 16 bytes). No sprintf: -noixemul. */
static void fmt_ip(ULONG a, char *out)
{
  int i, n = 0;
  for (i = 3; i >= 0; i--) {
    ULONG part = (a >> (i * 8)) & 0xFF;
    if (part >= 100) out[n++] = (char)('0' + part / 100);
    if (part >= 10)  out[n++] = (char)('0' + (part / 10) % 10);
    out[n++] = (char)('0' + part % 10);
    if (i) out[n++] = '.';
  }
  out[n] = 0;
}

/* ------------------------------------------------------------ interfaces -- */

static void show_interfaces(void)
{
  struct List *l;
  struct Node *n;

  Printf((STRPTR)"Name   Mtu    Address           Ipkts   Ierrs    Opkts   Oerrs  State\n");

  l = ng_obtainiflist();
  if (l == NULL) {
    Printf((STRPTR)"  (no interfaces)\n");
    return;
  }

  for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
    struct TagItem tg[7];
    struct ng_sin sin;
    LONG  mtu = 0, state = 0;
    ULONG rx = 0, tx = 0, bad = 0, oerr = 0;
    char ip[16];
    int i;

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;

    tg[0].ti_Tag = IFQ_MTU;             tg[0].ti_Data = (ULONG)&mtu;
    tg[1].ti_Tag = IFQ_Address;         tg[1].ti_Data = (ULONG)&sin;
    tg[2].ti_Tag = IFQ_PacketsReceived; tg[2].ti_Data = (ULONG)&rx;
    tg[3].ti_Tag = IFQ_PacketsSent;     tg[3].ti_Data = (ULONG)&tx;
    tg[4].ti_Tag = IFQ_BadData;         tg[4].ti_Data = (ULONG)&bad;
    tg[5].ti_Tag = NGIFQ_OutErrors;     tg[5].ti_Data = (ULONG)&oerr;
    tg[6].ti_Tag = TAG_END;             tg[6].ti_Data = 0;
    if (ng_queryif((void *)n->ln_Name, tg) != 0)
      continue;

    tg[0].ti_Tag = IFQ_State;           tg[0].ti_Data = (ULONG)&state;
    tg[1].ti_Tag = TAG_END;             tg[1].ti_Data = 0;
    ng_queryif((void *)n->ln_Name, tg);

    if (sin.sin_addr) fmt_ip(sin.sin_addr, ip);
    else { ip[0] = '-'; ip[1] = 0; }

    Printf((STRPTR)"%-6s %-6ld %-16s %7ld %7ld %8ld %7ld  %s\n",
           (LONG)n->ln_Name, mtu, (LONG)ip,
           (LONG)rx, (LONG)bad, (LONG)tx, (LONG)oerr,
           (LONG)((state == NG_SM_Up) ? "Up" : "Down"));
  }
  ng_releaseiflist(l);
}

/* --------------------------------------------------------- routing table -- */

static void show_routes(void)
{
  char *base;
  struct ng_rtm *rtm;
  int guard = 0;

  /* AmiTCP's netstat printed a Refs column; the route records this library
   * returns carry no reference count, so it is shown as "-" rather than a
   * column of zeroes that would read as real data. */
  Printf((STRPTR)"Destination          Gateway              Flags     Refs     Use  Interface\n");

  base = (char *)ng_getrouteinfo(0 /*AF_UNSPEC*/, 0 /*every route*/);
  if (base == NULL) {
    Printf((STRPTR)"  (no routes)\n");
    return;
  }

  for (rtm = (struct ng_rtm *)base;
       rtm->rtm_msglen > 0 && guard < 4096;
       rtm = (struct ng_rtm *)((char *)rtm + rtm->rtm_msglen), guard++) {
    char *cp = (char *)(rtm + 1);
    struct ng_sin *dst, *gw = NULL;
    char d[16], g[16], fl[8];
    int nf = 0;

    /* A short record would make the walk step backwards or into the middle of
     * a record. Stop rather than trust the length. */
    if (rtm->rtm_msglen < sizeof(struct ng_rtm)) break;
    if ((rtm->rtm_addrs & NG_RTA_DST) == 0) continue;

    dst = (struct ng_sin *)cp;
    cp += NG_RT_ROUNDUP(dst->sin_len ? dst->sin_len : sizeof(struct ng_sin));
    if (rtm->rtm_addrs & NG_RTA_GATEWAY) gw = (struct ng_sin *)cp;

    if (dst->sin_addr == 0) {
      d[0]='d'; d[1]='e'; d[2]='f'; d[3]='a'; d[4]='u'; d[5]='l'; d[6]='t'; d[7]=0;
    } else {
      fmt_ip(dst->sin_addr, d);
    }
    /* A gateway sockaddr that is not AF_INET (family 2) is a link-layer address
     * for a directly attached route -- there is no dotted quad to print. */
    if (gw && gw->sin_family == 2 && gw->sin_addr) fmt_ip(gw->sin_addr, g);
    else { g[0]='*'; g[1]=0; }

    if (rtm->rtm_flags & NG_RTF_UP)      fl[nf++] = 'U';
    if (rtm->rtm_flags & NG_RTF_GATEWAY) fl[nf++] = 'G';
    if (rtm->rtm_flags & NG_RTF_HOST)    fl[nf++] = 'H';
    if (rtm->rtm_flags & NG_RTF_DYNAMIC) fl[nf++] = 'D';
    fl[nf] = 0;

    Printf((STRPTR)"%-20s %-20s %-8s %4s %7ld  %ld\n",
           (LONG)d, (LONG)g, (LONG)fl,
           (LONG)"-", (LONG)rtm->rtm_use, (LONG)rtm->rtm_index);
  }
  ng_freerouteinfo(base);
}

/* ------------------------------------------------ protocol statistics -- */

/* Field names in struct order. These MUST match the structs in the netinet headers --
 * the library hands us a flat array of longs with no self-description, so a
 * name table that has drifted mislabels every figure after the drift point
 * rather than failing. The sizes below are asserted in the library at compile
 * time (see NG_STAT_TCP_OUR / NG_STAT_UDP_FULL), so a struct that grows breaks
 * the build; if that happens, add the name here too. */

static const char *ip_names[] = {
  "total packets received", "bad checksums", "packets too short",
  "not enough data", "header length bad", "ip length bad",
  "fragments received", "fragments dropped", "fragments timed out",
  "packets forwarded", "unreachable destination", "redirects sent",
  "unknown protocol", "packets delivered", "packets generated here",
  "lost -- no buffers", "packets reassembled", "output fragmented",
  "output fragments created", "could not fragment"
};
#define IP_N ((int)(sizeof(ip_names)/sizeof(ip_names[0])))

/*
 * ICMP is NOT a flat array of scalars, and treating it as one is how you print
 * confident nonsense. GetNetworkStatistics hands back ROADSHOW's layout, which
 * interleaves two 19-entry per-type histograms with the scalars:
 *
 *   error, oldshort, oldicmp, outhist[19],
 *   badcode, tooshort, checksum, badlen, reflect, inhist[19]      = 46 longs
 *
 * A flat name table would label outhist[0..4] as "bad code", "message too
 * short", "bad checksum" and so on -- plausible-looking figures that are simply
 * the wrong counters. So ICMP gets its own reader.
 */
#define ICMP_MAXTYPE_	18
#define ICMP_NTYPE_	(ICMP_MAXTYPE_ + 1)
#define ICMP_LONGS_	(3 + ICMP_NTYPE_ + 5 + ICMP_NTYPE_)

static const char *icmp_scalars_head[] = {
  "calls to icmp_error", "errors not generated (old message)",
  "errors not generated (old message was icmp)"
};
static const char *icmp_scalars_tail[] = {
  "bad code", "message too short", "bad checksum", "bad length",
  "responses sent"
};
/* Only the types that exist; the gaps are unassigned and stay unnamed. */
static const char *icmp_type_name(int t)
{
  switch (t) {
  case 0:  return "echo reply";
  case 3:  return "destination unreachable";
  case 4:  return "source quench";
  case 5:  return "redirect";
  case 8:  return "echo request";
  case 11: return "time exceeded";
  case 12: return "parameter problem";
  case 13: return "timestamp request";
  case 14: return "timestamp reply";
  case 15: return "information request";
  case 16: return "information reply";
  case 17: return "address mask request";
  case 18: return "address mask reply";
  default: return "unassigned type";
  }
}

static const char *tcp_names[] = {
  "connection attempts", "connections accepted", "connections established",
  "connections dropped", "embryonic connections dropped", "connections closed",
  "segments timed for rtt", "rtt updates", "delayed acks sent",
  "connections dropped on timeout", "retransmit timeouts", "persist timeouts",
  "keepalive timeouts", "keepalive probes sent", "connections dropped by keepalive",
  "total segments sent", "data packets sent", "data bytes sent",
  "data packets RETRANSMITTED", "data bytes retransmitted", "ack-only packets sent",
  "window probes sent", "urgent packets sent", "window updates sent",
  "control packets sent", "total segments received", "packets received in sequence",
  "bytes received in sequence", "packets with BAD CHECKSUM", "packets with bad offset",
  "packets too short", "DUPLICATE packets received", "duplicate bytes received",
  "packets dropped by PAWS", "partly duplicate packets", "partly duplicate bytes",
  "OUT-OF-ORDER packets", "out-of-order bytes", "packets after window",
  "bytes after window", "packets after close", "window probes received",
  "DUPLICATE ACKS received", "acks for unsent data", "ack packets received",
  "bytes acked by received acks", "window update packets received"
};
#define TCP_N ((int)(sizeof(tcp_names)/sizeof(tcp_names[0])))

static const char *udp_names[] = {
  "datagrams received", "packets shorter than header", "bad checksums",
  "bad data length", "no socket on port", "of those, broadcast",
  "not delivered -- socket full", "pcb cache misses", "datagrams sent"
};
#define UDP_N ((int)(sizeof(udp_names)/sizeof(udp_names[0])))

/* Fetch one protocol's counters and print the non-zero ones. Zero-valued
 * counters are omitted: a wall of forty-odd zeroes buries the two or three
 * figures that matter. */
static void show_one_stat(const char *title, long type,
                          const char *const *names, int n)
{
  ULONG buf[64];
  LONG need, got;
  int i, shown = 0;

  Printf((STRPTR)"\n%s:\n", (LONG)title);

  need = ng_netstats(type, 1, (void *)0, 0);	/* ask the required size */
  if (need <= 0) {
    Printf((STRPTR)"  (not available from this library)\n");
    return;
  }
  if (need > (LONG)sizeof(buf))
    need = (LONG)sizeof(buf);

  for (i = 0; i < (int)(sizeof(buf)/sizeof(buf[0])); i++) buf[i] = 0;

  got = ng_netstats(type, 1, buf, need);
  if (got <= 0) {
    Printf((STRPTR)"  (unavailable)\n");
    return;
  }

  /* Never walk past what the library actually filled, even if our name table
   * is longer than this library's struct -- an older library legitimately
   * reports fewer fields. */
  if (n > (int)(need / 4)) n = (int)(need / 4);

  for (i = 0; i < n; i++) {
    if (buf[i] == 0) continue;
    Printf((STRPTR)"  %10ld  %s\n", (LONG)buf[i], (LONG)names[i]);
    shown++;
  }
  if (shown == 0)
    Printf((STRPTR)"  (all counters zero)\n");
}

/* ICMP, honouring the interleaved histogram layout described above. */
static void show_icmp(void)
{
  ULONG buf[ICMP_LONGS_ + 8];
  LONG need, got;
  int i, shown = 0;

  Printf((STRPTR)"\nicmp:\n");

  need = ng_netstats(NG_NS_ICMP, 1, (void *)0, 0);
  if (need <= 0) { Printf((STRPTR)"  (not available from this library)\n"); return; }
  if (need > (LONG)sizeof(buf)) need = (LONG)sizeof(buf);
  for (i = 0; i < (int)(sizeof(buf)/sizeof(buf[0])); i++) buf[i] = 0;
  got = ng_netstats(NG_NS_ICMP, 1, buf, need);
  if (got <= 0) { Printf((STRPTR)"  (unavailable)\n"); return; }

  /* Refuse to guess if the library returned a layout we do not recognise --
   * printing the first N longs under our labels would be exactly the mistake
   * this reader exists to avoid. */
  if (need < (LONG)(ICMP_LONGS_ * 4)) {
    Printf((STRPTR)"  (unexpected layout: %ld bytes, expected %ld)\n",
           (LONG)need, (LONG)(ICMP_LONGS_ * 4));
    return;
  }

  for (i = 0; i < 3; i++)
    if (buf[i]) { Printf((STRPTR)"  %10ld  %s\n", (LONG)buf[i],
                         (LONG)icmp_scalars_head[i]); shown++; }
  for (i = 0; i < 5; i++) {
    ULONG v = buf[3 + ICMP_NTYPE_ + i];
    if (v) { Printf((STRPTR)"  %10ld  %s\n", (LONG)v,
                    (LONG)icmp_scalars_tail[i]); shown++; }
  }
  for (i = 0; i < ICMP_NTYPE_; i++) {
    ULONG v = buf[3 + i];
    if (v) { Printf((STRPTR)"  %10ld  sent: %s\n", (LONG)v,
                    (LONG)icmp_type_name(i)); shown++; }
  }
  for (i = 0; i < ICMP_NTYPE_; i++) {
    ULONG v = buf[3 + ICMP_NTYPE_ + 5 + i];
    if (v) { Printf((STRPTR)"  %10ld  received: %s\n", (LONG)v,
                    (LONG)icmp_type_name(i)); shown++; }
  }
  if (shown == 0) Printf((STRPTR)"  (all counters zero)\n");
}

static void show_stats(void)
{
  show_one_stat("ip",   NG_NS_IP,   ip_names,   IP_N);
  show_icmp();
  show_one_stat("tcp",  NG_NS_TCP,  tcp_names,  TCP_N);
  show_one_stat("udp",  NG_NS_UDP,  udp_names,  UDP_N);
}

/* ------------------------------------------------------------------ main -- */

static void usage(void)
{
  Printf((STRPTR)"usage: netstat [-i] [-r] [-s] [-a]\n"
                 "  -i  interfaces        -r  routing table\n"
                 "  -s  protocol statistics   -a  connections (not available)\n"
                 "  no option: interfaces and routing table\n");
}

int main(int argc, char **argv)
{
  int want_i = 0, want_r = 0, want_s = 0, want_a = 0, i;
  int rc = RETURN_OK;

  for (i = 1; i < argc; i++) {
    char *a = argv[i];
    if (a[0] == '-' || a[0] == '/') a++;		/* accept -i, /i and i */
    if      (ci_eq(a, "i") || ci_eq(a, "INTERFACES")) want_i = 1;
    else if (ci_eq(a, "r") || ci_eq(a, "ROUTES"))     want_r = 1;
    else if (ci_eq(a, "s") || ci_eq(a, "STATS"))      want_s = 1;
    else if (ci_eq(a, "a") || ci_eq(a, "ALL"))        want_a = 1;
    else if (ci_eq(a, "?") || ci_eq(a, "HELP"))       { usage(); return RETURN_OK; }
    else {
      Printf((STRPTR)"netstat: unknown option '%s'\n", (LONG)argv[i]);
      usage();
      return RETURN_FAIL;
    }
  }
  if (!want_i && !want_r && !want_s && !want_a)
    want_i = want_r = 1;

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    Printf((STRPTR)"netstat: cannot open bsdsocket.library\n");
    return RETURN_FAIL;
  }

  if (want_a) {
    /* Say why, rather than print an empty table that reads as "nothing is
     * connected". The library cannot enumerate its protocol control blocks
     * yet -- GetNetworkStatistics answers EINVAL for the socket-list types. */
    Printf((STRPTR)"netstat: -a is not available -- this library cannot enumerate\n"
                   "         its TCP/UDP control blocks yet.\n");
    rc = RETURN_WARN;
  }
  if (want_i) show_interfaces();
  if (want_r) { if (want_i) Printf((STRPTR)"\n"); show_routes(); }
  if (want_s) show_stats();

  CloseLibrary(SocketBase);
  return rc;
}
