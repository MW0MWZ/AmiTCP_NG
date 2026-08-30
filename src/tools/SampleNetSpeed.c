/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * SampleNetSpeed -- per-interface throughput, as a graph or as text.
 *
 * Our equivalent of Roadshow's SampleNetSpeed: an Intuition window sampling once a
 * second, received throughput drawn in the top half and sent in the bottom, both on
 * one shared scale so the two can be compared by eye. CLI gives the same sampling as
 * text, which is what you want over a serial console or in a script.
 *
 * WHERE THE NUMBERS COME FROM. IFQ_GetBytesIn / IFQ_GetBytesOut on each interface,
 * differenced between samples. Those are the same counters ShowNetStatus reports, so
 * the two agree by construction. There is no rate anywhere in the stack to read -- a
 * rate is a difference over time, and this is the thing that does the differencing.
 *
 * With no INTERFACE named, the graph shows the SUM across all interfaces and the text
 * mode shows one row each. Summing is the useful thing for a graph (several traces on
 * a shared scale are unreadable at 640 pixels) and listing is the useful thing for text.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/SampleNetSpeed.c -o SampleNetSpeed
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <utility/tagitem.h>

#include "ng_lvo.h"

struct Library *SocketBase = 0;
/* Typed to match the NDK's proto/ declarations exactly. Declaring these as a plain
 * struct Library * conflicts with proto/intuition.h and proto/graphics.h, which
 * already declare them -- and defining them here rather than relying on libnix's
 * auto-open keeps the open and close visibly ours, in one place. */
struct IntuitionBase *IntuitionBase = 0;
struct GfxBase       *GfxBase = 0;

#define PROG		"SampleNetSpeed"
#define MAXIF		16		/* interfaces tracked at once */
#define NAMELEN		32
#define TICKS_PER_SEC	50L		/* DateStamp ds_Tick resolution */
#define TICKS_PER_MIN	(60L * TICKS_PER_SEC)

struct ifsample {
  char  name[NAMELEN];
  ULONG in, out;			/* last raw counter reading */
  int   seen;				/* still in the interface list this round */
  int   primed;				/* we have a previous reading to difference */
};

struct rate_row {
  char  name[NAMELEN];
  ULONG in, out;			/* bytes/second */
};

static struct ifsample slots[MAXIF];
static int nslots = 0;

/* ------------------------------------------------------------------ */

static int streq(const char *a, const char *b)
{
  while (*a && *a == *b) { a++; b++; }
  return *a == '\0' && *b == '\0';
}

