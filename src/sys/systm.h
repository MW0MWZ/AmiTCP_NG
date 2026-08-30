/*
 * $Id: systm.h,v 1.18 1994/02/15 23:15:49 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: systm.h,v $
 * Revision 1.18  1994/02/15  23:15:49  jraja
 * Added prototypes for cs_putchar() and csprintn(),
 * changed vlog() to return the length (unsigned long).
 *
 * Revision 1.17  1993/11/07  00:05:41  ppessi
 * Added csprintf(), vsprintf() and vcsprintf() functions.
 *
 * Revision 1.16  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.15  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.14  1993/04/28  13:01:01  puhuri
 * Fixed stdargs.h to stdarg.h
 *
 * Revision 1.13  93/04/27  10:27:58  10:27:58  puhuri (Markus Peuhkuri)
 * Add prototype for vlog().
 * 
 * Revision 1.12  93/03/12  23:57:26  23:57:26  ppessi (Pekka Pessi)
 * Fixed logging function prototypes
 * 
 * Revision 1.11  93/03/11  17:02:13  17:02:13  jraja (Jarno Tapio Rajahalme)
 * Changed back to c types, removed obsolete stuff.
 * 
 * Revision 1.10  93/03/10  17:16:57  17:16:57  puhuri (Markus Peuhkuri)
 * Fixed function prototypes (for kern/subr_prf.c/1.12)
 * 
 * Revision 1.9  93/03/05  03:29:43  03:29:43  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.8  93/03/03  15:50:23  15:50:23  jraja (Jarno Tapio Rajahalme)
 * Cleanup.
 * 
 * Revision 1.7  93/02/25  19:14:05  19:14:05  ppessi (Pekka Pessi)
 * 	Fixed bug with queue_node.
 * 
 * Revision 1.6  93/02/25  19:09:17  19:09:17  puhuri (Markus Peuhkuri)
 * fixed defination of log, panic, printf and add kprintf.
 * 
 * Revision 1.5  93/02/24  11:16:19  11:16:19  jraja (Jarno Tapio Rajahalme)
 * Changed path uxkern to kern.
 * 
 * Revision 1.4  93/02/04  18:22:16  18:22:16  jraja (Jarno Tapio Rajahalme)
 * Cleaned up _insque and _remque definitions.
 * Commented out some obsolete definitions.
 * 
 * Revision 1.3  93/01/06  19:16:57  19:16:57  jraja (Jarno Tapio Rajahalme)
 * Added inline definitions for _insque and _remque.
 * Also added #include <uxkern/amiga_subr.h> for bcmp(), bcopy() and bzero()
 * definitions.
 * 
 * Revision 1.2  92/11/20  15:57:06  15:57:06  jraja (Jarno Tapio Rajahalme)
 * Added #ifndef AMITCP's to make this compile.
 * 
 * Revision 1.1  92/11/20  15:48:10  15:48:10  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 */

/*-
 * Copyright (c) 1982, 1988, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)systm.h	7.17 (Berkeley) 5/25/91
 */

#ifndef SYS_SYSTM_H
#define SYS_SYSTM_H

/* Forward-declare the aggregate type named in the prototypes below (cs_putchar,
 * csprintf, vcsprintf), so those prototypes use the real tag instead of a
 * throwaway function-prototype-scope tag. Zero behaviour/ABI impact. */
struct CSource;

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif

#ifndef SYS_CDEFS_H
#include <sys/cdefs.h>		/* for the inlines */
#endif

#include <stdarg.h>

/*
 * queue node definition for _insque and _remque. _insque and _remque expect
 * to find this structure from the start of every node they handle.
 */

struct queue_node {
  struct queue_node *next;
  struct queue_node *prev;
};

/* casts to keep lint happy */
#define	insque(q,p)	_insque((struct queue_node *)q,(struct queue_node *)p)

static inline void 
_insque(register struct queue_node *node, register struct queue_node *pred)
{
  node->next = pred->next;
  node->prev = pred;
  (pred->next)->prev = node;
  pred->next = node;
}

