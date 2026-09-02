/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: amiga_generic2.c,v 3.13 1994/04/07 20:46:52 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * Created: Fri Dec 10 22:57:55 1993 too
 * Last modified: Thu Apr  7 23:46:06 1994 jraja
 *
 * HISTORY
 * $Log: amiga_generic2.c,v $
 * Revision 3.13  1994/04/07  20:46:52  jraja
 * Moved SBTC_COMPAT43 inside #ifdef notyet,
 * organized tags in the autodoc to alphapetical order.
 *
 * Revision 3.12  1994/04/02  10:47:23  jraja
 * Changed hErrno to be accessed via a pointer, added tag handling for the
 * hErrnoPtr.
 *
 * Revision 3.11  1994/03/29  12:56:35  ppessi
 * Added SBTC_COMPAT43 tag
 *
 * Revision 3.10  1994/03/22  08:37:28  jraja
 * Added tag list entry for the SBTC_FDCALLBACK (fdCallback),
 * added extern definitions for the __sys_nerr and __sys_errlist,
 * updated SocketBaseTags autodoc.
 *
 * Revision 3.9  1994/02/26  18:05:56  jraja
 * Changed socketbasetags.h to amitcp/socketbasetags.h.
 *
 * Revision 3.8  1994/02/16  06:25:51  jraja
 * Added manual page for the SocketBaseTagList().
 *
 * Revision 3.7  1994/01/20  02:26:40  jraja
 * Added errno size restriction and return value to the SetErrnoPtr().
 * Added errnoPtr handling and rest of the error lists to the
 * SocketBaseTagList().
 *
 * Revision 3.6  1994/01/18  22:55:06  jraja
 * Added some checks and '%m' functionality to the syslog().
 *
 * Revision 3.5  1994/01/18  19:22:32  jraja
 * Changed direct access to errnoPtr to baseErrno() macro (not all of them!)
 *
 * Revision 3.4  1994/01/13  07:05:24  jraja
 * Added external declarations for h_errlist and h_nerr.
 *
 * Revision 3.3  1994/01/12  07:34:26  jraja
 * Moved _getdtablesize() from amiga_generic.c to here.
 *
 * Revision 3.2  1994/01/12  07:23:28  jraja
 * Added implementation of the _SocketBaseTagList(). Moved SetDTableSize and
 * getlastfd() from amiga_generic.c to here. Removed redundant (empty) functions
 *
 * Revision 3.1  1994/01/04  14:12:28  too
 * Moved some functions from amiga_generic.c. Added GetHErrno().
 * Made cores to most AmiTCP/IP 3 functions.
 *
 */

/*
 * amiga_generic2.c --- SocketBaseTagList(): configure a SocketBase.
 *
 * SocketBaseTagList() is how an application (or a C runtime like net.lib on its
 * behalf) tunes ITS OWN library base after opening bsdsocket.library. It takes a
 * standard AmigaOS tag list -- an array of {tag, value} pairs -- and gets/sets
 * per-opener settings. The tags a student should recognise:
 *   SBTC_ERRNOPTR / SBTC_ERRNOBYTEPTR / ...  where to write this program's errno
 *                                            (so socket errors surface as the C
 *                                            library's errno).
 *   SBTC_FDCALLBACK   a hook the library calls whenever this program's socket fd
 *                     table changes, so a C runtime can mirror it (net.lib relies
 *                     on installing this; failing to install it is why the stock
 *                     tools abort -- see PORTING.md / docs).
 *   SBTC_HERRNO       the resolver's h_errno.
 *   SBTC_LOG*         per-opener syslog facility/mask/tag.
 *   SBTC_BREAKMASK    which signal(s) break a blocking socket call.
 * This tag mechanism is exactly the surface the Roadshow-compatible config tools
 * drive against, which is why it matters for the project's compatibility goal.
 *
 * Read SocketBaseTagList() and note the CASE_LONG()/CASE_* dispatch macros that
 * map each tag to a field of the SocketBase.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/errno.h>

#include <kern/amiga_includes.h>

#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <api/allocdatabuffer.h>

#include <api/apicalls.h>

#include <net/bpf.h>			/* NG_BPF_MAXCHAN for SBTC_NUM_PACKET_FILTER_CHANNELS */

#include <stdarg.h>

#include <kern/amiga_log.h>	/* log_dest_name, log_hook */
#include <amitcp/socketbasetags.h>

extern const char * const __sys_errlist[];
extern const int __sys_nerr;
extern const char * const h_errlist[];
extern const int h_nerr;
extern const char * const io_errlist[];
extern const short io_nerr;
extern const char * const sana2io_errlist[];
extern const short sana2io_nerr;
extern const char * const sana2wire_errlist[];
extern const short sana2wire_nerr;

LONG /* SAVEDS */ RAF1(_Errno,
		       struct SocketBase *, libPtr, a6)
#if 0
{
#endif
  return (LONG)readErrnoValue(libPtr);
}

LONG /* SAVEDS */ RAF3(_SetErrnoPtr,
		       struct SocketBase *,	libPtr,	a6,
		       VOID *,			err_p,	a0,
		       UBYTE,			size,	d0)
#if 0     
{
#endif
  if (size == 4 || size == 2 || size == 1) {
    if (size & 0x1 || !((ULONG)err_p & 0x1)) {	/* either odd size or address even */
      /*
       * PORT (AmiTCP_NG) fix: publish the pair indivisibly.
       *
       * errnoSize and errnoPtr must agree: writeErrnoValue()/readErrnoValue()
       * read the size and then cast-dereference the pointer by it. Written as
       * two plain stores on a preemptive OS, a task sharing this base could see
       * the NEW size with the OLD pointer -- e.g. size 4 against a caller's
       * 1-byte errno -- and write three bytes past it.
       *
       * Forbid(), not the syscall semaphore: the readers are lock-free BY
       * DESIGN (API_STD_RETURN deliberately writes errno after releasing the
       * semaphore), so a semaphore here would serialise against nothing.
       * Nothing between Forbid() and Permit() can block.
       */
      Forbid();
      libPtr->errnoSize = size;
      libPtr->errnoPtr = err_p;
      Permit();
      return 0;
    }
  }

  writeErrnoValue(libPtr, EINVAL);
  return -1;
}

#if DIAGNOSTIC
/*
 * Called from the CHECK_TASK macros (amiga_libcallentry.h) at every library-vector
 * entry in a DIAGNOSTIC build. Warns when the CALLER's task stack is nearly
 * exhausted: there is no MMU/guard page, so a protocol descent (~1.5 KB deepest)
 * that runs off the bottom would silently corrupt whatever task's memory sits
 * below -- this turns that into a diagnosable log line. Reads the TRUE SP (a
 * local's address floats by the frame size under -fomit-frame-pointer); reading it
 * here, one frame below the vector, is fine -- it only makes the estimate a hair
 * more conservative.
 */
