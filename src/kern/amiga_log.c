/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: amiga_log.c,v 1.41 1994/05/02 19:59:24 jraja Exp $";
/* 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: amiga_log.c,v $
 * Revision 1.41  1994/05/02  19:59:24  jraja
 * Removed not-used local variable.
 *
 * Revision 1.40  1994/04/22  13:55:46  jraja
 * Fixed the bug in log file/console name change notify function:
 * NATTRACE's DosBase is no more used before initialization.
 *
 * Revision 1.39  1994/02/17  18:21:54  jraja
 * Changed vcsprintf to csprintf.
 *
 * Revision 1.38  1994/02/16  21:49:02  jraja
 * Changed log date formatting to use vcsprintf() with '32-bit' formatting.
 *
 * Revision 1.37  1994/01/23  22:04:57  jraja
 * Fixed void return on log_task().
 *
 * Revision 1.36  1994/01/19  21:39:17  jraja
 * Added Seek()ing to the end of the old log file if necessary.
 *
 * Revision 1.35  1994/01/05  10:07:36  jraja
 * Made ARexx port visible only after API is visible.
 * Moved IntuitionBase open&close to the panic() function.
 * Added static variables for the memory (addrs&sizes) allocated, so that
 * FreeMem() would not crash if configuration is changed.
 * General code & comments clean-up.
 *
 * Revision 1.34  1993/11/14  19:46:43  jraja
 * Fixed off-by-one errors in month & day names.
 *
 * Revision 1.33  1993/11/06  23:51:22  ppessi
 * Changed log message format. Converted to use csprintf where possible.
 *
 * Revision 1.31  1993/10/07  22:41:34  ppessi
 * Added time to the log message.
 *
 * Revision 1.30  1993/08/06  08:58:37  jraja
 * Set required bsdsocket.library version to 2.
 *
 * Revision 1.29  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.28  1993/06/01  08:38:04  puhuri
 * Fixed declaration of logOpen. (missing static)
 *
 * Revision 1.27  1993/05/27  16:43:26  puhuri
 * Made better file opening (now tries everything).
 * Add few commets.
 *
 * Revision 1.26  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.25  1993/05/05  16:10:02  puhuri
 * Fixes for final demo.
 *
 * Revision 1.24  93/05/04  12:41:31  12:41:31  puhuri (Markus Peuhkuri)
 * Fixed defination of stuffchar(), perdence errors.
 * 
 * Revision 1.23  93/05/03  18:47:48  18:47:48  puhuri (Markus Peuhkuri)
 * Add check to GetLogMsg to avoid calling GetMsg with NULL pointer
 * (So we can call log before NETTRACE is activated, it just does not 
 * success) It also tries to open socket.library when it gots CTRL-F.
 * 
 * Revision 1.22  93/04/29  22:02:04  22:02:04  puhuri (Markus Peuhkuri)
 * Moved configuration structure log_cnf to amiga_log.h
 * 
 * Revision 1.21  93/04/28  19:24:30  19:24:30  puhuri (Markus Peuhkuri)
 * Fixed variables to give reliable configuration.
 * 
 * Revision 1.20  93/04/26  20:34:28  20:34:28  puhuri (Markus Peuhkuri)
 * Converts control-charactes to spaces, Add ability to modify
 * console and log filenames.
 * 
 * Revision 1.19  93/04/26  18:52:07  18:52:07  puhuri (Markus Peuhkuri)
 * Moved closelog() from subr_prf inside of log_deinit() as it was the
 * only place where it was calld. (Was for historial reasons)
 * Moved closing of Dos earlier in log_close to avoid problems in closing.
 * 
 * Revision 1.18  93/04/23  02:27:44  02:27:44  ppessi (Pekka Pessi)
 * Number and length of logging messages made configreable.
 * 
 * Revision 1.17  93/04/21  19:05:26  19:05:26  puhuri (Markus Peuhkuri)
 * Removed panic()'s if log file couldn't be opened/wroted. Add comments.
 * Now uses new structure of log_msg.
 * 
 * Revision 1.16  93/04/20  18:38:01  18:38:01  puhuri (Markus Peuhkuri)
 * Fixed prototype of log_poll, logmask was number of signal, not mask.
 * pointers to *file were automatic variables, now static.
 * 
 * Revision 1.15  93/04/17  17:56:29  17:56:29  puhuri (Markus Peuhkuri)
 * Fixed few trivial errors from previous version. 
 * (Should compile before ci)
 * 
 * Revision 1.14  93/04/17  17:16:51  17:16:51  puhuri (Markus Peuhkuri)
 * Moved REXX-stuff to NETTRACE-task (from amiga_main)
 * 
 * Revision 1.13  93/04/06  15:15:30  15:15:30  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.12  93/03/20  07:07:11  07:07:11  ppessi (Pekka Pessi)
 * Fixed memory leak caused by task_remove()
 * 
 * Revision 1.11  93/03/12  23:59:17  23:59:17  ppessi (Pekka Pessi)
 * Prototype fixes.
 * 
 * Revision 1.10  93/03/09  13:29:26  13:29:26  puhuri (Markus Peuhkuri)
 * Now stores information of failed log attempts using new function
 * GetLogMsg. (Increments the number of failed, which is then printed
 * (and zeroed) as logging task gets next message (ts handles it))
 * 
 * Revision 1.9  93/03/05  21:10:51  21:10:51  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.8  93/03/05  12:26:58  12:26:58  jraja (Jarno Tapio Rajahalme)
 * Added CloseLibrary() calls to close logDOSBase on exit.
 * 
 * Revision 1.7  93/03/05  03:25:45  03:25:45  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.6  93/03/04  10:03:51  10:03:51  jraja (Jarno Tapio Rajahalme)
 * Fixed includes.
 * 
 * Revision 1.5  93/03/01  19:09:39  19:09:39  puhuri (Markus Peuhkuri)
 * Add variable.
 * 
 * Revision 1.4  93/02/26  19:42:46  19:42:46  puhuri (Markus Peuhkuri)
 * Modified to use MsgPort as list of free messages.
 * 
 * Revision 1.3  93/02/25  19:33:55  19:33:55  puhuri (Markus Peuhkuri)
 * Cleanup and consistency with other modules, removed amiga.lib stuff,
 * made compatible with SAS, log task has own DOSBase
 * 
 */

