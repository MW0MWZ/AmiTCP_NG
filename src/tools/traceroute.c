/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * traceroute -- show the path to a host, one hop at a time.
 *
 * Probes with a deliberately small IP TTL. A router that has to discard a packet
 * because its TTL reached zero sends back ICMP "time exceeded", and the source
 * address of that message is the router. Raising the TTL by one each round walks
 * the path outwards until the probe survives all the way and the destination
 * answers instead.
 *
 * ICMP ECHO probes rather than the classic high-port UDP ones. The Unix original
 * uses UDP and watches for "port unreachable" to know it arrived; that needs the
 * same raw ICMP socket to read the errors anyway, so UDP buys nothing here and
 * costs a second socket -- and ICMP probes are what gets through the largest
 * number of home routers.
 *
 * MATCHING REPLIES IS THE WHOLE JOB. A raw ICMP socket receives every ICMP packet
 * for this machine that nothing else has claimed, so unrelated traffic -- another
 * program's ping, an unreachable from some other connection -- arrives in the
 * middle of a run. A time-exceeded message quotes the IP header and first 8 bytes
 * of the packet that died, which is our own echo header, so the id and sequence
 * we put there come back to us and are what identify a reply as ours. Anything
 * that does not match is ignored and the wait continues on the REMAINING timeout,
 * never a fresh one, or the timeout would not bound anything.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/traceroute.c -o traceroute
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

#define PROG	"traceroute"

/* ---- bsdsocket.library vectors. Offsets computed from the SFD with ==varargs
 * aliases not consuming a slot, and every one of them cross-checks against the
 * values ping.c has proven in the field. -------------------------------------- */
static long ng_socket(long d, long t, long p) {			/* socket -30 */
  register long _d0 __asm("d0")=d; register long _d1 __asm("d1")=t;
  register long _d2 __asm("d2")=p; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a0","a1","memory");
  return _d0;
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
static long ng_setsockopt(long s, long lvl, long name, void *val, long len) { /* setsockopt -90 */
  register long _d0 __asm("d0")=s; register long _d1 __asm("d1")=lvl;
  register long _d2 __asm("d2")=name; register void *_a0 __asm("a0")=val;
  register long _d3 __asm("d3")=len;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-90)":"+r"(_d0),"+r"(_d1),"+r"(_d2),"+r"(_a0),"+r"(_d3):"r"(_a6):"a1","memory");
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
/* gethostbyaddr -216, (a0,d0,d1) per the SFD: addr, len, type -- and the result
 * comes back in d0 too, so len goes IN through the same register the pointer comes
 * OUT of. One "+r" variable, not two bound to d0. */
static void *ng_gethostbyaddr(void *a, long len, long type) {
  register long _d0 __asm("d0")=len; register void *_a0 __asm("a0")=a;
  register long _d1 __asm("d1")=type;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-216)":"+r"(_d0),"+r"(_a0),"+r"(_d1):"r"(_a6):"a1","memory");
  return (void *)_d0;
}

/* ---- minimal network structures (we only touch these fields) --------------- */
struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE zero[8]; };
typedef char ng_sin_must_be_16[(sizeof(struct ng_sin) >= 16) ? 1 : -1];
struct ng_hostent { char *h_name; char **h_aliases; long h_addrtype, h_length; char **h_addr_list; };
struct ng_icmp { UBYTE type, code; UWORD cksum; UWORD id, seq; };
struct ng_ip   { UBYTE vhl, tos; UWORD len; UWORD id, off; UBYTE ttl, proto; UWORD cksum;
                 ULONG src, dst; };

#define NG_AF_INET		2
#define NG_SOCK_RAW		3
#define NG_IPPROTO_IP		0
#define NG_IPPROTO_ICMP		1
#define NG_IP_TTL		4
#define NG_ICMP_ECHOREPLY	0
#define NG_ICMP_UNREACH		3
#define NG_ICMP_ECHO		8
#define NG_ICMP_TIMXCEED	11

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
static struct MsgPort     *g_tport = 0;
static struct timerequest *g_treq  = 0;

static int timer_open(void)
{
  if ((g_tport = CreateMsgPort()) == NULL) return 0;
  if ((g_treq = (struct timerequest *)CreateIORequest(g_tport, sizeof(*g_treq))) == NULL) {
    DeleteMsgPort(g_tport); g_tport = 0; return 0;
  }
  if (OpenDevice((STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)g_treq, 0) != 0) {
    DeleteIORequest((struct IORequest *)g_treq); g_treq = 0;
    DeleteMsgPort(g_tport); g_tport = 0; return 0;
  }
  return 1;
}
static void timer_close(void)
{
  if (g_treq)  { CloseDevice((struct IORequest *)g_treq); DeleteIORequest((struct IORequest *)g_treq); }
  if (g_tport) DeleteMsgPort(g_tport);
}
static void timer_now(struct timeval *tv)
{
  if (!g_treq) { tv->tv_secs = tv->tv_micro = 0; return; }
  g_treq->tr_node.io_Command = TR_GETSYSTIME;
  DoIO((struct IORequest *)g_treq);
  *tv = g_treq->tr_time;
}
static long usec_diff(struct timeval *a, struct timeval *b)
{
  return (long)(b->tv_secs - a->tv_secs) * 1000000L + ((long)b->tv_micro - (long)a->tv_micro);
}