#define NG_LOW_STACK_MARGIN 1024	/* warn below ~one deepest-descent of headroom */
void ng_low_stack_check(void)
{
  char *sp;
  struct Task *t = SysBase->ThisTask;

  __asm__ __volatile__ ("move.l %%sp,%0" : "=r"(sp));
  if (t && sp < (char *)t->tc_SPLower + NG_LOW_STACK_MARGIN)
    log(LOG_WARNING, "AmiTCP_NG: low stack (%ld bytes) in task %s",
	(long)(sp - (char *)t->tc_SPLower),
	t->tc_Node.ln_Name ? (STRPTR)t->tc_Node.ln_Name : (STRPTR)"?");
}
#endif

VOID SAVEDS RAF4(_Syslog,
		 struct SocketBase *,	libPtr, 	a6,
		 ULONG,			pri,		d0,
		 const char *,		fmt,		a0,
		 va_list,		ap,		a1)
#if 0
{
#endif
  int saved_errno;
  char fallback[128];		/* used only if the heap is exhausted */
  char *fmt_cpy;
  ULONG bufsize;
  register char *p;

  CHECK_TASK_VOID();

  /* check for invalid bits or no priority set */
  if (!LOG_PRI(pri) || (pri &~ (LOG_PRIMASK|LOG_FACMASK)) ||
      !(LOG_MASK(pri) & libPtr->LogMask))
    return;

  saved_errno = readErrnoValue(libPtr);
  if (saved_errno >= __sys_nerr)
    saved_errno = 0;						/* XXX */

  /* set default facility if none specified */
  if ((pri & LOG_FACMASK) == 0)
    pri |= libPtr->LogFacility;

  /* Assemble off the CALLER's (possibly small) task stack: syslog() is a public
   * vector and a 1 KB stack buffer is a poor citizen on a ~4 KB stack. Fall back
   * to a small stack buffer only if the heap is exhausted, so logging still
   * works (truncated) under memory pressure. Freed before we return. */
  fmt_cpy = (char *)AllocVec(1024, MEMF_PUBLIC);
  if (fmt_cpy != NULL) bufsize = 1024;
  else { fmt_cpy = fallback; bufsize = sizeof(fallback); }
  p = fmt_cpy;

  /*
   * Build the message into fmt_cpy, bounded at every write so an over-long
   * LogTag or format string cannot overflow this fixed stack buffer (the
   * message is truncated instead). Leave one byte for the NUL terminator.
   */
  {
    char *end = fmt_cpy + bufsize - 1;
    char ch, *t1;
    const char *t2;

    if (libPtr->LogTag)			/* copy as DATA, never a format string */
      for (t2 = libPtr->LogTag; *t2 && p < end; )
	*p++ = *t2++;
    if (libPtr->LogStat & LOG_PID) {
      char pidbuf[16];
      /* The task actually logging, not the base's opener -- they differ once
	 * SBTC_CAN_SHARE_LIBRARY_BASES lets another task in, and stamping the
	 * opener's pointer on a sharer's line misidentifies it. */
      sprintf(pidbuf, "[%lx]", SysBase->ThisTask);	/* fixed format, <= 16 */
      for (t2 = pidbuf; *t2 && p < end; )
	*p++ = *t2++;
    }
    if (libPtr->LogTag) {
      if (p < end) *p++ = ':';
      if (p < end) *p++ = ' ';
    }
    /* Copy fmt, expanding %m to the errno string; keep %% and other %X for vlog. */
    for (t1 = p; (ch = *fmt) && t1 < end; ++fmt) {
      if (ch == '%' && fmt[1] == 'm') {
	++fmt;
	for (t2 = __sys_errlist[saved_errno]; *t2 && t1 < end; )
	  *t1++ = *t2++;
	continue;
      }
      if (ch == '%' && fmt[1] == '%') {
	++fmt;
	if (t1 < end) *t1++ = '%';
	if (t1 < end) *t1++ = '%';
	continue;
      }
      *t1++ = ch;
    }
    *t1 = '\0';
  }

  vlog(pri, fmt_cpy, ap);
  if (fmt_cpy != fallback)
    FreeVec(fmt_cpy);
}

VOID SAVEDS RAF4(_SetSocketSignals,
		 struct SocketBase *,	libPtr, 	a6,
		 ULONG,			SIGINTR,	d0,
		 ULONG,			SIGIO,		d1,
		 ULONG,			SIGURG,		d2)
#if 0
{
#endif
  CHECK_TASK_VOID();
  /*
   * The operations below are atomic so no need to protect them
   */
  libPtr->sigIntrMask = SIGINTR;
  libPtr->sigIOMask = SIGIO;
  libPtr->sigUrgMask = SIGURG;
}

LONG /* SAVEDS */ RAF1(_getdtablesize,
		       struct SocketBase *,	libPtr, a6)
#if 0
{
#endif
  NG_ENSURE_STACK();

  return (LONG)libPtr->dTableSize;
}

static int getLastSockFd(struct SocketBase * libPtr)
{
  int bit, lastmlong = (libPtr->dTableSize - 1) / NFDBITS;
  unsigned long *smaskp, cmask, rmask;

  for (smaskp = (ULONG *)(libPtr->dTable + libPtr->dTableSize + lastmlong);
       lastmlong >= 0; smaskp--, lastmlong--)
    if (*smaskp)
      break;

  if (lastmlong < 0)
    return -1;

  cmask = *smaskp;
  if ((rmask = cmask & 0xFFFF0000)) { bit = 16; cmask = rmask; }
  else bit = 0;
  if ((rmask = cmask & 0xFF00FF00)) { bit += 8; cmask = rmask; }
  if ((rmask = cmask & 0xF0F0F0F0)) { bit += 4; cmask = rmask; }
  if ((rmask = cmask & 0xCCCCCCCC)) { bit += 2; cmask = rmask; }
  if ((rmask = cmask & 0xAAAAAAAA)) bit += 1;

  return lastmlong * 32 + bit;
}

/*
 * Set size of descriptor tab|e
 */
