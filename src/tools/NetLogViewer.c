/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * NetLogViewer -- watch the stack's log as it happens, with scrollback.
 *
 * The stack can already log to a console window (LOGCONSOLE=ON) and to a file
 * (LOGFILENAME=). What neither gives you is going BACK: the console shows what
 * just happened and the file needs reopening to see what has been added. This
 * keeps a scrollback ring, follows the file as it grows, and lets you filter it.
 *
 * HOW IT FOLLOWS THE FILE. No notification, just a size check on a timer: seek to
 * the end, compare with where we last read to, read the difference. Deliberately
 * dumb, because the log is written by the stack -- possibly from inside the
 * network process -- and anything cleverer would mean a lock or a handshake with
 * code that must never block. If the file SHRINKS it was replaced or cleared, so
 * the ring is emptied and reading restarts; otherwise a truncated file would show
 * the tail of the old one glued to the head of the new.
 *
 * The display is a BOOPSI class of its own (nlv_class.c) so the prop gadget can
 * drive it directly. See there for why.
 *
 * Build: two sources, and it needs amiga.lib for DoSuperMethodA/DoMethodA.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/imageclass.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include "nlv_class.h"

/* Typed exactly as the NDK's proto/ headers declare them -- a plain
 * struct Library * conflicts with those declarations. */
struct Library       *SocketBase = 0;	/* only opened for LIVE -- see resolve_log() */
struct IntuitionBase *IntuitionBase = 0;
struct GfxBase       *GfxBase = 0;
struct UtilityBase   *UtilityBase = 0;

/*
 * SocketBaseTagList, LVO -294. Offset cross-checked against the field-proven stub
 * in src/tools/GetNetStatus.c -- and against a second, independently written stub
 * in the extension-vector test program; SBTM_GETVAL is TAG_USER with the
 * code shifted left one, mirrored from src/tools/ng_lvo.h.
 */
