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
static void log_validate_dest(void);	/* PORT (AmiTCP_NG), NETTRACE only */

static struct log_msg *log_messages = NULL;
static char *log_buffers = NULL;
static LONG log_messages_mem_size, log_buffers_mem_size;
static int GetLogMsgFail;

/*
 * PORT (AmiTCP_NG): the two logging controls. Both are plain LONGs rather than
 * compile-time constants because both are settable at runtime -- from
 * AmiTCP.config at bring-up (LOGGING=, LOGLEVEL=) and over ARexx thereafter.
 *
 * log_enabled is the master switch (LOGGING=ON|OFF). It is separate from the
 * level on purpose: turning logging off and back on must not lose the verbosity
 * you had chosen, and "off" must not be expressible as a priority number that
 * also means something else (0 is LOG_EMERG, what panic() logs at).
 *
 * ON by default -- an earlier version of this comment said "OFF by default in
 * every build" two paragraphs above the sentence that says the opposite, and the
 * code has always agreed with this one (log_enabled = 1). It is safe to default
 * on because of log_console_enabled below, which is what makes that acceptable.
 * The two used to be one switch, and the console is the intrusive
 * half: it opens a window that puts itself in front of whatever the user is
 * doing. Tying the log FILE to that meant the only way to stop the window was to
 * stop recording anything, so a machine that hit a panic had nothing to show for
 * it. Split, the sensible default falls out on its own -- a quiet file that is
 * there when something breaks, and no window unless asked.
 *
 * log_level is the highest (least important) priority recorded. Read on the hot
 * path by the log() macro in sys/systm.h -- keep both cheap to load.
 */
LONG log_enabled = 1;

/*
 * PORT (AmiTCP_NG): LOGCONSOLE=ON|OFF, the console half of logging, OFF by
 * default. Purely about the extra copy that goes to a CON: window; it does not
 * gate the log file, and it does not gate whether messages are formatted -- a
 * message is written to the file either way.
 *
 * The window is opened LAZILY, on the first message that is actually destined
 * for it (log_poll()), so leaving this off costs no window, no CON: handler and
 * no DOS Open() at all.
 */
LONG log_console_enabled = 0;
LONG log_level = NG_LOG_LEVEL_DEFAULT;

/*
 * PORT (AmiTCP_NG): result of the bring-up validation of the log destination.
 * log_open_error holds the IoErr() from the LAST failed Open(); it is reported in
 * the banner when a fallback had to be used, and is the only trace left if every
 * candidate failed (in which case there is, by definition, nowhere to log it).
 */
static LONG log_open_error = 0;

/*
 * PORT (AmiTCP_NG): the destination ACTUALLY in use, which is not always the one
 * that was configured -- log_validate_dest() falls back when the configured name
 * cannot be opened.
 *
 * It is deliberately a separate variable rather than just reassigning logfilename.
 * logfilename is owned by the configuration layer: setvalue() (kern/amiga_config.c)
 * bsd_malloc()s each value it stores and sets VF_FREE, meaning "bsd_free() the old
 * value before replacing it". Pointing logfilename at one of the fallback string
 * LITERALS would leave VF_FREE set on a pointer Exec never allocated, and the next
 * LOGFILENAME= would call FreeVec() on it -- which reads a size header from below a
 * literal and links the result into the free-memory list. With no MMU that is silent
 * corruption of the allocator itself, surfacing later as an unrelated crash.
 *
 * Leaving logfilename alone also keeps the retry-the-configured-name-first behaviour
 * when logging is reopened at runtime, which is what the user asked for.
 */
STRPTR log_dest_name = NULL;

/* Same ownership rule for the console: the name actually in use, never the
 * config-owned consolename. NULL means "not chosen yet -- take consolename". */
