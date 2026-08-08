/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * nslookup -- look up a host name or address, in the shape of the Unix tool.
 *
 *   nslookup <name>            forward lookup via the configured resolver
 *   nslookup <dotted-quad>     reverse lookup via the configured resolver
 *   nslookup <name> <server>   ask THAT server directly
 *   nslookup                   report which name servers are configured
 *
 * With no server named it goes through the library's resolver, which is the
 * right default because that is what applications actually use -- testing
 * anything else would answer a question nobody asked.
 *
 * Naming a server speaks DNS directly to it instead, which is the whole
 * diagnostic point: it separates "my resolver is broken" from "the network is
 * broken" from "the name really does not exist". The library resolver cannot do
 * that, since it always asks whichever servers are configured.
 *
 * WHY THIS EXISTS. Without it there is no way to test name resolution at all: if
 * DNS is broken, `ping somehost` fails in exactly the same way as the network
 * being down, and nothing distinguishes them. That is the single most common
 * "the network doesn't work" question, and until now this stack could not answer
 * it. AmiTCP 3.0b2 shipped `resolve` and `askhost` for the same reason.
 *
 * It always prints the server list first, like the Unix tool, and that is the
 * point rather than decoration: "no servers configured" is by far the most common
 * cause of a failed lookup, and it is invisible from a bare failure message. A
 * DHCP lease that produced an address but no name servers looks identical to a
 * working setup until you see this.
 *
 * WHAT IT DOES NOT DO, deliberately. The real nslookup speaks DNS itself, so it
 * can select a server, ask for a specific record type (MX, NS, TXT) and report
 * whether an answer was authoritative. This one goes through the library's
 * resolver -- gethostbyname/gethostbyaddr -- so it sees only what that returns:
 * names and addresses. It says so rather than printing a misleading
 * "Non-authoritative answer:" banner it cannot actually justify. Speaking DNS
 * directly would be a much larger tool and would bypass the very resolver most
 * users actually want tested.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase = 0;		/* ng_lvo.h's LVO stubs read this */

#include "ng_lvo.h"

/* h_addr_list entries are 4-byte network-order addresses for AF_INET. */
struct ng_hostent {
  char  *h_name;
  char **h_aliases;
  long   h_addrtype, h_length;
  char **h_addr_list;
};

static ULONG ng_inet_addr_(const char *s) {			/* inet_addr -180 */
  register ULONG _d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-180)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
static void *ng_gethostbyname_(const char *s) {			/* gethostbyname -210 */
  register void *_d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-210)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
/* gethostbyaddr -216. Registers are (a0,d0,d1) per the SFD -- addr, len, type.
 * Taken from ref/roadshow-sdk/sfd/bsdsocket_lib.sfd, not guessed: getting a
 * vector's register assignment wrong produces plausible nonsense rather than a
 * failure, which has already cost this project once. */
static void *ng_gethostbyaddr_(const void *a, long len, long type) {
  register const void *_a0 __asm("a0")=a;
  register long _d0 __asm("d0")=len; register long _d1 __asm("d1")=type;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-216)":"+r"(_d0),"+r"(_a0),"+r"(_d1)
                       :"r"(_a6):"a1","memory");
  return (void *)_d0;
}
static long ng_herrno_(void) {					/* h_errno via SocketBaseTagList */
  struct TagItem tg[2];
  tg[0].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_HERRNO);   /* get by value: answer lands in ti_Data */
  tg[0].ti_Data = 0;
  tg[1].ti_Tag = TAG_END; tg[1].ti_Data = 0;
  ng_sbtaglist(tg);
  return (long)tg[0].ti_Data;
}

#define NG_AF_INET	2
#define NG_SOCK_DGRAM	2
#define DNS_PORT	53
#define DNSBUF		1500		/* a UDP DNS reply cannot exceed this */
#define DNS_TIMEOUT	5

/* ---- direct DNS, for when a specific server is named ----------------------- */

static long ng_socket_(long d, long t, long p) {		/* -30  (d0,d1,d2) */
  register long _d0 __asm("d0")=d, _d1 __asm("d1")=t, _d2 __asm("d2")=p;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a0","a1","memory");
  return _d0;
}
/* send (-66). The socket is connect()ed, so this is the correct call: sendto()
 * with an address on a connected socket is refused with EISCONN. */