static long ng_sbtaglist(void *tags)
{
  register void *_a0 __asm("a0") = tags;
  register struct Library *_a6 __asm("a6") = SocketBase;
  register long _d0 __asm("d0");
  __asm__ __volatile__("jsr a6@(-294)"
    :"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
#define SBTM_GETVAL(code)	(0x80000000UL | (((code) & 0x3FFF) << 1))
#define SBTC_LOG_FILE_NAME	52

#define PROG		"NetLogViewer"
#define DEF_LINES	500
#define MAXLINE		512
#define TICK_SECS	1

/* ---- the scrollback ring ------------------------------------------ */

/*
 * Fixed capacity, oldest overwritten. A log grows without limit and a viewer that
 * grew with it would be the thing that finally exhausted a 2 MB machine while the
 * user was trying to find out what went wrong on it.
 */
struct ring {
  char **line;
  LONG   size, count, head;
};

static struct ring     g_ring;
static struct nlv_view g_view;			/* what the display currently shows */
/*
 * TWO buffers, deliberately.
 *
 * g_filter is OURS: view_rebuild() and set_title() read it from this task.
 * g_gadbuf is the string gadget's, and is the one handed to strgclass as its
 * initial text -- because if the class edits the buffer it was given in place
 * (as the classic string gadget does), then every keystroke the user types is
 * Intuition's task writing into it, including multi-byte shifts on insert and
 * backspace. Reading that from our task while it happens is a torn read of a
 * string, and it is the same shape as the ring bug already fixed here: shared
 * mutable state, two tasks, no lock.
 *
 * So the gadget never touches g_filter. We copy across once, under Forbid(),
 * when the user actually confirms the field.
 */
static char            g_filter[64];
static char            g_gadbuf[64];
static int             g_frozen = 0;
static int             g_maxlevel = 8;		/* 8 = show everything */

/*
 * The names the stack writes, in severity order, from the `levels` table in
 * kern/amiga_log.c. A line looks like
 *     Wed Jul 18 12:00:01 2026 [warn ]: smoke0: ...
 * so filtering by level is a matter of finding that bracketed field. Kept in the
 * same order as syslog's priorities, which is what makes "this level and worse"
 * a simple comparison.
 */
static const char *g_levelname[8] = {
  "emerg", "alert", "crit", "err", "warn", "note", "info", "debug"
};

/* Severity of a log line, or -1 if it does not carry one. */
static int line_level(const char *s)
{
  int i, j;

  if (!s) return -1;
  for (i = 0; s[i]; i++) {
    if (s[i] != '[') continue;
    for (j = 0; j < 8; j++) {
      const char *n = g_levelname[j];
      int k;
      for (k = 0; n[k]; k++) {
        char a = s[i + 1 + k], b = n[k];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) break;
      }
      if (!n[k]) return j;
    }
    return -1;				/* a '[' but not a level we know */
  }
  return -1;
}

static int ring_init(struct ring *r, LONG size)
{
  LONG i;

  r->line = (char **)AllocVec(sizeof(char *) * size, MEMF_PUBLIC | MEMF_CLEAR);
  if (!r->line) return 0;
  for (i = 0; i < size; i++) r->line[i] = NULL;
  r->size = size; r->count = 0; r->head = 0;
  return 1;
}

static void ring_free(struct ring *r)
{
  LONG i;

  if (!r->line) return;
  for (i = 0; i < r->size; i++)
    if (r->line[i]) FreeVec(r->line[i]);
  FreeVec(r->line);
  r->line = NULL;
}

static void ring_clear(struct ring *r)
{
  LONG i;

  for (i = 0; i < r->size; i++)
    if (r->line[i]) { FreeVec(r->line[i]); r->line[i] = NULL; }
  r->count = 0; r->head = 0;
}

/* Oldest-first index i (0..count-1) -> slot. */
static char *ring_get(struct ring *r, LONG i)
{
  if (i < 0 || i >= r->count) return NULL;
  return r->line[(r->head + i) % r->size];
}

/* len < 0 means "measure it" -- the callers that pass a literal should not have to
 * count it, and an earlier version silently turned -1 into an empty line. */
static void ring_add(struct ring *r, const char *s, LONG len)
{
  LONG slot;
  char *copy;

  if (len < 0) { len = 0; while (s && s[len]) len++; }
  if (len > MAXLINE) len = MAXLINE;
  if ((copy = (char *)AllocVec(len + 1, MEMF_PUBLIC)) == NULL)
    return;					/* drop it; better than dying */
  { LONG i; for (i = 0; i < len; i++) copy[i] = s[i]; copy[len] = '\0'; }

  if (r->count < r->size) {
    slot = (r->head + r->count) % r->size;
    r->count++;
  } else {
    slot = r->head;				/* overwrite the oldest */
    r->head = (r->head + 1) % r->size;
    if (r->line[slot]) FreeVec(r->line[slot]);
  }
  r->line[slot] = copy;
}

/* ---- filtering ---------------------------------------------------- */

/* Case-insensitive EQUALITY. ci_find() below is a substring search and using it
 * to match a config keyword would make MYLOGGING match LOGGING. */
static int ci_eq(const char *a, const char *b)
{
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

static int ci_find(const char *hay, const char *needle)
{
  int i, j;

  if (!needle || !needle[0]) return 1;		/* no filter matches everything */
  if (!hay) return 0;
  for (i = 0; hay[i]; i++) {
    for (j = 0; needle[j]; j++) {
      char a = hay[i + j], b = needle[j];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) break;
    }
    if (!needle[j]) return 1;
  }
  return 0;
}

/*
 * Rebuild the visible list. The view is an array of pointers INTO the ring, not
 * copies, so this is cheap enough to redo whenever the filter or the contents
 * change -- and it means the display class never has to know a filter exists.
 */
static void view_rebuild(void)
{
  LONG i, n = 0;

  for (i = 0; i < g_ring.count; i++) {
    char *s = ring_get(&g_ring, i);
    int   lv;

    if (!s || !ci_find(s, g_filter))
      continue;
    /* A line with no level at all (the "--- log opened ---" banner, a
     * continuation) is always kept: hiding it would make the log look like it
     * had gaps, which is worse than showing one line too many. */
    lv = line_level(s);
    if (lv >= 0 && lv > g_maxlevel)
      continue;
    g_view.item[n++] = s;
  }
  g_view.count = n;
}

/* ---- finding the log ------------------------------------------------ */

/*
 * The fallback destinations, in the order kern/amiga_log.c tries them at bring-up
 * (_PATH_LOG_FALLBACKS in sys/syslog.h). Mirrored rather than included because
 * that header is the stack's, not a tool's; if the two ever disagree the stack
 * wins and this list is the bug.
 */
static const char *g_fallback[] = {
  "ram:AmiTCP.log", "t:AmiTCP.log", "AmiTCP:AmiTCP.log", "sys:AmiTCP.log"
};
#define NFALLBACK (int)(sizeof(g_fallback) / sizeof(g_fallback[0]))

static int file_exists(const char *path)
{
  BPTR l = Lock((STRPTR)path, ACCESS_READ);
  if (l) { UnLock(l); return 1; }
  return 0;
}

static void copystr(char *dst, int sz, const char *src)
{
  int i = 0;
  if (sz <= 0) return;
  while (src && src[i] && i < sz - 1) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

/*
 * Which file is the stack actually writing?
 *
 * Not a constant, which is what an earlier version assumed: the default is
 * ram:AmiTCP.log but LOGFILENAME= overrides it, and if that destination cannot be
 * opened at bring-up the stack falls back down a list. Opening the wrong file
 * shows an empty window that looks exactly like "nothing is being logged".
 *
 * Order: an explicit FILE always wins. LIVE asks the running stack, which is the
 * only authoritative answer because it reports the destination IN USE including
 * any fallback -- at the cost of opening bsdsocket.library, which starts the whole
 * stack if it is not already up, so it is opt-in rather than the default. Failing
 * that, LOGFILENAME= from the config, then each fallback, taking the first that
 * actually exists.
 *
 * Sets *logging_off if the config says LOGGING=OFF, so the caller can say so
 * rather than presenting an empty pane as though it were news.
 */
/*
 * Pull LOGFILENAME= and LOGGING= out of a config file, following WITH includes.
 *
 * WITH is a real directive (kern/amiga_config.c's read_sets -> parsefile, which
 * recurses), so a flat read of AmiTCP.config can miss the very setting we came
 * for and silently fall back to a file the stack is not writing. Nothing shipped
 * uses WITH today, which is exactly why it would have gone unnoticed.
 *
 * Depth-limited rather than cycle-detected: a config that includes itself is a
 * mistake, and four levels is far past any real layout.
 */
static void scan_config(const char *path, int depth, char *cfgname, int cfgsz,
                        int *logging_off)
{
  /*
   * Heap, not stack, and NOT static either.
   *
   * This function recurses through WITH includes to depth 5, and 514 bytes of
   * line buffer per frame is ~3K of a Shell's ~4K stack before anything else is
   * counted -- silent corruption with no MMU to catch it. `static` is the usual
   * answer on this platform but is wrong here precisely BECAUSE it recurses:
   * the inner call would overwrite the buffer its caller is still parsing.
   */
  char *line = (char *)AllocVec(MAXLINE + 2, MEMF_PUBLIC);
  BPTR fh;

  if (line == NULL)
    return;

  if (depth > 4) {
    FreeVec(line);
    return;
  }
  if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0) {
    FreeVec(line);
    return;
  }

  while (FGets(fh, (STRPTR)line, MAXLINE)) {
    char *p = line, *kw, *val;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#' || *p == ';' || *p == '\n' || *p == '\r') continue;
    kw = p;
    while (*p && *p != '=' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    if (*p) { *p = '\0'; p++; }
    while (*p == '=' || *p == ' ' || *p == '\t') p++;
    val = p;
    while (*p && *p != '#' && *p != ';' && *p != '\n' && *p != '\r') p++;
    while (p > val && (p[-1] == ' ' || p[-1] == '\t')) p--;
    *p = '\0';

    if (ci_eq(kw, "WITH")) {
      if (val[0])
        scan_config(val, depth + 1, cfgname, cfgsz, logging_off);
    } else if (ci_eq(kw, "LOGFILENAME") || ci_eq(kw, "LOGF")) {
      copystr(cfgname, cfgsz, val);	/* last one wins, as the stack does */
    } else if (ci_eq(kw, "LOGGING")) {
      *logging_off = (ci_eq(val, "OFF") || ci_eq(val, "NO"));
    }
  }
  Close(fh);
  FreeVec(line);
}

static void resolve_log(char *out, int outsz, int live, int *logging_off)
{
  char cfgname[128];
  int  i;

  cfgname[0] = '\0';
  *logging_off = 0;

  if (live) {
    if ((SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4L)) != NULL) {
      ULONG tags[4];
      STRPTR nm = NULL;

      tags[0] = SBTM_GETVAL(SBTC_LOG_FILE_NAME); tags[1] = 0;
      tags[2] = TAG_END;                         tags[3] = 0;
      if (ng_sbtaglist(tags) == 0)
        nm = (STRPTR)tags[1];
      if (nm && *nm) {
        copystr(out, outsz, (const char *)nm);
        CloseLibrary(SocketBase); SocketBase = 0;
        return;
      }
      CloseLibrary(SocketBase); SocketBase = 0;
    }
    /* Could not ask, or it had nothing to say: fall through to the files. */
  }

  scan_config("AmiTCP:db/AmiTCP.config", 0, cfgname, (int)sizeof(cfgname),
              logging_off);

  if (cfgname[0] && file_exists(cfgname)) { copystr(out, outsz, cfgname); return; }
  for (i = 0; i < NFALLBACK; i++)
    if (file_exists(g_fallback[i])) { copystr(out, outsz, g_fallback[i]); return; }

  /* Nothing exists yet. Name the configured destination if there is one, so the
   * title shows the file the user is waiting on rather than a default they never
   * chose -- it may simply not have been written to yet. */
  copystr(out, outsz, cfgname[0] ? cfgname : g_fallback[0]);
}

/* ---- following the file -------------------------------------------- */

struct tailer {
  BPTR  fh;
  LONG  pos;					/* how far we have read */
  int   cleared;				/* we emptied the ring: rebuild! */
  char  path[128];
};

static void tail_close(struct tailer *t)
{
  if (t->fh) { Close(t->fh); t->fh = 0; }
}

static int tail_open(struct tailer *t)
{
  if (t->fh) return 1;
  t->fh = Open((STRPTR)t->path, MODE_OLDFILE);
  return t->fh != 0;
}

/*
 * Read whatever has been appended since last time. Returns the number of lines
 * added. A file that has become SHORTER than our position was replaced or
 * cleared, so start over rather than splice two different files together.
 */
static LONG tail_poll(struct tailer *t, char *buf, LONG bufsz)
{
  LONG end, got, added = 0, start, i;

  if (!tail_open(t))
    return 0;

  end = Seek(t->fh, 0, OFFSET_END);		/* returns the OLD position */
  end = Seek(t->fh, 0, OFFSET_CURRENT);
  if (end < 0)
    return 0;

  if (end < t->pos) {
    /*
     * The file shrank (rotated or truncated), so the ring is emptied -- which
     * FREES every string g_view.item[] is pointing at. Say so, because the
     * caller only rebuilds the view when we report lines ADDED, and there are
     * two ordinary ways to clear the ring and then add nothing: the file is now
     * empty, or what is left has no complete line in it yet. Either way the
     * view would be left pointing into freed memory, and the renderer runs on
     * Intuition's task and would read it. Same class of bug as the one already
     * fixed on the key-handler paths -- just reached from the file changing
     * underneath us rather than from the user pressing anything.
     */
    ring_clear(&g_ring);
    t->cleared = 1;
    t->pos = 0;
  }
  if (end == t->pos) {
    Seek(t->fh, t->pos, OFFSET_BEGINNING);
    return 0;
  }

  Seek(t->fh, t->pos, OFFSET_BEGINNING);
  while ((got = Read(t->fh, buf, bufsz - 1)) > 0) {
    buf[got] = '\0';
    start = 0;
    for (i = 0; i < got; i++) {
      if (buf[i] == '\n' || buf[i] == '\r') {
        if (i > start) { ring_add(&g_ring, buf + start, i - start); added++; }
        else if (i == start) { /* blank line: keep it, the log uses them */
          ring_add(&g_ring, "", 0); added++;
        }
        /* CR LF is ONE terminator, not two. Our own log writes bare LF, but FILE
         * takes any path, and treating them separately gave a CRLF file a phantom
         * blank line after every real one. */
        if (buf[i] == '\r' && i + 1 < got && buf[i + 1] == '\n')
          i++;
        start = i + 1;
      }
    }
    /*
     * A partial last line is left for next time: rewinding to its start means the
     * next poll re-reads and completes it, instead of showing half a message now
     * and the other half as a separate line a second later.
     */
    /*
     * A whole bufferful with no terminator anywhere in it is NOT a partial last
     * line still being written -- it is one line longer than we can hold. The
     * two have to be told apart, because the handling below rewinds to `start`
     * and, with start still 0, that rewinds to exactly where we began: t->pos
     * never advances, the next poll reads the same window, finds no terminator
     * again, and the viewer stops following the log FOREVER. Silently -- no
     * error, no message, the pane just quietly stops updating and everything
     * appended after that line is never seen either. One overlong write (a
     * hexdump, a packet trace, anything without a newline) wedges it for good.
     *
     * So take the block as a truncated line and move on. A split line is a far
     * smaller lie than a viewer that has stopped working without saying so.
     */
    if (start == 0 && got == bufsz - 1) {
      ring_add(&g_ring, buf, got);
      added++;
      start = got;				/* advance past it */
    }

    t->pos += start;
    Seek(t->fh, t->pos, OFFSET_BEGINNING);
    if (start == 0)				/* a genuine partial line: wait */
      break;
  }
  return added;
}

/* ---- the application ----------------------------------------------- */

static char g_title[160];

/* The title carries what the graph cannot: which file, how many lines survived
 * the filter, and whether following is frozen. Set at startup as well as on every
 * redraw -- a window that opens with a bare program name looks like it has not
 * loaded anything. */
static void set_title(struct Window *win, const char *path)
{
  int at = 0, k, n = 0;
  char num[12];
  LONG v = g_view.count;

  for (k = 0; PROG " "[k]; k++) g_title[at++] = PROG " "[k];
  for (k = 0; path[k] && at < 90; k++) g_title[at++] = path[k];
  g_title[at++] = ' '; g_title[at++] = '(';
  do { num[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
  while (n > 0) g_title[at++] = num[--n];
  for (k = 0; " lines)"[k]; k++) g_title[at++] = " lines)"[k];
  if (g_maxlevel < 8) {
    for (k = 0; " level<="[k]; k++) g_title[at++] = " level<="[k];
    g_title[at++] = (char)('0' + g_maxlevel);
  }
  if (g_filter[0]) {
    for (k = 0; " find:"[k]; k++) g_title[at++] = " find:"[k];
    for (k = 0; g_filter[k] && at < 140; k++) g_title[at++] = g_filter[k];
  }
  if (g_frozen)
    for (k = 0; " [frozen]"[k]; k++) g_title[at++] = " [frozen]"[k];
  g_title[at] = '\0';
  SetWindowTitles(win, (UBYTE *)g_title, (UBYTE *)~0);
}

/*
 * A recessed frame around the search box. strgclass renders its text and cursor
 * and nothing else, so an empty one on a plain background is invisible -- the
 * user cannot see there is anywhere to type. Drawn from the screen's own pens so
 * it matches whatever palette is in use.
 */
static void draw_searchframe(struct Window *win, Object *strgad)
{
  struct DrawInfo *dri = GetScreenDrawInfo(win->WScreen);
  struct Gadget *g = (struct Gadget *)strgad;
  struct RastPort *rp = win->RPort;
  WORD x, y, w, h;

  if (!dri) return;
  x = g->LeftEdge - 1; y = g->TopEdge - 1;
  w = g->Width + 2;    h = g->Height + 2;

  SetDrMd(rp, JAM1);
  SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
  Move(rp, x, y + h - 1); Draw(rp, x, y); Draw(rp, x + w - 1, y);
  SetAPen(rp, dri->dri_Pens[SHINEPEN]);
  Draw(rp, x + w - 1, y + h - 1); Draw(rp, x, y + h - 1);
  FreeScreenDrawInfo(win->WScreen, dri);
}

/*
 * Place the three gadgets for the window's current size. One routine, called from
 * both the initial setup and IDCMP_NEWSIZE: laying out in two places is how a
 * resize ends up subtly different from a fresh open.
 *
 * The search box height comes from the screen's font, not a constant, so it stays
 * usable on a big-font screen.
 */
static void nlv_layout(struct Window *win, Object *display, Object *prop,
                       Object *strgad)
{
  WORD iw   = win->Width  - win->BorderLeft - win->BorderRight;
  WORD ih   = win->Height - win->BorderTop  - win->BorderBottom;
  WORD strh = win->WScreen->RastPort.TxHeight + 4;
  WORD texth;

  if (strh < 10) strh = 10;
  texth = ih - strh - 2;
  if (texth < 8) texth = 8;
  /* Widths need the same floor as the height. iw comes from the window minus its
   * borders, and every use below subtracts from it before an unsigned cast into a
   * tag -- a small negative WORD would become a four-billion-pixel request. */
  if (iw < 24) iw = 24;

  SetGadgetAttrs((struct Gadget *)display, win, NULL,
                 GA_Left,   (ULONG)win->BorderLeft,
                 GA_Top,    (ULONG)win->BorderTop,
                 GA_Width,  (ULONG)(iw - 16),
                 GA_Height, (ULONG)texth,
                 TAG_END);
  SetGadgetAttrs((struct Gadget *)prop, win, NULL,
                 GA_Left,   (ULONG)(win->BorderLeft + iw - 14),
                 GA_Top,    (ULONG)win->BorderTop,
                 GA_Width,  (ULONG)12,
                 GA_Height, (ULONG)texth,
                 TAG_END);
  SetGadgetAttrs((struct Gadget *)strgad, win, NULL,
                 GA_Left,   (ULONG)win->BorderLeft,
                 GA_Top,    (ULONG)(win->BorderTop + texth + 2),
                 GA_Width,  (ULONG)(iw - 2),
                 GA_Height, (ULONG)strh,
                 TAG_END);
}

int main(void)
{
  struct RDArgs *rda;
  LONG a[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  int  logging_off = 0;
  struct IClass *nlvclass = NULL;
  struct Screen *scr = NULL;
  struct Window *win = NULL;
  struct MsgPort *tport = NULL;
  struct timerequest *treq = NULL;
  Object *display = NULL, *prop = NULL, *strgad = NULL;
  struct tailer tail;
  char *buf = NULL;
  LONG  lines, i;
  int   rc = RETURN_OK, timer_open = 0, io_sent = 0, gadgets_added = 0;
  int   first_draw = 0;		/* one layout/scroll pass before the first Wait() */
  ULONG winsig, timsig;

  rda = ReadArgs((STRPTR)"FILE/K,LINES/K/N,FILTER/K,LEVEL/K/N,LIVE/S,"
                         "LEFT/N,TOP/N,WIDTH/N,HEIGHT/N", a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }
#ifdef NLV_DIAG
  { BPTR df = Open((STRPTR)"SYS:nlv.log", MODE_NEWFILE);
    if (df) SelectOutput(df);
    Printf((STRPTR)"DIAG args ok\n"); Flush(Output()); }
#define DTRACE(s) do { Printf((STRPTR)"DIAG " s "\n"); Flush(Output()); } while (0)
#else
#define DTRACE(s) do { } while (0)
#endif

  lines = a[1] ? *(LONG *)a[1] : DEF_LINES;
  if (lines < 20)    lines = 20;
  if (lines > 20000) lines = 20000;

  if (a[0])
    copystr(tail.path, (int)sizeof(tail.path), (const char *)a[0]);
  else
    resolve_log(tail.path, (int)sizeof(tail.path), a[4] != 0, &logging_off);
  tail.fh = 0; tail.pos = 0;
  if (a[3]) { g_maxlevel = (int)*(LONG *)a[3];
              if (g_maxlevel < 0) g_maxlevel = 0;
              if (g_maxlevel > 8) g_maxlevel = 8; }
  g_filter[0] = '\0';
  if (a[2]) { const char *f = (const char *)a[2];
              for (i = 0; f[i] && i < (LONG)sizeof(g_filter) - 1; i++) g_filter[i] = f[i];
              g_filter[i] = '\0'; }
  /* Seed the gadget's own buffer with the same text, so a FILTER given on the
   * command line still shows in the search box. The two are independent from
   * here on: the gadget edits g_gadbuf, we read g_filter. */
  for (i = 0; g_filter[i] && i < (LONG)sizeof(g_gadbuf) - 1; i++) g_gadbuf[i] = g_filter[i];
  g_gadbuf[i] = '\0';

  if ((IntuitionBase = (struct IntuitionBase *)
         OpenLibrary((STRPTR)"intuition.library", 37L)) == NULL ||
      (GfxBase = (struct GfxBase *)
         OpenLibrary((STRPTR)"graphics.library", 37L)) == NULL ||
      (UtilityBase = (struct UtilityBase *)
         OpenLibrary((STRPTR)"utility.library", 37L)) == NULL) {
    DTRACE("library open FAILED");
    Printf((STRPTR)PROG ": needs intuition, graphics and utility.library v37\n");
    rc = RETURN_FAIL; goto out;
  }

  if (!ring_init(&g_ring, lines) ||
      (g_view.item = (char **)AllocVec(sizeof(char *) * lines, MEMF_PUBLIC)) == NULL ||
      (buf = (char *)AllocVec(4096, MEMF_PUBLIC)) == NULL) {
    DTRACE("alloc FAILED");
    Printf((STRPTR)PROG ": out of memory\n");
    rc = RETURN_FAIL; goto out;
  }
  g_view.count = 0;

  DTRACE("libs+alloc ok");
  if ((nlvclass = nlv_makeclass()) == NULL) {
    DTRACE("MakeClass FAILED");
    Printf((STRPTR)PROG ": could not create the display class\n");
    rc = RETURN_FAIL; goto out;
  }

  InitSemaphore(&g_view.lock);

  /* Read whatever is already there, so the window opens with history in it. No
   * lock needed yet: the class does not exist, so nothing can be rendering. */
  tail_poll(&tail, buf, 4096);

  /*
   * An empty window is indistinguishable from a broken one. If there is nothing
   * to show, say why in the pane itself -- that is the one thing LOGCONSOLE gave
   * for free, because a console that never opened told you something too.
   */
  if (g_ring.count == 0) {
    if (logging_off)
      ring_add(&g_ring, PROG ": logging is switched OFF (LOGGING=OFF in "
                             "AmiTCP:db/AmiTCP.config) -- nothing is being recorded.", -1);
    else if (!file_exists(tail.path))
      ring_add(&g_ring, PROG ": no log file yet. Waiting for the stack to "
                             "write one.", -1);
    else
      ring_add(&g_ring, PROG ": the log file is empty.", -1);
  }
  view_rebuild();

  DTRACE("class ok");
  display = NewObject(nlvclass, NULL,
        GA_Left,   (ULONG)4,
        GA_Top,    (ULONG)4,
        GA_Width,  (ULONG)100,
        GA_Height, (ULONG)100,
        NLVA_View, (ULONG)&g_view,
        NLVA_Top,  (ULONG)0,
        TAG_END);
  if (display == NULL) {
    DTRACE("NewObject(display) FAILED");
    Printf((STRPTR)PROG ": could not create the display\n");
    rc = RETURN_FAIL; goto out;
  }

  /*
   * The scroller drives the display DIRECTLY: ICA_TARGET names the display object
   * and ICA_MAP renames PGA_Top to NLVA_Top on the way, so dragging it redraws the
   * text with this program still asleep in Wait(). No code of ours runs in
   * between, which is the whole reason the display is a BOOPSI class.
   */
  DTRACE("display ok");
  { static struct TagItem map[] = { { PGA_Top, NLVA_Top }, { TAG_END, 0 } };

    prop = NewObject(NULL, (UBYTE *)"propgclass",
          GA_ID,        (ULONG)1,
          GA_RelRight,  (ULONG)-14,
          GA_Top,       (ULONG)4,
          GA_Width,     (ULONG)12,
          GA_RelHeight, (ULONG)-8,
          GA_Previous,  (ULONG)display,
          PGA_Freedom,  (ULONG)FREEVERT,
          PGA_Total,    (ULONG)(g_view.count > 0 ? g_view.count : 1),
          PGA_Visible,  (ULONG)1,
          PGA_Top,      (ULONG)0,
          PGA_NewLook,  (ULONG)TRUE,
          ICA_TARGET,   (ULONG)display,
          ICA_MAP,      (ULONG)map,
          TAG_END);
  }
  if (prop == NULL) {
    DTRACE("NewObject(prop) FAILED");
    Printf((STRPTR)PROG ": could not create the scroller\n");
    rc = RETURN_FAIL; goto out;
  }
  /*
   * The search box. A string gadget rather than a requester: the whole point is
   * to narrow the view while watching it, and a modal requester would hide the
   * thing you are trying to narrow.
   */
  strgad = NewObject(NULL, (UBYTE *)"strgclass",
        GA_ID,          (ULONG)2,
        GA_Left,        (ULONG)4,
        GA_Top,         (ULONG)4,
        GA_Width,       (ULONG)100,
        GA_Height,      (ULONG)10,
        GA_Previous,    (ULONG)prop,
        GA_TabCycle,    (ULONG)TRUE,
        STRINGA_TextVal,(ULONG)g_gadbuf,	/* the gadget's, never g_filter */
        STRINGA_MaxChars,(ULONG)(sizeof(g_gadbuf) - 1),
        TAG_END);
  if (strgad == NULL) {
    DTRACE("NewObject(string) FAILED");
    Printf((STRPTR)PROG ": could not create the search box\n");
    rc = RETURN_FAIL; goto out;
  }

  DTRACE("prop ok");
  win = OpenWindowTags(NULL,
        WA_Left,          (ULONG)(a[5] ? *(LONG *)a[5] : 0),
        WA_Top,           (ULONG)(a[6] ? *(LONG *)a[6] : 0),
        WA_InnerWidth,    (ULONG)(a[7] ? *(LONG *)a[7] : 480),
        WA_InnerHeight,   (ULONG)(a[8] ? *(LONG *)a[8] : 170),
        WA_MinWidth,      (ULONG)120,
        WA_MinHeight,     (ULONG)60,
        WA_MaxWidth,      (ULONG)~0,
        WA_MaxHeight,     (ULONG)~0,
        WA_Title,         (ULONG)PROG,
        WA_CloseGadget,   (ULONG)TRUE,
        WA_DepthGadget,   (ULONG)TRUE,
        WA_DragBar,       (ULONG)TRUE,
        WA_SizeGadget,    (ULONG)TRUE,
        WA_Activate,      (ULONG)TRUE,
        WA_SimpleRefresh, (ULONG)TRUE,
        WA_IDCMP,         (ULONG)(IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE |
                                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                                  IDCMP_RAWKEY |
                                  IDCMP_GADGETUP),	/* no IDCMPUPDATE: nothing handles it */
        TAG_END);
  if (win == NULL) {
    DTRACE("OpenWindow FAILED");
    Printf((STRPTR)PROG ": could not open a window\n");
    rc = RETURN_FAIL; goto out;
  }
  scr = win->WScreen;

  DTRACE("window ok");
  if ((tport = CreateMsgPort()) == NULL ||
      (treq = (struct timerequest *)CreateIORequest(tport, sizeof(*treq))) == NULL ||
      OpenDevice((STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)treq, 0) != 0) {
    DTRACE("timer.device FAILED");
    Printf((STRPTR)PROG ": could not open timer.device\n");
    rc = RETURN_FAIL; goto out;
  }
  timer_open = 1;
  winsig = 1UL << win->UserPort->mp_SigBit;
  timsig = 1UL << tport->mp_SigBit;

  treq->tr_node.io_Command = TR_ADDREQUEST;
  treq->tr_time.tv_secs = TICK_SECS; treq->tr_time.tv_micro = 0;
  SendIO((struct IORequest *)treq);
  io_sent = 1;

#ifdef NLV_DIAG
  { extern LONG nlv_renders, nlv_lastw, nlv_lasth, nlv_lastcount, nlv_lastvis;
    struct Gadget *dg = (struct Gadget *)display;
    Printf((STRPTR)"DIAG ring=%ld view=%ld\n", g_ring.count, g_view.count);
    Printf((STRPTR)"DIAG win %ldx%ld borders L%ld T%ld R%ld B%ld\n",
           (LONG)win->Width, (LONG)win->Height, (LONG)win->BorderLeft,
           (LONG)win->BorderTop, (LONG)win->BorderRight, (LONG)win->BorderBottom);
    Printf((STRPTR)"DIAG gadget pre-layout %ld,%ld %ldx%ld flags=%lx\n",
           (LONG)dg->LeftEdge, (LONG)dg->TopEdge, (LONG)dg->Width,
           (LONG)dg->Height, (LONG)dg->Flags);
    (void)nlv_renders; (void)nlv_lastw; (void)nlv_lasth;
    (void)nlv_lastcount; (void)nlv_lastvis; }
#endif

  /*
   * Lay out FIRST, then attach. Passing WA_Gadgets to OpenWindowTags() renders
   * everything at whatever geometry the objects were created with -- which is
   * placeholder geometry, because the real sizes depend on the window we did not
   * have yet. Intuition drew the search box across the title bar and a second
   * copy of the scroller, and moving them afterwards does not erase where they
   * used to be. Attaching after the layout means they are only ever drawn once,
   * in the right place.
   */
  nlv_layout(win, display, prop, strgad);
  AddGList(win, (struct Gadget *)display, -1, -1, NULL);
  gadgets_added = 1;
  RefreshGList((struct Gadget *)display, win, NULL, -1);
  draw_searchframe(win, strgad);
  set_title(win, tail.path);

  /*
   * Show the END of the log, and size the scroller, BEFORE the first Wait().
   *
   * The display was created with NLVA_Top 0 and the prop with
   * PGA_Visible 1 / PGA_Total 1, and the code that corrects both lives inside
   * `if (redraw)` -- which cannot run until something has already happened.
   * Point this at an existing log on a quiet stack and it opened showing the
   * OLDEST lines under a scrollbar claiming one line of many, and stayed that
   * way until the next message arrived. That is the first thing anyone sees.
   *
   * `redraw` is per-iteration, so this seeds the first pass instead.
   */
  first_draw = 1;
#ifdef NLV_DIAG
  { extern LONG nlv_renders, nlv_lastw, nlv_lasth, nlv_lastcount, nlv_lastvis;
    struct Gadget *dg = (struct Gadget *)display;
    Printf((STRPTR)"DIAG gadget post-layout %ld,%ld %ldx%ld flags=%lx\n",
           (LONG)dg->LeftEdge, (LONG)dg->TopEdge, (LONG)dg->Width,
           (LONG)dg->Height, (LONG)dg->Flags);
    Printf((STRPTR)"DIAG renders=%ld last %ldx%ld vis=%ld count=%ld\n",
           nlv_renders, nlv_lastw, nlv_lasth, nlv_lastvis, nlv_lastcount);
    Flush(Output()); }
#endif

  for (;;) {
    ULONG got = Wait(winsig | timsig | SIGBREAKF_CTRL_C);
    int   quit = 0, redraw = first_draw;

    first_draw = 0;

    if (got & SIGBREAKF_CTRL_C)
      break;

    if (got & winsig) {
      struct IntuiMessage *msg;

      while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
        ULONG cls = msg->Class;
        UWORD code = msg->Code;

        ReplyMsg((struct Message *)msg);

        if (cls == IDCMP_CLOSEWINDOW) {
          quit = 1;
        } else if (cls == IDCMP_NEWSIZE) {
          /* Out of the window before the boxes move, back in afterwards:
           * Intuition caches gadget geometry and moving a gadget it still owns
           * leaves the old rectangle on screen. */
          RemoveGList(win, (struct Gadget *)display, -1);
          nlv_layout(win, display, prop, strgad);
          AddGList(win, (struct Gadget *)display, -1, -1, NULL);
          draw_searchframe(win, strgad);
          redraw = 1;
        } else if (cls == IDCMP_GADGETUP) {
          /* The search box was confirmed. */
          UBYTE *txt = NULL;
          GetAttr(STRINGA_TextVal, strgad, (ULONG *)&txt);
          /* Copy under Forbid(): txt points at the gadget's own storage, which
           * Intuition's task edits as the user types. The field has just been
           * confirmed so nobody should be typing into it right now, but "should
           * be" is not a guarantee against a preemptive scheduler, and this is
           * a handful of bytes. */
          Forbid();
          g_filter[0] = '\0';
          if (txt) {
            LONG k;
            for (k = 0; txt[k] && k < (LONG)sizeof(g_filter) - 1; k++)
              g_filter[k] = (char)txt[k];
            g_filter[k] = '\0';
          }
          Permit();
          ObtainSemaphore(&g_view.lock); view_rebuild(); ReleaseSemaphore(&g_view.lock);
          redraw = 1;
        } else if (cls == IDCMP_REFRESHWINDOW) {
          BeginRefresh(win);
          RefreshGList((struct Gadget *)display, win, NULL, -1);
          draw_searchframe(win, strgad);
          EndRefresh(win, TRUE);
        } else if (cls == IDCMP_VANILLAKEY) {
          if (code == 'f' || code == 'F') { g_frozen = !g_frozen; redraw = 1; }
          else if (code == 'c' || code == 'C') {
            /* The lock covers ring_clear TOO, not just the rebuild. The ring
             * owns the strings g_view.item[] points at, so freeing them while
             * Intuition's task is inside nlv_render() holding the shared lock
             * is a use-after-free -- and with no MMU it is a silent one. */
            ObtainSemaphore(&g_view.lock);
            ring_clear(&g_ring); view_rebuild();
            ReleaseSemaphore(&g_view.lock);
            redraw = 1;
          } else if (code == 27) {		/* ESC clears both filters */
            g_filter[0] = '\0'; g_maxlevel = 8;
            SetGadgetAttrs((struct Gadget *)strgad, win, NULL,
                           STRINGA_TextVal, (ULONG)"", TAG_END);
            ObtainSemaphore(&g_view.lock); view_rebuild(); ReleaseSemaphore(&g_view.lock);
            redraw = 1;
          } else if (code >= '0' && code <= '7') {
            /* 0-7 set the severity floor, matching LOGLEVEL's own numbering, and
             * 8 (or ESC) means everything. Same numbers the user already knows
             * from the config file rather than a second scale to learn. */
            g_maxlevel = code - '0';
            ObtainSemaphore(&g_view.lock); view_rebuild(); ReleaseSemaphore(&g_view.lock);
            redraw = 1;
          } else if (code == '8' || code == '9') {
            g_maxlevel = 8;
            ObtainSemaphore(&g_view.lock); view_rebuild(); ReleaseSemaphore(&g_view.lock);
            redraw = 1;
          } else if (code == 'q' || code == 'Q') {
            quit = 1;
          }
        } else if (cls == IDCMP_RAWKEY) {
          LONG top = 0, vis = 1;

          GetAttr(NLVA_Top, display, (ULONG *)&top);
          GetAttr(NLVA_Visible, display, (ULONG *)&vis);
          switch (code) {
          case 0x4C: top -= 1;    g_frozen = 1; break;	/* cursor up   */
          case 0x4D: top += 1;    g_frozen = 1; break;	/* cursor down */
          case 0x3E: top = 0;     g_frozen = 1; break;	/* Home (kp 7) */
          case 0x3F: top -= vis;  g_frozen = 1; break;	/* PgUp (kp 9) */
          case 0x1F: top += vis;  g_frozen = 1; break;	/* PgDn (kp 3) */
          /* End goes to the bottom AND resumes following -- that is what
           * "jump to the end" means for a log that is still being written. */
          case 0x1D: top = g_view.count; g_frozen = 0; break;	/* End (kp 1) */
          default: continue;
          }
          /*
           * Scrolling by hand freezes the view. Without this every scroll key
           * was inert: the redraw block below re-derives top as
           * (count - visible) whenever g_frozen is clear, and it runs in the
           * SAME pass, so it overwrote whatever the key had just set. The keys
           * appeared to do nothing at all unless the user already knew to press
           * F first -- and nothing on screen says F exists.
           *
           * Auto-freezing on a manual scroll is also what someone reading back
           * through the scrollback actually wants: they are looking at
           * something, and the log is still growing underneath them.
           */
          if (top < 0) top = 0;
          SetGadgetAttrs((struct Gadget *)display, win, NULL,
                         NLVA_Top, (ULONG)top, TAG_END);
          redraw = 1;
        }
      }
      if (quit) break;
    }

    if (got & timsig) {
      LONG added;

      GetMsg(tport);
      io_sent = 0;

      /*
       * Exclusive for the whole mutate-then-republish sequence. tail_poll() frees
       * and reallocates ring lines that g_view.item still points at, and the
       * renderer runs on INTUITION'S task, not ours -- see the note in
       * nlv_class.h. The DOS reads happen inside, which is not ideal, but the
       * alternative (drop the lock between reading and rebuilding) is the exact
       * window the bug lived in. Reads are from a file that has already been
       * written, so they do not block on anything slow.
       */
      ObtainSemaphore(&g_view.lock);
      tail.cleared = 0;
      added = tail_poll(&tail, buf, 4096);
      /* Rebuild if the ring was EMPTIED as well as if lines were added -- a
       * clear that adds nothing back still invalidates every pointer the view
       * holds. See the note in tail_poll(). */
      if (added > 0 || tail.cleared)
        view_rebuild();
      ReleaseSemaphore(&g_view.lock);
      if (added > 0 || tail.cleared)
        redraw = 1;

      treq->tr_node.io_Command = TR_ADDREQUEST;
      treq->tr_time.tv_secs = TICK_SECS; treq->tr_time.tv_micro = 0;
      SendIO((struct IORequest *)treq);
      io_sent = 1;
    }

    if (redraw) {
      LONG vis = 1, top = 0;

      GetAttr(NLVA_Visible, display, (ULONG *)&vis);
      if (vis < 1) vis = 1;

      /* Following the tail is the default and freezing is explicit: someone
       * reading back through the scrollback while the stack is busy does not
       * want the view yanked to the bottom every second. */
      if (!g_frozen) {
        top = g_view.count - vis;
        if (top < 0) top = 0;
      } else {
        GetAttr(NLVA_Top, display, (ULONG *)&top);
      }

      SetGadgetAttrs((struct Gadget *)display, win, NULL,
                     NLVA_Top, (ULONG)top, TAG_END);
      SetGadgetAttrs((struct Gadget *)prop, win, NULL,
                     PGA_Total,   (ULONG)(g_view.count > 0 ? g_view.count : 1),
                     PGA_Visible, (ULONG)vis,
                     PGA_Top,     (ULONG)top,
                     TAG_END);
      RefreshGList((struct Gadget *)display, win, NULL, -1);

      set_title(win, tail.path);
    }
  }

out:
  if (io_sent) { AbortIO((struct IORequest *)treq); WaitIO((struct IORequest *)treq); }
  if (timer_open) CloseDevice((struct IORequest *)treq);
  if (treq)  DeleteIORequest((struct IORequest *)treq);
  if (tport) DeleteMsgPort(tport);
  /* The gadgets must leave the window before the objects they are made of are
   * disposed, or Intuition keeps drawing something that has been freed. */
  /* Only if they were actually attached: the window can open and the timer then
   * fail, which jumps here having never called AddGList. Every other resource in
   * this ladder is guarded by a flag set at the point of acquisition; this one was
   * not. */
  if (win) {
    if (gadgets_added) RemoveGList(win, (struct Gadget *)display, -1);
    CloseWindow(win);
  }
  if (strgad)  DisposeObject(strgad);
  if (prop)    DisposeObject(prop);
  if (display) DisposeObject(display);
  if (nlvclass) nlv_freeclass(nlvclass);
  tail_close(&tail);
  if (buf) FreeVec(buf);
  if (g_view.item) FreeVec(g_view.item);
  ring_free(&g_ring);
  if (UtilityBase)   CloseLibrary((struct Library *)UtilityBase);
  if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
  FreeArgs(rda);
  (void)scr;
  return rc;
}
