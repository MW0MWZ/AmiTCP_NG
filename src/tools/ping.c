/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ping -- send ICMP ECHO_REQUEST packets to a host and report the replies. Our own
 * name-and-behaviour-compatible replacement for Roadshow's ping; it uses the same
 * ReadArgs template (-c=COUNT, -i=INTERVAL, -s=SIZE, -t=TIMEOUT, -n=NUMERIC, -q=QUIET,
 * HOST/A, ...) and the familiar BSD ping output so existing scripts and habits carry
 * over. It opens a raw ICMP socket through the public bsdsocket.library API.
 *
 * NOTE: this tool is exercised only on real ICMP-capable networks. The project's
 * headless test rig (Amiberry + SLIRP) does NOT pass ICMP, so ping cannot be validated
 * there; it is build-verified and reviewed against the raw-socket path our stack
 * provides (netinet/in_proto.c: SOCK_RAW/IPPROTO_ICMP -> rip_usrreq/icmp).
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 src/tools/ping.c -o ping
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase = 0;

/* ---- bsdsocket.library core socket vectors (standard AmiTCP LVOs) ----------- */
static long ng_socket(long d, long t, long p) {			/* socket -30 */
  register long _d0 __asm("d0")=d; register long _d1 __asm("d1")=t;
  register long _d2 __asm("d2")=p; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a0","a1","memory"); return _d0;
}
static long ng_sendto(long s, void *m, long len, long fl, void *to, long tl) { /* sendto -60 */
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=m;
  register long _d1 __asm("d1")=len; register long _d2 __asm("d2")=fl;
  register void *_a1 __asm("a1")=to; register long _d3 __asm("d3")=tl;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-60)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_d3):"r"(_a6):"memory");
  return _d0;
}
static long ng_recvfrom(long s, void *b, long len, long fl, void *fr, long *frl) { /* recvfrom -72 */
  register long _d0 __asm("d0")=s; register void *_a0 __asm("a0")=b;
  register long _d1 __asm("d1")=len; register long _d2 __asm("d2")=fl;
  register void *_a1 __asm("a1")=fr; register long *_a2 __asm("a2")=frl;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-72)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_a2):"r"(_a6):"memory");
  return _d0;
}
static long ng_closesocket(long s) {				/* CloseSocket -120 */
  register long _d0 __asm("d0")=s; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static long ng_waitselect(long n, void *r, void *w, void *e, void *tv, ULONG *sig) { /* WaitSelect -126 */
  register long _d0 __asm("d0")=n; register void *_a0 __asm("a0")=r;
  register void *_a1 __asm("a1")=w; register void *_a2 __asm("a2")=e;
  register void *_a3 __asm("a3")=tv; register ULONG *_d1 __asm("d1")=sig;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-126)":"+r"(_d0),"+r"(_a0),"+r"(_a1),"+r"(_a2),"+r"(_a3),"+r"(_d1):"r"(_a6):"memory");
  return _d0;
}
static long ng_errno(void) {					/* Errno -162 */
  register long _d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static ULONG ng_inet_addr(const char *s) {			/* inet_addr -180 */
  register ULONG _d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-180)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}
static void *ng_gethostbyname(const char *s) {			/* gethostbyname -210 */
  register void *_d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-210)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}

/* ---- minimal network structures (we only touch these fields) --------------- */
struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE zero[8]; };
/* Must be a full 16-byte sockaddr_in -- recvfrom/library writes assume that. Build
 * breaks here rather than overrunning if this is ever shrunk (see ShowNetStatus.c). */
typedef char ng_sin_must_be_16[(sizeof(struct ng_sin) >= 16) ? 1 : -1];
struct ng_hostent { char *h_name; char **h_aliases; long h_addrtype, h_length; char **h_addr_list; };
struct ng_icmp { UBYTE type, code; UWORD cksum; UWORD id, seq; };
#define NG_AF_INET	2
#define NG_SOCK_RAW	3
#define NG_IPPROTO_ICMP	1
#define NG_ICMP_ECHO	8
#define NG_ICMP_ECHOREPLY 0