/* ------------------------------------------------------------------ */

static void print_addr(ULONG a)
{
  /* Cast every argument, even though ULONG is already 32 bits here and these
   * are 0-255. RawDoFmt takes what it is given: leaving the casts off works
   * only for as long as nobody widens one of these expressions, and this
   * project has shipped that exact bug more than once. */
  Printf((STRPTR)"%ld.%ld.%ld.%ld",
         (LONG)((a >> 24) & 0xFF), (LONG)((a >> 16) & 0xFF),
         (LONG)((a >> 8) & 0xFF), (LONG)(a & 0xFF));
}

/*
 * A failed send, in the same one-letter form. These are our own stack's verdicts,
 * not something a router told us, but they answer the same question -- where the
 * path stopped -- so they read better in the same notation.
 */
static const char *send_mark(long err)
{
  switch (err) {
  case 64: return "!H";			/* EHOSTDOWN    -- next hop never answered ARP */
  case 65: return "!H";			/* EHOSTUNREACH */
  case 51: return "!N";			/* ENETUNREACH  */
  case 13: return "!X";			/* EACCES       */
  default: return "!";
  }
}

/* What an ICMP unreachable code means, in the one-letter form traceroute uses. */
static const char *unreach_mark(UBYTE code)
{
  switch (code) {
  case 0:  return "!N";			/* network unreachable   */
  case 1:  return "!H";			/* host unreachable      */
  case 2:  return "!P";			/* protocol unreachable  */
  case 3:  return "!";			/* port unreachable = arrived */
  case 4:  return "!F";			/* fragmentation needed  */
  case 5:  return "!S";			/* source route failed   */
  case 13: return "!X";			/* administratively prohibited */
  default: return "!?";
  }
}

