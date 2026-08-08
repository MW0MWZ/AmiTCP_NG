/*
 * $Id: syslog.h,v 1.18 1994/03/17 04:21:50 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: syslog.h,v $
 * Revision 1.18  1994/03/17  04:21:50  jraja
 * Added INACTIVE switch to the log window parameters.
 * (Suggested by Carsten Heyl.)
 *
 * Revision 1.17  1994/01/24  07:41:10  jraja
 * Added prototypes for the openlog(), closelog() and setlogmask().
 *
 * Revision 1.16  1994/01/06  22:11:25  jraja
 * Removed the unnecessary #ifndef AMITCP lines.
 *
 * Revision 1.15  1994/01/06  22:05:08  jraja
 * Added inclusion of bsdsocket.h if necessary, cleaned the log.
 *
 * Revision 1.10  93/04/27  18:17:47  18:17:47  puhuri (Markus Peuhkuri)
 * Changed default names of console and log.
 * 
 */

/*
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
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
 *	@(#)syslog.h	7.20 (Berkeley) 2/23/91
 */

#ifndef SYS_SYSLOG_H
#define SYS_SYSLOG_H

/*
 * PORT (AmiTCP_NG): the default log lives on RAM:, not T:.
 *
 * T: is an ASSIGN, normally to RAM:T, made by the Startup-Sequence. The
 * self-starting LIBS:bsdsocket.library comes up on the first library call from
 * ANY program, which can be earlier in the boot than that assign -- and the stack
 * process runs with pr_WindowPtr = -1 (amiga_main.c), so a missing assign fails
 * the Open() silently instead of putting up a requester. RAM: needs no assign,
 * always exists, and is the same physical place T: usually points at anyway.
 */
#define	_PATH_LOG	"ram:AmiTCP.log"
#define _PATH_CON       "con:0/0/600/100/AmiTCPIP Log/AUTO/INACTIVE"

/*
 * PORT (AmiTCP_NG): fallback log destinations, tried in order when the
 * configured one cannot be opened at bring-up (log_validate_dest()).
 *
 * These matter most when the user has set LOGFILENAME= to somewhere that is not
 * there yet (an unmounted volume, a drawer that does not exist) -- including the
 * disk destination this stack's own config file suggests for a log that survives
 * a reboot. Without them that is an unreachable log with no way to tell it apart
 * from "nothing was logged" -- exactly the state this fork was found in.
 *
 * ram: stays in the list even though it is also the default: it needs no assign
 * and always exists, so it must remain reachable when an override fails.
 */
#define _PATH_LOG_FALLBACKS \
	"ram:AmiTCP.log", "t:AmiTCP.log", "AmiTCP:AmiTCP.log", "sys:AmiTCP.log"


/*
 * priorities/facilities are encoded into a single 32-bit quantity, where the
 * bottom 3 bits are the priority (0-7) and the top 28 bits are the facility
 * (0-big number).  Both the priorities and the facilities map roughly
 * one-to-one to strings in the syslogd(8) source code.  This mapping is
 * included in this file.
 *
 * priorities (these are ordered)
 */
#define	LOG_EMERG	0	/* system is unusable */
#define	LOG_ALERT	1	/* action must be taken immediately */
#define	LOG_CRIT	2	/* critical conditions */
#define	LOG_ERR		3	/* error conditions */
#define	LOG_WARNING	4	/* warning conditions */
#define	LOG_NOTICE	5	/* normal but significant condition */
#define	LOG_INFO	6	/* informational */
#define	LOG_DEBUG	7	/* debug-level messages */

/*
 * PORT (AmiTCP_NG): default logging verbosity -- the highest (least important)
 * priority that will actually be recorded. Everything above it is discarded at
 * the call site (see the log() macro in sys/systm.h), before the arguments are
 * even evaluated.
 *
 * Logging is compiled into EVERY build -- there is no build without it and no
 * flag that removes it -- and it is on by default, quietly, with the console
 * window off. Three separate switches, because they are three separate
 * questions:
 *
 *   LOGGING=ON|OFF     is anything recorded at all           (default ON)
 *   LOGLEVEL=0..7      how much                              (default 5, below)
 *   LOGCONSOLE=ON|OFF  is it ALSO thrown at a CON: window    (default OFF)
 *
 * Splitting the console out is what lets the first default be ON. While one
 * switch controlled both, "record errors so a panic leaves a trace" and "put a
 * window in front of the user" were the same request, so the only way to stop
 * the window was to stop recording -- and a machine that then died had nothing
 * to show for it. Apart is strictly better: a quiet file that is always there,
 * and a window only when asked for.
 *
 * The rest of the design:
 *   - Nothing is compiled out, so any machine can produce a full debug log by
 *     editing one line of AmiTCP.config. No special build, no hand-delivered
 *     binary, nothing that depends on reaching the machine.
 *   - The console is opened LAZILY, on the first message actually bound for it,
 *     so LOGCONSOLE=OFF costs no window, no CON: handler and no DOS Open().
 *
 * There is deliberately NO per-build variation. An earlier revision made a
 * -beta default to LOG_DEBUG, on the theory that testers want everything; that
 * put the diagnostic behaviour of the binary in the hands of a build flag rather
 * than the user, which is exactly the arrangement that once shipped a "ready for
 * diagnosis" beta with its tracer silently switched off. One default, everywhere,
 * and the user decides.
 *
 * The level below is only what applies once LOGGING=ON; LOGLEVEL= overrides it.
 */
#define NG_LOG_LEVEL_DEFAULT	LOG_NOTICE	/* 5 -- drops info + debug */