/* Internet checksum (RFC 1071) over len bytes at addr. */
static UWORD in_cksum(UWORD *addr, int len)
{
  long sum = 0;
  while (len > 1) { sum += *addr++; len -= 2; }
  if (len == 1) { UWORD last = 0; *(UBYTE *)&last = *(UBYTE *)addr; sum += last; }
  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  return (UWORD)(~sum);
}

/* ---- timer.device: GetSysTime timestamps for RTT --------------------------- */
static struct MsgPort    *g_tport = 0;
static struct timerequest *g_treq = 0;

static int timer_open(void)
{
  g_tport = CreateMsgPort();
  if (!g_tport) return 0;
  g_treq = (struct timerequest *)CreateIORequest(g_tport, sizeof(struct timerequest));
  if (!g_treq) { DeleteMsgPort(g_tport); g_tport = 0; return 0; }
  if (OpenDevice((STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)g_treq, 0) != 0) {
    DeleteIORequest((struct IORequest *)g_treq); g_treq = 0;
    DeleteMsgPort(g_tport); g_tport = 0; return 0;
  }
  return 1;
}
static void timer_close(void)
{
  if (g_treq) { CloseDevice((struct IORequest *)g_treq); DeleteIORequest((struct IORequest *)g_treq); }
  if (g_tport) DeleteMsgPort(g_tport);
}
static void timer_now(struct timeval *tv)
{
  if (!g_treq) { tv->tv_secs = tv->tv_micro = 0; return; }
  g_treq->tr_node.io_Command = TR_GETSYSTIME;
  DoIO((struct IORequest *)g_treq);
  *tv = g_treq->tr_time;
}
/* elapsed microseconds a->b */
static long usec_diff(struct timeval *a, struct timeval *b)
{
  return (long)(b->tv_secs - a->tv_secs) * 1000000L + ((long)b->tv_micro - (long)a->tv_micro);
}