static long ng_send_(long s, void *b, long l, long f) {		/* -66  (d0,a0,d1,d2) */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f;
  register void *_a0 __asm("a0")=b; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-66)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a1","memory");
  return _d0;
}
static long ng_recv_(long s, void *b, long l, long f) {		/* -78  (d0,a0,d1,d2) */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f;
  register void *_a0 __asm("a0")=b; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-78)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a1","memory");
  return _d0;
}
static long ng_connect_(long s, void *n, long l) {		/* -54  (d0,a0,d1) */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l;
  register void *_a0 __asm("a0")=n; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-54)":"+r"(_d0),"+r"(_a0),"+r"(_d1):"r"(_a6):"d2","a1","memory");
  return _d0;
}
static long ng_errno_(void) {					/* Errno -162 */
  register long _d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static void ng_close_(long s) {					/* -120 (d0) */
  register long _d0 __asm("d0")=s; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_a6):"d1","a0","a1","memory");
}
static long ng_waitselect_(long n, void *r, void *w, void *e, void *tv, ULONG *sig) { /* -126 */
  register long _d0 __asm("d0")=n; register void *_a0 __asm("a0")=r, *_a1 __asm("a1")=w;
  register void *_a2 __asm("a2")=e, *_a3 __asm("a3")=tv; register ULONG *_d1 __asm("d1")=sig;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-126)":"+r"(_d0),"+r"(_a0),"+r"(_a1),"+r"(_a2),"+r"(_a3),"+r"(_d1)
                       :"r"(_a6):"memory");
  return _d0;
}

struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE zero[8]; };
struct ng_tv  { long tv_secs, tv_micro; };

static UBYTE dq[DNSBUF], dr[DNSBUF];

/* h_errno values (netdb.h). */
#define HOST_NOT_FOUND	1
#define TRY_AGAIN	2
#define NO_RECOVERY	3
#define NO_DATA		4

static const char *herr_text(long e)
{
  switch (e) {
  case HOST_NOT_FOUND: return "no such host is known";
  case TRY_AGAIN:      return "server failed or timed out -- try again";
  case NO_RECOVERY:    return "unrecoverable server error";
  case NO_DATA:        return "the name is valid but has no address";
  case 0:              return "no answer (is a name server configured?)";
  default:             return "lookup failed";
  }
}

static unsigned long now_ticks_(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return (unsigned long)ds.ds_Minute * 3000UL + (unsigned long)ds.ds_Tick;
}

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

/*
 * Minimal DNS client. Everything here parses bytes from a remote server on a
 * machine with no memory protection, so every walk is bounded by the received
 * length and every pointer is range-checked -- a malformed reply must fail, not
 * wander.
 */

/*
 * Encode "a.b.c" as length-prefixed labels. Returns bytes written, or -1.
 * Enforces BOTH limits RFC 1035 sets: 63 bytes per label and 255 for the whole
 * encoded name. The per-label cap alone is not enough -- a long argument of many
 * short labels satisfies it and still runs to any length.
 */
#define DN_MAXNAME	255

static int dn_put(UBYTE *o, int cap, const char *name)
{
  int n = 0, lab;

  if (cap > DN_MAXNAME) cap = DN_MAXNAME;

  while (*name) {
    const char *p = name;
    while (*p && *p != '.') p++;
    lab = (int)(p - name);
    if (lab == 0 || lab > 63) return -1;		/* empty or over-long label */
    if (n + 1 + lab + 1 > cap) return -1;
    o[n++] = (UBYTE)lab;
    while (name < p) o[n++] = (UBYTE)*name++;
    if (*name == '.') name++;
  }
  if (n + 1 > cap) return -1;
  o[n++] = 0;						/* root label */
  return n;
}

/* Step over a name at `off`. Returns the offset just past it, or -1. */
static int dn_skip(const UBYTE *b, int len, int off)
{
  while (off >= 0 && off < len) {
    int c = b[off];
    if ((c & 0xC0) == 0xC0) return (off + 2 <= len) ? off + 2 : -1;  /* pointer ends it */
    if (c == 0) return off + 1;
    if (c > 63) return -1;
    off += 1 + c;
  }
  return -1;
}

/*
 * Expand a name into `out`. Compression pointers are followed, with a hard cap
 * on jumps -- a reply pointing at itself would otherwise loop forever, which is
 * the classic way to hang a DNS parser.
 */
static int dn_expand(const UBYTE *b, int len, int off, char *out, int outsz)
{
  int n = 0, jumps = 0;

  while (off >= 0 && off < len) {
    int c = b[off];
    if ((c & 0xC0) == 0xC0) {
      if (off + 1 >= len || ++jumps > 16) return -1;
      off = ((c & 0x3F) << 8) | b[off + 1];
      continue;
    }
    if (c == 0) { out[n] = 0; return 0; }
    if (c > 63 || off + 1 + c > len) return -1;
    if (n && n < outsz - 1) out[n++] = '.';
    if (n + c >= outsz) return -1;
    { int i; for (i = 0; i < c; i++) out[n++] = (char)b[off + 1 + i]; }
    off += 1 + c;
  }
  return -1;
}

