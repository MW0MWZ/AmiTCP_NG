/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * sntp -- set the clock from a network time server (SNTP, RFC 4330).
 *
 *   sntp <host> [QUERY] [OFFSET <minutes>] [PORT <n>]
 *
 * SETS THE CLOCK BY DEFAULT, because that is how Roadshow's sntp is invoked from
 * a startup script -- `c:sntp pool.ntp.org >ram:sntp.log`, no keyword. A drop-in
 * that needed an extra argument would silently do nothing in every existing
 * script, which is a worse failure than the one it was guarding against: the
 * script reports success and the clock stays wrong.
 *
 * QUERY reports without touching either clock, for when you only want the drift
 * or want to check a server before trusting it. SET is still accepted and does
 * nothing, so anything written against the earlier beta keeps working.
 *
 * WHY THIS EXISTS. Plenty of Amigas have no working battery clock and boot with
 * a meaningless date, which quietly breaks file timestamps, anything comparing
 * modification times, and every build tool. AmiTCP 3.0b2 shipped SynClock for the
 * same reason. install/db/netdb already lists ntp 123/udp.
 *
 * SETTING THE TIME IS AN IO COMMAND, NOT A LIBRARY CALL. timer.device exposes
 * GetSysTime as a vector but there is no SetSysTime vector; TR_SETSYSTIME is a
 * DoIO on a timerequest. Reaching for the vector that "should" be next after
 * GetSysTime jumps off the end of the table, and on a machine with no memory
 * protection that crashes rather than failing. The battery clock is written as
 * well, via battclock.resource, so the time survives a power cycle.
 *
 * THREE THINGS THAT ARE EASY TO GET WRONG, and are handled explicitly below:
 *
 *  - THE EPOCH. NTP counts seconds from 1900-01-01, AmigaOS from 1978-01-01. The
 *    difference is 2,461,449,600 seconds. Getting it wrong yields a plausible
 *    date decades out rather than an obvious failure.
 *
 *  - THE 2036 ROLLOVER. NTP's seconds field is 32 bits and wraps on 2036-02-07.
 *    That is not hypothetical for a machine that may well still be running then,
 *    and this tool exists precisely for machines whose own clock cannot be
 *    trusted to tell you which side of it you are on. A wrapped value is
 *    detectable -- it is smaller than the epoch difference, which no real time
 *    after 1978 can be -- so era 1 is handled rather than ignored. Both eras are
 *    verified against known timestamps with docker/bench/sntpd.py: NTP
 *    3944678400 must give 2025-01-01, and the wrapped 123010304 must give
 *    2040-01-01.
 *
 *  - TRUSTING THE REPLY. The server's transmit timestamp is used, but only after
 *    checking the reply is a server reply, is synchronised, has a sane stratum,
 *    and echoes the transmit timestamp we sent. That last check is what stops any
 *    passing UDP packet from setting the clock: without it, an attacker (or a
 *    stray datagram) needs only to reach port 123 on the right socket.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <utility/date.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

struct Library *SocketBase = 0;
/* proto/utility.h already declares UtilityBase as struct UtilityBase * --
 * define it with that type rather than shadowing it with a different one. */
struct UtilityBase *UtilityBase = 0;

