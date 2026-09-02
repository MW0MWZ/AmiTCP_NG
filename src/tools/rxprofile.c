/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * rxprofile -- receive-path diagnostic. BETA BUILDS ONLY: this command is packaged
 * only when AMITCP_NG_VER carries a -beta suffix (see docker/build-release.sh), so a
 * stable release ships the ordinary Roadshow-compatible command set and nothing else.
 * It exists so a tester can say WHERE a transfer is losing time instead of guessing.
 *
 * Everything it reports comes from counters the library already maintains and already
 * exposes through QueryInterfaceTagList -- this command adds no instrumentation to the
 * stack and costs nothing when it is not running.
 *
 *   rxprofile <interface>            one sample, then exit (since-boot totals)
 *   rxprofile <interface> WATCH      sample every ~2s until Ctrl-C, printing the
 *                                    DELTA per interval -- run a transfer under it
 *
 * There is no CLEAR, deliberately. The stack's counters are global, shared and
 * free-running; zeroing them would blind anything else observing at the same
 * time and would race the net task mid-increment. WATCH instead takes a
 * baseline when it starts and prints a "this run only" report on Ctrl-C, which
 * gives you the figures for just your transfer with no side effects. If you
 * want to measure one transfer, use WATCH and read that block -- the plain
 * single-sample form is since boot and will be dominated by earlier traffic.
 *
 * READING THE RESULT -- the point of the tool:
 *
 *   InputDrops climbing
 *       The IP input queue was full when a frame arrived, so it was discarded and
 *       (for TCP) retransmitted. Receive BUFFERING is the limit. Try a larger ring
 *       with iprequests= in the interface config and re-measure.
 *
 *   InputDrops flat, but throughput still short
 *       Every frame the wire delivered was accepted, so the time is going into the
 *       per-packet COST -- the copy, the checksum, the socket/ACK path -- not into
 *       buffering. A bigger ring would achieve nothing.
 *
 *   BadData climbing
 *       The device itself reports bad frames: look below the stack, at the driver
 *       or the wire.
 *
 *   BPS reported as 0
 *       The driver reports no link speed, so the receive ring was sized from the RAM
 *       tier rather than the link -- which on a fast NIC can leave it far too small.
 *
 *   tcpfp% low during a bulk TCP transfer
 *       The TCP header-prediction fast path is MISSING, so segments are taking the
 *       full input path: option parsing, PCB lookup, the whole state machine. A
 *       steady bulk transfer should sit high; a low figure means something is
 *       defeating prediction every segment, and that is a real cost on a 68000.
 *       The single-sample report breaks down WHY, which is the part you can act
 *       on -- measured on real hardware, download sits at 98% but upload at 9%,
 *       and the two plausible causes want opposite responses:
 *         "peer window moved"  -- costs CPU only. The peer is a receiver draining
 *              its buffer, so its advertised window changes constantly and
 *              prediction (which requires it unchanged) rejects nearly every ACK.
 *         "cwnd below window"  -- NOT a CPU problem. The congestion window never
 *              caught up to what the peer advertised, so the transfer is capped
 *              by congestion control, not by processing cost.
 *       Read the breakdown before changing anything: they look identical in the
 *       hit rate and are completely different faults.
 *
 * NOTE: CopyIn/CopyOut count SANA-II byte-copy callbacks (S2_CopyToBuff /
 * S2_CopyFromBuff). A driver that moves payload some other way reports 0 here --
 * that is a property of the driver, not an error. Compare against PacketsIn: one
 * call per frame is the healthy case; many per frame means the receive chain is
 * being copied piecewise.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase = 0;		/* ng_lvo.h's LVO stubs read this */

#include "ng_lvo.h"


/* SANA-II copy statistics. This MUST be a byte-exact mirror of the library's
 * `struct SANA2CopyStats` (see amiga_roadshow_compat.c) -- it is a 5-field subset
 * of the full SANA-II definition, NOT the whole thing. Declaring the complete
 * spec struct here puts ByteIn/ByteOut at offsets 24/28 while the library writes
 * them at 8/12, so the tool silently reports 0 forever. */
struct ng_copystats {
  ULONG s2cs_DMAIn;
  ULONG s2cs_DMAOut;
  ULONG s2cs_ByteIn;		/* S2_CopyToBuff   calls (RX) */
  ULONG s2cs_ByteOut;		/* S2_CopyFromBuff calls (TX) */
  ULONG s2cs_WordOut;
};