#define	remque(q)	_remque((struct queue_node *)q)

static inline struct queue_node *
_remque(register struct queue_node *node)
{
  (node->next)->prev = node->prev;
  (node->prev)->next = node->next;
  return(node);
}

/*
 * General function declarations.
 */

void cs_putchar(unsigned char, struct CSource *);
void panic(const char *, ...);
void log(unsigned long, const char *, ...);

/*
 * SYNCHRONOUS diagnostic line -- see ng_diag() in kern/subr_prf.c.
 *
 * Use this, NOT log(), when you are trying to find out where something hangs.
 * log() queues to the NETTRACE task, so a wedge loses every message still in the
 * queue and the last line written is not the last line reached. ng_diag() writes
 * and flushes on the calling task before it returns.
 *
 * TASK LEVEL ONLY: it calls DOS. Never from an interrupt, a device completion, or
 * inside splnet()/splimp().
 */
#define NG_DIAG_FILE	"SYS:ngdiag.log"
void ng_diag(const char *, ...);
unsigned long vlog(unsigned long, const char *, va_list);

/*
 * PORT (AmiTCP_NG): logging controls, and the call-site fast path.
 *
 * log_enabled is the master switch and log_level the highest (least important)
 * syslog priority that gets recorded. They are independent on purpose: switching
 * logging off and back on must not lose the verbosity you chose, and "off" must
 * not be expressible as a priority number that already means something else.
 * LOGGING= and LOGLEVEL= in AmiTCP.config set them, and both are visible over
 * ARexx. Defined in kern/amiga_log.c.
 *
 * The macro is what makes it cheap to leave logging calls in the source. vlog()
 * filters too -- it has to, since it is also reachable through printf() -- but by
 * then the caller has already evaluated every argument and made the call. Several
 * log sites pass ntohl(), pointer arithmetic or inet_ntoa() results, and on a
 * 7MHz 68000 that is real work done purely to be thrown away. Testing here means
 * a switched-off log() costs one load, one compare and a not-taken branch, and
 * its arguments are never touched.
 *
 * 7 rather than LOG_PRIMASK on purpose: this header is included in translation
 * units that do not pull in <sys/syslog.h>, and the syslog priority encoding has
 * been three bits since 4.2BSD.
 *
 * The parentheses around (log) in the expansion suppress the macro, so this
 * calls the real function. kern/subr_prf.c defines it the same way.
 *
 * TWO CONSTRAINTS ON CALLERS, both of which every current call site satisfies:
 *   - `level` is evaluated TWICE, so it must have no side effects. Pass a LOG_*
 *     constant (or a side-effect-free expression, as accesscontrol.c does).
 *   - The expansion is a statement, not an expression. `log(...)` cannot be used
 *     for its value -- it has none -- and needs its trailing semicolon as usual.
 *
 * If a translation unit ever appears to ignore the level, check for a `#undef
 * log` reaching it after this header: kern/amiga_includes.h carried an
 * unconditional one for years (a SAS/C <math.h> workaround), which silently
 * disabled this fast path in most of the tree while still compiling and logging
 * correctly.
 */
extern long log_level;
extern long log_enabled;

#define log(level, ...)							\
  do {									\
    if (log_enabled && (long)((level) & 7) <= log_level)		\
      (log)((level), __VA_ARGS__);					\
  } while (0)
unsigned long printf(const char *, ...);
unsigned long sprintf(char * buf, const char * fmt, ...);
unsigned long csprintf(struct CSource* buf, const char * fmt, ...);
unsigned long vsprintf(char * buf, const char * fmt, va_list);
unsigned long vcsprintf(struct CSource* buf, const char * fmt, va_list);
char * csprintn(u_long n, int base, char *buf, int buflen);
int  ultoa(unsigned long ul, char *buffer);

#ifndef AMIGA_SUBR_H
#include <kern/amiga_subr.h>
#endif

#endif /* !SYS_SYSTM_H */