/*
 * PORT (AmiTCP_NG): note there is deliberately no "LOGLEVEL=0 means off". 0 is
 * LOG_EMERG, the priority panic() logs at; making it mean "off" would silently
 * discard the one message you most need, and would be indistinguishable in a
 * config file from someone asking for emergencies only. Off is its own switch,
 * LOGGING= -- see log_enabled in kern/amiga_log.c.
 */

#define	LOG_PRIMASK	0x07	/* mask to extract priority part (internal) */
				/* extract priority */
#define	LOG_PRI(p)	((p) & LOG_PRIMASK)
#define	LOG_MAKEPRI(fac, pri)	(((fac) << 3) | (pri))

#ifdef SYSLOG_NAMES
#define	INTERNAL_NOPRI	0x10	/* the "no priority" priority */
				/* mark "facility" */
#define	INTERNAL_MARK	LOG_MAKEPRI(LOG_NFACILITIES, 0)
typedef struct _code {
	char	*c_name;
	int	c_val;
} CODE;

CODE prioritynames[] = {
	"alert",	LOG_ALERT,
	"crit",		LOG_CRIT,
	"debug",	LOG_DEBUG,
	"emerg",	LOG_EMERG,
	"err",		LOG_ERR,
	"error",	LOG_ERR,		/* DEPRECATED */
	"info",		LOG_INFO,
	"none",		INTERNAL_NOPRI,		/* INTERNAL */
	"notice",	LOG_NOTICE,
	"panic", 	LOG_EMERG,		/* DEPRECATED */
	"warn",		LOG_WARNING,		/* DEPRECATED */
	"warning",	LOG_WARNING,
	NULL,		-1,
};
#endif

/* facility codes */
#define	LOG_KERN	(0<<3)	/* kernel messages */
#define	LOG_USER	(1<<3)	/* random user-level messages */
#define	LOG_MAIL	(2<<3)	/* mail system */
#define	LOG_DAEMON	(3<<3)	/* system daemons */
#define	LOG_AUTH	(4<<3)	/* security/authorization messages */
#define	LOG_SYSLOG	(5<<3)	/* messages generated internally by syslogd */
#define	LOG_LPR		(6<<3)	/* line printer subsystem */
#define	LOG_NEWS	(7<<3)	/* network news subsystem */
#define	LOG_UUCP	(8<<3)	/* UUCP subsystem */
#define	LOG_CRON	(9<<3)	/* clock daemon */
#define	LOG_AUTHPRIV	(10<<3)	/* security/authorization messages (private) */

	/* other codes through 15 reserved for system use */
#define	LOG_LOCAL0	(16<<3)	/* reserved for local use */
#define	LOG_LOCAL1	(17<<3)	/* reserved for local use */
#define	LOG_LOCAL2	(18<<3)	/* reserved for local use */
#define	LOG_LOCAL3	(19<<3)	/* reserved for local use */
#define	LOG_LOCAL4	(20<<3)	/* reserved for local use */
#define	LOG_LOCAL5	(21<<3)	/* reserved for local use */
#define	LOG_LOCAL6	(22<<3)	/* reserved for local use */
#define	LOG_LOCAL7	(23<<3)	/* reserved for local use */

#define	LOG_NFACILITIES	24	/* current number of facilities */
#define	LOG_FACMASK	0x03f8	/* mask to extract facility part */
				/* facility of pri */
#define	LOG_FAC(p)	(((p) & LOG_FACMASK) >> 3)


#ifdef SYSLOG_NAMES
CODE facilitynames[] = {
	"auth",		LOG_AUTH,
	"authpriv",	LOG_AUTHPRIV,
	"cron", 	LOG_CRON,
	"daemon",	LOG_DAEMON,
	"kern",		LOG_KERN,
	"lpr",		LOG_LPR,
	"mail",		LOG_MAIL,
	"mark", 	INTERNAL_MARK,		/* INTERNAL */
	"news",		LOG_NEWS,
	"security",	LOG_AUTH,		/* DEPRECATED */
	"syslog",	LOG_SYSLOG,
	"user",		LOG_USER,
	"uucp",		LOG_UUCP,
	"local0",	LOG_LOCAL0,
	"local1",	LOG_LOCAL1,
	"local2",	LOG_LOCAL2,
	"local3",	LOG_LOCAL3,
	"local4",	LOG_LOCAL4,
	"local5",	LOG_LOCAL5,
	"local6",	LOG_LOCAL6,
	"local7",	LOG_LOCAL7,
	NULL,		-1,
};
#endif

#ifdef KERNEL
#define	LOG_PRINTF	-1	/* pseudo-priority to indicate use of printf */
#endif

/*
 * arguments to setlogmask.
 */
#define	LOG_MASK(pri)	(1 << (pri))		/* mask for one priority */
#define	LOG_UPTO(pri)	((1 << ((pri)+1)) - 1)	/* all priorities through pri */

/*
 * Option flags for openlog.
 *
 * LOG_ODELAY no longer does anything.
 * LOG_NDELAY is the inverse of what it used to be.
 */
#define	LOG_PID		0x01	/* log the pid with each message */
#define	LOG_CONS	0x02	/* log on the console if errors in sending */
#define	LOG_ODELAY	0x04	/* delay open until first syslog() (default) */
#define	LOG_NDELAY	0x08	/* don't delay open */
#define	LOG_NOWAIT	0x10	/* don't wait for console forks: DEPRECATED */
#define	LOG_PERROR	0x20	/* log to stderr as well */

#ifndef KERNEL
void openlog(const char *, int, int);
void closelog(void);
int setlogmask(int);
/*
 * Include protos/inlines/pragmas for the syslog()
 * (+ all other AmiTCP functions)
 */
#ifndef BSDSOCKET_H
#include <bsdsocket.h>
#endif
#endif /* !KERNEL */

#endif /* !SYS_SYSLOG_H */