/*
 * amiga_log.c --- logging, plus the NETTRACE process and the ARexx control port.
 *
 * Two jobs live here. First, the stack's syslog-style logging (log(LOG_ERR, ...)):
 * messages are queued to a dedicated process so that even code running at spl /
 * interrupt time can log without touching DOS. Second, that same process --
 * NETTRACE -- owns the "AMITCP" ARexx port, which is how external tools control a
 * running stack (online/offline/ShowNetStatus send ARexx commands to it; stopnet
 * sends "KILL"). docs/ARCHITECTURE.md section 6.
 *
 * STARTUP HANDSHAKE (worth studying -- it is a clean example of CreateNewProc +
 * message rendezvous). log_init(), running in the main task:
 *   1. allocates the log-message pool and a reply MsgPort;
 *   2. CreateNewProcTags() spawns log_task() as a separate Process;
 *   3. Wait()s for the child to signal readiness.
 * log_task(), in the new Process: opens its OWN dos/utility bases (a spawned
 * Process does not inherit the parent's), creates its message port, initialises
 * the ARexx subsystem (rexx_init), then PutMsg()s the reply port to unblock the
 * parent. Only once both sides have rendezvoused is logging live.
 *
 * PORT (AmiTCP_NG) war story: this handshake is exactly where the ported stack
 * used to hang -- not here, but because the very first 32-bit multiply in log_init
 * crashed (bebbo's __mulsi3 -> utility.library through a NULL UtilityBase). See
 * amiga_main.c's utility.library note and PORTING.md. A good reminder that a hang
 * "in" a function can be caused by something earlier.
 *
 * Read log_init() then log_task().
 */

#include <conf.h>

#include <bsdsocket.library_rev.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>

#include <kern/amiga_includes.h>
#include <sys/syslog.h>
#include <kern/amiga_log.h>
#include <dos/rdargs.h>	/* PORT (AmiTCP_NG): struct CSource (AmigaDOS) definition */
#include <kern/amiga_rexx.h>
#include <utility/date.h>