int main(void)
{
  struct RDArgs *rda;
  LONG a[6] = { 0, 0, 0, 0, 0, 0 };
  struct ng_sin to, from;
  struct ng_hostent *he;
  /*
   * static, NOT stack. ping.c carries the scar: two packet buffers in main()'s
   * frame overflowed a Shell's ~4 KB stack once the library-call chain was added,
   * corrupted the return address and crashed on exit with a Guru. These are
   * smaller, but the same shape, and there is no reason to find out where the
   * line is. static also puts them in BSS, which is longword-aligned -- these are
   * UBYTE arrays reinterpreted as structs holding UWORD/ULONG fields, and an
   * odd-aligned access of that kind is a bus fault on a 68000, not a slowdown.
   */
  static UBYTE rbuf[1024];
  static UBYTE pkt[64];
  const char *host;
  ULONG  dest = 0;
  LONG   maxhops, probes, timeout;
  LONG   sock = -1, ttl, seq = 0, ident;
  int    rc = RETURN_OK, lookup, quiet, done = 0, stop = 0;
  long   senderr = 0;

  rda = ReadArgs((STRPTR)"HOST/A,MAXHOPS/K/N,PROBES/K/N,TIMEOUT/K/N,LOOKUP/S,QUIET/S",
                 a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }

  host    = (const char *)a[0];
  maxhops = a[1] ? *(LONG *)a[1] : 30;
  probes  = a[2] ? *(LONG *)a[2] : 3;
  timeout = a[3] ? *(LONG *)a[3] : 3;
  lookup  = a[4] != 0;
  quiet   = a[5] != 0;
  if (maxhops < 1)   maxhops = 1;
  if (maxhops > 255) maxhops = 255;
  if (probes  < 1)   probes  = 1;
  if (probes  > 10)  probes  = 10;
  if (timeout < 1)   timeout = 1;
  if (timeout > 60)  timeout = 60;

  if ((SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4L)) == NULL) {
    Printf((STRPTR)PROG ": cannot open bsdsocket.library v4\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  /* Resolve the destination: a dotted quad first, a name second. */
  dest = ng_inet_addr(host);
  if (dest == 0xFFFFFFFFUL) {
    if ((he = (struct ng_hostent *)ng_gethostbyname(host)) == NULL ||
        he->h_addr_list == NULL || he->h_addr_list[0] == NULL) {
      Printf((STRPTR)PROG ": cannot resolve '%s'\n", (LONG)host);
      rc = RETURN_ERROR;
      goto out;
    }
    /*
     * Byte by byte, NOT *(ULONG *)he->h_addr_list[0].
     *
     * The resolver packs its answer into one buffer with the hostname and any
     * aliases in front of the addresses and no re-alignment, so the address a
     * DNS reply lands at is at an odd offset whenever the name that preceded it
     * has even length. A longword read of an odd address on a 68000 is an
     * Address Error -- a hard crash, not a slow access -- and -m68000 is our
     * default build. So the crash would depend on the LENGTH OF THE HOSTNAME
     * the user typed, which is about as confusing as a bug report gets.
     * ping.c already does it this way for the same reason.
     */
    {
      UBYTE *p = (UBYTE *)he->h_addr_list[0];
      dest = ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
             ((ULONG)p[2] << 8)  |  (ULONG)p[3];
    }
  }

  if ((sock = ng_socket(NG_AF_INET, NG_SOCK_RAW, NG_IPPROTO_ICMP)) < 0) {
    Printf((STRPTR)PROG ": cannot open a raw ICMP socket (errno %ld)\n", ng_errno());
    rc = RETURN_FAIL;
    goto out;
  }
  if (!timer_open()) {
    Printf((STRPTR)PROG ": cannot open timer.device\n");
    rc = RETURN_FAIL;
    goto out;
  }

  /* The id ties replies to THIS run, so two traceroutes at once do not read each
   * other's answers. The task pointer is unique while we are running. */
  ident = (LONG)((ULONG)FindTask(NULL) & 0xFFFF);

  to.sin_len = sizeof(to); to.sin_family = NG_AF_INET;
  to.sin_port = 0; to.sin_addr = dest;
  { int z; for (z = 0; z < 8; z++) to.zero[z] = 0; }

  if (!quiet) {
    Printf((STRPTR)"traceroute to %s (", (LONG)host);
    print_addr(dest);
    Printf((STRPTR)"), %ld hops max\n", maxhops);
  }

  for (ttl = 1; ttl <= maxhops && !done && !stop; ttl++) {
    ULONG lastaddr = 0;
    int   p, shown = 0;

    Printf((STRPTR)"%2ld ", ttl);

    for (p = 0; p < probes; p++) {
      struct ng_icmp *ic = (struct ng_icmp *)pkt;
      struct timeval t0, t1, tnow, tvto;
      LONG opt = ttl, n;
      int  got = 0;

      if (ng_setsockopt(sock, NG_IPPROTO_IP, NG_IP_TTL, &opt, sizeof(opt)) < 0) {
        Printf((STRPTR)"\n" PROG ": cannot set the TTL (errno %ld)\n", ng_errno());
        rc = RETURN_FAIL;
        goto out;
      }

      seq++;
      { int z; for (z = 0; z < (int)sizeof(pkt); z++) pkt[z] = 0; }
      ic->type = NG_ICMP_ECHO;
      ic->code = 0;
      ic->cksum = 0;
      ic->id  = (UWORD)ident;
      ic->seq = (UWORD)seq;
      ic->cksum = in_cksum((UWORD *)pkt, sizeof(pkt));

      timer_now(&t0);
      if (ng_sendto(sock, pkt, sizeof(pkt), 0, &to, sizeof(to)) < 0) {
        /*
         * The send itself failed, which is a LOCAL verdict -- no route, or ARP
         * gave up on the next hop -- not a silent hop. Retrying it up the
         * remaining TTLs would print the same failure thirty times and tell the
         * user nothing new, so report it once and stop.
         */
        senderr = ng_errno();
        Printf((STRPTR)"  %s", (LONG)send_mark(senderr));
        stop = 1;
        break;
      }

      for (;;) {
        ULONG rd = 1UL << sock, *rdp = &rd, sig;
        long  rem_s, rem_us, ihl, frlen;
        struct ng_icmp *ric;

        /* Always the REMAINING time, never a fresh timeout: a raw ICMP socket sees
         * traffic that is not ours, and re-arming the full value on each discarded
         * packet would mean TIMEOUT bounded nothing at all. Kept as seconds plus
         * microseconds because timeout*1000000 overflows a 32-bit long. */
        timer_now(&tnow);
        rem_s  = (long)(t0.tv_secs + (ULONG)timeout) - (long)tnow.tv_secs;
        rem_us = (long)t0.tv_micro - (long)tnow.tv_micro;
        if (rem_us < 0) { rem_us += 1000000L; rem_s--; }
        if (rem_s < 0 || (rem_s == 0 && rem_us <= 0))
          break;					/* this probe timed out */

        tvto.tv_secs = (ULONG)rem_s; tvto.tv_micro = (ULONG)rem_us;
        sig = SIGBREAKF_CTRL_C;
        n = ng_waitselect(sock + 1, rdp, 0, 0, &tvto, &sig);
        if (n < 0) {
          Printf((STRPTR)"\n" PROG ": select failed (errno %ld)\n", ng_errno());
          rc = RETURN_FAIL;
          goto out;
        }
        if (sig & SIGBREAKF_CTRL_C) { stop = 1; break; }
        if (n == 0) break;				/* timed out */

        frlen = sizeof(from);
        if ((n = ng_recvfrom(sock, rbuf, sizeof(rbuf), 0, &from, &frlen)) < 0) {
          /* Not the same thing as a timeout, and it used to print the same "*".
           * select() said a packet was there, so a failure here is the socket
           * or the interface going wrong -- and reporting it as ordinary packet
           * loss hides that for the whole rest of the run. The send and select
           * failures in this same loop are both reported; this was the one that
           * was not. */
          Printf((STRPTR)"  !recv(%ld)", ng_errno());
          break;
        }

        /* A raw ICMP read starts at the IP header. Require a sane header length so
         * everything read past it is inside what recvfrom() actually delivered. */
        ihl = (rbuf[0] & 0x0F) * 4;
        if (ihl < 20 || n < ihl + (long)sizeof(struct ng_icmp))
          continue;
        ric = (struct ng_icmp *)(rbuf + ihl);

        if (ric->type == NG_ICMP_ECHOREPLY) {
          /* Arrived. Only ours, and only this probe's: the id is constant for the
           * run, so without the sequence a late reply to an earlier probe would be
           * timed against this one and report an RTT far shorter than the truth. */
          if (ric->id != (UWORD)ident || ric->seq != (UWORD)seq)
            continue;
          got = 1; done = 1;
        } else if (ric->type == NG_ICMP_TIMXCEED || ric->type == NG_ICMP_UNREACH) {
          /* The quoted packet follows the 8-byte ICMP header: our original IP
           * header, then the first 8 bytes of our echo -- enough to carry the id
           * and sequence we put there, which is what proves it is ours. */
          struct ng_ip   *qip;
          struct ng_icmp *qic;
          long qihl;

          if (n < ihl + 8 + 20)
            continue;
          qip  = (struct ng_ip *)(rbuf + ihl + 8);
          qihl = (qip->vhl & 0x0F) * 4;
          if (qihl < 20 || n < ihl + 8 + qihl + 8)
            continue;
          if (qip->proto != NG_IPPROTO_ICMP)
            continue;
          qic = (struct ng_icmp *)((UBYTE *)qip + qihl);
          if (qic->id != (UWORD)ident || qic->seq != (UWORD)seq)
            continue;					/* somebody else's */
          got = 1;
          if (ric->type == NG_ICMP_UNREACH)
            done = 1;					/* the path ends here */
        } else {
          continue;					/* not interesting */
        }

        timer_now(&t1);
        if (from.sin_addr != lastaddr) {
          /* Print the address only when it changes: three probes usually come back
           * from the same router, and repeating it three times per line is noise. */
          if (shown) Printf((STRPTR)"\n   ");
          print_addr(from.sin_addr);
          if (lookup) {
            struct ng_hostent *rh =
              (struct ng_hostent *)ng_gethostbyaddr(&from.sin_addr, 4, NG_AF_INET);
            if (rh && rh->h_name) Printf((STRPTR)" (%s)", (LONG)rh->h_name);
          }
          lastaddr = from.sin_addr;
          shown = 1;
        }
        { long us = usec_diff(&t0, &t1);
          if (us < 0) us = 0;
          Printf((STRPTR)"  %ld.%ld ms", us / 1000, (us % 1000) / 100); }
        if (ric->type == NG_ICMP_UNREACH && ric->code != 3)
          Printf((STRPTR)" %s", (LONG)unreach_mark(ric->code));
        break;
      }

      if (stop) break;
      if (!got)
        Printf((STRPTR)"  *");
    }
    Printf((STRPTR)"\n");
  }

  if (senderr) {
    Printf((STRPTR)PROG ": cannot send to that address (errno %ld) -- no route, or "
                   "the next hop is not answering\n", senderr);
    rc = RETURN_WARN;
  } else if (stop) {
    Printf((STRPTR)"***Break\n");
    rc = RETURN_WARN;
  } else if (!done && !quiet) {
    /* Say so rather than just stopping: a silent end at hop 30 looks like the
     * path really is 30 hops long. */
    Printf((STRPTR)PROG ": did not reach the destination within %ld hops\n", maxhops);
    rc = RETURN_WARN;
  }

out:
  timer_close();
  if (sock >= 0) ng_closesocket(sock);
  if (SocketBase) CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