static LONG
setdtablesize(struct SocketBase * libPtr, UWORD size)
{
  
  LONG oldsize = (LONG)libPtr->dTableSize;
  LONG copysize;
  struct socket ** dTable;
  struct socket ** oldTable = libPtr->dTable;	/* retired, not freed -- see below */
  int olddmasksize, copydmasksize, dmasksize;
  
  if (size < oldsize) {
    int i;

    if ((i = getLastSockFd(libPtr)) > size)
      size = i + 1;
    copysize = size;
  }
  else
    copysize = oldsize;

  olddmasksize	= (oldsize - 1) /  NFDBITS + 1;
  copydmasksize	= (copysize - 1) / NFDBITS + 1;
  dmasksize 	= (size - 1) /	   NFDBITS + 1;
  
  if ((dTable = AllocMem(size * sizeof (struct socket *) +
			 dmasksize * sizeof (fd_mask),
			 MEMF_PUBLIC|MEMF_CLEAR)) == NULL)
    return oldsize;

  aligned_bcopy(libPtr->dTable, dTable, copysize * sizeof (struct socket *));
  aligned_bcopy(libPtr->dTable + oldsize, dTable + size,
		copydmasksize * sizeof (fd_mask));

  /*
   * PORT (AmiTCP_NG) fix: PUBLISH, THEN RETIRE -- and publish both fields as one
   * indivisible step.
   *
   * This used to FreeMem() the old table and only then store the new pointer and
   * size, as two separate assignments. Three defects came out of that:
   *
   *  1. Between the free and the store, libPtr->dTable pointed at freed memory --
   *     true even single-threaded.
   *  2. dTable and dTableSize are a PAIR: the fd_mask bitmap lives inside the same
   *     allocation at offset (dTable + dTableSize), so every reader derives the
   *     bitmap address from both. AmigaOS is priority-preemptive, so a task switch
   *     between the two stores leaves a reader computing the bitmap at the wrong
   *     offset -- an out-of-bounds write, with no MMU to catch it.
   *  3. The old block was freed while another task might still hold a pointer to
   *     it (see below).
   *
   * Forbid() here is NOT redundant with the syscall semaphore the caller holds.
   * The semaphore only excludes other semaphore-holding callers; selscan()
   * (api/amiga_generic.c) cannot take it -- _WaitSelect blocks, and holding one
   * global semaphore across a block would stall the whole stack -- so it snapshots
   * the pair under Forbid() instead. Forbid here is what that snapshot pairs with.
   * Nothing between Forbid() and Permit() blocks or calls another vector; keep it
   * that way.
   */
  Forbid();
  libPtr->dTable = dTable;
  libPtr->dTableSize = size;
  Permit();

  /*
   * The retired table is deliberately NOT freed. No lock can help a reader that
   * already loaded the old pointer into a local before the swap and is preempted
   * before using it -- selscan() does exactly that, because it cannot hold a lock
   * across its whole scan. Retaining the block turns that reader's stale pointer
   * into a stale-but-VALID read instead of a use-after-free. The blocks are
   * chained off the base and released in UL_Close(), which is the same trade this
   * codebase already makes deliberately there: leak rather than risk corruption.
   *
   * A caller that resizes repeatedly accumulates retired blocks until it closes
   * the library. That is bounded and intentional, not an oversight.
   */
  {
    struct ng_retired_dtable *r =
      AllocMem(sizeof (struct ng_retired_dtable), MEMF_PUBLIC);

    if (r != NULL) {
      r->rd_mem  = (APTR)oldTable;
      r->rd_size = oldsize * sizeof (struct socket *) +
		   olddmasksize * sizeof (fd_mask);
      Forbid();
      r->rd_next = libPtr->retiredTables;
      libPtr->retiredTables = r;
      Permit();
    }
    /* If even that small node cannot be allocated we simply never reclaim this
     * block. Leaking it is the safe direction; freeing it is the unsafe one. */
  }

  return size;
}


#define CASE_LONG(code, baseField)\
 case (code << SBTB_CODE): /* get */ \
  *tagData = (ULONG)libPtr->baseField;\
  break;\
 case (code << SBTB_CODE) | SBTF_SET: /* set */\
  *(ULONG *)&libPtr->baseField = *tagData;\
  break

/*
 * PORT (AmiTCP_NG): the same get/set pair for a STACK-WIDE `int`, not a field of
 * the caller's base. The Roadshow API documents these as affecting the whole
 * protocol stack rather than the library base they were set through, which is
 * the only thing that makes sense for one shared IP layer -- so unlike
 * CASE_LONG() there is deliberately no per-base copy.
 */
#define CASE_GLOBAL_INT(code, global)\
 case (code << SBTB_CODE): /* get */ \
  *tagData = (ULONG)(global);\
  break;\
 case (code << SBTB_CODE) | SBTF_SET: /* set */\
  (global) = (int)*tagData;\
  break

/*
 * As above but for a global whose legal values are 0..(limit-1). An out-of-range
 * value fails the tag rather than being clamped: these select BEHAVIOUR (how to
 * treat an ICMP request), and silently substituting a different behaviour for
 * the one asked for is how a machine ends up answering probes its owner believes
 * are switched off.
 */
#define CASE_GLOBAL_ENUM(code, global, limit)\
 case (code << SBTB_CODE): /* get */ \
  *tagData = (ULONG)(global);\
  break;\
 case (code << SBTB_CODE) | SBTF_SET: /* set */\
  if (*tagData >= (ULONG)(limit))\
    return errIndex;	/* the documented failure answer: which tag failed */\
  (global) = (int)*tagData;\
  break

#define CASE_WORD(code, baseField)\
 case (code << SBTB_CODE): /* get */ \
  *tagData = (ULONG)libPtr->baseField;\
  break;\
 case (code << SBTB_CODE) | SBTF_SET: /* set */\
  *(UWORD *)&libPtr->baseField = (UWORD)*tagData;\
  break

#define CASE_BYTE(code, baseField)\
 case (code << SBTB_CODE): /* get */ \
  *tagData = (ULONG)libPtr->baseField;\
  break;\
 case (code << SBTB_CODE) | SBTF_SET: /* set */\
  *(UBYTE *)&libPtr->baseField = (UBYTE)*tagData;\
  break

/*
 * PORT (AmiTCP_NG): a GET-only global, for the SBTC_NG_* diagnostic codes.
 * There are enough of these now (the header-prediction miss breakdown alone is
 * thirteen) that writing each one out longhand is mostly an invitation to
 * paste the wrong variable next to the right tag. A SET is meaningless for a
 * read-only counter, so it is left to fall through to the default and be
 * ignored rather than writing through a caller-supplied pointer.
 */
#define CASE_NG_GET(code, expr)\
 case (code << SBTB_CODE): /* get only */ \
  *tagData = (ULONG)(expr);\
  break

/*
 * NB (AmiTCP_NG): the SET arm is a read-modify-write on libPtr->flags, which
 * two tasks can reach at once on a shared base, and _SocketBaseTagList() holds
 * no lock -- hence the Disable()/Enable() below. Without it, two tasks setting
 * different bits could lose one of them.
 * Also note `flag` must be an SBFF_ MASK, never an SBFB_ bit number.
 */
#define CASE_FLAG(code, flag)\
  case (code << SBTB_CODE): /* get */ \
    *tagData = ((ULONG)libPtr->flags & (flag)) != 0;\
    break;\
  case (code << SBTB_CODE) | SBTF_SET: /* set */\
    /* Read-modify-write of a byte two tasks may reach once the base is\
     * shared, and this function holds no lock. Disable() is the cheapest\
     * thing that makes it indivisible; nothing inside can block. */\
    Disable();\
    if (*tagData) \
      *(UBYTE *)&libPtr->flags |= (flag);\
    else \
      *(UBYTE *)&libPtr->flags &= ~(flag);\
    Enable();\
    break