#if __SASC 
#include <proto/dos.h>
#include <proto/utility.h>
#elif __GNUC__
#define DOSBase logDOSBase
#define UtilityBase logUtilityBase
#include <inline/dos.h>
#include <inline/utility.h>
#undef DOSBase
#else 
#error Compiler not supported!
#endif

struct MsgPort *logPort,*logReplyPort;

static struct Library *logDOSBase = NULL;
static struct Library *logUtilityBase = NULL;

extern struct Task *AmiTCP_Task;
extern struct DosLibrary *DOSBase;

extern void REGARGFUN stuffchar();
extern int logname_changed(void *p, LONG new);

static struct log_msg *log_poll(void);
static void log_task(void);
static void log_close(struct log_msg *msg);
static BPTR logOpen(STRPTR name);

static struct log_msg *log_messages = NULL;
static char *log_buffers = NULL;
static LONG log_messages_mem_size, log_buffers_mem_size;
static int GetLogMsgFail;


UBYTE consoledefname[] = _PATH_CON;
UBYTE logfiledefname[] = _PATH_LOG;
STRPTR logfilename = logfiledefname;
STRPTR consolename = consoledefname;


struct log_cnf log_cnf = { LOG_BUFS, LOG_BUF_LEN };

/*
 * Initialization function for the logging subsystem
 */

BOOL
log_init(void)
{
  struct Message *msg;
  int i;
  ULONG sig;

  if (logReplyPort)
    return(TRUE);		/* We're allready initialized */

  /*
   * Allocate buffers for log messages.
   *
   * Save lengths to static variables, since the configuration variables might
   * change.
   */
  log_messages_mem_size = sizeof(struct log_msg) * log_cnf.log_bufs;
  log_buffers_mem_size = log_cnf.log_bufs * log_cnf.log_buf_len * sizeof(char);
  if ((log_messages = AllocMem(log_messages_mem_size, MEMF_CLEAR|MEMF_PUBLIC)))
    if ((log_buffers = AllocMem(log_buffers_mem_size, MEMF_CLEAR|MEMF_PUBLIC))) {
      logPort = NULL; /* NETTRACE will set this on success */
      GetLogMsgFail = 0;

      if (logReplyPort = CreateMsgPort()) {
	/*
	 * Start the NETTRACE process
	 */
	if (CreateNewProcTags(NP_Entry, (LONG)&log_task,
			      NP_Name, (LONG)LOG_TASK_NAME,
			      NP_Priority, LOG_TASK_PRI,
			      TAG_DONE, NULL)) {
	  for (;;) {
	    /*
	     * Wait for a signal for success or failure
	     */
	    sig = Wait(1<<logReplyPort->mp_SigBit | SIGBREAKF_CTRL_F);

	    if (sig & SIGBREAKF_CTRL_F && logPort == (struct MsgPort *)-1) {
	      /* Initializion failed */
	      logPort = NULL;
	      break;
	    }
	    else if (msg = GetMsg(logReplyPort)) { /* Got message back? */
	      ReplyMsg(msg);
	      logReplyPort->mp_Flags = PA_IGNORE;
	      /* 
	       * Initialize messages
	       */
	      for (i = 0; i < log_cnf.log_bufs; i++) {
		log_messages[i].msg.mn_ReplyPort = logReplyPort;
		log_messages[i].msg.mn_Length = sizeof(struct log_msg);
		log_messages[i].level = 0;
		log_messages[i].string = (UBYTE *)(log_buffers+i*log_cnf.log_buf_len);
		log_messages[i].chars = 0;
		PutMsg(logReplyPort, (struct Message *)&log_messages[i]);
	      }
	      return(TRUE);	/* We're done */
	    }
	  }
	}
      }
    }
  /*
   * Something went wrong
   */
  log_deinit();
  return(FALSE);
}

/*
 * This function may be called only if no other tasks (applications) are 
 * accessing the logging system (the messages, to be exact).
 */