int main(void)
{
  struct RDArgs *rda;
  LONG   a[14];			/* one slot per template item */
  STRPTR host;
  int    quiet, numeric;
  long   count, interval, size, timeout;
  long   i, sock, ident, sent = 0, recvd = 0;
  int    stop = 0;			/* set on Ctrl-C to end an unbounded (no -c) ping */
  long   tmin = 0x7FFFFFFF, tmax = 0, tsum = 0;
  struct ng_sin to;
  struct ng_hostent *hp;
  ULONG  addr;
  char   namebuf[64];
  int    rc = RETURN_OK;

  for (i = 0; i < 14; i++) a[i] = 0;
  rda = ReadArgs((STRPTR)
    "-c=COUNT/K/N,-d=DEBUG/S,-i=INTERVAL/K/N,-l=LOAD/K/N,-n=NUMERICONLY=NUMERIC/S,"
    "-o=ONEREPLY/S,-q=QUIET/S,-R=RECORDROUTE/S,DONTROUTE/S,-s=SIZE/K/N,-t=TIMEOUT/K/N,"
    "-v=VERBOSE/S,BELL/S,HOST/A", a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)"ping"); return RETURN_ERROR; }

  count    = a[0]  ? *(LONG *)a[0]  : -1;	/* -1 == until Ctrl-C */
  interval = a[2]  ? *(LONG *)a[2]  : 1;	/* seconds between packets */
  numeric  = a[4]  ? 1 : 0;
  quiet    = a[6]  ? 1 : 0;
  size     = a[9]  ? *(LONG *)a[9]  : 56;	/* ICMP payload bytes */
  timeout  = a[10] ? *(LONG *)a[10] : 5;	/* seconds to wait per reply */
  host     = (STRPTR)a[13];
  if (size < 0) size = 0;
  if (size > 1400) size = 1400;
  /* Clamp the timing args too -- a negative -i/-t would cast to a huge ULONG in
   * Delay()/the WaitSelect timeval and effectively hang the tool. */
  if (interval < 0) interval = 1;
  if (interval > 3600) interval = 3600;
  if (timeout  < 1) timeout  = 1;
  if (timeout  > 3600) timeout  = 3600;
  (void)numeric;

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) { Printf((STRPTR)"ping: bsdsocket.library v4+ not available.\n"); FreeArgs(rda); return RETURN_FAIL; }

  /* Resolve the target: dotted-quad first, else a name lookup. inet_addr returns the
   * address in network byte order; on this big-endian CPU the ULONG's bytes are
   * already a.b.c.d, so (addr>>24)&0xFF is the first octet. */
  addr = ng_inet_addr((const char *)host);
  if (addr == 0xFFFFFFFFUL) {
    hp = (struct ng_hostent *)ng_gethostbyname((const char *)host);
    if (!hp || !hp->h_addr_list || !hp->h_addr_list[0]) {
      Printf((STRPTR)"ping: unknown host %s\n", (LONG)host);
      CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_ERROR;
    }
    { UBYTE *p = (UBYTE *)hp->h_addr_list[0]; addr = ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3]; }
    { int k=0,j; for (j=0; hp->h_name && hp->h_name[j] && k<63; j++) namebuf[k++]=hp->h_name[j]; namebuf[k]=0; }
  } else {
    int k=0,j; for (j=0; host[j] && k<63; j++) namebuf[k++]=host[j]; namebuf[k]=0;
  }

  sock = ng_socket(NG_AF_INET, NG_SOCK_RAW, NG_IPPROTO_ICMP);
  if (sock < 0) {
    Printf((STRPTR)"ping: cannot open raw socket (errno %ld) -- ICMP requires a privileged stack.\n", ng_errno());
    CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL;
  }

  { struct ng_sin z; int j; UBYTE *bp=(UBYTE*)&z; for(j=0;j<(int)sizeof(z);j++) bp[j]=0;
    z.sin_len=sizeof(z); z.sin_family=NG_AF_INET; z.sin_addr=addr; to=z; }

  ident = (long)((ULONG)FindTask(NULL) & 0xFFFF);
  if (!timer_open() && !quiet)
    Printf((STRPTR)"ping: (no timer.device -- round-trip times unavailable)\n");

  Printf((STRPTR)"PING %s (%ld.%ld.%ld.%ld): %ld data bytes\n", (LONG)namebuf,
         (addr>>24)&0xFF,(addr>>16)&0xFF,(addr>>8)&0xFF,addr&0xFF, size);
  Flush(Output());

  for (i = 0; !stop && (count < 0 || i < count); i++) {
    UBYTE pkt[1500]; struct ng_icmp *ic = (struct ng_icmp *)pkt;
    UBYTE rbuf[1500];
    struct timeval t0, t1; struct timeval tvto; ULONG sig;
    long n; long frlen; struct ng_sin from;
    int  plen = (int)sizeof(struct ng_icmp) + (int)size;
    int  j;

    if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) { stop = 1; break; }

    ic->type = NG_ICMP_ECHO; ic->code = 0; ic->cksum = 0;
    ic->id = (UWORD)ident; ic->seq = (UWORD)i;
    for (j = 0; j < (int)size; j++) pkt[sizeof(struct ng_icmp)+j] = (UBYTE)(j & 0xFF);
    ic->cksum = in_cksum((UWORD *)pkt, plen);

    timer_now(&t0);
    n = ng_sendto(sock, pkt, plen, 0, &to, sizeof(to));
    if (n < 0) { if (!quiet) Printf((STRPTR)"ping: sendto failed (errno %ld)\n", ng_errno()); continue; }
    sent++;

    /* Wait for a matching echo reply, up to `timeout` seconds; break on Ctrl-C.
     * Keep waiting (without resetting the timeout budget grossly) if a packet that is
     * not our reply arrives -- other ICMP or a stray datagram. */
    for (;;) {
      ULONG rd = 1UL << sock; ULONG *rdp = &rd;
      int ihl; struct ng_icmp *ric;

      tvto.tv_secs = timeout; tvto.tv_micro = 0;
      sig = SIGBREAKF_CTRL_C;
      n = ng_waitselect(sock + 1, rdp, 0, 0, &tvto, &sig);
      if (n < 0) { if (!quiet) Printf((STRPTR)"ping: select failed (errno %ld)\n", ng_errno()); break; }
      if (sig & SIGBREAKF_CTRL_C) { stop = 1; break; }
      if (n == 0) { if (!quiet) Printf((STRPTR)"Request timeout for icmp_seq %ld\n", i); break; }

      frlen = sizeof(from);
      n = ng_recvfrom(sock, rbuf, sizeof(rbuf), 0, &from, &frlen);
      if (n < 0) { if (!quiet) Printf((STRPTR)"ping: recvfrom failed (errno %ld)\n", ng_errno()); break; }

      /* Raw ICMP delivers the full IP datagram: skip the IP header. Require a valid
       * minimum IP header (ihl >= 20) so the TTL read at rbuf[8] and the ICMP header are
       * both within the bytes recvfrom() actually delivered. */
      ihl = (rbuf[0] & 0x0F) * 4;
      if (ihl < 20 || n < ihl + (int)sizeof(struct ng_icmp)) continue;	/* short/malformed */
      ric = (struct ng_icmp *)(rbuf + ihl);
      if (ric->type != NG_ICMP_ECHOREPLY || ric->id != (UWORD)ident)
        continue;						/* not our echo reply */

      timer_now(&t1);
      recvd++;
      { long us = g_treq ? usec_diff(&t0, &t1) : -1;
        long ttl = rbuf[8];
        if (us >= 0) {
          if (us < tmin) tmin = us;
          if (us > tmax) tmax = us;
          tsum += us;
          Printf((STRPTR)"%ld bytes from %ld.%ld.%ld.%ld: icmp_seq=%ld ttl=%ld time=%ld.%ld ms\n",
                 (LONG)(n - ihl), (from.sin_addr>>24)&0xFF,(from.sin_addr>>16)&0xFF,
                 (from.sin_addr>>8)&0xFF,from.sin_addr&0xFF, (LONG)ric->seq, ttl,
                 us/1000, (us%1000)/100);
        } else {
          Printf((STRPTR)"%ld bytes from %ld.%ld.%ld.%ld: icmp_seq=%ld ttl=%ld\n",
                 (LONG)(n - ihl), (from.sin_addr>>24)&0xFF,(from.sin_addr>>16)&0xFF,
                 (from.sin_addr>>8)&0xFF,from.sin_addr&0xFF, (LONG)ric->seq, ttl);
        }
      }
      if (a[12] /*BELL*/ && !quiet) Printf((STRPTR)"\007");
      break;
    }
    Flush(Output());

    if (count < 0 || i + 1 < count) Delay((ULONG)interval * 50);	/* ~interval seconds */
  }

  Printf((STRPTR)"\n--- %s ping statistics ---\n", (LONG)namebuf);
  { long loss = sent ? (100 * (sent - recvd)) / sent : 0;
    Printf((STRPTR)"%ld packets transmitted, %ld packets received, %ld%% packet loss\n", sent, recvd, loss); }
  if (recvd > 0 && g_treq)
    Printf((STRPTR)"round-trip min/avg/max = %ld.%ld/%ld.%ld/%ld.%ld ms\n",
           tmin/1000,(tmin%1000)/100, (tsum/recvd)/1000,((tsum/recvd)%1000)/100, tmax/1000,(tmax%1000)/100);
  if (sent && recvd == 0) rc = RETURN_WARN;

  timer_close();
  ng_closesocket(sock);
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
