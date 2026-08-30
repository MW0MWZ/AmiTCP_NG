/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * nlv_class -- the NetLogViewer text-display gadget class (BOOPSI).
 *
 * Why a class at all, rather than just drawing in the main loop: a prop gadget can
 * be pointed straight at this object with ICA_TARGET/ICA_MAP, so dragging the
 * scroller sends OM_UPDATE here and the display redraws itself with the
 * application asleep in Wait(). That is what makes the scrolling feel attached to
 * the mouse rather than to the event loop.
 *
 * FONT SENSITIVITY comes from the DrawInfo the gadget is rendered with, never from
 * an assumption: row height is the font's tf_YSize and the first row is placed at
 * tf_Baseline, so an 8-point topaz screen and a 15-point one both come out right.
 * Lines are truncated with TextFit() rather than a character count, because a
 * proportional font makes "how many characters fit" a per-string question.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <graphics/gfx.h>
#include <graphics/text.h>
#include <exec/semaphores.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include "nlv_class.h"

/* UtilityBase comes from proto/utility.h; NetLogViewer.c defines and opens it. */

#ifdef NLV_DIAG
#include <proto/dos.h>
LONG nlv_renders = 0, nlv_lastw = -1, nlv_lasth = -1, nlv_lastcount = -1, nlv_lastvis = -1;
#endif

struct nlvdata {
  struct nlv_view *view;
  LONG		   top;			/* index of the first visible line */
  LONG		   visible;		/* rows that fitted at the last render */
};

/* amiga.lib */
extern ULONG DoSuperMethodA(struct IClass *cl, Object *o, Msg msg);

/* ------------------------------------------------------------------ */

/*
 * Clamp `top` so the view can never be scrolled past its own end, and so a
 * shrinking view (the filter just got narrower) cannot leave us pointing at
 * nothing. Returns 1 if the value changed.
 */
static int clamp_top(struct nlvdata *d)
{
  LONG max, old = d->top;

  /* count is a single aligned 32-bit read, which is instruction-atomic on 68k, so
   * this sees one value or the other and never a torn one -- no lock needed for a
   * bounds clamp that is re-done on every render anyway. */
  max = (d->view ? d->view->count : 0) - d->visible;
  if (max < 0) max = 0;
  if (d->top > max) d->top = max;
  if (d->top < 0)   d->top = 0;
  return d->top != old;
}

static void nlv_render(struct IClass *cl, Object *o, struct RastPort *rp,
		       struct DrawInfo *dri)
{
  struct nlvdata *d = (struct nlvdata *)INST_DATA(cl, o);
  struct Gadget  *g = (struct Gadget *)o;
  struct TextFont *font;
  WORD x, y, w, h, rowh, base, row;
  LONG i;

  if (rp == NULL || dri == NULL)
    return;

  x = g->LeftEdge; y = g->TopEdge; w = g->Width; h = g->Height;
  if (w < 4 || h < 4)
    return;

  font = dri->dri_Font;
  if (font == NULL)
    return;
  SetFont(rp, font);
  rowh = font->tf_YSize;
  base = font->tf_Baseline;
  if (rowh < 1)
    return;

  d->visible = h / rowh;
  clamp_top(d);
#ifdef NLV_DIAG
  nlv_renders++;
  nlv_lastw = w; nlv_lasth = h;
  nlv_lastvis = d->visible;
  nlv_lastcount = d->view ? d->view->count : -1;
#endif

  /* Background first: this is a full redraw, and leaving the previous contents
   * showing through short lines is how a scrolling text pane ends up looking
   * like it has garbage in it. */
  SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
  SetDrMd(rp, JAM1);
  RectFill(rp, x, y, x + w - 1, y + h - 1);

  if (d->view == NULL)
    return;

  /* Shared: several renders may overlap, but none may overlap the application
   * rewriting the list. Taken AFTER the background fill so a blocked render does
   * not leave the pane half-cleared. */
  ObtainSemaphoreShared(&d->view->lock);
  if (d->view->count == 0) {
    ReleaseSemaphore(&d->view->lock);
    return;
  }

  SetAPen(rp, dri->dri_Pens[TEXTPEN]);
  for (row = 0; row < d->visible; row++) {
    struct TextExtent te;		/* TextFit writes here -- NOT optional */
    char *s;
    LONG  len, fit;

    i = d->top + row;
    if (i >= d->view->count)
      break;
    s = d->view->item[i];
    if (s == NULL)
      continue;
    /* Bounded scan. An unbounded strlen over a pointer that turned out to be
     * stale would run through memory hunting for a stray zero byte, and there is
     * no MMU to stop it. The lock should make that impossible; the bound means a
     * mistake costs a garbled line instead of the machine. */
    for (len = 0; len < NLV_MAXLINE && s[len]; len++)
      ;
    if (len == 0)
      continue;

    /*
     * How much of this line actually fits, measured in this font.
     *
     * The TextExtent argument is REQUIRED -- graphics.library writes the measured
     * extent through it. Passing NULL (which reads like an "I don't care" the way
     * the constraining extent below is) writes to address 0, and with no MMU that
     * is not a crash you can see: it quietly corrupts low memory and the symptom
     * turns up somewhere else entirely. Here it took out the window that was
     * being opened at the time.
     */
    fit = (LONG)TextFit(rp, (STRPTR)s, (ULONG)len, &te, NULL, 1,
                        (UWORD)(w - 4), (UWORD)rowh);
    if (fit <= 0)
      continue;
    Move(rp, x + 2, y + row * rowh + base);
    Text(rp, (STRPTR)s, (ULONG)fit);
  }
  ReleaseSemaphore(&d->view->lock);
}