/* 64-bit byte counters arrive as {high, low}; we only ever set low. */
struct ng_quad { ULONG hi, lo; };

#define NMISS 16	/* header-prediction miss buckets (see missdef[] below) */

struct sample {
  ULONG rx, tx, bad, idrops, odrops;
  struct ng_quad bin, bout;
  struct ng_copystats cs;
  LONG  bps, mtu, state;
  ULONG predack, preddat, predwin, rcvtotal; /* TCP header-prediction accounting */
  ULONG pcbmiss;			/* one-entry PCB cache misses */
  ULONG reassfull;			/* segments dropped: reassembly queue full */
  ULONG cp32in, cp32out;		/* SANA-II R4 32-bit copy callbacks */
  ULONG dmain, dmaout;			/* SANA-II DMA: frames the driver moved itself */
  ULONG dmaask, dmaaskout;		/* ...and how often it ASKED, accepted or not */
  ULONG dmanobuf, dmanolen, dmanoalign;	/* why we declined */
  ULONG wkcalls, wkrcv, wkwait, wksel, wkasync;	/* socket wakeup accounting */
  ULONG miss[NMISS];			/* why prediction rejected a segment */
};

/* Miss buckets. Table-driven so the tag, the label and the display order cannot
 * drift apart -- with sixteen of them, hand-written Printf calls would. */
static const struct { ULONG tag; const char *name; } missdef[NMISS] = {
  { NG_SBTC_TPM_WIN,     "peer window moved  " },	/* suspect 1 -- listed */
  { NG_SBTC_TPM_CWND,    "cwnd below window  " },	/* suspect 2 -- first  */
  { NG_SBTC_TPM_STATE,   "not ESTABLISHED    " },
  { NG_SBTC_TPM_FLAGS,   "SYN/FIN/RST/URG    " },
  { NG_SBTC_TPM_TSTAMP,  "timestamp backwards" },
  { NG_SBTC_TPM_SEQ,     "out of sequence    " },
  { NG_SBTC_TPM_REXMIT,  "retransmit pending " },
  { NG_SBTC_TPM_DUPACK,  "dup-ack recovery   " },
  { NG_SBTC_TPM_SACK,    "SACK recovery      " },
  { NG_SBTC_TPM_ACK,     "ack out of range   " },
  { NG_SBTC_TPM_ACKDATA, "data that also acks" },
  { NG_SBTC_TPM_REASS,   "reassembly queued  " },
  { NG_SBTC_TPM_SPACE,   "no socket buf space" },
  { NG_SBTC_TPM_ZEROWIN, "peer window shut   " },
  { NG_SBTC_TPM_WINONLY, "win move REFUSED   " },	/* stale/reordered */
  { NG_SBTC_TPM_ACKDUP,  "true dup ACK (loss)" }
};

/*
 * TCP fast path (Van Jacobson header prediction, netinet/tcp_input.c). An inbound
 * segment on an ESTABLISHED connection that is exactly what we expected -- in
 * sequence, nothing queued for reassembly, window unchanged -- skips the whole
 * general input path. On a 68000 that is the difference between a handful of
 * compares and several hundred instructions per segment, so the HIT RATE is the
 * single most useful number for judging receive-side CPU cost.
 *
 * These are stack-wide, not per-interface: the tags come from SocketBaseTagList,
 * not QueryInterfaceTagList, so on a multi-homed machine they cover all of it.
 */
static void take_tcp(struct sample *s)
{
  /* +11 = 5 base counters + 5 sowakeup counters + TAG_END. Adding a tag before
   * the miss loop means bumping this AND the miss base index below. */
  struct TagItem tg[NMISS + 12];
  int i, n = 0;

  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_PREDACK);  tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_PREDDAT);  tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_RCVTOTAL); tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_PCBMISS);  tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_PREDWIN);  tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_SOWK_CALLS);   tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_SOWK_RCV);     tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_SOWK_WAIT);    tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_SOWK_SEL);     tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_SOWK_ASYNC);   tg[n++].ti_Data = 0;
  tg[n].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_REASSFULL);tg[n++].ti_Data = 0;
  for (i = 0; i < NMISS; i++) {
    tg[n].ti_Tag = NG_SBTM_GETVAL(missdef[i].tag);     tg[n++].ti_Data = 0;
  }
  tg[n].ti_Tag = TAG_END;                              tg[n].ti_Data = 0;

  ng_sbtaglist(tg);

  s->predack  = tg[0].ti_Data;
  s->preddat  = tg[1].ti_Data;
  s->rcvtotal = tg[2].ti_Data;
  s->pcbmiss  = tg[3].ti_Data;
  s->predwin  = tg[4].ti_Data;
  s->wkcalls  = tg[5].ti_Data;  s->wkrcv   = tg[6].ti_Data;
  s->wkwait   = tg[7].ti_Data;  s->wksel   = tg[8].ti_Data;
  s->wkasync  = tg[9].ti_Data;
  s->reassfull= tg[10].ti_Data;
  for (i = 0; i < NMISS; i++)
    s->miss[i] = tg[11 + i].ti_Data;
}

