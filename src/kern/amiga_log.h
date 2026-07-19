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
#define LOG_BUFS 4
#define LOG_BUF_LEN 128
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