void 
log_deinit(void)
{
  struct log_msg *msg, *dump;

  if (logReplyPort) {		/* We have our port? */
    if (logPort) {		/* Logport exists? (=> NETTRACE is running) */
      /*
       * Turn on signalling on returned messages again
       */
      logReplyPort->mp_Flags = PA_SIGNAL;

      /*
       * Get a free message, Wait() for it if necessary
       */
      while((msg = (struct log_msg *)GetMsg(logReplyPort)) == NULL) 
	Wait(1<<logReplyPort->mp_SigBit);

      /* 
       * Initalize END_MESSAGE
       */
      msg->msg.mn_ReplyPort = logReplyPort;
      msg->msg.mn_Length = sizeof(struct log_msg);
      msg->level = LOG_CLOSE;

      PutMsg(logPort, (struct Message *)msg);
      
      for (;;) {
	dump = (struct log_msg *)GetMsg(logReplyPort);
	if (dump) {
	  if (dump->level == LOG_CLOSE)	/* got the Close message back */
	    break;		/* It was the last one */
	}	      
	else
	  Wait(1<<logReplyPort->mp_SigBit);
      }
    }

    /*
     * ensure that the port is empty
     */
    while(GetMsg(logReplyPort))
      ;
    DeleteMsgPort(logReplyPort);
    logReplyPort = NULL;
  }
  if (log_buffers) {
    FreeMem(log_buffers, log_buffers_mem_size);
    log_buffers = NULL;
  }
  if (log_messages) {
    FreeMem(log_messages, log_messages_mem_size);
    log_messages = NULL;
  }
}

/* A little stub for calling GetMsg w/ error reporting and cheking */
struct log_msg *GetLogMsg(struct MsgPort *port)
{
  struct Message *msg;

  if (port && (msg = GetMsg(port)))  /* Get a message */
    /* We should have a port, if not-> fail */
    return (struct log_msg *)msg; 

  ++GetLogMsgFail;		/* Increment number of failed messages */
  return NULL;
}

/*
 * Functions following these defines may be called from the NETTRACE
 * task ONLY! These defines cause the SAS/C to generate calls to
 * dos.library and utility.library using these bases, respectively.
 */
#define DOSBase logDOSBase
#define UtilityBase logUtilityBase

struct Library *SocketBase = NULL;
struct Task *Nettrace_Task = NULL;

/* Main loop for NETTRACE */
static void SAVEDS
log_task(void)
{
  struct log_msg *initmsg = NULL;
  ULONG rexxmask = 0, logmask = 0, sigmask = 0;

  Nettrace_Task = FindTask(NULL); /* Store task pointer for AmiTCP */

  /* We need our own DosBase */
  if ((logDOSBase = OpenLibrary((STRPTR)DOSNAME, 0L)) == NULL)
    goto fail;

  if ((logUtilityBase = OpenLibrary((STRPTR)"utility.library", 37L)) == NULL)
    goto fail;

  /* Allocate message to reply startup */
  if ((initmsg = AllocMem(sizeof(struct log_msg), MEMF_CLEAR|MEMF_PUBLIC))
      == NULL)
    goto fail;

  /* Create our port for log messages */
  if ((logPort = CreateMsgPort()) == NULL)
    goto fail;

  logmask = 1<<logPort->mp_SigBit;

  /*
   * Initialize rexx subsystem
   */
  if (!(rexxmask = rexx_init()))
    goto fail;

  /*
   * Syncronize with AmiTCP/IP
   */
  initmsg->msg.mn_ReplyPort = logPort;
  initmsg->msg.mn_Length = sizeof(struct log_msg);
  PutMsg(logReplyPort, (struct Message *)initmsg);
  do {
    Wait(logmask);
  } while(initmsg != (struct log_msg *)GetMsg(logPort));

  FreeMem(initmsg, sizeof(struct log_msg));
  initmsg = NULL;

  sigmask = logmask | rexxmask | SIGBREAKF_CTRL_F;
  
  /* 
   * Main loop of the NETTRACE
   */
  for (;;) {
    ULONG sig;
    struct log_msg *msg;

    sig = Wait(sigmask);	/* Wait for signals */
    do {
				/* Signal from the AmiTCP/IP: API ready */
      if ((sig & SIGBREAKF_CTRL_F) && (SocketBase == NULL)) {
	sig &= ~SIGBREAKF_CTRL_F;
	/*
	 * Open a base to our own library so that ARexx message handling
	 * can use socket functions.
	 * This name does not work with the "nthLibrary" system
	 */
	if (SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", VERSION)) {
	  /*
	   * Make our ARexx port public
	   */
	  rexx_show();
	  sigmask &= ~SIGBREAKF_CTRL_F;
	}
      }

      if (sig & logmask) {	/* Process log messages,
				 * handles all ones pending.
				 */
	if ((msg = log_poll())) {	/* Got an LOG_CLOSE-message? */
	  log_close(msg);
	  return;
	}
	sig &= ~logmask;
      }

      if (sig & rexxmask) {	/* One rexx message at time */
	if (SocketBase) {
	  if (!rexx_poll())
	    sig &= ~rexxmask;
	} else
	  sig &= ~rexxmask;
      }

      sig |= SetSignal(0L, sigmask) & sigmask; /* Signals left? */
    } while (sig);
  }

 fail:				
  /* Initializion Failed */
  if (initmsg)
    FreeMem(initmsg, sizeof(struct log_msg));

  log_close(NULL);

  logPort = (struct MsgPort *)-1;
  /* Inform AmiTCP that we failed */
  Signal(AmiTCP_Task, SIGBREAKF_CTRL_F); 
}