static int wait_readable_(long s, long secs)
{
  ULONG fds[16];
  struct ng_tv tv;
  ULONG sigs = SIGBREAKF_CTRL_C;
  long r;
  int i;

  if (s < 0 || s >= 512) return -1;
  for (i = 0; i < 16; i++) fds[i] = 0;
  fds[s / 32] |= (1UL << (s % 32));
  tv.tv_secs = secs; tv.tv_micro = 0;
  r = ng_waitselect_(s + 1, fds, (void *)0, (void *)0, &tv, &sigs);
  if (sigs & SIGBREAKF_CTRL_C) return -1;
  return (r > 0) ? 1 : (r < 0 ? -1 : 0);
}

/*
 * Ask `server` for `qname`/`qtype` and print what comes back.
 * qtype 1 = A, 12 = PTR. Returns 0 if something was printed.
 */
static int dns_ask(ULONG server, const char *qname, int qtype)
{
  struct ng_sin to;
  long s;
  int qn, qlen, n, off, i, ancount, qdcount, shown = 0;
  UWORD id;
  char nm[256], ip[16];

  /* Reserve the four bytes of QTYPE/QCLASS appended below -- they used to be
   * written unconditionally, so a name that filled the buffer exactly ran four
   * bytes past the end of it. */
  qn = dn_put(&dq[12], DNSBUF - 12 - 4, qname);
  if (qn < 0) { Printf((STRPTR)"nslookup: name too long\n"); return -1; }

  id = (UWORD)(now_ticks_() & 0xFFFF);
  dq[0] = (UBYTE)(id >> 8); dq[1] = (UBYTE)id;
  dq[2] = 0x01; dq[3] = 0x00;			/* recursion desired */
  dq[4] = 0; dq[5] = 1;				/* one question */
  dq[6] = dq[7] = dq[8] = dq[9] = dq[10] = dq[11] = 0;
  qlen = 12 + qn;
  dq[qlen++] = 0; dq[qlen++] = (UBYTE)qtype;	/* QTYPE  */
  dq[qlen++] = 0; dq[qlen++] = 1;		/* QCLASS IN */

  for (i = 0; i < (int)sizeof(to); i++) ((char *)&to)[i] = 0;
  to.sin_len = sizeof(to); to.sin_family = NG_AF_INET;
  to.sin_port = DNS_PORT; to.sin_addr = server;

  s = ng_socket_(NG_AF_INET, NG_SOCK_DGRAM, 0);
  if (s < 0) { Printf((STRPTR)"nslookup: socket failed\n"); return -1; }
  /*
   * Connect the datagram socket. Nothing is "connected" on the wire, but it
   * binds the address pair so the stack discards anything from another sender
   * before we ever see it. Without that, any host reaching this ephemeral port
   * could answer, and the only thing standing in its way would be a 16-bit
   * transaction id derived from a coarse clock -- guessable. A tool whose job is
   * to be trusted when DNS is suspect must not be that easy to lie to.
   */
  if (ng_connect_(s, &to, sizeof(to)) < 0) {
    Printf((STRPTR)"nslookup: connect failed (errno %ld)\n", ng_errno_()); ng_close_(s); return -1;
  }
  if (ng_send_(s, dq, qlen, 0) < 0) {
    Printf((STRPTR)"nslookup: send failed (errno %ld)\n", ng_errno_()); ng_close_(s); return -1;
  }
  { int w = wait_readable_(s, DNS_TIMEOUT);
    if (w <= 0) {
      Printf((STRPTR)"*** no response from the server\n");
      ng_close_(s); return -1;
    } }
  n = (int)ng_recv_(s, dr, DNSBUF, 0);
  ng_close_(s);

  if (n < 12) { Printf((STRPTR)"*** short reply\n"); return -1; }
  if (dr[0] != dq[0] || dr[1] != dq[1] ||	/* not our query */
      (dr[2] & 0x80) == 0) {			/* not a response at all */
    Printf((STRPTR)"*** reply did not match the query\n"); return -1;
  }
  switch (dr[3] & 0x0F) {			/* RCODE */
  case 0: break;
  case 3: Printf((STRPTR)"*** no such name (NXDOMAIN)\n"); return -1;
  case 2: Printf((STRPTR)"*** server failure\n"); return -1;
  case 5: Printf((STRPTR)"*** query refused\n"); return -1;
  default: Printf((STRPTR)"*** lookup failed (rcode %ld)\n", (LONG)(dr[3] & 0x0F)); return -1;
  }
  if ((dr[2] & 0x04) == 0) Printf((STRPTR)"Non-authoritative answer:\n");

  qdcount = (dr[4] << 8) | dr[5];
  ancount = (dr[6] << 8) | dr[7];

  off = 12;
  for (i = 0; i < qdcount; i++) {		/* step over the question(s) */
    off = dn_skip(dr, n, off);
    if (off < 0 || off + 4 > n) { Printf((STRPTR)"*** malformed reply\n"); return -1; }
    off += 4;
  }

  for (i = 0; i < ancount; i++) {
    int type, rdlen, rdat;
    off = dn_skip(dr, n, off);
    if (off < 0 || off + 10 > n) break;
    type  = (dr[off] << 8) | dr[off + 1];
    rdlen = (dr[off + 8] << 8) | dr[off + 9];
    rdat  = off + 10;
    if (rdat + rdlen > n) break;		/* record runs past the packet */

    if (type == 1 && rdlen == 4) {			/* A */
      ULONG a = ((ULONG)dr[rdat] << 24) | ((ULONG)dr[rdat+1] << 16) |
                ((ULONG)dr[rdat+2] << 8) | dr[rdat+3];
      fmt_ip(a, ip);
      Printf((STRPTR)"%s %s\n", (LONG)(shown ? "        " : "Address:"), (LONG)ip);
      shown++;
    } else if (type == 12) {				/* PTR */
      if (dn_expand(dr, n, rdat, nm, sizeof(nm)) == 0) {
        Printf((STRPTR)"Name:    %s\n", (LONG)nm);
        shown++;
      }
    } else if (type == 5) {				/* CNAME */
      if (dn_expand(dr, n, rdat, nm, sizeof(nm)) == 0)
        Printf((STRPTR)"Alias:   %s\n", (LONG)nm);
    }
    off = rdat + rdlen;
  }

  if (!shown) Printf((STRPTR)"*** the server returned no usable answer\n");
  return shown ? 0 : -1;
}