/****** bsdsocket.library/SocketBaseTagList ***********************************

    NAME
         SocketBaseTagList - Set/Get SocketBase attributes.

    SYNOPSIS
         #include <amitcp/socketbasetags.h>

         ULONG SocketBaseTagList(struct TagItem *);

         error = SocketBaseTagList(taglist)
         D0                        A0

         error = SocketBaseTags(ULONG tag, ...);

    FUNCTION
        Set or get a list of (mostly) SocketBase instance dependent attributes
        from the AmiTCP.

    INPUTS
        These functions expect as their argument a standard tag list, one or
        several array of struct TagItem as defined in the header file
        <utility/tagitem.h>. The structure contains two fields: ti_Tag and
        ti_Data.  The ti_Tag field contains tag code, which determines what
        the SocketBaseTagList() should do with its argument, the ti_Data
        field.

        The include file <amitcp/socketbasetags.h> defines macros for base tag
        code values.  Base tag code macros begin with `SBTC_' (as Socket Base
        Tag Code).  The base tag value defines what data item the tag item
        refers.

        The tag code contains other information besides the referred data
        item.  It controls, whether the SocketBaseTagList() should set or get
        the appropriate parameter, and whether the argument of the tag in
        question is passed by value or by reference.  

        The include file <amitcp/socketbasetags.h> defines the following
        macros, which are used to construct the ti_Tag values from the base
        tag codes:

             SBTM_GETREF(code) - get by reference
             SBTM_GETVAL(code) - get by value
             SBTM_SETREF(code) - set by reference
             SBTM_SETVAL(code) - set by value

        If the actual data is stored directly into the ti_Data field, you
        should use the 'by value' macros, SBTM_GETVAL() or SBTM_SETVAL().
        However, if the ti_Data field contains a pointer to actual data, you
        should use the 'by reference' macros, SBTM_GETREF() or SBTM_SETREF().
        In either case the actual data should always be a LONG aligned to even
        address.

        According the used tag naming scheme a tag which has "PTR" suffix
        takes an pointer as its argument.  Don't mix the pointer arguments
        with 'by reference' argument passing.  It is possible to pass a
        pointer by reference (in which case the ti_Data is a pointer to the
        actual pointer).

        The list of all defined base tag codes is as follows:

             SBTC_BREAKMASK       Tag data contains the INTR signal mask.  If
                                  the calling task receives a signal in the
                                  INTR mask, the AmiTCP interrupts current
                                  function calls and returns with the error
                                  code EINTR.  The INTR mask defaults to the
                                  CTRL-C signal (SIGBREAKF_C, bit 12).

             SBTC_DTABLESIZE      Socket Descriptor Table size. This
                                  defaults to 64.

             SBTC_ERRNO           The errno value. The values are defined in
                                  <sys/errno.h>.

             SBTC_ERRNOBYTEPTR
             SBTC_ERRNOWORDPTR
             SBTC_ERRNOLONGPTR
             SBTC_ERRNOPTR(size)  Set (only) the pointer to the errno
                                  variable defined by the program.  AmiTCP
                                  defines a value for this by default, but
                                  the application must set the pointer (and
                                  the size of the errno) with one of these
                                  tags, if it wishes to access the errno
                                  variable directly.

                                  The SBTC_ERRNOPTR(size) is a macro, which
                                  expands to one of the other (BYTE, WORD or
                                  LONG) tag codes, meaning that only 1, 2
                                  and 4 are legal size values.

                                  The netlib autoinit.c sets the errno
                                  pointer for the application, if the
                                  application is linked with it.

             SBTC_ERRNOSTRPTR     Returns an error string pointer describing
                                  the errno value given on input. You can not
                                  set the error message, only get is allowed.

                                  On call the ti_Data must contain the error
                                  code number.  On return the ti_Data is
                                  assigned to the string pointer.  (*ti_Data,
                                  if passed by reference).  See the file
                                  <sys/errno.h> for symbolic definitions for
                                  the errno codes.

             SBTC_FDCALLBACK      A callback function pointer for coordination
                                  of file descriptor usage between AmiTCP and
                                  link-library.  By default no callback is
                                  called and the value of this pointer is
                                  NULL.  The prototype for the callback
                                  function is:

                                  int error = fdCallback(int fd, int action);
                                      D0                     D0      D1

                                  where

                                  error -  0 for success or one of the error
                                           codes in <sys/errno.h> in case of
                                           error. The AmiTCP API function
                                           that calls the callback usually
                                           returns the 'error' back to the
                                           caller without any further
                                           modification.

                                  fd -     file descriptor number to take
                                           'action' on.

                                  action - one of the following actions
                                           (defined in
                                           <amitcp/socketbasetags.h>):

                                           FDCB_FREE -  mark the 'fd' as
                                                        unused on the link
                                                        library structure. If
                                                        'fd' represents a
                                                        file handled by the
                                                        link library, the
                                                        error (ENOTSOCK)
                                                        should be returned.

                                           FDCB_ALLOC - mark the 'fd'
                                                        allocated as a
                                                        socket.

                                           FDCB_CHECK - check if the 'fd' is
                                                        free. If an error is
                                                        returned, the 'fd' is
                                                        marked as used in the
                                                        AmiTCP/IP structures.

                                  The AmiTCP/IP calls the callback every time
                                  a socket descriptor is allocated or freed.
                                  AmiTCP/IP uses the FDCB_CHECK before actual
                                  allocation to check that it agrees with the
                                  link library on the next free descriptor
                                  number.  Thus the link library doesn't need
                                  to tell the AmiTCP if it creates a new file
                                  handle in open(), for example.

                                  See file _chkufb.c on the net.lib sources
                                  for an example implementation of the
                                  callback function for the SAS/C.

             SBTC_HERRNO          The name resolver error code value. Get
                                  this to find out why the gethostbyname()
                                  or gethostbyaddr() failed. The values are
                                  defined in <netdb.h>

             SBTC_HERRNOSTRPTR    Returns host error string for error number
                                  in tag data.  Host error is set on
                                  unsuccesful gethostbyname() and
                                  gethostbyaddr() calls. See the file
                                  <netdb.h> for the symbolic definitions for
                                  the herrno valus.

                                  Notes for the SBTC_ERRNOSTRPTR apply also
                                  to this tag code.

             SBTC_IOERRNOSTRPTR   Returns an error string for standard
                                  AmigaOS I/O error number as defined in the
                                  header file <exec/errors.h>.  Note that the
                                  error number taken by this tag code is
                                  positive, so the error codes must be
                                  negated (to be positive).  The positive
                                  error codes depend on the particular IO
                                  device, the standard Sana-II error codes
                                  can be retrieved by the tag code
                                  SBTC_S2ERRNOSTRPTR.

                                  Notes for the SBTC_ERRNOSTRPTR apply also
                                  to this tag code.

             SBTC_LOGFACILITY     Facility code for the syslog messages as
                                  defined in the header file <sys/syslog.h>.
                                  Defaults to LOG_USER.

             SBTC_LOGMASK         Sets the filter mask of the syslog
                                  messages.  By default the mask is 0xff,
                                  meaning that all messages are passed to the
                                  log system.

             SBTC_LOGSTAT         Syslog options defined in <sys/syslog.h>.

             SBTC_LOGTAGPTR       A pointer to a string which is used by
                                  syslog() to mark individual syslog
                                  messages. This defaults to NULL, but is
                                  set to the name of the calling program by
                                  the autoinit code in netlib:autoinit.c.
                                  This is for compatibility with pre-3.0
                                  programs.

             SBTC_S2ERRNOSTRPTR   Returns an error string for a Sana-II
                                  specific I/O error code as defined in the
                                  header file <devices/sana2.h>.

                                  Notes for the SBTC_ERRNOSTRPTR apply also
                                  to this tag code.

             SBTC_S2WERRNOSTRPTR  Returns an error string for a Sana-II Wire
                                  Error code as defined in the header file
                                  <devices/sana2.h>.

                                  Notes for the SBTC_ERRNOSTRPTR apply also
                                  to this tag code.

             SBTC_SIGIOMASK       The calling task is sent the signals
                                  specified by mask in tag data when
                                  asynhronous I/O is to be notified. The
                                  default value is zero, ie. no signal is
                                  sent.

             SBTC_SIGURGMASK      The calling task is sent the signals
                                  specified by mask in tag data when urgent
                                  data for the TCP arrives. The default value
                                  is zero, ie. no signal is sent.

    RESULT 
        Returns 0 on success, and a (positive) index of the failing tag on
        error.  Note that the value 1 means _first_ TagItem, 2 the second one,
        and so on.  The return value is NOT a C-language index, which are 0
        based.

    EXAMPLES
        To be written, see net.lib sources for various examples.

    NOTES

    BUGS
        None known.

    SEE ALSO
        <netinclude:amitcp/socketbasetags.h>, <include:utility/tagitem.h>

*****************************************************************************
*
*/
#ifdef notyet
/*
             SBTC_COMPAT43        Tag data is handled as boolean.  If it is
                                  true, AmiTCP/IP uses 4.3BSD compatible
                                  sockaddr structure for this application.

                                  The unreleased AS225r2 uses also 4.3BSD-
                                  compatible sockaddr structures.

*/
#endif

