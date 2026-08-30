/*
 * $Id: amiga_log.h,v 1.19 1994/01/05 10:02:50 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: amiga_log.h,v $
 * Revision 1.19  1994/01/05  10:02:50  jraja
 * Removed external definition of the log_message.
 *
 * Revision 1.18  1993/11/06  23:51:22  ppessi
 * Removed struct stuffchar.
 *
 * Revision 1.17  1993/10/07  22:41:34  ppessi
 * Added time to the log message.
 *
 * Revision 1.16  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.15  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.14  1993/05/05  16:10:05  puhuri
 * Fixes for final demo.
 *
 * Revision 1.13  93/04/29  22:03:40  22:03:40  puhuri (Markus Peuhkuri)
 * replaced distinct configuration variables with structure,
 * 
 * Revision 1.12  93/04/28  12:58:42  12:58:42  puhuri (Markus Peuhkuri)
 * Add defination for configuration variables.
 * 
 * Revision 1.11  93/04/26  18:54:29  18:54:29  puhuri (Markus Peuhkuri)
 * Removed declaration of closelog() as such function does not exists any more.
 * 
 * Revision 1.10  93/04/23  02:28:44  02:28:44  ppessi (Pekka Pessi)
 * Number and length of logging messages made configreable.
 * 
 * Revision 1.9  93/04/21  19:11:21  19:11:21  puhuri (Markus Peuhkuri)
 * Moved some constants from sys/syslog.h.
 * 
 * Revision 1.8  93/04/17  17:17:51  17:17:51  puhuri (Markus Peuhkuri)
 * Changed the name of logging task to NETTRACE
 * 
 * Revision 1.7  93/03/09  13:32:55  13:32:55  puhuri (Markus Peuhkuri)
 * Now stores information of failed log attempts using new function
 * GetLogMsg. (Increments the number of failed, which is then printed
 * (and zeroed) as logging task gets next message (ts handles it))
 * 
 * Revision 1.6  93/03/05  03:25:51  03:25:51  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.5  93/03/04  09:43:29  09:43:29  jraja (Jarno Tapio Rajahalme)
 * Fixed includes.
 * 
 * Revision 1.4  93/03/01  19:10:08  19:10:08  puhuri (Markus Peuhkuri)
 * Add variable buuffer
 * 
 * Revision 1.3  93/02/25  19:36:30  19:36:30  puhuri (Markus Peuhkuri)
 * Protected for multitime include, functions redefined,
 * AMITCP-name removed and struct log_msg moved from sys/syslog.h
 * 
*/
#ifndef KERN_AMIGA_LOG_H
#define KERN_AMIGA_LOG_H

#define LOG_TASK_NAME "NETTRACE"
#define LOG_TASK_PRI 4
/*
 * PORT (AmiTCP_NG): log message pool -- COUNT and per-line LENGTH.
 *
 * Upstream shipped 4 buffers of 128 bytes, and both were too small in a way
 * that is invisible from the log itself:
 *
 *   LENGTH -- vlog() formats into a CSource of exactly log_buf_len bytes
 *   (kern/subr_prf.c). stuffChar() silently DISCARDS every character past the
 *   end and the terminator costs one more, so a line was cut at 127 characters
 *   with no ellipsis and no marker -- it simply stopped. Since it is the
 *   FORMATTED length that counts, a short format string still truncated as soon
 *   as a %s substituted a device name or a full path, which is exactly when the
 *   line mattered. (The timestamp/level prefix is written separately from
 *   LEVELBUF and does not eat into this.)
 *
 *   COUNT -- vlog() drops the message entirely when no buffer is free, and the
 *   "N log messages lost" accounting only reports once a later message gets
 *   through. Four in flight is nothing for a burst, so a fault that logs hard
 *   (the case worth reading) is the case most likely to lose its own evidence.
 *
 * These are the FALLBACK values. ng_ram_tier() (kern/amiga_main.c) overwrites
 * log_cnf on both init paths before log_init() consumes it, so the real sizes
 * scale with installed RAM -- a 512 KB A500 must not pay a big machine's price.
 * They are still raised here so the fallback alone is not a truncating one.
 */
#define LOG_BUFS 32
#define LOG_BUF_LEN 512
#define TOCONS	0x01
#define TOTTY	0x02
#define TOLOG	0x04
#define END_LOG -1

/*
 * Configuration structure
 */ 
struct log_cnf {
  u_long log_bufs;
  u_long log_buf_len;
}; 
extern struct log_cnf log_cnf;

/*
 * These are options to config log
 */
#define LOG_CLOSE 0xff000000
#define LOG_CONFILE 0xfe000000
#define LOG_LOGFILE 0xfd000000
#define LOG_PORTOPEN 0xfc000000
#define LOG_PORTCLOSE 0xfb000000
#define LOG_CONGIF 0xff000000

extern struct Task *Nettrace_Task;
extern struct Process *logProc;
extern BOOL log_init(void);
extern void log_deinit(void);
extern struct log_msg *GetLogMsg(struct MsgPort *);

extern struct MsgPort *logReplyPort;
extern struct MsgPort *logPort;

/*
 * PORT (AmiTCP_NG): the logging controls -- LOGGING= (master switch) and
 * LOGLEVEL= (highest priority recorded, LOG_EMERG 0 .. LOG_DEBUG 7). Defined in
 * amiga_log.c; also declared in <sys/systm.h>, which is where the log() macro
 * that reads them on the hot path lives.
 */
extern LONG log_enabled;
extern LONG log_level;

/* The log destination actually opened -- may differ from logfilename when
 * log_validate_dest() had to fall back. Defined in amiga_log.c. */
extern STRPTR log_dest_name;
/* PORT (AmiTCP_NG): SBTC_LOG_HOOK. When non-NULL, the LOG TASK hands each
 * message to this hook instead of writing it to the file and console. Delivered
 * from there, never from vlog(), because vlog() runs under splnet() (=Forbid) at
 * many call sites -- see ng_log_hook_deliver() in kern/subr_prf.c. Stack-wide. */
extern struct Hook *log_hook;
/* Returns TRUE if the hook consumed the message (skip file + console). */
extern int ng_log_hook_deliver(struct log_msg *msg);

struct log_msg {
  struct Message msg;		/* Standard Exec message */
  ULONG level;			/* Level of log message */
  UBYTE * string;		/* Pointer to string */
  ULONG chars;			/* Length of string */
  ULONG time;			/* Logging time */
};

extern struct log_msg *log_message;
extern STRPTR consolename, logfilename;
extern struct log_cnf log_cnf;

/* extern void stuffchar(...);*/

#endif /* KERN_AMIGA_LOG_H */