static BPTR confile = NULL;
static BPTR logfile = NULL;
static BOOL fileopenfail = FALSE, conopenfail = FALSE;
static BOOL filewritefail = FALSE, conwritefail = FALSE;

static char *months =
  "Jan\0Feb\0Mar\0Apr\0May\0Jun\0Jul\0Aug\0Sep\0Oct\0Nov\0Dec";

static char *wdays = 
  "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat";

static char *levels = 
  "emerg\0"
  "alert\0"
  "crit \0"
  "err  \0"
  "warn \0"
  "note \0"
  "info \0"
  "debug";

/* 
 * Process all pending log messages 
 */
static
struct log_msg *log_poll()
{     
  struct log_msg *msg;
  ULONG where;
  LONG i;
  struct ClockData clockdata;

  /* 28 for date, 14 for level */
# define LEVELBUF 28+14

  UBYTE buffer[LEVELBUF];
  static ULONG TotalFail;

  /* Process all messages */
  while (msg = (struct log_msg *)GetMsg(logPort)) { 
    struct CSource cs;

    /* All except LOG_EMERG to both log and console */
    where = TOLOG;
    if (msg->level != LOG_EMERG)
      where |= TOCONS;

    if (msg->level == LOG_CLOSE) {
      return (msg);
    }

    cs.CS_Buffer = buffer;
    cs.CS_Length = LEVELBUF;
    cs.CS_CurChr = 0;

    Amiga2Date(msg->time, &clockdata);

    csprintf(&cs, 
#ifdef HAVE_TIMEZONES
	     "%s %s %02d %02d:%02d:%02d %s %4d [%s]: ", 
#else
	     "%s %s %02d %02d:%02d:%02d %4d [%s]: ", 
#endif
	     wdays + 4 * clockdata.wday,
	     months + 4 * (clockdata.month - 1),
	     clockdata.mday,
	     clockdata.hour,
	     clockdata.min,
	     clockdata.sec,
#ifdef HAVE_TIMEZONES
	     "UCT",	/* Universal Coordinated Time */
#endif
	     clockdata.year,
	     levels + 6 * ((msg->level <= LOG_DEBUG) ? msg->level : LOG_DEBUG)
	     );

    /* Remove last newline */
    if (msg->chars > 0 && msg->string[msg->chars - 1] == '\n') {
      msg->chars--;
    }

    /* Replace all control chars with space */
    for (i = 0; i < msg->chars; ++i) {
      if ((msg->string)[i] < ' ')
	(msg->string)[i] = ' ';
    }

    if (where & TOCONS) {
      /* If console is not open, open it */
      while (confile == NULL) {
	if ((confile = logOpen(consolename)) == NULL) {
	  if (!conopenfail) /* log only once */
	    log(LOG_ERR,"Opening console log '%s' failed", consolename);
	  if (consolename == consoledefname) {
	    conopenfail = TRUE;	    
	    break;
	  }
	  /* try again with the default name */
	  consolename = consoledefname;
	  conopenfail = conwritefail = FALSE;
	}
      }
      if (confile != NULL) {
	int error = 
	  FPuts(confile, buffer) == -1 ||
	    FWrite(confile, msg->string, msg->chars, 1) != 1 ||
	      FPutC(confile, '\n') == -1;
	Flush(confile);
	if (error && !conwritefail) {	/* To avoid loops */
	  conwritefail = TRUE;
	  log(LOG_ERR, "log: Write failed to console '%s'", consolename);
	}
      }
    }
    if (where & TOLOG) {
      /* If log file is not open, open it */
      while (logfile == NULL) {
	if ((logfile = logOpen(logfilename)) == NULL) {
	  if (!fileopenfail) /* log only once */
	    log(LOG_ERR,"Opening log file '%s' failed", logfilename);
	  if (logfilename == logfiledefname) {
	    fileopenfail = TRUE;	    
	    break;
	  }
	  /* try again with the default name */
	  logfilename = logfiledefname;
	  fileopenfail = filewritefail = FALSE;
	}
      }
      if (logfile != NULL) {
	int error = 
	  FPuts(logfile, buffer) == -1 ||
	    FWrite(logfile, msg->string, msg->chars, 1) != 1 ||
	      FPutC(logfile, '\n') == -1;
	Flush(logfile);
	if (error && !filewritefail) {	/* To avoid loops */
	  filewritefail = TRUE;
	  log(LOG_ERR, "log: Write failed to file '%s'", logfilename);
	}
      }
    }

    ReplyMsg((struct Message *)msg);
    if (GetLogMsgFail != TotalFail) {
      int t = GetLogMsgFail;	/* Check if we have lost messages */

      log(LOG_WARNING,"%ld log messages lost (total %ld lost)\n",
	  t - TotalFail, TotalFail);
      TotalFail = t;
    }
  }
  return(NULL);
}  