/* Print the configured name servers. Returns how many there were -- zero is the
 * answer to most "why did my lookup fail" questions, so the caller says so. */
static int show_servers(void)
{
  struct List *l;
  int n = 0;

  l = ng_obtaindnslist();
  if (l != NULL) {
    struct ng_dnsnode *dn;
    for (dn = (struct ng_dnsnode *)l->lh_Head;
         dn->dnsn_MinNode.mln_Succ != NULL;
         dn = (struct ng_dnsnode *)dn->dnsn_MinNode.mln_Succ) {
      Printf((STRPTR)"%s %s\n", (LONG)(n == 0 ? "Server: " : "        "),
             (LONG)dn->dnsn_Address);
      n++;
    }
    ng_releasednslist(l);
  }
  if (n == 0)
    Printf((STRPTR)"Server:  (none configured)\n");
  return n;
}

static void show_addresses(struct ng_hostent *hp)
{
  char ip[16];
  int i;

  Printf((STRPTR)"Name:    %s\n", (LONG)(hp->h_name ? hp->h_name : (char *)"?"));

  /* Also catch an empty (but non-NULL) list: neither resolver backend can
   * currently produce one, but printing a Name with no address and no error
   * would be silently unhelpful if that ever changed. */
  if (hp->h_addrtype != NG_AF_INET || hp->h_length != 4 ||
      !hp->h_addr_list || !hp->h_addr_list[0]) {
    Printf((STRPTR)"Address: (no IPv4 address returned)\n");
    return;
  }
  for (i = 0; hp->h_addr_list[i]; i++) {
    UBYTE *p = (UBYTE *)hp->h_addr_list[i];
    ULONG a = ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
    fmt_ip(a, ip);
    Printf((STRPTR)"%s %s\n", (LONG)(i == 0 ? "Address:" : "        "), (LONG)ip);
  }

  /* Aliases matter when you are chasing a CNAME chain. */
  if (hp->h_aliases && hp->h_aliases[0]) {
    Printf((STRPTR)"Aliases: ");
    for (i = 0; hp->h_aliases[i]; i++)
      Printf((STRPTR)"%s%s", (LONG)(i ? ", " : ""), (LONG)hp->h_aliases[i]);
    Printf((STRPTR)"\n");
  }
}