/* Percentage of `total` that `part` represents, rounded, 0 when total is 0.
 * Integer only -- there is no FPU to assume and no reason to want one here. */
static ULONG pct(ULONG part, ULONG total)
{
  if (total == 0) return 0;

  /* part * 200 overflows a 32-bit ULONG past ~21.5M, which a machine left up for a
   * few days of traffic will reach. Scale both sides down first: losing the bottom
   * bits of a percentage is fine, silently printing a wrapped one is not. */
  while (part > 0x01000000UL) {
    part  >>= 4;
    total >>= 4;
  }
  if (total == 0) return 0;

  /* The three counters are sampled by one SocketBaseTagList call but the net task
   * runs independently, so a segment can land between two of the reads and make the
   * parts momentarily exceed the total. Clamp rather than print >100%. */
  if (part > total) return 100;

  return (ULONG)(((part * 200UL) / total + 1) / 2);
}

/* ASCII case-insensitive compare -- same local helper the other tools use rather
 * than opening utility.library for one call. */
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

static int take(char *ifname, struct sample *s)
{
  struct TagItem tg[21];

  /* Zero everything: an untouched buffer reads back as stack garbage, which is
   * exactly the class of bug that made ShowNetStatus print impossible figures. */
  { int i; for (i = 0; i < (int)sizeof(*s); i++) ((char *)s)[i] = 0; }

  tg[0].ti_Tag  = IFQ_BPS;              tg[0].ti_Data  = (ULONG)&s->bps;
  tg[1].ti_Tag  = IFQ_MTU;              tg[1].ti_Data  = (ULONG)&s->mtu;
  tg[2].ti_Tag  = IFQ_State;            tg[2].ti_Data  = (ULONG)&s->state;
  tg[3].ti_Tag  = IFQ_PacketsReceived;  tg[3].ti_Data  = (ULONG)&s->rx;
  tg[4].ti_Tag  = IFQ_PacketsSent;      tg[4].ti_Data  = (ULONG)&s->tx;
  tg[5].ti_Tag  = IFQ_BadData;          tg[5].ti_Data  = (ULONG)&s->bad;
  tg[6].ti_Tag  = IFQ_InputDrops;       tg[6].ti_Data  = (ULONG)&s->idrops;
  tg[7].ti_Tag  = IFQ_OutputDrops;      tg[7].ti_Data  = (ULONG)&s->odrops;
  tg[8].ti_Tag  = IFQ_GetBytesIn;       tg[8].ti_Data  = (ULONG)&s->bin;
  tg[9].ti_Tag  = IFQ_GetBytesOut;      tg[9].ti_Data  = (ULONG)&s->bout;
  tg[10].ti_Tag = IFQ_GetSANA2CopyStats;tg[10].ti_Data = (ULONG)&s->cs;
  tg[11].ti_Tag = NGIFQ_Copy32In;       tg[11].ti_Data = (ULONG)&s->cp32in;
  tg[12].ti_Tag = NGIFQ_Copy32Out;      tg[12].ti_Data = (ULONG)&s->cp32out;
  tg[13].ti_Tag = NGIFQ_DmaIn;          tg[13].ti_Data = (ULONG)&s->dmain;
  tg[14].ti_Tag = NGIFQ_DmaOut;         tg[14].ti_Data = (ULONG)&s->dmaout;
  tg[15].ti_Tag = NGIFQ_DmaAsk;         tg[15].ti_Data = (ULONG)&s->dmaask;
  tg[16].ti_Tag = NGIFQ_DmaAskOut;      tg[16].ti_Data = (ULONG)&s->dmaaskout;
  tg[17].ti_Tag = NGIFQ_DmaNoBuf;       tg[17].ti_Data = (ULONG)&s->dmanobuf;
  tg[18].ti_Tag = NGIFQ_DmaNoLen;       tg[18].ti_Data = (ULONG)&s->dmanolen;
  tg[19].ti_Tag = NGIFQ_DmaNoAlign;     tg[19].ti_Data = (ULONG)&s->dmanoalign;
  tg[20].ti_Tag = TAG_END;              tg[20].ti_Data = 0;

  if (ng_queryif((void *)ifname, tg) != 0)
    return -1;

  take_tcp(s);
  return 0;
}