static void copyname(char *dst, const char *src)
{
  int i = 0;
  while (src[i] && i < NAMELEN - 1) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

/*
 * Elapsed hundredths of a second between two DateStamps. Hundredths rather than
 * seconds because the rate divides by this: a whole-second figure would quantise a
 * 1-second sample to either 0 or 1 and make every rate wrong by up to 100%.
 */
static LONG elapsed_cs(const struct DateStamp *a, const struct DateStamp *b)
{
  LONG dt;

  dt = (b->ds_Days   - a->ds_Days)   * (24L * 60L * TICKS_PER_MIN)
     + (b->ds_Minute - a->ds_Minute) * TICKS_PER_MIN
     + (b->ds_Tick   - a->ds_Tick);
  return (dt * 100L) / TICKS_PER_SEC;
}

/*
 * Append to buf at *at, never past `lim` (the buffer's size, terminator
 * included). No stdio and no RawDoFmt callback stub -- two integers and a unit do
 * not justify either -- but they DO justify a bound: an earlier version took no
 * limit and the title-bar path fed it the INTERFACE argument straight from
 * ReadArgs, so a long enough interface name walked off the end of a static
 * buffer. Everything else in this file truncates names to NAMELEN; this was the
 * one place that did not.
 */
static void appnum(char *buf, int *at, int lim, ULONG v)
{
  char tmp[12];
  int n = 0;

  do { tmp[n++] = (char)('0' + (v % 10UL)); v /= 10UL; } while (v);
  while (n > 0 && *at < lim - 1) buf[(*at)++] = tmp[--n];
}

static void appstr(char *buf, int *at, int lim, const char *s)
{
  while (*s && *at < lim - 1) buf[(*at)++] = *s++;
}

/*
 * bytes/second from a byte delta and an elapsed time in hundredths.
 *
 * Split into quotient and remainder rather than the obvious (delta * 100) / cs:
 * that multiply overflows 32 bits for a delta over ~43 MB, which a 10-second sample
 * on a fast link reaches. Everything here stays inside 32 bits.
 */
static ULONG rate_of(ULONG delta, LONG cs)
{
  ULONG c = (ULONG)cs;

  if (c == 0)
    return 0;
  return (delta / c) * 100UL + ((delta % c) * 100UL) / c;
}

/* Format bytes/second into buf, auto-scaled, one decimal. Integer maths only. */
#define RATEBUF 24			/* every fmt_rate() caller uses this size */

static void fmt_rate(ULONG bps, char *buf)
{
  int at = 0;

  if (bps >= 1048576UL) {
    appnum(buf, &at, RATEBUF, bps / 1048576UL);
    if (at < RATEBUF - 1) buf[at++] = '.';
    appnum(buf, &at, RATEBUF, ((bps % 1048576UL) * 10UL) / 1048576UL);
    appstr(buf, &at, RATEBUF, " MB/s");
  } else if (bps >= 1024UL) {
    appnum(buf, &at, RATEBUF, bps / 1024UL);
    if (at < RATEBUF - 1) buf[at++] = '.';
    appnum(buf, &at, RATEBUF, ((bps % 1024UL) * 10UL) / 1024UL);
    appstr(buf, &at, RATEBUF, " KB/s");
  } else {
    appnum(buf, &at, RATEBUF, bps);
    appstr(buf, &at, RATEBUF, " B/s");
  }
  buf[at] = '\0';
}

/* ------------------------------------------------------------------ */
/* The sampling engine, shared by both displays.                       */

/* Read one interface's byte counters. Returns 0 on success. */
static int read_counters(const char *name, ULONG *in, ULONG *out)
{
  /* SBQUAD_T: {sbq_High, sbq_Low}. Must be a two-ULONG pair -- passing a single
   * ULONG leaves the library writing the low word past the variable. */
  ULONG bin[2], bout[2];
  struct TagItem tg[3];

  bin[0] = bin[1] = bout[0] = bout[1] = 0;
  tg[0].ti_Tag = IFQ_GetBytesIn;  tg[0].ti_Data = (ULONG)bin;
  tg[1].ti_Tag = IFQ_GetBytesOut; tg[1].ti_Data = (ULONG)bout;
  tg[2].ti_Tag = TAG_END;         tg[2].ti_Data = 0;

  if (ng_queryif((void *)name, tg) != 0)
    return -1;
  *in  = bin[1];
  *out = bout[1];
  return 0;
}

static struct ifsample *slot_for(const char *name)
{
  int i;

  for (i = 0; i < nslots; i++)
    if (streq(slots[i].name, name))
      return &slots[i];
  if (nslots >= MAXIF)
    return NULL;
  copyname(slots[nslots].name, name);
  slots[nslots].in = slots[nslots].out = 0;
  slots[nslots].primed = 0;
  return &slots[nslots++];
}

/*
 * Establish a baseline. A rate needs two readings, so without this the first pass is
 * spent getting one and COUNT 3 reports twice. Doing it up front also lets an unknown
 * interface name be reported once, before sampling starts, instead of once per sample
 * for ever.
 *
 * Returns interfaces matched, or -1 if the list could not be read.
 */
static int prime_baseline(const char *want, int complain, struct DateStamp *when)
{
  struct List *iflist;
  struct Node *nd;
  int matched = 0;

  if ((iflist = ng_obtainiflist()) == NULL)
    return -1;

  for (nd = (struct Node *)iflist->lh_Head; nd->ln_Succ; nd = nd->ln_Succ) {
    struct ifsample *sl;
    ULONG in, out;

    if (want && !streq((const char *)nd->ln_Name, want)) continue;
    if (read_counters(nd->ln_Name, &in, &out) != 0)      continue;
    if ((sl = slot_for(nd->ln_Name)) == NULL)            continue;
    sl->in = in; sl->out = out; sl->primed = 1;
    matched++;
  }

  if (want && matched == 0 && complain) {
    /* Name the interfaces that DO exist. The name in DEVS:NetInterfaces is not
     * necessarily the interface name -- a config called "smoke" produces "smoke0" --
     * and "no such interface" without the list leaves the user guessing which. */
    Printf((STRPTR)PROG ": no interface named '%s'. Available:", (LONG)want);
    for (nd = (struct Node *)iflist->lh_Head; nd->ln_Succ; nd = nd->ln_Succ)
      Printf((STRPTR)" %s", (LONG)nd->ln_Name);
    Printf((STRPTR)"\n");
  }

  /*
   * Stamp the clock HERE, with the readings, and let the sampling loops start from
   * it. Priming in main() and stamping later in the loop meant everything that
   * happened in between -- opening a window, opening timer.device, allocating the
   * history -- was counted in the byte delta but not in the elapsed time, so the
   * FIRST sample came out overstated. On screen that was a single full-height spike
   * at the oldest column, which is how it was noticed: no log would have shown it.
   */
  if (when)
    DateStamp(when);

  ng_releaseiflist(iflist);
  return matched;
}

/*
 * One round: fill rows[] with each interface's rate since the last call, return how
 * many. cs is the elapsed hundredths of a second.
 */
static int collect_rates(const char *want, LONG cs, struct rate_row *rows, int maxrows)
{
  struct List *iflist;
  struct Node *nd;
  int n = 0, i;

  if ((iflist = ng_obtainiflist()) == NULL)
    return -1;

  for (i = 0; i < nslots; i++) slots[i].seen = 0;

  for (nd = (struct Node *)iflist->lh_Head; nd->ln_Succ; nd = nd->ln_Succ) {
    struct ifsample *sl;
    ULONG in, out, din, dout;

    if (want && !streq((const char *)nd->ln_Name, want)) continue;
    if (read_counters(nd->ln_Name, &in, &out) != 0)      continue;
    if ((sl = slot_for(nd->ln_Name)) == NULL)            continue;
    sl->seen = 1;

    if (!sl->primed) {			/* appeared since the last round */
      sl->in = in; sl->out = out; sl->primed = 1;
      continue;
    }

    /* Unsigned subtraction, which is also what makes a 32-bit counter wrap
     * harmlessly: one wrap still yields the correct difference. */
    din  = in  - sl->in;
    dout = out - sl->out;
    sl->in = in; sl->out = out;

    if (n < maxrows) {
      copyname(rows[n].name, sl->name);
      rows[n].in  = rate_of(din,  cs);
      rows[n].out = rate_of(dout, cs);
      n++;
    }
  }
  ng_releaseiflist(iflist);

  /* An interface that went away (NetShutdown, RemoveNetInterface) must not keep a
   * stale reading: if it comes back its counters restart from zero, and differencing
   * against the old ones would report one enormous bogus rate. */
  for (i = 0; i < nslots; i++)
    if (!slots[i].seen)
      slots[i].primed = 0;

  return n;
}

/* ------------------------------------------------------------------ */
/* Text display                                                        */

static int cli_loop(const char *want, LONG interval, LONG count,
                    const struct DateStamp *start)
{
  struct DateStamp prev, now;
  struct rate_row rows[MAXIF];
  LONG n;
  int rc = RETURN_OK, header = 0;

  prev = *start;			/* stamped when the baseline was taken */

  for (n = 0; count == 0 || n < count; n++) {
    LONG cs;
    int nr, i;

    if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) {
      Printf((STRPTR)"***Break\n");
      rc = RETURN_WARN;
      break;
    }

    /*
     * Sleep in one-second pieces rather than one Delay(interval) call.
     * Delay() waits on the timer's own signal only -- it does NOT wake on
     * SIGBREAKF_CTRL_C -- so a single long Delay() meant Ctrl-C was not noticed
     * until the whole interval had elapsed, and INTERVAL goes up to an hour.
     * The user would have concluded the tool had hung. (gui_loop() below never
     * had this problem: it Wait()s on the break signal alongside the timer.)
     */
    {
      LONG left = interval;
      while (left-- > 0) {
        Delay(TICKS_PER_SEC);
        if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C)
          break;
      }
    }
    if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) {
      Printf((STRPTR)"***Break\n");
      rc = RETURN_WARN;
      break;
    }

    DateStamp(&now);
    cs = elapsed_cs(&prev, &now);
    if (cs <= 0) cs = interval * 100L;	/* clock moved backwards: use the nominal */
    prev = now;

    if ((nr = collect_rates(want, cs, rows, MAXIF)) < 0) {
      Printf((STRPTR)PROG ": could not read the interface list\n");
      rc = RETURN_ERROR;
      break;
    }
    for (i = 0; i < nr; i++) {
      char rbuf[24], tbuf[24];

      if (!header) {
        Printf((STRPTR)"%-16s %14s %14s\n",
               (LONG)"Interface", (LONG)"Received", (LONG)"Sent");
        header = 1;
      }
      fmt_rate(rows[i].in,  rbuf);
      fmt_rate(rows[i].out, tbuf);
      Printf((STRPTR)"%-16s %14s %14s\n", (LONG)rows[i].name, (LONG)rbuf, (LONG)tbuf);
    }
    /* An interface that disappears mid-run simply stops producing rows; that is not
     * an error, and saying so every second would bury the rows that still matter. */
  }
  return rc;
}

