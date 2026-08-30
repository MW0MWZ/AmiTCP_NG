/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ipbench -- how fast does the STACK itself move a packet, on this machine?
 *
 * WHY THIS EXISTS. The 68020/68040 archives are gcc's default codegen at a higher
 * -march, with no hand tuning, and on a real 68030 that codegen can be WORSE than
 * the plain 68000 build: struct ip's ip_v:4/ip_hl:4 bitfields made gcc emit BFINS
 * -- a read-modify-write of the packet header in memory -- once per transmitted
 * packet, an instruction the 68000 build cannot even encode. Whether that costs
 * anything MEASURABLE is not answerable by reading a disassembly. This answers it.
 *
 * WHY LOOPBACK. Every packet goes ip_output -> looutput -> ip_input -> udp_input
 * and back to the socket, so the whole per-packet path is exercised -- but no NIC,
 * no cable, no other machine, and no driver is involved. That makes it CPU-bound
 * and repeatable, which is exactly what is needed to compare three library builds
 * on ONE machine. A throughput test over real hardware measures the card as much
 * as the stack.
 *
 * HOW TO USE IT. The binary is built for 68000 so it runs anywhere; it is the
 * LIBRARY that changes. Run it once per bsdsocket.library variant, on the same
 * machine, with nothing else running:
 *
 *     copy <variant> LIBS:bsdsocket.library   (then reboot, or restart the stack)
 *     ipbench
 *
 * It prints packets/sec and payload throughput for several sizes. Small sizes
 * expose PER-PACKET cost (where header bitfield work lives); large sizes expose
 * per-BYTE cost (checksum and copying). If the builds differ, they will differ
 * most at the small end.
 *
 * BEST-of-N, not mean: on a real Amiga the machine is doing other things, and
 * every one of them makes a round SLOWER, never faster. The fastest round is the
 * one least disturbed, so it is the honest estimate of the code's own cost.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>	/* GetSysTime() -- needs TimerBase, set from the device base */

struct Library *SocketBase;
static struct MsgPort     *tport;
static struct timerequest *treq;
struct Device *TimerBase;		/* proto/timer.h declares this extern; define it here */

/* ------------------------------------------------------------------------- *
 * bsdsocket.library vectors.
 *
 * LVOs and register assignments taken from ref/roadshow-sdk/sfd/bsdsocket_lib.sfd
 * (==bias 30, -6 per slot, ==varargs entries share the previous slot), and
 * cross-checked against the three already in src/tools/ng_lvo.h -- socket -30,
 * IoctlSocket -114, CloseSocket -120 all agree.
 *
 * EVERY register that carries a parameter is "+r", never a bare input. d0/d1/a0/a1
 * are unconditionally scratch across an AmigaOS library call, and letting the
 * compiler assume one survives the jsr is the exact bug that broke DHCP on the
 * 68040 build of this project. a6 stays a plain input: the library base IS
 * preserved.
 * ------------------------------------------------------------------------- */
static long v_socket(long dom, long typ, long proto)		/* socket -30 */
{
  register long _d0 __asm("d0")=dom, _d1 __asm("d1")=typ, _d2 __asm("d2")=proto;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)"
    : "+r"(_d0),"+r"(_d1),"+r"(_d2) : "r"(_a6) : "a0","a1","cc","memory");
  return _d0;
}
static long v_bind(long s, void *name, long len)		/* bind -36 */
{
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=len;
  register void *_a0 __asm("a0")=name;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-36)"
    : "+r"(_d0),"+r"(_d1),"+r"(_a0) : "r"(_a6) : "a1","cc","memory");
  return _d0;
}
static long v_sendto(long s, void *buf, long len, long flags, void *to, long tolen)
{								/* sendto -60 */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=len, _d2 __asm("d2")=flags,
                _d3 __asm("d3")=tolen;
  register void *_a0 __asm("a0")=buf, *_a1 __asm("a1")=to;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-60)"
    : "+r"(_d0),"+r"(_d1),"+r"(_d2),"+r"(_d3),"+r"(_a0),"+r"(_a1)
    : "r"(_a6) : "cc","memory");
  return _d0;
}
static long v_recvfrom(long s, void *buf, long len, long flags, void *from, void *fromlen)
{								/* recvfrom -72 */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=len, _d2 __asm("d2")=flags;
  register void *_a0 __asm("a0")=buf, *_a1 __asm("a1")=from, *_a2 __asm("a2")=fromlen;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-72)"
    : "+r"(_d0),"+r"(_d1),"+r"(_d2),"+r"(_a0),"+r"(_a1),"+r"(_a2)
    : "r"(_a6) : "d3","cc","memory");
  return _d0;
}
static long v_getsockname(long s, void *name, void *len)	/* getsockname -102 */
{
  register long _d0 __asm("d0")=s;
  register void *_a0 __asm("a0")=name, *_a1 __asm("a1")=len;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-102)"
    : "+r"(_d0),"+r"(_a0),"+r"(_a1) : "r"(_a6) : "d1","cc","memory");
  return _d0;
}
static long v_ioctl(long s, unsigned long req, void *arg)	/* IoctlSocket -114 */
{
  register long _d0 __asm("d0")=s;
  register unsigned long _d1 __asm("d1")=req;
  register void *_a0 __asm("a0")=arg;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-114)"
    : "+r"(_d0),"+r"(_d1),"+r"(_a0) : "r"(_a6) : "a1","cc","memory");
  return _d0;
}
static long v_closesocket(long s)				/* CloseSocket -120 */
{
  register long _d0 __asm("d0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)"
    : "+r"(_d0) : "r"(_a6) : "d1","a0","a1","cc","memory");
  return _d0;
}