static void banner(char *ifname, struct sample *s)
{
  Printf((STRPTR)"rxprofile: %s  state=%s  MTU=%ld  BPS=%ld\n",
         (LONG)ifname, (LONG)((s->state == NG_SM_Up) ? "Up" : "Down"),
         s->mtu, s->bps);
  if (s->bps == 0)
    Printf((STRPTR)"  NOTE: driver reports BPS=0 -- the receive ring was sized from the\n"
                   "        RAM-tier default, NOT the link speed. See the header comment.\n");
  Printf((STRPTR)"\n");
}

/* The header-prediction breakdown. Every inbound TCP segment lands in exactly one
 * of three buckets, so the three lines account for 100% of TCP receive work. */
static void fastpath(struct sample *s)
{
  ULONG fast = s->predack + s->preddat + s->predwin;
  ULONG slow, acct;
  int i;

  Printf((STRPTR)"\n  TCP fast path (whole stack, not just this interface):\n");

  if (s->rcvtotal == 0) {
    Printf((STRPTR)"    no TCP segments received yet -- or this library predates the\n"
                   "    counters, in which case they read 0 forever.\n");
    return;
  }

  /* Guard the subtraction: the counters are read while the net task keeps running,
   * so the parts can momentarily exceed the total and wrap an unsigned difference
   * into a four-billion-segment "slow path". */
  slow = (s->rcvtotal > fast) ? s->rcvtotal - fast : 0;

  Printf((STRPTR)"    TCP segments in = %ld\n", (LONG)s->rcvtotal);
  Printf((STRPTR)"    fast pure-ACK   = %-10ld (%ld%%)\n",
         (LONG)s->predack, (LONG)pct(s->predack, s->rcvtotal));
  Printf((STRPTR)"    fast data       = %-10ld (%ld%%)\n",
         (LONG)s->preddat, (LONG)pct(s->preddat, s->rcvtotal));
  Printf((STRPTR)"    fast win update = %-10ld (%ld%%)\n",
         (LONG)s->predwin, (LONG)pct(s->predwin, s->rcvtotal));
  Printf((STRPTR)"    slow path       = %-10ld (%ld%%)  <-- the expensive one\n",
         (LONG)slow, (LONG)pct(slow, s->rcvtotal));
  Printf((STRPTR)"    PCB cache miss  = %-10ld (%ld%%)\n",
         (LONG)s->pcbmiss, (LONG)pct(s->pcbmiss, s->rcvtotal));
  /* Non-zero means segments were REFUSED admission to the reassembly queue.
   * A handful is survivable; a climbing count during a stall is the cause. */
  if (s->reassfull)
    Printf((STRPTR)"  reassembly queue FULL, segments dropped = %ld  <-- transfer will stall\n",
           (LONG)s->reassfull);

  if (slow == 0)
    return;

  /* Only the non-zero buckets: thirteen mostly-zero lines buries the one that
   * matters. Percentages are of the SLOW PATH, not of all segments, because the
   * question this answers is "of the segments prediction rejected, why?". */
  Printf((STRPTR)"\n    why prediction was missed (%% of slow path):\n");
  for (i = 0; i < NMISS; i++)
    if (s->miss[i])
      Printf((STRPTR)"      %s %-10ld (%ld%%)\n",
             (LONG)missdef[i].name, (LONG)s->miss[i],
             (LONG)pct(s->miss[i], slow));

  /*
   * The buckets do NOT account for the whole slow path, and the difference is
   * meaningful rather than an error. tcps_rcvtotal is incremented early in
   * tcp_input, but several paths (bad checksum, no matching PCB, RST handling)
   * drop the segment before header prediction is ever consulted. Those segments
   * are in the slow-path figure yet were never offered to the fast path, so
   * report them as their own bucket instead of leaving the reader to wonder why
   * the column does not add up.
   */
  for (i = 0, acct = 0; i < NMISS; i++)
    acct += s->miss[i];
  if (slow > acct)
    Printf((STRPTR)"      %s %-10ld (%ld%%)\n",
           (LONG)"dropped pre-check  ", (LONG)(slow - acct),
           (LONG)pct(slow - acct, slow));
  else if (acct > slow)
    /* Only reachable through sampling skew: the counters are read one tag at a
     * time while the net task keeps running. Say so rather than printing a
     * negative-looking bucket. */
    Printf((STRPTR)"      (counters moved during sampling; totals approximate)\n");
}