/* SBTC_SYSTEM_STATUS helper -- computes the SBSYSSTAT_* bitmask (api/amiga_roadshow_compat.c). */
extern ULONG ng_system_status(struct SocketBase *libPtr);

/* Live values behind the SBTC_NG_* diagnostic query codes (GetNetStatus DEBUG). */
extern unsigned long ng_detected_ram;			/* kern/amiga_main.c   */
extern unsigned long tcp_sendspace, tcp_recvspace;	/* netinet/tcp_usrreq.c */
extern unsigned long sb_max;				/* kern/uipc_socket2.c  */
extern unsigned long ng_last_if_baudrate;		/* api/amiga_roadshow_compat.c */
extern int tcppredack, tcppreddat, tcppcbcachemiss, tcppredwin, tcpreassfull;
/* Socket wakeup accounting -- defined in kern/uipc_socket2.c, NOT tcp_input.c. */
extern unsigned long ng_sowk_calls, ng_sowk_rcv, ng_sowk_wait, ng_sowk_sel, ng_sowk_async;	/* netinet/tcp_input.c  */
extern u_long ng_tcp_rcvtotal();			/* netinet/tcp_input.c  */
/* Header-prediction miss attribution (netinet/tcp_input.c tcp_predict_miss). */
extern int tcppm_state, tcppm_flags, tcppm_tstamp, tcppm_seq, tcppm_win,
	   tcppm_rexmit, tcppm_dupack, tcppm_sack, tcppm_ack, tcppm_cwnd,
	   tcppm_ackdata, tcppm_reass, tcppm_space, tcppm_zerowin,
	   tcppm_ackdup, tcppm_winonly;

ULONG SAVEDS RAF2(_SocketBaseTagList,
		  struct SocketBase *,	libPtr,		a6,
		  struct TagItem *,	tags,		a0)