/* ---- bsdsocket vectors ------------------------------------------------------ */
static long ng_socket(long d, long t, long p) {			/* -30  (d0,d1,d2) */
  register long _d0 __asm("d0")=d, _d1 __asm("d1")=t, _d2 __asm("d2")=p;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a0","a1","memory");
  return _d0;
}
static long ng_sendto(long s, void *b, long l, long f, void *to, long tl) { /* -60 */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f, _d3 __asm("d3")=tl;
  register void *_a0 __asm("a0")=b, *_a1 __asm("a1")=to;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-60)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_d3)
                       :"r"(_a6):"memory");
  return _d0;
}
static long ng_recvfrom(long s, void *b, long l, long f, void *from, long *fl) { /* -72 */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f;
  register void *_a0 __asm("a0")=b, *_a1 __asm("a1")=from;
  register long *_a2 __asm("a2")=fl;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-72)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_a2)
                       :"r"(_a6):"memory");
  return _d0;
}
static void ng_close(long s) {					/* -120 (d0) */
  register long _d0 __asm("d0")=s; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_a6):"d1","a0","a1","memory");
}
static long ng_waitselect(long n, void *r, void *w, void *e, void *tv, ULONG *sig) { /* -126 */
  register long _d0 __asm("d0")=n; register void *_a0 __asm("a0")=r, *_a1 __asm("a1")=w;
  register void *_a2 __asm("a2")=e, *_a3 __asm("a3")=tv; register ULONG *_d1 __asm("d1")=sig;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-126)":"+r"(_d0),"+r"(_a0),"+r"(_a1),"+r"(_a2),"+r"(_a3),"+r"(_d1)
                       :"r"(_a6):"memory");
  return _d0;
}
static long ng_errno(void) {					/* -162 */
  register long _d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static ULONG ng_inet_addr(const char *s) {			/* -180 (a0) */
  register ULONG _d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-180)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}
static void *ng_gethostbyname(const char *s) {			/* -210 (a0) */
  register void *_d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-210)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}

struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE zero[8]; };
typedef char ng_sin_must_be_16[(sizeof(struct ng_sin) >= 16) ? 1 : -1];
struct ng_hostent { char *h_name; char **h_aliases; long h_addrtype, h_length; char **h_addr_list; };
struct ng_tv { long tv_secs, tv_micro; };

#define NG_AF_INET	2
#define NG_SOCK_DGRAM	2
#define NTP_PORT	123
#define NTP_PKT		48
#define MAX_FD		512
#define RETRIES		3
#define MAX_STRAY	100	/* unusable replies tolerated before giving up */
#define TIMEOUT_SECS	5

/*
 * Seconds between 1900-01-01 (NTP) and 1978-01-01 (AmigaOS).
 * 78 years = 28470 days, plus 19 leap days (1904..1976; 1900 is NOT a leap year,
 * being divisible by 100 but not 400) = 28489 days x 86400.
 * Cross-check: NTP->Unix is the well-known 2208988800, and Unix->Amiga is
 * 252460800, which sum to the same figure.
 */
#define NTP_TO_AMIGA	2461449600UL

/* NTP era 1 begins when the 32-bit seconds field wraps, on 2036-02-07. */
#define ERA1_ADJUST	(4294967296.0)		/* documented; see ntp_to_amiga() */

static UBYTE pkt[NTP_PKT];

/* ---- helpers ---------------------------------------------------------------- */

static unsigned long now_ticks(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return (unsigned long)ds.ds_Minute * 3000UL + (unsigned long)ds.ds_Tick;
}

static int wait_readable(long s, long secs)
{
  ULONG fds[16];
  struct ng_tv tv;
  ULONG sigs = SIGBREAKF_CTRL_C;
  long r;
  int i;

  if (s < 0 || s >= MAX_FD) return -1;
  for (i = 0; i < 16; i++) fds[i] = 0;
  fds[s / 32] |= (1UL << (s % 32));
  tv.tv_secs = secs; tv.tv_micro = 0;
  r = ng_waitselect(s + 1, fds, (void *)0, (void *)0, &tv, &sigs);
  if (sigs & SIGBREAKF_CTRL_C) return -1;
  if (r < 0) return -1;
  return (r > 0) ? 1 : 0;
}

static ULONG get32(const UBYTE *p)
{
  return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}
static void put32(UBYTE *p, ULONG v)
{
  p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16);
  p[2] = (UBYTE)(v >> 8);  p[3] = (UBYTE)v;
}

/*
 * NTP seconds -> AmigaOS seconds, handling the 2036 wrap.
 *
 * Any real time after 1978 is at least NTP_TO_AMIGA seconds into the NTP epoch,
 * so a value SMALLER than that cannot be an era-0 timestamp -- it has wrapped,
 * and belongs to era 1. Adding 2^32 before subtracting recovers it. Done in
 * 32-bit arithmetic by rearranging rather than widening: era 1 gives
 * ntp + (2^32 - NTP_TO_AMIGA), which fits.
 *
 * Returns 0 if the result is not a believable date, so the caller can refuse.
 */