static void absolute(struct sample *s)
{
  Printf((STRPTR)"  PacketsIn  = %-10ld  BytesIn  = %ld\n", (LONG)s->rx, (LONG)s->bin.lo);
  Printf((STRPTR)"  PacketsOut = %-10ld  BytesOut = %ld\n", (LONG)s->tx, (LONG)s->bout.lo);
  Printf((STRPTR)"  InputDrops = %-10ld  <-- queue-full drops on receive\n", (LONG)s->idrops);
  Printf((STRPTR)"  BadData    = %-10ld  (device-reported input errors)\n", (LONG)s->bad);
  Printf((STRPTR)"  OutputDrops= %ld\n", (LONG)s->odrops);
  Printf((STRPTR)"  CopyIn calls = %-8ld CopyOut calls = %ld\n",
         (LONG)s->cs.s2cs_ByteIn, (LONG)s->cs.s2cs_ByteOut);
  /* Which buffer-management methods the driver actually chose. The mandatory
   * pair always works; a non-zero 32-bit count means the driver understands
   * SANA-II R4, which is what decides whether the DMA variants are worth
   * building. Zero here is a real answer, not a missing feature. */
  /* THE ONE THAT ANSWERS "is DMA actually being used?". These count frames the
   * driver moved itself, so non-zero is proof, not inference. Zero with the hooks
   * offered means the driver looked and declined -- which is the common case. */
  /* ASKED and ACCEPTED are reported separately because a zero accept count on its
   * own is ambiguous -- "never asked" and "asks constantly, refused every time"
   * look identical, and they call for opposite responses. */
  Printf((STRPTR)"  DMA asked in/out = %ld/%ld   accepted in/out = %ld/%ld\n",
         (LONG)s->dmaask, (LONG)s->dmaaskout, (LONG)s->dmain, (LONG)s->dmaout);
  if (s->dmain || s->dmaout)
    Printf((STRPTR)"    <-- DMA IS IN USE\n");
  else if (s->dmaask || s->dmaaskout)
    Printf((STRPTR)"    <-- driver ASKS but we refuse every time; declined:"
                   " no-buffer %ld  bad-length %ld  misaligned %ld\n",
           (LONG)s->dmanobuf, (LONG)s->dmanolen, (LONG)s->dmanoalign);
  else
    Printf((STRPTR)"    (driver never asks -- it copies; DMA costs nothing here)\n");
  Printf((STRPTR)"  R4 copy32 in = %-8ld R4 copy32 out = %ld%s\n",
         (LONG)s->cp32in, (LONG)s->cp32out,
         (LONG)((s->cp32in || s->cp32out) ? "  <-- driver is R4-aware"
                                          : "  (driver uses the mandatory pair only)"));
  /* NOT printed: s2cs_DMAIn/DMAOut. The library writes those as literal 0
   * (api/amiga_roadshow_compat.c), because we advertise only S2_CopyToBuff and
   * S2_CopyFromBuff to the driver -- there is no DMA hook for it to call, so
   * there is nothing to count. Displaying them would present a hardcoded
   * constant as a measurement, and a reader would reasonably conclude "the
   * driver does not use DMA" when what it actually shows is "we never offered
   * it". If the SANA-II R3 buffer-management variants are ever wired up, count
   * them for real and show them then. */

  /* Socket wakeups. The fast path calls sorwakeup() for every in-sequence
   * segment, but that is only EXPENSIVE when someone is actually waiting: with
   * no waiter it is three flag tests, while "blocked waiter" means a real
   * wakeup() -- semaphore, queue walk, Signal. The second figure is the one
   * that decides whether coalescing them is worth anything. */
  Printf((STRPTR)"\n  Socket wakeups (whole stack):\n");
  Printf((STRPTR)"    sowakeup calls  = %-10ld (receive side %ld)\n",
         (LONG)s->wkcalls, (LONG)s->wkrcv);
  Printf((STRPTR)"    blocked waiter  = %-10ld (%ld%%)  <-- the expensive ones\n",
         (LONG)s->wkwait, (LONG)pct(s->wkwait, s->wkcalls));
  Printf((STRPTR)"    select()        = %-10ld\n", (LONG)s->wksel);
  Printf((STRPTR)"    async signal    = %ld\n", (LONG)s->wkasync);

  fastpath(s);
}

