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
      libPtr->errnoSize = size;
      libPtr->errnoPtr = err_p;
      return 0;
    }
  }

  writeErrnoValue(libPtr, EINVAL);
  return -1;
}

VOID SAVEDS RAF4(_Syslog,
		 struct SocketBase *,	libPtr, 	a6,
		 ULONG,			pri,		d0,
		 const char *,		fmt,		a0,
		 va_list,		ap,		a1)
#if 0
{
#endif
  int saved_errno;
  char fmt_cpy[1024];
  register char *p = fmt_cpy;

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

  /* 
   * Build the new format string
   */
  if (libPtr->LogTag) {
    p += sprintf(p, libPtr->LogTag);
  }
  if (libPtr->LogStat & LOG_PID) {
    p += sprintf(p, "[%lx]", libPtr->thisTask);
  }
  if (libPtr->LogTag) {
    *p++ = ':';
    *p++ = ' ';
  }
  /*
   * Substitute error message for %m.
   */
  {
    char ch, *t1;
    const char *t2;

    for (t1 = p; (ch = *fmt); ++fmt) {
      if (ch == '%') {
	if (fmt[1] == '%') {
	  ++fmt;
	  *t1++ = ch;
	}
	else if (fmt[1] == 'm') {
	  ++fmt;
	  for (t2 = __sys_errlist[saved_errno]; (*t1 = *t2++); ++t1)
	    ;
	  continue;
	}
      }
      *t1++ = ch;
    }
    *t1 = '\0';
  }

  vlog(pri, fmt_cpy, ap);
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
  
  FreeMem(libPtr->dTable, oldsize * sizeof (struct socket *) +
	  olddmasksize * sizeof (fd_mask));
  
  libPtr->dTable = dTable;
  libPtr->dTableSize = size;
  
  return size;
}


#define CASE_LONG(code, baseField)\
 case (code << SBTB_CODE): /* get */ \
  *tagData = (ULONG)libPtr->baseField;\
  break;\
 case (code << SBTB_CODE) | SBTF_SET: /* set */\
  *(ULONG *)&libPtr->baseField = *tagData;\
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

#define CASE_FLAG(code, flag)\
  case (code << SBTB_CODE): /* get */ \
    *tagData = ((ULONG)libPtr->flags & (flag)) != 0;\
    break;\
  case (code << SBTB_CODE) | SBTF_SET: /* set */\
    if (*tagData) \
      *(UBYTE *)&libPtr->flags |= (flag);\
    else \
      *(UBYTE *)&libPtr->flags &= ~(flag);\
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

/* SBTC_SYSTEM_STATUS helper -- computes the SBSYSSTAT_* bitmask (api/amiga_roadshow.c). */
extern ULONG ng_system_status(struct SocketBase *libPtr);

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

  static const char * const strErr = "Errlist lookup error";

  CHECK_TASK();

  while((tag = tags->ti_Tag) != TAG_END) {
    if ((LONG)tag < 0) {		/* TAG_USER is the sign bit */
      /* get pointer to the actual data */
      tagData = ((UWORD)tag & SBTF_REF) ?
	(ULONG *)tags->ti_Data : &tags->ti_Data;

      switch ((UWORD)tag & ~SBTF_REF) {

      CASE_LONG( SBTC_BREAKMASK,  sigIntrMask );

      CASE_LONG( SBTC_SIGIOMASK,  sigIOMask );

      CASE_LONG( SBTC_SIGURGMASK, sigUrgMask );

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
        *libPtr->hErrnoPtr = (LONG) *tagData;
	break;

      case (SBTC_DTABLESIZE << SBTB_CODE): /* get */
	*tagData = (ULONG)libPtr->dTableSize;
	break;
      case (SBTC_DTABLESIZE << SBTB_CODE) | SBTF_SET: /* set */
	if ((tmp = (WORD)*tagData) > 0)
	  setdtablesize(libPtr, tmp);
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
       * PORT (AmiTCP_NG): honestly answer Roadshow's extension-API capability
       * queries. A Roadshow-aware client probes these (GET only) to decide which
       * extension families it may use; the original fell through to `default`
       * and FAILED the whole SocketBaseTagList call on any unknown code, which
       * some tools treat as a hard error. Return 1 for a family we implement, 0
       * for one still stubbed -- so clients cleanly use what works and skip the
       * rest. Flip a 0 to 1 as each tranche lands (see api/amiga_roadshow.c and
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
	tags++; errIndex++;
	break;
      default:
        return errIndex;	/* fail */
      }
    }
    
    tags++; errIndex++;
  }
  
  return 0;
}