/* sockaddr_in, laid out by hand so this file needs no netinclude headers. */
struct sa_in { UBYTE len, fam; UWORD port; ULONG addr; UBYTE pad[8]; };

#define AF_INET_    2
#define SOCK_DGRAM_ 2

/* Aim each timed round at roughly this long. Five rounds per size, four sizes,
 * so a whole run is about 4 x 5 x this plus probes -- comfortably inside the
 * emulator harness timeout as well as a human's patience. */
#define NG_TARGET_US 400000UL

/* Printf + Flush. An unflushed buffer loses exactly the line that says where it
 * died -- which is precisely what happened the first time this hung. */
static void say(const char *fmt, long a, long b, long c, long d)
{
  Printf((STRPTR)fmt, a, b, c, d);
  Flush(Output());
}

static ULONG now_us(void)
{
  struct timeval tv;
  GetSysTime(&tv);
  return (ULONG)tv.tv_secs * 1000000UL + (ULONG)tv.tv_micro;
}

/* One measured round: `iters` send+receive round trips of `len` bytes.
 * Returns microseconds, or 0 if anything went wrong. */
static ULONG round_trip(long s, struct sa_in *self, UBYTE *tx, UBYTE *rx,
                        long len, long iters)
{
  struct sa_in from;
  long fromlen, i, n;
  ULONG t0, t1;

  t0 = now_us();
  for (i = 0; i < iters; i++) {
    long spin;

    if (v_sendto(s, tx, len, 0, self, (long)sizeof(*self)) != len)
      return 0;
    /*
     * The socket is NON-BLOCKING and this poll is BOUNDED, deliberately.
     *
     * A blocking recvfrom() here hung the whole run the first time this was
     * tried: if the datagram never comes back the program simply never returns,
     * and a benchmark that can hang the machine it is measuring is worse than no
     * benchmark. On loopback the packet is delivered synchronously inside
     * sendto(), so the first recvfrom() normally succeeds immediately and the
     * poll costs nothing measurable.
     */
    for (spin = 0; spin < 20000; spin++) {
      fromlen = (long)sizeof(from);
      n = v_recvfrom(s, rx, len, 0, &from, &fromlen);
      if (n == len) break;
      if (n >= 0) return 0;		/* wrong length -- not a timing problem */
    }
    if (spin >= 20000)
      return 0;				/* gave up; reported as FAILED, never a hang */
  }
  t1 = now_us();
  return (t1 - t0) ? (t1 - t0) : 1;
}