/*
 * d = b - a, for the counters only.
 *
 * This is what makes a WATCH run answer "what did THIS transfer do" rather than
 * "what has this machine done since it booted". The stack's counters are global
 * and free-running, and deliberately cannot be zeroed -- they are shared state,
 * so a diagnostic that reset them would blind every other observer and would be
 * racing the net task while it increments them. Differencing against a baseline
 * taken at startup gets the same answer with no side effects at all.
 *
 * Link state, MTU and BPS are carried across from the later sample: a
 * difference of those is meaningless.
 */
/* Clamp rather than wrap. The WATCH loop's reset guard checks only some of the
 * fields differenced here, and it is safe today only because every interface
 * counter is zeroed together by one bzero in iface_make() -- an invariant that
 * lives in net/if_sana.c, not here. A future counter that does not share it
 * would otherwise print an unsigned underflow as a four-billion-event total.
 * Same idiom fastpath() already uses on the slow-path subtraction. */
#define DSUB(f) (b->f > a->f ? b->f - a->f : 0)

static void delta(struct sample *d, struct sample *b, struct sample *a)
{
  int i;

  d->rx     = DSUB(rx);
  d->tx     = DSUB(tx);
  d->bad    = DSUB(bad);
  d->idrops = DSUB(idrops);
  d->odrops = DSUB(odrops);
  d->bin.hi  = 0; d->bin.lo  = DSUB(bin.lo);
  d->bout.hi = 0; d->bout.lo = DSUB(bout.lo);

  d->cs.s2cs_DMAIn   = DSUB(cs.s2cs_DMAIn);
  d->cs.s2cs_DMAOut  = DSUB(cs.s2cs_DMAOut);
  d->cs.s2cs_ByteIn  = DSUB(cs.s2cs_ByteIn);
  d->cs.s2cs_ByteOut = DSUB(cs.s2cs_ByteOut);
  d->cs.s2cs_WordOut = DSUB(cs.s2cs_WordOut);

  d->predack  = DSUB(predack);
  d->predwin  = DSUB(predwin);
  d->wkcalls  = DSUB(wkcalls); d->wkrcv   = DSUB(wkrcv);
  d->wkwait   = DSUB(wkwait);  d->wksel   = DSUB(wksel);
  d->wkasync  = DSUB(wkasync);
  d->preddat  = DSUB(preddat);
  d->rcvtotal = DSUB(rcvtotal);
  d->pcbmiss  = DSUB(pcbmiss);
  d->reassfull= DSUB(reassfull);
  d->cp32in   = DSUB(cp32in);
  d->cp32out  = DSUB(cp32out);
  d->dmain    = DSUB(dmain);
  d->dmaout   = DSUB(dmaout);
  d->dmaask   = DSUB(dmaask);
  d->dmaaskout= DSUB(dmaaskout);
  d->dmanobuf = DSUB(dmanobuf);
  d->dmanolen = DSUB(dmanolen);
  d->dmanoalign=DSUB(dmanoalign);
  for (i = 0; i < NMISS; i++)
    d->miss[i] = (b->miss[i] > a->miss[i]) ? b->miss[i] - a->miss[i] : 0;

  d->bps = b->bps; d->mtu = b->mtu; d->state = b->state;
}
#undef DSUB