/* ------------------------------------------------------------------ */

static ULONG nlv_setattrs(struct IClass *cl, Object *o, struct opSet *msg)
{
  struct nlvdata *d = (struct nlvdata *)INST_DATA(cl, o);
  struct TagItem *tstate = msg->ops_AttrList, *ti;
  ULONG changed = 0;

  while ((ti = NextTagItem(&tstate)) != NULL) {
    switch (ti->ti_Tag) {
    case NLVA_View:
      d->view = (struct nlv_view *)ti->ti_Data;
      changed = 1;
      break;
    case NLVA_Top:
      if (d->top != (LONG)ti->ti_Data) {
        d->top = (LONG)ti->ti_Data;
        changed = 1;
      }
      break;
    default:
      break;
    }
  }
  if (clamp_top(d))
    changed = 1;
  return changed;
}

static ULONG ASM_dispatch(struct IClass *cl, Object *o, Msg msg)
{
  struct nlvdata *d;

  switch (msg->MethodID) {

  case OM_NEW: {
    Object *no = (Object *)DoSuperMethodA(cl, o, msg);
    if (no) {
      d = (struct nlvdata *)INST_DATA(cl, no);
      d->view = NULL;
      d->top = 0;
      d->visible = 1;		/* until the first render measures the font */
      nlv_setattrs(cl, no, (struct opSet *)msg);
    }
    return (ULONG)no;
  }

  case OM_SET:
  case OM_UPDATE: {
    struct opSet *ops = (struct opSet *)msg;
    ULONG changed;

    /* Keep the superclass's answer: gadgetclass may itself want a refresh for a
     * standard GA_* attribute we do not interpret, and swallowing that would make
     * such an attribute silently do nothing on this class. */
    changed = (ULONG)DoSuperMethodA(cl, o, msg);
    changed |= nlv_setattrs(cl, o, ops);

    /*
     * Redraw here rather than returning 1 and leaving it to the application.
     * OM_UPDATE arrives from the prop gadget while the application is asleep in
     * Wait(), so "tell the app to refresh" would mean no redraw until the next
     * event -- the scroller would move and the text would not.
     *
     * ObtainGIRPort() is the only legal way to get a RastPort here: the one in
     * GadgetInfo belongs to Intuition and must be borrowed, not used directly.
     */
    if (changed && ops->ops_GInfo) {
      struct RastPort *rp = ObtainGIRPort(ops->ops_GInfo);
      if (rp) {
        nlv_render(cl, o, rp, ops->ops_GInfo->gi_DrInfo);
        ReleaseGIRPort(rp);
      }
    }
    return 0;
  }

  case OM_GET: {
    struct opGet *opg = (struct opGet *)msg;

    d = (struct nlvdata *)INST_DATA(cl, o);
    switch (opg->opg_AttrID) {
    case NLVA_Top:     *opg->opg_Storage = (ULONG)d->top;     return 1;
    case NLVA_Visible: *opg->opg_Storage = (ULONG)d->visible; return 1;
    default: break;
    }
    return DoSuperMethodA(cl, o, msg);
  }

  case GM_RENDER: {
    struct gpRender *gpr = (struct gpRender *)msg;

    nlv_render(cl, o, gpr->gpr_RPort,
               gpr->gpr_GInfo ? gpr->gpr_GInfo->gi_DrInfo : NULL);
    return 0;
  }

  default:
    return DoSuperMethodA(cl, o, msg);
  }
}

/*
 * The dispatcher entry. BOOPSI calls it as a Hook: a0 = class, a2 = object,
 * a1 = message -- NOT the C argument order, so the registers are captured before
 * anything else can touch them. Same technique the library skeleton uses for its
 * RTF_AUTOINIT entry point.
 */
static ULONG nlv_dispatch(void)
{
  register struct IClass *_a0 __asm("a0");
  register Msg            _a1 __asm("a1");
  register Object        *_a2 __asm("a2");
  struct IClass *cl = _a0;
  Msg            msg = _a1;
  Object        *o  = _a2;

  return ASM_dispatch(cl, o, msg);
}

/* ------------------------------------------------------------------ */

struct IClass *nlv_makeclass(void)
{
  struct IClass *cl;

  if ((cl = MakeClass(NULL, (UBYTE *)"gadgetclass", NULL,
                      sizeof(struct nlvdata), 0)) == NULL)
    return NULL;
  cl->cl_Dispatcher.h_Entry = (ULONG (*)())nlv_dispatch;
  cl->cl_Dispatcher.h_SubEntry = NULL;
  cl->cl_Dispatcher.h_Data = NULL;
  return cl;
}

void nlv_freeclass(struct IClass *cl)
{
  /* Every object must be gone first; FreeClass() refuses otherwise and returning
   * without checking would leak the class rather than crash, which is the right
   * way round. */
  if (cl)
    FreeClass(cl);
}