/* ------------------------------------------------------------------ */
/* Graphical display                                                   */

struct histpt { ULONG in, out; };

/* Fixed pen numbers: on a standard Workbench palette 1 is the detail/text pen, 2 is
 * white and 3 is blue. Looking them up through DrawInfo would still need exactly
 * these as the fallback on a 4-colour screen, which is the case that matters. */
#define PEN_BG		0
#define PEN_AXIS	1
#define PEN_RX		3
#define PEN_TX		2

/*
 * Scale a rate to a bar height without overflowing.
 *
 * rate * half must fit in 32 bits. An earlier version divided rate and peak by a
 * fixed 16 and claimed that made it safe -- it did not, because `half` was left
 * alone: on a tall window (a big RTG screen) the product still overflowed at a
 * few tens of MB/s, and the wrapped value cast to a signed WORD could come out
 * negative, which the minimum clamp then turned into the SHORTEST bar for the
 * BIGGEST sample. Exactly the inversion a throughput graph must not draw.
 *
 * Shifting both terms down until the product provably fits works for any window
 * height and any rate, and keeps full precision until it actually has to give
 * some up.
 */
static WORD bar_height(ULONG rate, ULONG peak, WORD half)
{
  ULONG r = rate, p = peak, h = (ULONG)(half > 0 ? half : 1);
  WORD  bh;

  if (p == 0) p = 1;
  while (r > (0xFFFFFFFFUL / h)) {	/* r * h would wrap: lose a bit from each */
    r >>= 1;
    p >>= 1;
    if (p == 0) { p = 1; break; }
  }
  bh = (WORD)((r * h) / p);

  if (bh < 1)    bh = 1;		/* any traffic at all is visible */
  if (bh > half) bh = half;
  return bh;
}