int main(int argc, char **argv)
{
  struct sample a, b, first, d;
  int watch = 0, wrapped = 0;

  if (argc < 2) {
    Printf((STRPTR)"usage: rxprofile <interface> [WATCH]\n");
    return RETURN_FAIL;
  }
  /* Require the keyword: a typo'd second argument should not silently drop a
   * tester into an infinite sampling loop. */
  if (argc > 2) {
    if (!ci_eq(argv[2], "WATCH")) {
      Printf((STRPTR)"rxprofile: unknown argument '%s' (did you mean WATCH?)\n", (LONG)argv[2]);
      return RETURN_FAIL;
    }
    watch = 1;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    Printf((STRPTR)"rxprofile: cannot open bsdsocket.library\n");
    return RETURN_FAIL;
  }

  if (take(argv[1], &a) != 0) {
    Printf((STRPTR)"rxprofile: no such interface '%s'\n", (LONG)argv[1]);
    CloseLibrary(SocketBase);
    return RETURN_FAIL;
  }
  banner(argv[1], &a);

  if (!watch) {
    absolute(&a);
    CloseLibrary(SocketBase);
    return RETURN_OK;
  }

  /* Baseline for the end-of-run totals. Everything the stack counts is
   * free-running since boot, so without this the final report would be
   * dominated by whatever ran before the test rather than by the test. */
  first = a;

  Printf((STRPTR)"Sampling every ~2s -- run the transfer now. Ctrl-C to stop.\n");
  Printf((STRPTR)"%8s %8s %7s %8s %9s %8s %6s\n",
         (LONG)"pkts/in", (LONG)"KB/in", (LONG)"DROPS", (LONG)"baddata",
         (LONG)"copyin", (LONG)"pkts/out", (LONG)"tcpfp%");

  for (;;) {
    ULONG dpk, dkb, ddr, dbd, dci, dpo, dtcp, dfast;

    if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) break;
    Delay(100);					/* ~2s */
    if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) break;

    if (take(argv[1], &b) != 0) break;

    /* An interface down/up between samples resets the counters, so b < a for a
     * reason other than genuine ULONG wraparound. Print a marker rather than a
     * nonsense delta -- a diagnostic must never show a figure it cannot justify. */
    if (b.rx     < a.rx     || b.tx     < a.tx     ||
        b.bin.lo < a.bin.lo || b.bad    < a.bad    ||
        b.idrops < a.idrops || b.cs.s2cs_ByteIn < a.cs.s2cs_ByteIn ||
        b.rcvtotal < a.rcvtotal || b.predack < a.predack ||
        b.preddat < a.preddat || b.predwin < a.predwin) {
      Printf((STRPTR)"%8s %8s %7s %8s %9s %8s %6s  <-- COUNTERS RESET\n",
             (LONG)"-", (LONG)"-", (LONG)"-", (LONG)"-", (LONG)"-", (LONG)"-", (LONG)"-");
      /* The startup baseline is now meaningless -- the counters restarted
       * underneath it, so the end-of-run totals would be nonsense. Rebase and
       * say so at the end rather than quietly reporting a wrong figure. */
      first = b;
      wrapped = 1;
      a = b;
      continue;
    }

    dpk = b.rx - a.rx;
    dkb = (b.bin.lo - a.bin.lo) / 1024;
    ddr = b.idrops - a.idrops;
    dbd = b.bad - a.bad;
    dci = b.cs.s2cs_ByteIn - a.cs.s2cs_ByteIn;
    dpo = b.tx - a.tx;

    /* Fast-path hit rate over THIS interval, not since boot -- a since-boot figure
     * is dominated by whatever ran before the test and barely moves while you watch. */
    dtcp  = b.rcvtotal - a.rcvtotal;
    dfast = (b.predack - a.predack) + (b.preddat - a.preddat)
	  + (b.predwin - a.predwin);	/* all three, as fastpath() does */

    Printf((STRPTR)"%8ld %8ld %7ld %8ld %9ld %8ld %6ld%s\n",
           (LONG)dpk, (LONG)dkb, (LONG)ddr, (LONG)dbd, (LONG)dci, (LONG)dpo,
           (LONG)pct(dfast, dtcp),
           (LONG)(ddr ? "  <-- DROPPING" : ""));
    /* WATCH is meant to be read WHILE the transfer runs. Printf() buffers, so a
     * redirected run (`rxprofile x WATCH >log`) would otherwise emit rows in lumps
     * rather than as they happen -- which defeats the point of a live sampler. */
    Flush(Output());

    a = b;
  }

  /* THIS RUN first -- it is the number the tester actually wants, and putting
   * the since-boot figures first invites reading the wrong one. */
  Printf((STRPTR)"\nThis run only (since rxprofile started):\n");
  if (wrapped)
    Printf((STRPTR)"  NOTE: the counters restarted mid-run (interface down/up), so this\n"
                   "        covers only since that point, not the whole run.\n");
  delta(&d, &a, &first);
  absolute(&d);

  Printf((STRPTR)"\nSince boot:\n");
  absolute(&a);
  CloseLibrary(SocketBase);
  return RETURN_OK;
}