static STRPTR con_dest_name = NULL;


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
	/*
	 * NP_StackSize is NOT optional here, despite this being "just" the log
	 * process. NETTRACE also runs the ARexx/RoadshowControl port, and that
	 * path is deep: rexx_poll() (rbuf[REPLYBUFLEN], 255 bytes of locals) ->
	 * parseline() -> setvalue() -> a per-setting handler, several of which
	 * carry their own buffers -- rexx_sethostname()'s is MAXHOSTNAMELEN+1,
	 * which is 256 bytes now that the host-name store holds an FQDN. Worse,
	 * `SET SETS <file>` re-enters parsefile()/parseline(), so a client (or a
	 * chained config file) can nest the whole chain.
	 *
	 * Spawned without this tag the process took the AmigaDOS default, and a
	 * stack overrun here has no MMU to catch it -- it quietly corrupts
	 * whatever sits below the stack and surfaces later as something
	 * unrelated.
	 *
	 * WHERE 16384 COMES FROM, honestly: it is NOT derived from that chain.
	 * Adding up the locals above (255 + 24 + 24 + 256) plus call overhead
	 * comes to well under 1 KB, so almost any explicit size would do. 16 KB
	 * is borrowed from ng_stack_process (kern/amiga_main.c) because a
	 * consistent, obviously-ample number is worth more here than a tight one:
	 * there is no stack-check to catch a near miss, and `SET SETS <file>` can
	 * re-enter parsefile()/parseline() to an unbounded depth (each level is
	 * only a small frame -- parsefile's line buffer is AllocMem'd, not stack
	 * -- but the depth is attacker-chosen by whoever writes the config file).
	 *
	 * The cost is not quite zero: log_init() failing is fatal to stack
	 * bring-up (see its callers in amiga_main.c), so this allocation has to
	 * succeed. 16 KB against the 1 MB floor the A600 tier tests is noise.
	 */
	if (CreateNewProcTags(NP_Entry, (LONG)&log_task,
			      NP_Name, (LONG)LOG_TASK_NAME,
			      NP_Priority, LOG_TASK_PRI,
			      NP_StackSize, 16384,
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

  /*
   * PORT (AmiTCP_NG): open and validate the log destination NOW, at bring-up,
   * rather than lazily on the first message.
   *
   * Upstream opened the file inside log_poll() on demand, and every failure path
   * there reports itself by calling log() -- i.e. through the very queue whose
   * destination has just been proven unusable. The result was a stack that logs
   * absolutely nothing, with no way to tell "nothing was logged" apart from
   * "logging is broken", which is exactly the state this fork was found in.
   *
   * Doing it here means the destination is known-good (or known-bad, with an
   * IoErr) before a single message is queued, and the banner it writes is proof
   * on disk that the whole chain works. This runs before the rendezvous below, so
   * log_init() cannot return to a caller that then logs into a black hole.
   *
   * A failure here is deliberately NOT fatal: no log file is a degraded stack,
   * not a broken one, and refusing to network because T: is missing would be a
   * far worse failure than being quiet.
   */
  log_validate_dest();

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
   * PORT (AmiTCP_NG) fix -- LOST WAKEUP. Re-arm the log signal before the service
   * loop, because the rendezvous above almost certainly consumed it.
   *
   * Exec signals are a LEVEL, not a count: one Wait() clears the bit however many
   * PutMsg()s set it. The `do { Wait(logmask); } while (initmsg != GetMsg(logPort))`
   * above waits for our own init message to come back -- but any log() call made by
   * the parent in that window (log_init() ends with one, and it runs before this
   * process is scheduled again) queues a SECOND message on logPort and sets the
   * same bit. The Wait() clears it, GetMsg() returns initmsg first (FIFO), the loop
   * exits satisfied -- and the other message is left sitting on the port with its
   * wakeup already spent. We then Wait() forever on a signal nobody will send again.
   *
   * That message is not lost so much as postponed indefinitely: the NEXT log() to
   * arrive re-signals and drains it. On a stack that logs only on error, "the next
   * one" may never come, which is why this looked like logging being dead rather
   * than one message being late.
   *
   * SetSignal() re-asserts the bit so the first Wait() returns immediately and the
   * normal log_poll() path drains whatever is queued -- including the LOG_CLOSE
   * case, which is why this is a re-arm and not an inline log_poll() call here.
   */
  SetSignal(logmask, logmask);
  
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
/*
 * PORT (AmiTCP_NG): filewritefail is now a latch with nothing behind it. It used
 * to gate a log() call complaining that the log file could not be written -- a
 * message that had to be written to the log file to be seen. Removing that left
 * the flag inert: still set, still reset by logname_changed(), read by nothing.
 * It is kept rather than deleted only because dropping it would leave log_poll()'s
 * `error` result unused and fail -Werror; there is no safe channel to report a
 * failure to write the log to, so that failure is deliberately silent.
 */
static BOOL filewritefail = FALSE, conwritefail = FALSE;
/* One-shot: CONSOLENAME= names something that is not a console. Reset with the
 * other console latches whenever the name changes, so a corrected setting is not
 * silently un-reported and a re-broken one is reported again. */
static BOOL connotconsole = FALSE;

/*
 * PORT (AmiTCP_NG): validate the log destination at NETTRACE bring-up.
 *
 * Tries the configured name (LOGFILENAME=, default _PATH_LOG) and then each
 * _PATH_LOG_FALLBACKS candidate in turn, keeping the first that opens. Writes a
 * banner naming the file it settled on, the build, and the active log level --
 * so an empty log and a broken log are never the same observation, and a bug
 * report can say which of the two it is.
 *
 * NETTRACE ONLY: uses logDOSBase (see the #define above), which the caller has
 * just opened.
 */
static void log_validate_dest(void)
{
  static char *fallbacks[] = { _PATH_LOG_FALLBACKS };
  UBYTE banner[128];
  struct CSource cs;
  int i;

  /*
   * The configured name first. logfilename is whatever readconfig() left it as --
   * the config file is parsed before log_init() in both the program and the
   * library bring-up paths, so an explicit LOGFILENAME= is already in effect here.
   */
  /*
   * LOGGING=OFF: open nothing. Deliberately WITHOUT setting fileopenfail -- that
   * flag means "this destination is known bad, stop trying", which is not the
   * case here. Leaving it clear is what lets log_poll() open the file for real if
   * logging is switched back on at runtime.
   */
  if (!log_enabled)
    return;

  log_open_error = 0;
  log_dest_name = logfilename;
  if ((logfile = logOpen(logfilename)) == NULL) {
    log_open_error = IoErr();

    for (i = 0; i < (int)(sizeof fallbacks / sizeof fallbacks[0]); i++) {
      if ((logfile = logOpen((STRPTR)fallbacks[i])) != NULL) {
	log_dest_name = (STRPTR)fallbacks[i];
	break;
      }
      log_open_error = IoErr();
    }
  }

  if (logfile == NULL) {
    /*
     * Nowhere to log. Latch the "already complained" flags so log_poll() does not
     * retry the whole candidate list -- and, more importantly, does not call log()
     * about it from inside the routine that drains the log queue.
     */
    fileopenfail = TRUE;
    return;
  }

  cs.CS_Buffer = banner;
  cs.CS_Length = sizeof banner;
  cs.CS_CurChr = 0;

  csprintf(&cs, "--- AmiTCP_NG %s log opened (level %ld",
	   AMITCP_NG_VER, log_level);
  if (log_open_error)
    csprintf(&cs, ", fell back to '%s' after DOS error %ld",
	     log_dest_name, log_open_error);
  csprintf(&cs, ") ---\n");

  FWrite(logfile, banner, cs.CS_CurChr, 1);
  Flush(logfile);
}

/*
 * PORT (AmiTCP_NG): report a console problem to the LOG FILE, not through log().
 *
 * log_poll() is the routine that drains the log queue; calling log() from inside
 * it to complain about a destination is circular, and it was the "only once"
 * latches on those calls that made the original total failure invisible. The log
 * file is known-good here -- log_validate_dest() proved it at bring-up.
 *
 * NETTRACE only (uses logDOSBase). Callers must gate this on the relevant
 * one-shot latch: a console that never becomes available (headless machine, no
 * public screen, self-start before Workbench) would otherwise get one line per
 * message appended forever, which on a 512K machine logging to RAM: is unbounded
 * growth from the diagnostics themselves.
 */
static void log_console_problem(char *what, STRPTR name)
{
  if (logfile == NULL)
    return;
  FPuts(logfile, (STRPTR)"log: ");
  FPuts(logfile, (STRPTR)what);
  FPuts(logfile, (STRPTR)" '");
  FPuts(logfile, name);
  FPuts(logfile, (STRPTR)"' -- logging to file only\n");
  Flush(logfile);
}

/*
 * PORT (AmiTCP_NG): the same reporter for something that is not a failure -- the
 * console opened fine, the user just needs to know what they configured. Separate
 * from log_console_problem() only because that one ends "logging to file only",
 * which would be untrue here.
 */
static void log_console_note(char *what, STRPTR name)
{
  if (logfile == NULL)
    return;
  FPuts(logfile, (STRPTR)"log: ");
  FPuts(logfile, (STRPTR)what);
  FPuts(logfile, (STRPTR)" '");
  FPuts(logfile, name);
  FPuts(logfile, (STRPTR)"'\n");
  Flush(logfile);
}

/*
 * Does this destination look like a console rather than a file?
 *
 * CONSOLENAME is opened with a plain DOS Open(), so ANY path works -- and a plain
 * file works silently, giving a second near-duplicate log that no tool shows
 * (NetLogViewer follows LOGFILENAME, which is the complete record, banner and
 * all). The variable is inherited from AmiTCP, where it is documented as
 * "Filename for the log console", so this has always been possible.
 *
 * A prefix test, deliberately: the alternative -- try to Lock() it and see -- is
 * wrong, because the file we just opened for writing cannot be Lock()ed anyway
 * and a console that has not been created yet cannot either. This only drives an
 * advisory line, so a custom console handler being mistaken for a file costs one
 * inaccurate sentence, once, and nothing else.
 */
static BOOL log_name_is_console(STRPTR name)
{
  static char *devs[] = { "con:", "raw:", "nil:", "aux:", NULL };
  int d, i;

  if (name == NULL)
    return TRUE;			/* nothing to complain about */
  for (d = 0; devs[d]; d++) {
    for (i = 0; devs[d][i]; i++) {
      char c = name[i];
      if (c >= 'A' && c <= 'Z')
	c = (char)(c - 'A' + 'a');
      if (c != devs[d][i])
	break;
    }
    if (devs[d][i] == '\0')
      return TRUE;
  }
  return FALSE;
}

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

    /*
     * The file always; the console only if the user asked for one.
     *
     * PORT (AmiTCP_NG): LOGCONSOLE= (log_console_enabled) is the new half of
     * this decision. Everything still reaches the FILE -- that is what a log is
     * for, and it is what makes it safe to leave logging on by default.
     *
     * The LOG_EMERG exclusion is upstream's and is kept: panic() writes its own
     * message to the console directly and also puts up a requester, so echoing
     * it here would say the same thing twice.
     */
    where = TOLOG;
    if (log_console_enabled && msg->level != LOG_EMERG)
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

    /*
     * PORT (AmiTCP_NG): SBTC_LOG_HOOK. An installed hook REPLACES the file and
     * console -- the API describes it as calling the hook "rather than sending
     * log messages to the process which records and displays them".
     *
     * Delivered here, on the log task, rather than from vlog() where the API
     * documents it: vlog() is called from inside splnet() regions, and splnet()
     * here disables task switching, so calling application code from there could
     * stop the whole machine. See ng_log_hook_deliver() (kern/subr_prf.c) for the
     * full reasoning. The message still passes through the same queue in the same
     * order, so nothing an application can observe changes except the context.
     *
     * ReplyMsg() below still returns the buffer to the pool on this path, exactly
     * as for a message that was written to the file.
     *
     * Jumps to the COMMON exit rather than `continue`ing: the lost-message
     * accounting lives after ReplyMsg(), and skipping it would mean that with a
     * hook permanently installed the "N log messages lost" warning could never
     * be raised at all.
     */
    if (ng_log_hook_deliver(msg))
      goto log_replied;

    /*
     * PORT (AmiTCP_NG): the FILE is written first, and the console second.
     *
     * Upstream did the console first. That put the one destination which is
     * guaranteed to survive a reboot -- and which is what anyone asking for a bug
     * report actually wants -- behind an Open("CON:...") that has to negotiate
     * with intuition and a public screen. Any way that stalls or is slow takes the
     * log file down with it, for a message that had already been formatted and
     * was one FWrite() from being safely on disk.
     *
     * Neither block reports its own failure through log() any more. Doing so fed
     * a message back into the queue that log_poll() is in the middle of draining,
     * to complain about the destination that message would have to be written to
     * -- circular by construction, and silent by design once the "only once"
     * latches were set. A file failure is now recorded by log_validate_dest() at
     * bring-up instead; a console failure is reported to the log file, which is
     * known-good by the time we get here.
     */
    if (where & TOLOG) {
      /*
       * Normally already open (log_validate_dest() did it at bring-up). This
       * re-opens after a LOGFILENAME= change at runtime, which logname_changed()
       * signals by closing the file and clearing fileopenfail.
       */
      if (logfile == NULL && !fileopenfail)
	log_validate_dest();

      if (logfile != NULL) {
	int error =
	  FPuts(logfile, buffer) == -1 ||
	    FWrite(logfile, msg->string, msg->chars, 1) != 1 ||
	      FPutC(logfile, '\n') == -1;
	Flush(logfile);
	if (error && !filewritefail) {	/* To avoid loops */
	  filewritefail = TRUE;
	}
      }
    }
    if (where & TOCONS) {
      /* If console is not open, open it */
      if (con_dest_name == NULL)
	con_dest_name = consolename;		/* start from what is configured */
      while (confile == NULL) {
	if ((confile = logOpen(con_dest_name)) == NULL) {
	  /* Once per NAME: a custom CONSOLENAME= failing and the default failing
	   * are two different things to know about. conopenfail is reset below
	   * when we fall back, so each gets one line and no more. */
	  if (!conopenfail)
	    log_console_problem("cannot open console", con_dest_name);
	  if (con_dest_name == consoledefname) {
	    conopenfail = TRUE;
	    break;
	  }
	  /* Fall back to the default name -- in our OWN variable. Assigning to
	   * consolename here (as this did) would point a config-layer pointer at a
	   * static array while its VF_FREE flag still said "bsd_free() me", and the
	   * next CONSOLENAME= would FreeVec() memory Exec never allocated. See the
	   * note on log_dest_name. */
	  con_dest_name = consoledefname;
	  conopenfail = conwritefail = connotconsole = FALSE;
	}
      }
      if (confile != NULL) {
	/*
	 * PORT (AmiTCP_NG): say it once, here, because this is the only place
	 * that knows. Not an error and not a refusal -- the setting is honoured
	 * exactly as configured; the user is simply told that what they pointed
	 * the log CONSOLE at is a file, and that they now have two logs. It was
	 * silent before, and a second near-identical log appearing with no
	 * explanation is a genuinely confusing thing to find.
	 */
	if (!connotconsole && !log_name_is_console(con_dest_name)) {
	  connotconsole = TRUE;
	  log_console_note("CONSOLENAME is not a console device, so log messages "
			   "are being duplicated into the file", con_dest_name);
	}
	{
	  int error =
	    FPuts(confile, buffer) == -1 ||
	    FWrite(confile, msg->string, msg->chars, 1) != 1 ||
	    FPutC(confile, '\n') == -1;

	  Flush(confile);
	  if (error && !conwritefail) {	/* To avoid loops */
	    conwritefail = TRUE;
	    log_console_problem("write failed to console", con_dest_name);
	  }
	}
      }
    }

  log_replied:
    ReplyMsg((struct Message *)msg);
    if (GetLogMsgFail != TotalFail) {
      int t = GetLogMsgFail;	/* Check if we have lost messages */

      /*
       * PORT (AmiTCP_NG) fix: the running total reported was TotalFail, the count
       * from BEFORE this batch -- so it always lagged by exactly the number just
       * lost, and read 0 the first time messages were dropped. It is t.
       *
       * The message is queued (not written directly) on purpose: the pool has just
       * been shown to be under pressure, and ReplyMsg() above has freed a slot, so
       * this is the one place in log_poll() where re-entering the queue is both
       * safe and correct -- it self-limits, since it only fires when the count
       * moves.
       */
      log(LOG_WARNING,"%ld log messages lost (total %ld lost)\n",
	  t - TotalFail, t);
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
  /*
   * NULL them, do not merely close them.
   *
   * These are file handles, and every other place in this file that closes one
   * clears it in the same breath (logname_changed()). This path -- the only one
   * that runs at SHUTDOWN -- did not, leaving two stale BPTRs behind.
   *
   * That is harmless right up until the stack is started AGAIN in the same session,
   * which the self-starting library does on the next OpenLibrary() after a
   * NetShutdown. Bring-up parses AmiTCP.config, a LOGFILENAME= or CONSOLENAME= line
   * calls logname_changed(), and it dutifully does Close() on a handle that was
   * closed when the previous NETTRACE exited. Closing an already-closed file handle
   * hangs DOS, and it hangs it before logging exists -- so the stack wedged during
   * restart with not one line written to say why.
   */
  if (confile) {
    Close(confile);
    confile = NULL;
  }
  if (logfile) {
    Close(logfile);
    logfile = NULL;
  }
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
/*
 * PORT (AmiTCP_NG): validate LOGLEVEL= before it is applied.
 *
 * vlog() filters with `priority > log_level`, which quietly assumes log_level is
 * in range. A negative one -- `LOGLEVEL=-1`, an easy typo -- would filter out
 * EVERYTHING including LOG_EMERG, and panic() logs at LOG_EMERG: the one message
 * you most need would be the one silenced. Returning FALSE makes setvalue()
 * reject the assignment with ERR_VALUE and leave the old level in force, rather
 * than accepting a value that disables the log.
 */
int loglevel_changed(void *p, LONG new)
{
  return (new >= LOG_EMERG && new <= LOG_DEBUG);
}

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
    con_dest_name = NULL;	/* re-read consolename on the next message */
    conopenfail = conwritefail = connotconsole = FALSE;
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