int main(void)
{
  struct RDArgs *rda;
  LONG args[2];
  ULONG srvaddr = 0;
  const char *target;
  struct ng_hostent *hp;
  ULONG addr;
  int servers, rc = RETURN_OK;

  args[0] = args[1] = 0;
  rda = ReadArgs((STRPTR)"NAME,SERVER", args, NULL);
  if (!rda) {
    Printf((STRPTR)"usage: nslookup [<name>|<address>] [<server>]\n"
                   "  with no <server>, uses the configured resolver.\n"
                   "  with one, asks that server directly.\n"
                   "  with no arguments, lists the configured servers.\n");
    return RETURN_FAIL;
  }
  target = (const char *)args[0];

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    Printf((STRPTR)"nslookup: bsdsocket.library v4+ not available.\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  /* An explicit server must be a dotted quad: resolving the name of the server
   * you are about to test would use the resolver under test. */
  if (args[1]) {
    srvaddr = ng_inet_addr_((const char *)args[1]);
    if (srvaddr == 0xFFFFFFFFUL) {
      Printf((STRPTR)"nslookup: <server> must be an IP address, not a name\n");
      CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL;
    }
  }

  if (args[1]) {
    /* Report the server actually being queried, not the configured list -- the
     * configured list is irrelevant to what is about to happen. */
    Printf((STRPTR)"Server:  %s\n", (LONG)args[1]);
    servers = 1;
  } else {
    servers = show_servers();
  }

  if (!target) {
    Printf((STRPTR)"\n(no name given -- nothing to look up)\n");
    CloseLibrary(SocketBase);
    FreeArgs(rda);
    return RETURN_OK;
  }

  Printf((STRPTR)"\n");

  /* A dotted quad means a reverse lookup; anything else is a forward one.
   * inet_addr returns 0xFFFFFFFF for "not an address", which is also the
   * broadcast address -- irrelevant here, since nobody reverse-looks-up
   * 255.255.255.255 and treating it as a name simply fails honestly. */
  addr = ng_inet_addr_(target);
  if (addr != 0xFFFFFFFFUL) {
    UBYTE quad[4];
    char ip[16];
    /* inet_addr returns network byte order; this is a big-endian target, so the
     * MSB is the first dotted-quad octet and no swap is needed. */
    quad[0] = (UBYTE)(addr >> 24); quad[1] = (UBYTE)(addr >> 16);
    quad[2] = (UBYTE)(addr >> 8);  quad[3] = (UBYTE)addr;
    fmt_ip(addr, ip);

    if (srvaddr) {
      /* in-addr.arpa, reversed octets */
      char rev[40]; int rn = 0, k;
      for (k = 3; k >= 0; k--) {
        ULONG part = (addr >> (k * 8)) & 0xFF;
        if (part >= 100) rev[rn++] = (char)('0' + part / 100);
        if (part >= 10)  rev[rn++] = (char)('0' + (part / 10) % 10);
        rev[rn++] = (char)('0' + part % 10);
        rev[rn++] = '.';
      }
      { const char *sfx = "in-addr.arpa"; while (*sfx) rev[rn++] = *sfx++; }
      rev[rn] = 0;
      Printf((STRPTR)"Address: %s\n", (LONG)ip);
      if (dns_ask(srvaddr, rev, 12) != 0) rc = RETURN_WARN;
      goto finish;
    }
    hp = (struct ng_hostent *)ng_gethostbyaddr_(quad, 4, NG_AF_INET);
    if (hp) {
      Printf((STRPTR)"Address: %s\n", (LONG)ip);
      Printf((STRPTR)"Name:    %s\n", (LONG)(hp->h_name ? hp->h_name : (char *)"?"));
    } else {
      Printf((STRPTR)"*** no reverse entry for %s: %s\n",
             (LONG)ip, (LONG)herr_text(ng_herrno_()));
      rc = RETURN_WARN;
    }
  } else {
    if (srvaddr) {
      Printf((STRPTR)"Name:    %s\n", (LONG)target);
      if (dns_ask(srvaddr, target, 1) != 0) rc = RETURN_WARN;
      goto finish;
    }
    hp = (struct ng_hostent *)ng_gethostbyname_(target);
    if (hp) {
      show_addresses(hp);
    } else {
      Printf((STRPTR)"*** cannot resolve %s: %s\n",
             (LONG)target, (LONG)herr_text(ng_herrno_()));
      /* The most useful thing we can say when there is nowhere to ask. */
      if (servers == 0)
        Printf((STRPTR)"    No name servers are configured -- no lookup could be\n"
                       "    made. Set one with nameserver= in DEVS:NetInterfaces,\n"
                       "    or use configure=dhcp to obtain them automatically.\n");
      rc = RETURN_WARN;
    }
  }

finish:
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