#if 0     
{
#endif

  ULONG errIndex = 1;
  ULONG tag;
  ULONG *tagData;
  short tmp;
  UWORD utmp;
  /*
   * PORT (AmiTCP_NG): the stack-wide knobs the SBTC_ tags below drive. Declared
   * locally rather than in a header, matching how the rest of this fork's
   * Roadshow work reaches them (api/amiga_roadshow_compat.c does the same).
   * ip_defttl is also declared in netinet/ip_var.h for the IP code itself.
   */
  extern int udpcksum, ipforwarding, ipsendredirects, icmpmaskrepl;
  extern int icmp_process_echo, icmp_process_tstamp, ip_defttl;
  extern int tcp_ttl, udp_ttl;
  extern struct Hook *log_hook;	/* SBTC_LOG_HOOK (kern/subr_prf.c) */

  static const char * const strErr = "Errlist lookup error";

  CHECK_TASK();

  /*
   * PORT (AmiTCP_NG) fix: a NULL tag list was dereferenced immediately below.
   * With no MMU that reads address 0 rather than trapping, so the behaviour
   * depended on whatever the exception-vector table happened to hold -- in
   * practice a benign "bad tag" return, but if that word ever had its top bit
   * set the SBTF_REF path would take &tags->ti_Data (address 4, where Exec
   * keeps SysBase) as a writable target. The fork's newer vectors already
   * treat a NULL list this way; make this one consistent.
   */
  if (tags == NULL)
    return 0;

  while((tag = tags->ti_Tag) != TAG_END) {
    if ((LONG)tag < 0) {		/* TAG_USER is the sign bit */
      /* get pointer to the actual data */
      tagData = ((UWORD)tag & SBTF_REF) ?
	(ULONG *)tags->ti_Data : &tags->ti_Data;

      switch ((UWORD)tag & ~SBTF_REF) {

      CASE_LONG( SBTC_BREAKMASK,  sigIntrMask );

      CASE_LONG( SBTC_SIGIOMASK,  sigIOMask );

      CASE_LONG( SBTC_SIGURGMASK, sigUrgMask );

      /* PORT (AmiTCP_NG): tag 4, the asynchronous socket-event signal. Absent
       * from AmiTCP 3.0b2 -- this header's numbering jumped 3 -> 6 -- so every
       * Roadshow-model client that asked for it got "bad tag" and no signal.
       * Paired with setsockopt(SO_EVENTMASK) and GetSocketEvents(). */
      CASE_LONG( SBTC_SIGEVENTMASK, sigEventMask );

      case (SBTC_ERRNO << SBTB_CODE): /* get */ 
	*tagData = (ULONG)readErrnoValue(libPtr);
	break;
      case (SBTC_ERRNO << SBTB_CODE) | SBTF_SET: /* set */
        writeErrnoValue(libPtr, *tagData);
	break;

      case (SBTC_HERRNO << SBTB_CODE): /* get */ 
	*tagData = (ULONG)*libPtr->hErrnoPtr;
	break;
      case (SBTC_HERRNO << SBTB_CODE) | SBTF_SET: /* set */
	/* PORT (AmiTCP_NG): through writeHErrnoValue(), so an installed
	 * SBTC_ERROR_HOOK sees this the same way it sees an errno set --
	 * SBTC_ERRNO above already routes through writeErrnoValue(), and two
	 * structurally identical tags behaving differently is a trap. */
	writeHErrnoValue(libPtr, (int)*tagData);
	break;

      case (SBTC_DTABLESIZE << SBTB_CODE): /* get */
	*tagData = (ULONG)libPtr->dTableSize;
	break;
      case (SBTC_DTABLESIZE << SBTB_CODE) | SBTF_SET: /* set */
	/*
	 * PORT (AmiTCP_NG) fix: hold the syscall semaphore across the resize.
	 *
	 * Every other reader of the dTable/dTableSize pair -- getSock(), all the
	 * FD_SET/FD_ISSET/FD_CLR sites, Dup2Socket, the descriptor walk in
	 * GetSocketEvents -- runs under this semaphore and assumes the pair is
	 * stable while it is held. _SocketBaseTagList() takes no lock, so on a
	 * shared base (SBTC_CAN_SHARE_LIBRARY_BASES) a resize could free the table
	 * another task was indexing.
	 *
	 * SCOPED TO THIS ARM ON PURPOSE -- do NOT hoist this around the switch or
	 * the tag-walk loop. ObtainSyscallSemaphore() is not safely nestable: it
	 * saves the caller's priority into the BASE (libPtr->myPri) before boosting,
	 * so a nested obtain overwrites that saved value with the already-boosted
	 * one and the outer release then never restores the original -- the task
	 * stays at the net task's priority for good. Widening this hold would do
	 * exactly that for any tag list that also contains SBTC_SYSTEM_STATUS,
	 * whose handler (ng_system_status) takes the same semaphore itself.
	 */
	if ((tmp = (WORD)*tagData) > 0) {
	  ObtainSyscallSemaphore(libPtr);
	  setdtablesize(libPtr, tmp);
	  ReleaseSyscallSemaphore(libPtr);
	}
	break;

      CASE_LONG( SBTC_FDCALLBACK,   fdCallback );

      CASE_BYTE( SBTC_LOGSTAT,      LogStat );

      CASE_LONG( SBTC_LOGTAGPTR,    LogTag );

      case (SBTC_LOGFACILITY << SBTB_CODE): /* get */
	*tagData = (ULONG)libPtr->LogFacility;
	break;
      case (SBTC_LOGFACILITY << SBTB_CODE) | SBTF_SET: /* set */
	if ((utmp = (UWORD)*tagData) != 0 && (utmp &~ LOG_FACMASK) == 0)
	  libPtr->LogFacility = utmp;
	break;

      case (SBTC_LOGMASK << SBTB_CODE): /* get */
	*tagData = (ULONG)libPtr->LogMask;
	break;
      case (SBTC_LOGMASK << SBTB_CODE) | SBTF_SET: /* set */
	if ((utmp = (UWORD)*tagData) != 0)
	  libPtr->LogMask = (UBYTE)utmp;
	break;

      case SBTC_ERRNOSTRPTR << SBTB_CODE:
	/* get index */
	utmp = (UWORD)*tagData;
	/* return string pointer */
	*tagData = (ULONG)((utmp >= __sys_nerr) ?
			   strErr : __sys_errlist[utmp]);
	break;
      case SBTC_HERRNOSTRPTR << SBTB_CODE:
	/* get index */
	utmp = (UWORD)*tagData;
	/* return string pointer */
	*tagData = (ULONG)((utmp >= h_nerr) ?
			   strErr : h_errlist[utmp]);
	break;
      case SBTC_IOERRNOSTRPTR << SBTB_CODE:
	/* get index */
	utmp = (UWORD)*tagData;
	/* return string pointer */
	*tagData = (ULONG)((utmp >= io_nerr) ? 
			   strErr : io_errlist[utmp]);
	break;
      case SBTC_S2ERRNOSTRPTR << SBTB_CODE:
	/* get index */
	utmp = (UWORD)*tagData;
	/* return string pointer */
	*tagData = (ULONG)((utmp >= sana2io_nerr) ?
			   strErr : sana2io_errlist[utmp]);
	break;
      case SBTC_S2WERRNOSTRPTR << SBTB_CODE:
	/* get index */
	utmp = (UWORD)*tagData;
	/* return string pointer */
	*tagData = (ULONG)((utmp >= sana2wire_nerr) ?
			   strErr : sana2wire_errlist[utmp]);
	break;

      case (SBTC_ERRNOBYTEPTR << SBTB_CODE) | SBTF_SET: /* set */
        if (SetErrnoPtr(libPtr, (VOID *)*tagData, 1) < 0)
	  return errIndex;
        break;
      case (SBTC_ERRNOWORDPTR << SBTB_CODE) | SBTF_SET: /* set */
        if (SetErrnoPtr(libPtr, (VOID *)*tagData, 2) < 0)
	  return errIndex;
        break;
      case (SBTC_ERRNOLONGPTR << SBTB_CODE) | SBTF_SET: /* set */
        if (SetErrnoPtr(libPtr, (VOID *)*tagData, 4) < 0)
	  return errIndex;
        break;

      CASE_LONG( SBTC_HERRNOLONGPTR, hErrnoPtr );

#ifdef notyet
      CASE_FLAG( SBTC_COMPAT43, SBFB_COMPAT43 );
#endif

      /*
       * PORT (AmiTCP_NG): Roadshow's opt-in to using one library base from more
       * than one task. Until this is set we refuse non-opener callers in
       * CHECK_TASK(), as Roadshow does. Note CASE_FLAG() wants the MASK.
       * The restrictions this does NOT lift are listed at CHECK_TASK().
       */
      case (SBTC_CAN_SHARE_LIBRARY_BASES << SBTB_CODE): /* get */
	*tagData = ((ULONG)libPtr->flags & SBFF_CAN_SHARE) != 0;
	break;
      case (SBTC_CAN_SHARE_LIBRARY_BASES << SBTB_CODE) | SBTF_SET: /* set */
	/* Not CASE_FLAG: enabling also throws a latch that is never cleared,
	 * because the base pointer can already be in another task's hands
	 * before that task has called in. Turning sharing back off afterwards
	 * must not convince UL_Close it is safe to free the base. */
	Disable();
	if (*tagData) {
	  *(UBYTE *)&libPtr->flags |= SBFF_CAN_SHARE;
	  libPtr->everShared = TRUE;
	} else
	  *(UBYTE *)&libPtr->flags &= ~SBFF_CAN_SHARE;
	Enable();
	break;

      /*
       * PORT (AmiTCP_NG): stack-wide protocol behaviour controls. All of these
       * are Roadshow tags this fork simply did not have -- AmiTCP 3.0b2 predates
       * them -- so a Roadshow-aware tool could not configure the stack at all.
       * Each affects the whole stack, not this base, exactly as documented.
       */
      CASE_GLOBAL_INT( SBTC_UDP_CHECKSUM,        udpcksum );
      CASE_GLOBAL_INT( SBTC_IP_FORWARDING,       ipforwarding );
      CASE_GLOBAL_INT( SBTC_ICMP_SEND_REDIRECTS, ipsendredirects );
      CASE_GLOBAL_INT( SBTC_ICMP_MASK_REPLY,     icmpmaskrepl );

      /* IR_Process / IR_Ignore / IR_Drop (netinet/ip_icmp.h). */
      CASE_GLOBAL_ENUM( SBTC_ICMP_PROCESS_ECHO,   icmp_process_echo,   3 );
      CASE_GLOBAL_ENUM( SBTC_ICMP_PROCESS_TSTAMP, icmp_process_tstamp, 3 );

      /*
       * SBTC_IP_DEFAULT_TTL. Roadshow has ONE default TTL; 4.4BSD keeps tcp_ttl
       * and udp_ttl apart. Mirror into both, or the setting would visibly do
       * nothing to the traffic the caller actually cares about. Range 1..255:
       * a TTL of 0 is discarded by the first router -- arguably by the sending
       * host -- so accepting it would quietly disconnect the machine.
       */
      case (SBTC_IP_DEFAULT_TTL << SBTB_CODE): /* get */
	*tagData = (ULONG)ip_defttl;
	break;
      case (SBTC_IP_DEFAULT_TTL << SBTB_CODE) | SBTF_SET: /* set */
	if (*tagData == 0 || *tagData > 255)
	  return errIndex;
	ip_defttl = (int)*tagData;
	tcp_ttl   = ip_defttl;
	udp_ttl   = ip_defttl;
	break;

      /*
       * SBTC_RELEASESTRPTR -- the stack's name and version string. This is the
       * library's own lib_IdString (RELEASESTRING VSTRING, set in amiga_api.c),
       * so there is exactly one source of truth and it cannot drift from what
       * the Version command reports. GET only: the string is ours, not the
       * caller's, and a SET falls through to `default` and is ignored.
       */
      case (SBTC_RELEASESTRPTR << SBTB_CODE):
	*tagData = (ULONG)libPtr->libNode.lib_IdString;
	break;

      /*
       * SBTC_SIG_ADDRESS_CHANGE_MASK -- signal this task when an interface
       * address is added, changed or removed (netinet/in.c raises it).
       */
      CASE_LONG( SBTC_SIG_ADDRESS_CHANGE_MASK, sigAddrChangeMask );

      /*
       * SBTC_ERROR_HOOK -- install/remove the hook that takes delivery of errno
       * and h_errno. Per base, because errno is per base.
       */
      CASE_LONG( SBTC_ERROR_HOOK, errorHook );

      /*
       * SBTC_LOG_HOOK -- stack-wide, because there is one logging subsystem.
       * Installing NULL restores normal recording and display.
       */
      case (SBTC_LOG_HOOK << SBTB_CODE): /* get */
	*tagData = (ULONG)log_hook;
	break;
      case (SBTC_LOG_HOOK << SBTB_CODE) | SBTF_SET: /* set */
	log_hook = (struct Hook *)*tagData;
	break;

      /*
       * SBTC_LOG_FILE_NAME -- where the log is being written.
       *
       * GET reports the destination ACTUALLY in use, not the one that was
       * configured: bring-up falls back through ram:, t:, AmiTCP: and sys: if
       * the configured name cannot be opened, and answering with a name we
       * failed to open would send the caller looking for a file that is not
       * there.
       *
       * SET is deliberately refused. logfilename is owned by the configuration
       * layer, whose setvalue() FreeVec()s the previous value before replacing
       * it -- so handing it a caller's string, or one of our static fallback
       * names, sets up a free() of memory that was never allocated, which on a
       * machine with no MMU corrupts the allocator rather than failing. Changing
       * the live destination also means re-opening it, with the log task mid-
       * write. Failing the tag is honest and safe; LOGFILENAME= in
       * AmiTCP:db/AmiTCP.config is the supported way to choose the file.
       */
      case (SBTC_LOG_FILE_NAME << SBTB_CODE): /* get */
	*tagData = (ULONG)log_dest_name;
	break;

      /*
       * SBTC_IDN_DEFAULT_CHARACTER_SET -- we do not implement international
       * domain name translation, so the only value we can honestly report or
       * accept is IDNCS_ASCII, which is precisely the value that means "no
       * translation". Accepting IDNCS_ISO_8859_LATIN_1 would promise a
       * conversion that never happens, and the caller would silently look up
       * the wrong name; failing the tag tells it to encode names itself.
       */
      case (SBTC_IDN_DEFAULT_CHARACTER_SET << SBTB_CODE): /* get */
	*tagData = IDNCS_ASCII;
	break;
      case (SBTC_IDN_DEFAULT_CHARACTER_SET << SBTB_CODE) | SBTF_SET: /* set */
	if (*tagData != IDNCS_ASCII)
	  return errIndex;
	break;

      /*
       * SBTC_GET_BYTES_RECEIVED / SBTC_GET_BYTES_SENT -- stack-wide octet
       * totals, summed from the per-interface counters (net/if.c).
       *
       * These are the ONLY tags here that write more than a longword, and that
       * makes the reference flag a safety matter rather than a formality: the
       * value is an SBQUAD_T ({ULONG high; ULONG low;}, high word first) and the
       * API documents it as passed by reference. Without SBTF_REF, tagData
       * points at the TagItem's own ti_Data field, so writing eight bytes would
       * run four bytes past it and into the NEXT TagItem -- with no MMU, silent
       * corruption of the caller's tag list. So a by-value request fails the
       * call instead. (Same 64-bit shape as IFQ_GetBytesIn/Out, where writing
       * only 32 bits once produced the impossible "16,375,418 bytes" reading.)
       */
      case (SBTC_GET_BYTES_RECEIVED << SBTB_CODE):
      case (SBTC_GET_BYTES_SENT << SBTB_CODE):
	{
	  extern void ng_stack_byte_total(int out, ULONG *hip, ULONG *lop);
	  ULONG hi, lo;

	  if (!((UWORD)tag & SBTF_REF))
	    return errIndex;
	  ng_stack_byte_total(((UWORD)tag & ~SBTF_REF) ==
			      (SBTC_GET_BYTES_SENT << SBTB_CODE), &hi, &lo);
	  tagData[0] = hi;		/* sbq_High */
	  tagData[1] = lo;		/* sbq_Low  */
	}
	break;

      /*
       * PORT (AmiTCP_NG): honestly answer Roadshow's extension-API capability
       * queries. A Roadshow-aware client probes these (GET only) to decide which
       * extension families it may use; the original fell through to `default`
       * and FAILED the whole SocketBaseTagList call on any unknown code, which
       * some tools treat as a hard error. Return 1 for a family we implement, 0
       * for one still stubbed -- so clients cleanly use what works and skip the
       * rest. Flip a 0 to 1 as each tranche lands (see api/amiga_roadshow_compat.c and
       * api/amiga_libtables.c).
       */
      case (SBTC_HAVE_ADDRESS_CONVERSION_API << SBTB_CODE):
	*tagData = 1;			/* inet_aton/ntop/pton, In_Local/CanForward */
	break;
      case (SBTC_HAVE_DNS_API            << SBTB_CODE):
	*tagData = 1;			/* Add/Remove/Obtain/ReleaseDomainNameServer(List) */
	break;
      case (SBTC_HAVE_GETHOSTADDR_R_API << SBTB_CODE):
	*tagData = 1;			/* gethostbyname_r / gethostbyaddr_r */
	break;
      case (SBTC_HAVE_STATUS_API         << SBTB_CODE):
	*tagData = 1;			/* GetNetworkStatistics */
	break;
      case (SBTC_HAVE_ROUTING_API       << SBTB_CODE):
	*tagData = 1;			/* Add/DeleteRouteTagList, Get/FreeRouteInfo */
	break;
      case (SBTC_HAVE_INTERFACE_API      << SBTB_CODE):
	*tagData = 1;			/* Add/Configure/Obtain/Release/Query/RemoveInterface */
	break;
      case (SBTC_HAVE_ROADSHOWDATA_API   << SBTB_CODE):
	*tagData = 1;			/* Obtain/Release/ChangeRoadshowData */
	break;
      case (SBTC_HAVE_KERNEL_MEMORY_API  << SBTB_CODE):
	*tagData = 1;			/* mbuf_get/gethdr/free/.../pullup */
	break;
      case (SBTC_HAVE_LOCAL_DATABASE_API << SBTB_CODE):
	*tagData = 1;			/* set/get/end {net,proto,serv}ent */
	break;
      case (SBTC_HAVE_MONITORING_API     << SBTB_CODE):
      case (SBTC_HAVE_SERVER_API         << SBTB_CODE):
	*tagData = 0;			/* not implemented yet -- stubbed */
	break;

      case (SBTC_NUM_PACKET_FILTER_CHANNELS << SBTB_CODE):
	/*
	 * Berkeley Packet Filter: report how many capture channels the bpf_*
	 * vectors provide. A non-zero answer is how a Roadshow-aware client
	 * (e.g. a pcap/tcpdump port) discovers that BPF capture is available.
	 */
	*tagData = NG_BPF_MAXCHAN;
	break;

      case (SBTC_SYSTEM_STATUS           << SBTB_CODE):
	/*
	 * GET-only: report what the stack currently has configured, as an
	 * SBSYSSTAT_* bitmask (interfaces up, resolver, routes, default route).
	 * Roadshow's GetNetStatus reads this to decide online/offline. A SET is
	 * meaningless here, so leave the caller's value untouched for a SET.
	 */
	if (!((UWORD)tag & SBTF_SET))
	  *tagData = ng_system_status(libPtr);
	break;

      /*
       * AmiTCP_NG-private diagnostics (GET-only): report the stack's live tuning so
       * GetNetStatus DEBUG can show what the running stack actually detected/selected,
       * versus what its own RAM walk implies. A SET is meaningless -- leave it untouched.
       */
      case (SBTC_NG_DETECTED_RAM  << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)ng_detected_ram;
	break;
      case (SBTC_NG_TCP_SENDSPACE << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)tcp_sendspace;
	break;
      case (SBTC_NG_TCP_RECVSPACE << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)tcp_recvspace;
	break;
      case (SBTC_NG_SB_MAX        << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)sb_max;
	break;
      case (SBTC_NG_LINK_SPEED    << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)ng_last_if_baudrate;
	break;

      /*
       * TCP header-prediction accounting. PREDACK + PREDDAT are the fast-path hits;
       * RCVTOTAL is every TCP segment delivered to tcp_input, so the difference is
       * the slow path. Reported raw and free-running -- the caller samples twice and
       * differences, which is what makes them useful during a transfer rather than
       * as a since-boot total dominated by whatever ran earlier.
       */
      case (SBTC_NG_TCP_PREDACK   << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)tcppredack;
	break;
      case (SBTC_NG_TCP_PREDDAT   << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)tcppreddat;
	break;
      case (SBTC_NG_TCP_RCVTOTAL  << SBTB_CODE):
	if (!((UWORD)tag & SBTF_SET)) *tagData = (ULONG)ng_tcp_rcvtotal();
	break;

      CASE_NG_GET( SBTC_NG_TCP_PCBMISS, tcppcbcachemiss );
      CASE_NG_GET( SBTC_NG_TCP_REASSFULL, tcpreassfull );
      CASE_NG_GET( SBTC_NG_TCP_PREDWIN, tcppredwin      );

      CASE_NG_GET( SBTC_NG_SOWK_CALLS,  ng_sowk_calls );
      CASE_NG_GET( SBTC_NG_SOWK_RCV,    ng_sowk_rcv   );
      CASE_NG_GET( SBTC_NG_SOWK_WAIT,   ng_sowk_wait  );
      CASE_NG_GET( SBTC_NG_SOWK_SEL,    ng_sowk_sel   );
      CASE_NG_GET( SBTC_NG_SOWK_ASYNC,  ng_sowk_async );

      /*
       * Why prediction rejected a segment -- one bucket per segment. They do
       * NOT sum to the slow-path total: segments dropped before prediction is
       * consulted never reach the attribution. See tcp_predict_miss().
       */
      CASE_NG_GET( SBTC_NG_TPM_STATE,   tcppm_state   );
      CASE_NG_GET( SBTC_NG_TPM_FLAGS,   tcppm_flags   );
      CASE_NG_GET( SBTC_NG_TPM_TSTAMP,  tcppm_tstamp  );
      CASE_NG_GET( SBTC_NG_TPM_SEQ,     tcppm_seq     );
      CASE_NG_GET( SBTC_NG_TPM_WIN,     tcppm_win     );
      CASE_NG_GET( SBTC_NG_TPM_REXMIT,  tcppm_rexmit  );
      CASE_NG_GET( SBTC_NG_TPM_DUPACK,  tcppm_dupack  );
      CASE_NG_GET( SBTC_NG_TPM_SACK,    tcppm_sack    );
      CASE_NG_GET( SBTC_NG_TPM_ACK,     tcppm_ack     );
      CASE_NG_GET( SBTC_NG_TPM_CWND,    tcppm_cwnd    );
      CASE_NG_GET( SBTC_NG_TPM_ACKDATA, tcppm_ackdata );
      CASE_NG_GET( SBTC_NG_TPM_REASS,   tcppm_reass   );
      CASE_NG_GET( SBTC_NG_TPM_SPACE,   tcppm_space   );
      CASE_NG_GET( SBTC_NG_TPM_ZEROWIN, tcppm_zerowin );
      CASE_NG_GET( SBTC_NG_TPM_ACKDUP,  tcppm_ackdup  );
      CASE_NG_GET( SBTC_NG_TPM_WINONLY, tcppm_winonly );

      default:
	/*
	 * PORT (AmiTCP_NG): an unknown tag code must NOT fail the whole call.
	 * Roadshow's own bsdsocket.library is lenient here, and its closed
	 * configuration tools probe capability/attribute codes this port may not
	 * implement. Returning an error index (as the original did) makes such a
	 * tool treat the probe as fatal -- or spin on it -- and it never gets as
	 * far as configuring the interface. Instead: a GET of an unknown code
	 * reports the item as absent/zero; a SET of an unknown code is ignored.
	 * Either way we continue with the rest of the tag list.
	 */
	if (!((UWORD)tag & SBTF_SET))
	  *tagData = 0;
	break;
      }
    }
    else {			/* TAG_USER not set */
      switch(tags->ti_Tag) {
      case TAG_IGNORE:
	break;
      case TAG_MORE:
	tags = (struct TagItem *)tags->ti_Data;
	errIndex++;
	continue;
      case TAG_SKIP:
	/*
	 * PORT (AmiTCP_NG): TAG_SKIP means "skip this item AND the next
	 * ti_Data items", so the walk must advance by 1 + ti_Data. This
	 * used to advance by a fixed 2 and never read ti_Data at all,
	 * which is right only for ti_Data == 1; any other value left the
	 * walk pointing at memory the caller had marked as NOT a tag
	 * item, to be reinterpreted as one. Snapshot ti_Data BEFORE
	 * moving tags -- reading it afterwards would take it from the
	 * wrong item. The shared increment below supplies the +1.
	 * See ng_nexttag() in amiga_roadshow_compat.c, which agrees.
	 */
	{
	  ULONG skip = tags->ti_Data;

	  tags += skip; errIndex += skip;
	}
	break;
      default:
        return errIndex;	/* fail */
      }
    }
    
    tags++; errIndex++;
  }
  
  return 0;
}