int main(void)
{
  static const long sizes[] = { 64, 256, 576, 1472 };
  UBYTE *tx = NULL, *rx = NULL;
  struct sa_in me, self;
  long s = -1, salen, i, r, rc = RETURN_FAIL;

  tport = CreateMsgPort();
  treq  = tport ? (struct timerequest *)CreateIORequest(tport, sizeof(*treq)) : NULL;
  if (!treq || OpenDevice((STRPTR)"timer.device", UNIT_MICROHZ,
                          (struct IORequest *)treq, 0)) {
    Printf((STRPTR)"ipbench: no timer.device\n");
    goto out;
  }
  TimerBase = treq->tr_node.io_Device;
  say("ipbench: timer ok\n",0,0,0,0);

  if (!(SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 3L))) {
    Printf((STRPTR)"ipbench: no bsdsocket.library -- is the network running?\n");
    goto out;
  }
  if (!(tx = AllocMem(2048, MEMF_PUBLIC|MEMF_CLEAR)) ||
      !(rx = AllocMem(2048, MEMF_PUBLIC|MEMF_CLEAR))) {
    Printf((STRPTR)"ipbench: out of memory\n");
    goto out;
  }
  say("ipbench: library + buffers ok\n",0,0,0,0);
  for (i = 0; i < 2048; i++) tx[i] = (UBYTE)i;

  if ((s = v_socket(AF_INET_, SOCK_DGRAM_, 0)) < 0) {
    Printf((STRPTR)"ipbench: socket() failed\n");
    goto out;
  }
  /* Bind to loopback on a kernel-chosen port, then ask what we got, so the
   * datagrams have somewhere to come back to. */
  me.len = sizeof(me); me.fam = AF_INET_; me.port = 0;
  me.addr = 0x7f000001UL;			/* 127.0.0.1 */
  for (i = 0; i < 8; i++) me.pad[i] = 0;
  if (v_bind(s, &me, (long)sizeof(me)) < 0) {
    Printf((STRPTR)"ipbench: bind() to 127.0.0.1 failed\n");
    goto out;
  }
  salen = (long)sizeof(self);
  if (v_getsockname(s, &self, &salen) < 0) {
    Printf((STRPTR)"ipbench: getsockname() failed\n");
    goto out;
  }
  /* Non-blocking, so a datagram that never arrives can never hang the run --
   * see the bounded poll in round_trip(). */
  { long on = 1;
    if (v_ioctl(s, 0x8004667eUL /* FIONBIO */, &on) < 0) {
      Printf((STRPTR)"ipbench: could not set non-blocking mode\n");
      goto out;
    }
  }

  say("ipbench: socket bound, port %ld\n",(long)self.port,0,0,0);
  say("ipbench -- UDP loopback, best of 5 rounds\n",0,0,0,0);
  say("  size   packets/s      KB/s   us/packet\n",0,0,0,0);

  for (i = 0; (unsigned)i < sizeof(sizes)/sizeof(sizes[0]); i++) {
    long len = sizes[i];
    long iters;
    ULONG probe, best = 0;

    /*
     * CALIBRATE, do not guess. A fixed iteration count cannot suit both a 7 MHz
     * A600 and a PiStorm: too few and the timer resolution swamps the result,
     * too many and the run never finishes. (Guessing 2000 made the first version
     * need ~65 s for this size alone, which the 150 s emulator harness killed
     * mid-run -- and a benchmark that gets killed looks exactly like a hang.)
     *
     * Time a short probe, then pick the count that lands near NG_TARGET_US per
     * round on THIS machine.
     */
    probe = round_trip(s, &self, tx, rx, len, 20);
    if (!probe) {
      say("  %4ld   FAILED (send/receive mismatch)\n", len,0,0,0);
      continue;
    }
    iters = (long)((ULONG)20 * NG_TARGET_US / probe);
    if (iters < 20)   iters = 20;
    if (iters > 20000) iters = 20000;

    /* The probe above doubles as the warm round: the first pass through any
     * path pays for cold caches and a cold route lookup, and that is not what
     * we are measuring. */
    for (r = 0; r < 5; r++) {
      ULONG us = round_trip(s, &self, tx, rx, len, iters);
      if (!us) { best = 0; break; }
      if (!best || us < best) best = us;
    }
    if (!best) {
      say("  %4ld   FAILED\n", len,0,0,0);
      continue;
    }
    /* Scaled integer maths only -- no FPU, and no floating point in this stack. */
    say("  %4ld  %10ld %9ld %11ld\n",
        len,
        (long)((ULONG)iters * 1000000UL / best),
        (long)(((ULONG)iters * (ULONG)len / 1024UL) * 1000000UL / best),
        (long)(best / (ULONG)iters));
  }
  rc = RETURN_OK;

out:
  if (s >= 0)     v_closesocket(s);
  if (SocketBase) CloseLibrary(SocketBase);
  if (tx) FreeMem(tx, 2048);
  if (rx) FreeMem(rx, 2048);
  if (treq) {
    if (TimerBase) CloseDevice((struct IORequest *)treq);
    DeleteIORequest((struct IORequest *)treq);
  }
  if (tport) DeleteMsgPort(tport);
  return rc;
}