static void draw_graph(struct Window *win, struct histpt *hist, int nhist, int count)
{
  struct RastPort *rp = win->RPort;
  WORD x0 = win->BorderLeft;
  WORD y0 = win->BorderTop;
  WORD w  = win->Width  - win->BorderLeft - win->BorderRight;
  WORD h  = win->Height - win->BorderTop  - win->BorderBottom;
  WORD mid, half;
  ULONG peak = 1;
  int i;

  if (w <= 2 || h <= 4)
    return;
  mid  = y0 + h / 2;
  half = h / 2 - 1;
  if (half < 1)
    return;

  SetAPen(rp, PEN_BG);
  RectFill(rp, x0, y0, x0 + w - 1, y0 + h - 1);

  /* One scale for both halves, taken from the tallest sample on screen. That shared
   * scale is the point of the split display -- it is what makes received and sent
   * comparable by eye. Recomputed each redraw, so the graph autoscales. */
  for (i = 0; i < count; i++) {
    if (hist[i].in  > peak) peak = hist[i].in;
    if (hist[i].out > peak) peak = hist[i].out;
  }

  SetAPen(rp, PEN_AXIS);
  Move(rp, x0, mid);
  Draw(rp, x0 + w - 1, mid);

  /* Newest at the right-hand edge, so it scrolls like a chart recorder. */
  for (i = 0; i < count && i < nhist; i++) {
    WORD x = (WORD)(x0 + w - 1 - i);
    int  k = count - 1 - i;
    WORD bh;

    if (x < x0)
      break;
    if (hist[k].in > 0) {
      bh = bar_height(hist[k].in, peak, half);
      SetAPen(rp, PEN_RX);
      Move(rp, x, mid - 1);
      Draw(rp, x, mid - bh);
    }
    if (hist[k].out > 0) {
      bh = bar_height(hist[k].out, peak, half);
      SetAPen(rp, PEN_TX);
      Move(rp, x, mid + 1);
      Draw(rp, x, mid + bh);
    }
  }
}