/* Close logging subsystem */
static
void log_close(struct log_msg *msg)
{
  rexx_deinit();
  if (confile)
    Close(confile);
  if (logfile)
    Close(logfile);
  if (logUtilityBase) {
    CloseLibrary(logUtilityBase);
    logUtilityBase = NULL;
  }
  if (logDOSBase) {		/* DOS not needed below */
    CloseLibrary(logDOSBase);
    logDOSBase = NULL;
  }
  if (SocketBase) {
    CloseLibrary((struct Library *)SocketBase);
    SocketBase = NULL;
  }
  /*
   * Make sure that we get to end before task switch
   * and do not get messages from interrupts
   */
  Disable();

  if (logPort) {
    struct Message *m;

    while (m = GetMsg(logPort))	/* Check for messages and reply */
      ReplyMsg(m);

    DeleteMsgPort(logPort);	/* Delete port */
    logPort = NULL;
  }

  if (msg)
    ReplyMsg((struct Message *)msg);

  Nettrace_Task = NULL;

  /*
   * Interrupts are left disabled, 
   * they will be enabled again when this process dies 
   */
}

/*
 * Try first open w/ shared lock, then as an old file and finally as a new file
 */
static
BPTR logOpen(STRPTR name)
{
  BPTR file;

  if ((file = Open(name, MODE_READWRITE)) ||
      (file = Open(name, MODE_OLDFILE)))
    Seek(file, 0, OFFSET_END);
  else
    file = Open(name, MODE_NEWFILE);
 
  return file;
}

/*
 * This function might be called by either AmiTCP or NETTRACE. If the
 * call is done by the AmiTCP, no DOS calls may be done, since the
 * DosBase used by these functions is the one of the NETTRACE, and is
 * not initialized at that time!
 */
int logname_changed(void *p, LONG new)
{
  if (p == &logfilename) {	/* Is logname requested? */
     /*
      * logfile may be non-NULL only if the NETTRACE is already initialized
      */
    if (logfile != NULL) {
      Close(logfile);
      logfile = NULL;
    }
    fileopenfail = filewritefail = FALSE;
    /*
     * setvalue() (who called this) will set the new value when we return 
     * TRUE.
     */
    return TRUE;
  }

  if ( p == &consolename ) { /* Name of the console log */
    
    if (confile) { /* only if NETTRACE is already initialized */
      Close(confile);
      confile = NULL;
    }
    conopenfail = conwritefail = FALSE;
    /*
     * setvalue() (who called this) will set the new value when we return 
     * TRUE.
     */
    return TRUE;
  }

  /*
   * Some invalid pointer
   */
  return FALSE;
}