static ULONG ntp_to_amiga(ULONG ntp)
{
  ULONG asecs;

  if (ntp == 0) return 0;			/* server never set it */

  if (ntp >= NTP_TO_AMIGA)
    asecs = ntp - NTP_TO_AMIGA;			/* era 0: 1978 .. 2036 */
  else
    asecs = ntp + (ULONG)(4294967296.0 - (double)NTP_TO_AMIGA);	/* era 1: 2036+ */

  /* Sanity floor and ceiling, in Amiga seconds. A time before 2000 or after
   * 2100 from a time server means something is wrong; refuse rather than set
   * the clock to it. 2000-01-01 is 22 years after 1978 (5 leap days),
   * 2100-01-01 is 122 years (30 leap days). */
  if (asecs < (22UL * 365UL + 5UL) * 86400UL) return 0;
  if (asecs > (122UL * 365UL + 30UL) * 86400UL) return 0;
  return asecs;
}

static void show_time(const char *label, ULONG secs)
{
  struct ClockData cd;

  Amiga2Date(secs, &cd);
  Printf((STRPTR)"%s%ld-%02ld-%02ld %02ld:%02ld:%02ld\n", (LONG)label,
         (LONG)cd.year, (LONG)cd.month, (LONG)cd.mday,
         (LONG)cd.hour, (LONG)cd.min, (LONG)cd.sec);
}

/* ---- main ------------------------------------------------------------------- */