static int gui_loop(const char *want, LONG interval, LONG count, const char *pubscreen,
                    LONG left, LONG top, LONG width, LONG height,
                    const struct DateStamp *start)
{
  struct Screen *scr = NULL;
  struct Window *win = NULL;
  struct MsgPort *tport = NULL;
  struct timerequest *treq = NULL;
  struct histpt *hist = NULL;
  struct DateStamp prev, now;
  struct rate_row rows[MAXIF];
  static char title[160];
  int nhist = 0, nsamp = 0, taken = 0, rc = RETURN_OK, timer_open = 0, io_sent = 0;
  ULONG winsig, timsig;

  if ((IntuitionBase = (struct IntuitionBase *)
         OpenLibrary((STRPTR)"intuition.library", 37L)) == NULL ||
      (GfxBase = (struct GfxBase *)
         OpenLibrary((STRPTR)"graphics.library", 37L)) == NULL) {
    Printf((STRPTR)PROG ": needs intuition.library and graphics.library v37\n");
    rc = RETURN_FAIL;
    goto out;
  }

  if (pubscreen && (scr = LockPubScreen((STRPTR)pubscreen)) == NULL) {
    Printf((STRPTR)PROG ": no public screen named '%s'\n", (LONG)pubscreen);
    rc = RETURN_ERROR;
    goto out;
  }

  win = OpenWindowTags(NULL,
        WA_Left,          (ULONG)left,
        WA_Top,           (ULONG)top,
        WA_InnerWidth,    (ULONG)width,
        WA_InnerHeight,   (ULONG)height,
        WA_MinWidth,      (ULONG)80,
        WA_MinHeight,     (ULONG)40,
        WA_MaxWidth,      (ULONG)~0,
        WA_MaxHeight,     (ULONG)~0,
        WA_Title,         (ULONG)PROG,
        WA_CloseGadget,   (ULONG)TRUE,
        WA_DepthGadget,   (ULONG)TRUE,
        WA_DragBar,       (ULONG)TRUE,
        WA_SizeGadget,    (ULONG)TRUE,
        WA_Activate,      (ULONG)TRUE,
        WA_SimpleRefresh, (ULONG)TRUE,
        WA_IDCMP,         (ULONG)(IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW),
        scr ? WA_PubScreen : TAG_IGNORE, (ULONG)scr,
        TAG_END);
  if (win == NULL) {
    Printf((STRPTR)PROG ": could not open a window\n");
    rc = RETURN_FAIL;
    goto out;
  }

  /*
   * timer.device rather than Delay(). With Delay() the window would only notice the
   * close gadget or a resize once the current interval expired -- a whole second of
   * looking broken, every time. Waiting on the timer and the window port together
   * keeps it responsive between samples.
   */
  if ((tport = CreateMsgPort()) == NULL ||
      (treq = (struct timerequest *)CreateIORequest(tport, sizeof(*treq))) == NULL ||
      OpenDevice((STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)treq, 0) != 0) {
    Printf((STRPTR)PROG ": could not open timer.device\n");
    rc = RETURN_FAIL;
    goto out;
  }
  timer_open = 1;

  winsig = 1UL << win->UserPort->mp_SigBit;
  timsig = 1UL << tport->mp_SigBit;

  nhist = win->Width;			/* at most one sample per pixel column */
  if ((hist = (struct histpt *)AllocVec(sizeof(*hist) * nhist,
                                        MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
    Printf((STRPTR)PROG ": out of memory\n");
    rc = RETURN_FAIL;
    goto out;
  }

  prev = *start;			/* stamped when the baseline was taken */
  draw_graph(win, hist, nhist, nsamp);

  treq->tr_node.io_Command = TR_ADDREQUEST;
  treq->tr_time.tv_secs    = interval;
  treq->tr_time.tv_micro   = 0;
  SendIO((struct IORequest *)treq);
  io_sent = 1;

  for (;;) {
    ULONG got = Wait(winsig | timsig | SIGBREAKF_CTRL_C);

    if (got & SIGBREAKF_CTRL_C) {
      rc = RETURN_WARN;
      break;
    }

    if (got & winsig) {
      struct IntuiMessage *msg;
      int quit = 0;

      while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
        ULONG cls = msg->Class;

        ReplyMsg((struct Message *)msg);
        if (cls == IDCMP_CLOSEWINDOW) {
          quit = 1;
        } else if (cls == IDCMP_NEWSIZE) {
          /* The history is one sample per column, so a resize invalidates it. Start
           * again rather than stretch: a graph that silently resampled itself would
           * be lying about when things happened. */
          struct histpt *nh;
          int want_n = win->Width;

          if (want_n < 1) want_n = 1;
          if ((nh = (struct histpt *)AllocVec(sizeof(*nh) * want_n,
                                              MEMF_PUBLIC | MEMF_CLEAR)) != NULL) {
            FreeVec(hist);
            hist  = nh;
            nhist = want_n;
            nsamp = 0;
          }
          draw_graph(win, hist, nhist, nsamp);
        } else if (cls == IDCMP_REFRESHWINDOW) {
          BeginRefresh(win);
          draw_graph(win, hist, nhist, nsamp);
          EndRefresh(win, TRUE);
        }
      }
      if (quit)
        break;
    }

    if (got & timsig) {
      LONG cs;
      int nr, i, at = 0;
      ULONG sin = 0, sout = 0;
      char rbuf[24], tbuf[24];

      GetMsg(tport);
      io_sent = 0;

      DateStamp(&now);
      cs = elapsed_cs(&prev, &now);
      if (cs <= 0) cs = interval * 100L;
      prev = now;

      /*
       * A negative return means the interface list could not be read -- not
       * "no traffic". Testing for > 0 lumped the two together and drew a
       * confident flat zero line, so a stack that had gone away looked exactly
       * like an idle one. The CLI path reports this and exits; the GUI cannot
       * do that mid-run, so say it in the title and stop plotting.
       */
      nr = collect_rates(want, cs, rows, MAXIF);
      if (nr < 0) {
        SetWindowTitles(win, (UBYTE *)PROG ": cannot read the interface list "
                             "-- is the stack still running?", (UBYTE *)~0L);
        /*
         * RE-ARM BEFORE SKIPPING. The timer request is resent at the bottom of
         * this block, and io_sent was cleared when its reply was collected --
         * so a bare `continue` here left no request outstanding and the tick
         * never fired again. The window would report the problem once and then
         * sit frozen forever, never recovering even after the stack came back.
         * That is a worse failure than the flat-zero line this check was added
         * to fix: at least that one kept running.
         */
        treq->tr_node.io_Command = TR_ADDREQUEST;
        treq->tr_time.tv_secs = interval; treq->tr_time.tv_micro = 0;
        SendIO((struct IORequest *)treq);
        io_sent = 1;
        continue;
      }
      for (i = 0; i < nr; i++) { sin += rows[i].in; sout += rows[i].out; }

      if (nsamp < nhist) {
        hist[nsamp].in = sin; hist[nsamp].out = sout;
        nsamp++;
      } else {				/* full: drop the oldest */
        for (i = 1; i < nhist; i++) hist[i - 1] = hist[i];
        hist[nhist - 1].in = sin; hist[nhist - 1].out = sout;
      }
      draw_graph(win, hist, nhist, nsamp);

      /* The figures go in the title bar. The graph shows the shape, and a shape with
       * no scale on it is not a measurement. */
      fmt_rate(sin,  rbuf);
      fmt_rate(sout, tbuf);
      /* Short labels: the full program name plus both figures overflowed the title
       * bar of a default-sized window and the last unit was cut off ("out 1.3"),
       * which is exactly the digit you need. */
      appstr(title, &at, (int)sizeof(title), "NetSpeed ");
      appstr(title, &at, (int)sizeof(title), want ? want : "(all)");
      appstr(title, &at, (int)sizeof(title), "  in ");
      appstr(title, &at, (int)sizeof(title), rbuf);
      appstr(title, &at, (int)sizeof(title), "  out ");
      appstr(title, &at, (int)sizeof(title), tbuf);
      title[at] = '\0';
      SetWindowTitles(win, (UBYTE *)title, (UBYTE *)~0);

      /* COUNT applies to the window too, not just to text. Roadshow's tool runs until
       * you close it and COUNT is ours, but making it mean different things in the two
       * displays would be a trap -- and a GUI that can be told to take N samples and
       * exit is the only way this mode can be tested without a human to click it. */
      if (count != 0 && ++taken >= count)
        break;

      treq->tr_node.io_Command = TR_ADDREQUEST;
      treq->tr_time.tv_secs    = interval;
      treq->tr_time.tv_micro   = 0;
      SendIO((struct IORequest *)treq);
      io_sent = 1;
    }
  }

out:
  /* An IO request still in flight must be aborted AND waited for before its memory
   * goes away, or the timer completes into freed store. */
  if (io_sent) {
    AbortIO((struct IORequest *)treq);
    WaitIO((struct IORequest *)treq);
  }
  if (timer_open) CloseDevice((struct IORequest *)treq);
  if (treq)       DeleteIORequest((struct IORequest *)treq);
  if (tport)      DeleteMsgPort(tport);
  if (hist)       FreeVec(hist);
  if (win)        CloseWindow(win);
  if (scr)        UnlockPubScreen(NULL, scr);
  if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
  return rc;
}

/* ------------------------------------------------------------------ */

int main(void)
{
  struct RDArgs *rda;
  LONG a[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };	/* Roadshow's template, plus ours */
  const char *want, *pubscreen;
  LONG interval, count, left, top, width, height;
  int rc, matched;
  struct DateStamp start;

  rda = ReadArgs((STRPTR)"INTERFACE,LEFT/N,TOP/N,WIDTH/N,HEIGHT/N,SCREEN/K,"
                         "CLI/S,INTERVAL/N,COUNT/N", a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }

  want      = a[0] ? (const char *)a[0] : NULL;
  left      = a[1] ? *(LONG *)a[1] : 0;
  top       = a[2] ? *(LONG *)a[2] : 0;
  width     = a[3] ? *(LONG *)a[3] : 320;
  height    = a[4] ? *(LONG *)a[4] : 100;
  pubscreen = a[5] ? (const char *)a[5] : NULL;
  interval  = a[7] ? *(LONG *)a[7] : 1;
  count     = a[8] ? *(LONG *)a[8] : 0;		/* 0 = until closed / Ctrl-C */
  if (interval < 1) interval = 1;
  /* Ceiling as well as floor. elapsed_cs() converts ticks to hundredths with a
   * dt * 100 that overflows a signed 32-bit LONG past about five days, and
   * nothing else bounds the gap between two DateStamp() calls -- INTERVAL is the
   * gap. An hour is the same ceiling ping.c puts on its own intervals. */
  if (interval > 3600) interval = 3600;
  if (width  < 80)  width  = 80;
  if (height < 40)  height = 40;

  if ((SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4L)) == NULL) {
    Printf((STRPTR)PROG ": cannot open bsdsocket.library v4\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  matched = prime_baseline(want, 1, &start);
  if (matched < 0) {
    Printf((STRPTR)PROG ": could not read the interface list\n");
    CloseLibrary(SocketBase); FreeArgs(rda);
    return RETURN_ERROR;
  }
  if (want && matched == 0) {		/* prime_baseline() already listed what exists */
    CloseLibrary(SocketBase); FreeArgs(rda);
    return RETURN_WARN;
  }

  rc = a[6] ? cli_loop(want, interval, count, &start)
            : gui_loop(want, interval, count, pubscreen, left, top, width, height,
                       &start);

  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
