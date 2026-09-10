#ifndef NLV_CLASS_H
#define NLV_CLASS_H

/* Defined in NetLogViewer.c, used here. The AmigaOS proto headers used to declare
 * these for us; with __NOLIBBASE__ they declare none, so ours are the only ones.
 * Types must match NetLogViewer.c exactly. */
struct IntuitionBase;
struct GfxBase;
struct UtilityBase;

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;
extern struct UtilityBase   *UtilityBase;

/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * nlv_class -- the NetLogViewer text-display gadget class.
 *
 * A BOOPSI gadgetclass subclass that draws a slice of a list of lines, in the
 * screen's font, inside its own box. It knows nothing about log files, filtering
 * or where the lines came from: the application hands it a view (an array of
 * pointers plus a count) and a top line, and it draws. That separation is what
 * lets the filter be rebuilt without the display knowing, and lets the display be
 * driven straight from a prop gadget through ICA_TARGET without the application
 * being in the loop at all.
 */
#include <exec/types.h>
#include <exec/semaphores.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>

/*
 * What the class draws: whatever the application currently wants visible.
 *
 * THE LOCK IS NOT OPTIONAL, and an earlier comment here claiming otherwise ("the
 * class only ever reads it... no locking is needed on a cooperative single-window
 * program") was wrong in a way that mattered. `item` holds pointers INTO the
 * application's ring. The scroller is wired to this class with ICA_TARGET
 * precisely so that dragging it makes INTUITION'S TASK call OM_UPDATE and render
 * directly -- that is the feature. Meanwhile the application's own task is on a
 * timer, and a full ring frees the oldest line every time a new one arrives. Two
 * tasks, one preemptive scheduler, no MMU: the renderer could dereference a
 * pointer the timer had just freed and handed back out.
 *
 * So the reader takes it shared and the writer takes it exclusively. Held only
 * across pointer work, never across DOS I/O.
 */
struct nlv_view {
  struct SignalSemaphore lock;
  char **item;			/* item[0..count-1], newest last */
  LONG	 count;
};

/* Longest line the application will store, so the renderer can bound its scan. */
#define NLV_MAXLINE	512

#define NLVA_Base	(TAG_USER + 0x4E4C0000)	/* 'NL' */
#define NLVA_View	(NLVA_Base + 1)		/* struct nlv_view *  (set) */
#define NLVA_Top	(NLVA_Base + 2)		/* first visible line (set/get/update) */
#define NLVA_Visible	(NLVA_Base + 3)		/* rows that fit          (get) */

struct IClass *nlv_makeclass(void);
void           nlv_freeclass(struct IClass *cl);

#endif /* !NLV_CLASS_H */