int main(void)
{
  struct RDArgs *rda;
  LONG args[5];
  struct ng_sin srv, from;
  struct ng_hostent *hp;
  struct timerequest *tr = 0;
  struct MsgPort *tp = 0;
  const char *host;
  ULONG addr, mine_hi, mine_lo, t3, asecs, before, after;
  long s = -1, fromlen, n, port = NTP_PORT, offmin = 0;
  int i, try, rc = RETURN_OK, doset;

  for (i = 0; i < 5; i++) args[i] = 0;
  rda = ReadArgs((STRPTR)"HOST/A,QUERY/S,OFFSET/K/N,PORT/K/N,SET/S", args, NULL);
  if (!rda) {
    Printf((STRPTR)"usage: sntp <host> [QUERY] [OFFSET <minutes>] [PORT <n>]\n"
                   "  sets the system and battery clocks from <host>.\n"
                   "  QUERY reports the time without setting anything.\n"
                   "  OFFSET is minutes EAST of UTC (AmigaOS keeps local time).\n");
    return RETURN_FAIL;
  }
  host  = (const char *)args[0];
  doset = args[1] ? 0 : 1;		/* QUERY suppresses it; setting is the default */
  if (args[2]) offmin = *(LONG *)args[2];
  if (args[3]) port   = *(LONG *)args[3];
  /* args[4] is SET: accepted for compatibility with the earlier beta, but
   * setting is now the default so it has nothing to do. */

  if (offmin < -1440 || offmin > 1440) {
    Printf((STRPTR)"sntp: OFFSET must be -1440..1440 minutes\n");
    FreeArgs(rda); return RETURN_FAIL;
  }
  if (port < 1 || port > 65535) {
    Printf((STRPTR)"sntp: PORT must be 1..65535\n");
    FreeArgs(rda); return RETURN_FAIL;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    Printf((STRPTR)"sntp: bsdsocket.library v4+ not available.\n");
    FreeArgs(rda); return RETURN_FAIL;
  }
  UtilityBase = (struct UtilityBase *)OpenLibrary((STRPTR)"utility.library", 37);
  if (!UtilityBase) {
    Printf((STRPTR)"sntp: utility.library v37+ not available.\n");
    CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL;
  }

  addr = ng_inet_addr(host);
  if (addr == 0xFFFFFFFFUL) {
    hp = (struct ng_hostent *)ng_gethostbyname(host);
    if (!hp || !hp->h_addr_list || !hp->h_addr_list[0]) {
      Printf((STRPTR)"sntp: unknown host %s\n", (LONG)host);
      rc = RETURN_ERROR; goto done;
    }
    { UBYTE *p = (UBYTE *)hp->h_addr_list[0];
      addr = ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3]; }
  }

  for (i = 0; i < (int)sizeof(srv); i++) ((char *)&srv)[i] = 0;
  srv.sin_len = sizeof(srv); srv.sin_family = NG_AF_INET;
  srv.sin_port = (UWORD)port; srv.sin_addr = addr;

  s = ng_socket(NG_AF_INET, NG_SOCK_DGRAM, 0);
  if (s < 0) {
    Printf((STRPTR)"sntp: socket failed (errno %ld)\n", ng_errno());
    rc = RETURN_FAIL; goto done;
  }

  /*
   * Ask, and wait for a reply that is actually ours.
   *
   * Only a genuine TIMEOUT costs a retry and causes a resend. A packet that is
   * not ours is ignored without either -- otherwise any peer that keeps sending
   * datagrams refills the retry budget forever and this never terminates, and
   * resending on every rejected packet would amplify a flood back at the
   * network. The sibling tftp and ftp clients had exactly this bug; it is the
   * same fix.
   */
  {
    int stray = 0, need_send = 1;

    try = 0;
    for (;;) {
      int w;

      if (need_send) {
        /*
         * LI=0, VN=4, Mode=3 (client). Everything zero except our transmit
         * timestamp, which the server must echo back -- that echo is the only
         * thing separating its reply from any other datagram arriving here.
         *
         * The nonce is only as unguessable as the clock that seeds it: DateStamp
         * gives minute-and-tick, about 22 bits. Mixing in a stack address and the
         * socket number widens it, and deriving the two halves through separate
         * steps stops them being near-copies of one another -- as they were when
         * each came from its own back-to-back now_ticks() call. This raises the
         * bar for a blind off-path forgery; it cannot fix one, because SNTP
         * without authentication is spoofable by anyone who can see the wire.
         */
        ULONG seed = now_ticks() ^ (ULONG)(APTR)&stray
                     ^ ((ULONG)s << 16) ^ (ULONG)try;
        mine_hi = seed * 2654435761UL + 0x9E3779B9UL;
        mine_lo = (mine_hi ^ (mine_hi >> 13)) * 1103515245UL + 12345UL;

        for (i = 0; i < NTP_PKT; i++) pkt[i] = 0;
        pkt[0] = 0x23;
        put32(&pkt[40], mine_hi);
        put32(&pkt[44], mine_lo);

        before = now_ticks();
        if (ng_sendto(s, pkt, NTP_PKT, 0, &srv, sizeof(srv)) < 0) {
          Printf((STRPTR)"sntp: sendto failed (errno %ld)\n", ng_errno());
          rc = RETURN_ERROR; goto done;
        }
        need_send = 0;
      }

      w = wait_readable(s, TIMEOUT_SECS);
      if (w < 0) { Printf((STRPTR)"sntp: aborted\n"); rc = RETURN_ERROR; goto done; }
      if (w == 0) {
        if (++try > RETRIES) {
          Printf((STRPTR)"sntp: no reply from %s\n", (LONG)host);
          rc = RETURN_ERROR; goto done;
        }
        need_send = 1;
        continue;
      }

      for (i = 0; i < (int)sizeof(from); i++) ((char *)&from)[i] = 0;
      fromlen = sizeof(from);
      n = ng_recvfrom(s, pkt, NTP_PKT, 0, &from, &fromlen);
      after = now_ticks();

      /*
       * Everything below rejects WITHOUT resending. The echo is checked before
       * LI and stratum are believed, so a packet that merely spoofs the source
       * address cannot make us abort with "not synchronised" -- it has to guess
       * the nonce first.
       */
      if (n >= NTP_PKT && fromlen >= (long)sizeof(from) &&
          from.sin_addr == srv.sin_addr && (pkt[0] & 7) == 4 &&
          get32(&pkt[24]) == mine_hi && get32(&pkt[28]) == mine_lo) {

        if (((pkt[0] >> 6) & 3) == 3) {			/* LI = 3: unsynchronised */
          Printf((STRPTR)"sntp: server says it is not synchronised\n");
          rc = RETURN_ERROR; goto done;
        }
        if (pkt[1] == 0 || pkt[1] > 15) {		/* stratum 0 = kiss-o'-death */
          Printf((STRPTR)"sntp: server refused (stratum %ld)\n", (LONG)pkt[1]);
          rc = RETURN_ERROR; goto done;
        }
        break;						/* this one is ours */
      }

      if (++stray > MAX_STRAY) {
        Printf((STRPTR)"sntp: too many unusable replies -- giving up\n");
        rc = RETURN_ERROR; goto done;
      }
    }
  }

  t3 = get32(&pkt[40]);				/* server transmit, seconds */
  asecs = ntp_to_amiga(t3);
  if (asecs == 0) {
    Printf((STRPTR)"sntp: the server's timestamp is not a believable date -- ignoring it\n");
    rc = RETURN_ERROR; goto done;
  }

  /* Half the round trip, in whole seconds. Crude, but the alternative is
   * pretending to a precision this has no way to deliver. */
  { unsigned long rtt = (after >= before) ? (after - before) : 0;
    asecs += (rtt / 50UL) / 2UL;
  }
  asecs = asecs + (ULONG)(offmin * 60L);	/* local time, if asked. ULONG
				 * throughout: modular arithmetic is fully defined, and the
				 * sanity floor (year 2000) is far above any offset, so a
				 * negative one cannot underflow. */

  show_time("Server time: ", asecs);
  { struct DateStamp ds;
    DateStamp(&ds);
    show_time("System time: ",
              (ULONG)ds.ds_Days * 86400UL + (ULONG)ds.ds_Minute * 60UL +
              (ULONG)ds.ds_Tick / 50UL);
  }

  if (!doset) {
    Printf((STRPTR)"(QUERY -- neither clock was changed)\n");
    goto done;
  }

  /*
   * SET the clock -- both of them.
   *
   * The running system time is set with the TR_SETSYSTIME IO COMMAND, not a
   * library vector. That distinction cost me a crash: timer.device exposes
   * GetSysTime as a vector (-66) but there is no SetSysTime vector, so calling
   * the next slot jumped past the end of the table and took the machine down.
   * Setting the time has always been a DoIO on a timerequest -- devices/timer.h
   * has had TR_SETSYSTIME all along.
   *
   * The battery clock is written too, so the time survives a power cycle rather
   * than lasting only until the next boot.
   */
  tp = CreateMsgPort();
  if (tp) tr = (struct timerequest *)CreateIORequest(tp, sizeof(struct timerequest));
  if (!tr || OpenDevice((STRPTR)"timer.device", UNIT_VBLANK,
                        (struct IORequest *)tr, 0) != 0) {
    Printf((STRPTR)"sntp: cannot open timer.device -- clock not set\n");
    rc = RETURN_FAIL; goto done;
  }
  tr->tr_node.io_Command = TR_SETSYSTIME;
  tr->tr_time.tv_secs    = asecs;
  tr->tr_time.tv_micro   = 0;
  DoIO((struct IORequest *)tr);
  CloseDevice((struct IORequest *)tr);
  Printf((STRPTR)"System clock set.\n");

  /* And the battery clock, so it survives a power cycle. Not fatal if absent --
   * plenty of machines have no battery-backed clock, which is half the reason
   * this tool exists. */
  {
    struct Library *BattClockBase = (struct Library *)OpenResource((STRPTR)"battclock.resource");
    if (BattClockBase) {
      register ULONG _d0 __asm("d0") = asecs;
      register struct Library *_a6 __asm("a6") = BattClockBase;
      /* WriteBattClock(time)(d0), bias 6: ResetBattClock -6, ReadBattClock -12,
       * WriteBattClock -18. From the NDK fd, not inferred. */
      __asm__ __volatile__("jsr a6@(-18)":"+r"(_d0):"r"(_a6):"d1","a0","a1","memory");
      Printf((STRPTR)"Battery clock set.\n");
    }
  }

done:
  if (tr) DeleteIORequest((struct IORequest *)tr);
  if (tp) DeleteMsgPort(tp);
  if (s >= 0) ng_close(s);
  if (UtilityBase) CloseLibrary((struct Library *)UtilityBase);
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
