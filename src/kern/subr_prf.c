RCS_ID_C="$Id: subr_prf.c,v 1.33 1994/02/16 06:07:33 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: subr_prf.c,v $
 * Revision 1.33  1994/02/16  06:07:33  jraja
 * Replaced RawDoFmt() calls with vcsprintf(), which now does the formatting
 * itself. Changed stuffchar() to cs_putchar(), added csprintn() to print
 * out numbers.
 * Changed vlog() to return the number of characters written, changed
 * printf() to use vlog() (wheir code was identical).
 *
 * Revision 1.32  1994/01/05  10:27:30  jraja
 * Added IntuitionBase opening and closing to the panic().
 * Note: using a local library base variable might not work in GCC?
 * 
 * Revision 1.31  1993/12/21  22:06:48  jraja
 * Changed implicit structure member (s_sec) to explicit (tv_secs) on timeval.
 *
 * Revision 1.30  1993/11/06  23:51:22  ppessi
 * Added csprintf(), sprintf function using CSource.
 * Added vsprintf() and vcsprintf() as well.
 *
 * Revision 1.29  1993/10/07  22:41:34  ppessi
 * Added time to the log message.
 *
 * Revision 1.28  1993/05/16  00:11:12  jraja
 * Removed redundant volatile keyword.
 *
 * Revision 1.27  93/05/14  15:54:17  15:54:17  ppessi (Pekka Pessi)
 * Minor prototype fixes.
 * 
 * Revision 1.26  93/05/05  16:10:15  16:10:15  puhuri (Markus Peuhkuri)
 * Fixes for final demo.
 * 
 * Revision 1.25  93/05/04  12:40:07  12:40:07  puhuri (Markus Peuhkuri)
 * Fixed PANICBUFFER (SASC didnt like implicit alloca())
 * Now log takes care of keeping level on right range.
 * 
 * Revision 1.24  93/04/29  22:02:44  22:02:44  puhuri (Markus Peuhkuri)
 * fixed variable names to use configuration structure.
 * 
 * Revision 1.23  93/04/28  12:59:05  12:59:05  puhuri (Markus Peuhkuri)
 * Fixed stdargs.h to stdarg.h
 * 
 * Revision 1.22  93/04/27  10:23:19  10:23:19  puhuri (Markus Peuhkuri)
 * Add vlog-function for calling log from API. log() calls now vlog().
 * 
 * Revision 1.21  93/04/26  18:55:08  18:55:08  puhuri (Markus Peuhkuri)
 * Moved contents of closelog to log_deinit() and removed closelog.
 * 
 * Revision 1.20  93/04/26  11:54:46  11:54:46  too (Tomi Ollila)
 * Changed include paths of amiga_api.h, amiga_libcallentry.h and amiga_raf.h
 * from kern to api
 * 
 * Revision 1.19  93/04/23  02:28:38  02:28:38  ppessi (Pekka Pessi)
 * Number and length of logging messages made configureable.
 * 
 * Revision 1.18  93/04/21  19:08:46  19:08:46  puhuri (Markus Peuhkuri)
 * Now checks if process run into panic() was AmiTCP.
 * Now uses new structure to log_msg. (Printing of priority is in
 * NETTRACE)
 * 
 * Revision 1.17  93/04/06  15:15:55  15:15:55  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.16  93/03/20  07:06:38  07:06:38  ppessi (Pekka Pessi)
 * Fixed memory leak caused by task_remove()
 * 
 * Revision 1.15  93/03/19  14:14:54  14:14:54  too (Tomi Ollila)
 * Code changes at night 17-18 March 1993
 * 
 * Revision 1.14  93/03/15  09:12:42  09:12:42  jraja (Jarno Tapio Rajahalme)
 * Checkin' mods made by ppessi.
 * 
 * Revision 1.13  93/03/13  00:05:31  00:05:31  ppessi (Pekka Pessi)
 * Quick'n'dirty fix for PANICBUFFER; some prototypes fixed. 
 * 
 * Revision 1.12  93/03/10  17:14:12  17:14:12  puhuri (Markus Peuhkuri)
 * Fixed documentation. Now (s)printf returns number of printed chars.
 * 
 * Revision 1.11  93/03/09  13:33:11  13:33:11  puhuri (Markus Peuhkuri)
 * See messages for amiga_log.c/1.10.
 * Now uses new function to get log message.
 * Fixed a bug in closing of log (caused a read from low memory, 0x14)
 * 
 * Revision 1.10  93/03/05  21:11:17  21:11:17  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.9  93/03/05  12:32:33  12:32:33  jraja (Jarno Tapio Rajahalme)
 * Removed #include <kern/amiga_api_protos.h>.
 * 
 * Revision 1.8  93/03/05  03:26:17  03:26:17  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.7  93/03/02  18:25:12  18:25:12  puhuri (Markus Peuhkuri)
 * Add sprintf() and fixed documentation
 * 
 * Revision 1.6  93/03/02  16:10:35  16:10:35  puhuri (Markus Peuhkuri)
 * Removed amiga.lib functions, fixed memory managment
 * 
 * Revision 1.5  93/02/26  19:44:35  19:44:35  puhuri (Markus Peuhkuri)
 * Modified to use MsgPort as list of free messages,
 * made log() and printf() re-entrant, removed kprintf (replaced w/
 * exec/RawDoFmt()), kernputchar, ksprintn() and logpri.
 * As every BSD/Mach code was removed so was copyright.
 * 
 * Revision 1.4  93/02/25  19:39:25  19:39:25  puhuri (Markus Peuhkuri)
 * Includes fixed, compatible with both GCC and SAS, 
 * function declarations fixed, calls from panic() fixed,
 * checks for various places.
 * 
 * Revision 1.3  93/02/24  17:32:17  17:32:17  puhuri (Markus Peuhkuri)
 * Add printf (for printing to log)
 * 
 * Revision 1.2  93/02/24  17:22:23  17:22:23  puhuri (Markus Peuhkuri)
 * Made program to work much cleanier, still uses Amiga.lib
 * 
 * Revision 1.1  92/12/17  18:19:42  puhuri
 * Initial revision
 *
 */

/*
 * subr_prf.c --- formatted printing: the stack's log()/printf and sprintf core.
 *
 * BSD kernels have their own printf-family (they cannot use the C library's). This
 * file provides it for the stack: the shared formatting engine and the log()
 * function that the whole codebase calls to record events. log() does not print
 * directly -- it hands a formatted message to the NETTRACE process (kern/amiga_log.c),
 * which is what makes it safe to call from spl / interrupt context. The '32-bit'
 * csprintf variants exist because the Amiga's RawDoFmt defaults to 16-bit integer
 * args, which is wrong for a stack that deals in longs and pointers.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/time.h>

#include <kern/amiga_includes.h>
#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <amitcp/socketbasetags.h>	/* PORT (AmiTCP_NG): Log/Error hook messages */
/* PORT (AmiTCP_NG): DateStamp() for LogHookMessage, CallHookPkt() to invoke the
 * log hook. Global DOSBase/UtilityBase (kern/amiga_main.c). */
#if __SASC
#include <proto/dos.h>
#include <proto/utility.h>
#elif __GNUC__
#include <inline/dos.h>
#include <inline/utility.h>
#endif
#include <kern/amiga_log.h>
#include <stdarg.h>
#include <intuition/intuition.h>

#if __GNUC__
#include <inline/intuition.h>
#elif __SASC
#include <proto/intuition.h>
#endif

#include <kern/amiga_main_protos.h>
#include <dos/rdargs.h>		/* CSource */

extern void exit(int);

/*
 * PORT (AmiTCP_NG): SBTC_LOG_HOOK (tag 55). Stack-wide, not per-base: there is
 * one logging subsystem, and the API describes the hook as replacing where log
 * messages go rather than as a property of one library base. NULL -- the default
 * -- means messages are recorded and displayed as usual.
 */
struct Hook *log_hook = NULL;

/*
 * Hand one already-formatted, already-queued message to the log hook. Returns
 * TRUE if the hook took it, so the caller skips the file and console.
 *
 * CALLED FROM THE LOG TASK (log_poll(), kern/amiga_log.c), NOT from vlog().
 * That placement is the whole point and it is a deliberate deviation from the
 * documented behaviour, which says the hook runs "on the context of the process
 * which called into vsyslog(), which may be the TCP/IP stack itself".
 *
 * It cannot run there safely. log() is called from inside splnet() regions --
 * sana_poll() on the net task and the SIOCDIFADDR unlink in netinet/in.c are two
 * of many -- and splnet() on this port is Forbid()-equivalent: task switching is
 * off. Calling out to application code there means an application that blocks,
 * waits, or opens a requester stops the scheduler for the entire machine, with
 * no MMU and no preemption to recover. The API's "process the message as quickly
 * as possible" warning asks the application to be careful; it does not make it
 * safe for US to invite arbitrary code into a Forbid().
 *
 * Delivering from the log task instead costs the hook nothing it can observe
 * except the calling context: the same messages arrive, in the same order, with
 * the same text and priority, and the task is an ordinary process where blocking
 * is merely slow rather than fatal.
 *
 * The hook is invoked through CallHookPkt() so the documented register
 * convention (hook A0, reserved A2, message A1) is honoured -- calling h_Entry
 * directly from C would pass the arguments on the stack and enter the
 * application with garbage in those registers. Without utility.library we cannot
 * make that call correctly, so the message falls back to the file and console
 * rather than being dropped or the hook being called wrongly.
 */
int
ng_log_hook_deliver(struct log_msg *msg)
{
  extern struct Library *UtilityBase;
  struct LogHookMessage lhm;
  ULONG len;

  if (log_hook == NULL || UtilityBase == NULL)
    return FALSE;

  /*
   * msg->chars is a count, not a terminator. Terminate inside the buffer for
   * lhm_Message, which is documented as NUL-terminated. The clamp is written
   * against the unsigned length so a log_buf_len of 0 cannot produce the
   * string[-1] write a signed "len - 1" would.
   */
  len = (ULONG)msg->chars;
  if (len >= log_cnf.log_buf_len)
    len = log_cnf.log_buf_len ? log_cnf.log_buf_len - 1 : 0;
  msg->string[len] = '\0';

  lhm.lhm_Size     = sizeof lhm;
  lhm.lhm_Priority = (LONG)LOG_PRI(msg->level);
  lhm.lhm_Date.ds_Days = lhm.lhm_Date.ds_Minute = lhm.lhm_Date.ds_Tick = 0;
  if (DOSBase != NULL)
    DateStamp(&lhm.lhm_Date);
  lhm.lhm_Tag     = NULL;
  lhm.lhm_ID      = 0;
  lhm.lhm_Message = (STRPTR)msg->string;

  (void)CallHookPkt(log_hook, NULL, (APTR)&lhm);
  return TRUE;
}

void
cs_putchar(unsigned char ch, struct CSource * cs)
{
  if (cs->CS_CurChr < cs->CS_Length 
      && (cs->CS_Buffer[cs->CS_CurChr] = ch))
    cs->CS_CurChr++;
}

/****i* bsdsocket.library/panic ******************************************
*
*   NAME	
*	panic -- Inform user from serious failure.
*
*   SYNOPSIS
*	panic(Message, Arguments...)
*
*	void panic( STRPTR, ... )
*
*   FUNCTION
*	Calls api_setfunctions() with no arguments to stop programs using
*	AmiTCP. Writes message to log file. Sets up User Requester to
*	inform user about situation. Avoids self-loops.
*
*
*   INPUTS
*    	Messagestring - A pointer to string containing message to show
*	    to user and to write to log. It should describe problem so
*	    that user can take correcting action if it is failure with
*	    his configuration or is able to write bug report if it is
*	    a bug withn program.
*
*       Arguments - as in c-library printf()
*
*   RESULT
*    	This function does not return.
*
*   EXAMPLE
*    	if(Everything==WRONG)
*	    panic("Everything is wrong\nGoto sleep");
*
*   NOTES
*    	As panic does not return, it should be used only in extreme
*	cases
*
*   BUGS
*
*   SEE ALSO
*	log()
*
******************************************************************************
*
*/

#define PANICBUFFERSIZE 512

void
panic(const char *fmt,...)
{
  struct EasyStruct panicES = {
    sizeof( struct EasyStruct),
    NULL,
    (UBYTE *)"AmiTCP PANIC",
    (UBYTE *)"panic: %s" ,
    (UBYTE *)"Exit AmiTCP"
  };
  static int in_panic = 0;
  struct CSource cs;
  char buffer[PANICBUFFERSIZE];
  va_list ap;
  struct Library *IntuitionBase = NULL; /* local intuitionbase */
  extern struct Task *AmiTCP_Task;

  /*
   * PORT (AmiTCP_NG) fix: a second caller must NOT fall through to the
   * requester below. `buffer` is a per-call local that is only filled inside
   * this block, so a caller that found in_panic already set used to reach
   * EasyRequestArgs() with uninitialised stack -- and panicES's format is
   * "panic: %s", so it walked that garbage looking for a NUL. The decrement
   * was also outside the block, so the second caller cleared the first
   * caller's flag. This is a genuine two-task race, not just recursion:
   * panic() is called both from net-task code and from library vectors
   * running on an application's own task (see amiga_syscalls.c), and nothing
   * here serialises in_panic. A nested caller now simply blocks and lets the
   * first panic do the reporting.
   */
  if (!in_panic){
				/* If we're called previously.. */
    in_panic++;			/* We're in panic now */
    api_setfunctions();		/* Set libraries to return error code */

    cs.CS_Buffer = (UBYTE *)buffer;
    cs.CS_CurChr = 0;
    cs.CS_Length = PANICBUFFERSIZE;

    va_start(ap, fmt);
    vcsprintf(&cs, fmt, ap);
    va_end(ap);

    log(LOG_EMERG, "panic: %s", buffer); /* Write to log */
    in_panic--;
  } else
    Wait(0L);			/* another task is already panicking */

  /*
   * Inform user (if log system has failed...)
   * by opening a requester to the default public screen.
   *
   * Open a local IntuitionBase for the EasyRequestArgs()
   */
  if ((IntuitionBase = OpenLibrary((STRPTR)"intuition.library", 37L)) != NULL) {
    EasyRequestArgs(NULL, &panicES, NULL, (char *)&buffer);
    CloseLibrary(IntuitionBase);
    IntuitionBase = NULL;
  }
    
  /*
   * If the caller is not the AmiTCP task, sleep forever. 
   * This should go to API or where ever we came in AmiTCP code
   */
  if (FindTask(NULL) != AmiTCP_Task)
    Wait(0);

  Wait(SIGBREAKF_CTRL_F);	/* AmiTCP/IP waits here */

  /*
   * Following code is not safe. Probably we should wait infinetely long too...
   */
  deinit_all();			/* This returns !! */
  exit(20);			/* This should be safe... */
}

/****i* bsdsockets.library/log ******************************************
*
*   NAME	
*	log -- Write log message to log and/or console.
*
*   SYNOPSIS
*    	log(Level, FormatString, arguments...)
*
*	void log( ULONG, STRPTR, ...)
*
*   FUNCTION
* 	Writes message given as format string and arguments
*	(printf-style) to both log and console (except if level is
*       LOG_EMERG, LOG_EMERG is generated only by panic and it is
*       written only to the log file as panic() generates an User
*       Requester informing user.
*
*       This function can be called from interrupts.
*
*   INPUTS
*    	Level - indicates type of logged message. These levels are
*	    defined in sys/syslog.h.
*	FormatString - This is a printf-style format string with some
*	    restrictions (notably the floats are not handled).
*	arguments - as in printf() 
*
*   RESULT
*    	Returns no value.
*
*   EXAMPLE
*    	log(LOG_ERR,
*	    "arp: ether address is broadcast for IP address %x!\n",
*            ntohl(isaddr.s_addr));
*	fail=TRUE;
*	break;
*
*   NOTES
*
*   BUGS
*
*   SEE ALSO
*	Your C-compilers printf() documentation.
*
******************************************************************************
*
*/

void
(log)(unsigned long level, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vlog(level, fmt, ap);		/* Call actual function */
  va_end(ap);
}

ULONG
vlog(unsigned long level, const char *fmt, va_list ap)
{
  struct log_msg *msg;
  struct timeval now;

  /*
   * PORT (AmiTCP_NG): drop anything less important than the configured level
   * BEFORE claiming a message buffer.
   *
   * The log() macro (sys/systm.h) already filtered anything that came in that
   * way, and did it without evaluating the caller's arguments. This test is not
   * redundant: printf() and any direct vlog() caller arrive here having bypassed
   * the macro entirely, and filtering must not depend on which door was used.
   *
   * It also has to happen before GetLogMsg(): there are only log_cnf.log_bufs
   * (4 by default) message buffers, and letting a burst of debug messages empty
   * the pool would push out the LOG_ERR that actually mattered.
   *
   * LOG_PRI() masks off the facility bits, so a caller passing LOG_KERN|LOG_ERR
   * is compared on its priority alone. log_enabled is tested separately and
   * gates everything including LOG_EMERG, so LOGGING=OFF really does mean off.
   */
  if (!log_enabled || (LONG)LOG_PRI(level) > log_level)
    return 0;

  if ((msg = (struct log_msg *)GetLogMsg(logReplyPort))) {
    /*
     * PORT (AmiTCP_NG): SBTC_LOG_HOOK is delivered by the LOG TASK
     * (kern/amiga_log.c), not from here. See the note on ng_log_hook_deliver().
     */
    ULONG ret;
    struct CSource cs;
    cs.CS_Buffer = msg->string;
    cs.CS_Length = log_cnf.log_buf_len;
    cs.CS_CurChr = 0;

    /*
     * PORT (AmiTCP_NG) fix: do not timestamp through a timer.device that is not
     * open yet. GetSysTime() is a jsr through TimerBase, which timer_init()
     * fills in -- and timer_init() runs AFTER log_init() in both the program and
     * the self-starting library bring-up (amiga_main.c). So every log() call made
     * during early init jumped through a NULL base. With no MMU that reads the
     * CPU exception vectors at address 0 and jumps to whatever is there: the
     * machine dies silently, mid-boot, with nothing written anywhere.
     *
     * Nothing in normal running hits this -- which is why it survived. It fires
     * exactly when something goes wrong early enough to be worth logging, so the
     * one message that would have explained a failed bring-up was also the thing
     * that killed the machine before it could be written.
     *
     * An unstamped message is worth far more than a dead Amiga; log_poll()
     * formats time 0 as 1-Jan-1978, which is an obvious "before the clock
     * started" marker rather than a plausible wrong date.
     */
    if (TimerBase)
      GetSysTime(&now);
    else
      now.tv_secs = now.tv_micro = 0;

    vcsprintf(&cs, fmt, ap);
    msg->level = level & (LOG_FACMASK | LOG_PRIMASK);	/* Level of message */
    ret = msg->chars = cs.CS_CurChr;
    msg->time = now.tv_secs;
    PutMsg(logPort, (struct Message *)msg);
    return ret;
  }
  return 0;
}

/****i* bsdsockets.library/printf ******************************************
*
*   NAME	
*	printf -- print to log
*
*   SYNOPSIS
*    	printf(FormatString, Arguments...)
*
*	ULONG printf( const char *, ...)
*
*   FUNCTION
*   	Prints messages to log
*       As log, also this is callable from interrupts.
*
*       Specially for debugging prints.
*
*   INPUTS
*	FormatString - This is a printf-style format string as for vcsprintf().
*	Arguments - as in C-librarys printf() 
*
*   RESULT
*       Number of characters printed.
*
*   EXAMPLE
*
*		printf("line=%d, val=%x\n", 
*                      __LINE__, very.interesting->value);
*
*   NOTES
*
*   BUGS
*
*   SEE ALSO
*	vcsprintf(), vlog()
*
******************************************************************************
*
*/
ULONG 
printf(const char *fmt, ...)
{
#if 1
  ULONG ret;
  va_list ap;

  va_start(ap, fmt);
  ret = vlog(LOG_INFO, fmt, ap);
  va_end(ap);
  
  return ret;
#else
  va_list ap;
  struct log_msg *msg;
  struct timeval now;

  if (msg = GetLogMsg(logReplyPort)) {	/* Get next free message */
    struct CSource cs;

    GetSysTime(&now);

    cs.CS_Buffer = msg->string;
    cs.CS_Length = log_cnf.log_buf_len;
    cs.CS_CurChr = 0;

    va_start(ap, fmt);
    vcsprintf(&cs, fmt, ap);
    va_end(ap);

    msg->level = LOG_INFO;
    msg->chars = cs.CS_CurChr;
    msg->time = now.tv_secs;
    PutMsg(logPort,(struct Message *)msg);

    return (ULONG)cs.CS_CurChr;
  } else
    return 0;
#endif
}

/****i* bsdsockets.library/sprintf ******************************************
*
*   NAME	
*	sprintf -- print to buffer
*
*   SYNOPSIS
*    	len = sprintf(Buffer, FormatString, Arguments...)
*       len = csprintf(cs, FormatString, Arguments...)
*
*	ULONG sprintf(STRPTR, const char*, ...)
*       ULONG csprintf(struct CSource *, const char*, ...)
*
*   FUNCTION
* 	Prints to a simple buffer or to a CSource buffer. These functions
*       are similar to C-library sprintf() with restricted formatting.
*
*   INPUTS
*       Buffer       - Pointer to buffer.
*	FormatString - This is a printf()-style format string with some
*                      restrictions. Numbers are being taken as 'long' by
*                      default, however.
*	Arguments    - as in printf() .
*       cs           - Pointer to CSource structure.
*
*   RESULT
*       Number of characters printed.
*
*   EXAMPLE
*	sprintf(mybuf, "line=%d, val=%x\n", 
*               __LINE__, very.interesting->value);
*
*   BUGS
*       Function sprintf() assumes that no print is longer than 1024 chars.
*       It does not check for buffer owerflow (there no way to check, the
*       definition of sprintf misses it).
*
*       sprintf strings are truncated to maximum of 1024 chars (including
*       final NUL)
*
*   SEE ALSO
*	vcsprintf()
*
******************************************************************************
*
*/

ULONG 
vcsprintf(struct CSource *cs, const char *fmt, va_list ap)
{
  ULONG start = cs->CS_CurChr;
  char *p, ch, padc;
  u_long ul;
  int n, base, lflag, tmp, width, precision, leftjustify;
  char buf[sizeof(long) * NBBY + 2]; /* wide enough for base 2 (binary) + sign + '\0' */

  if (cs->CS_Length && cs->CS_CurChr < cs->CS_Length) {

    for (;;) {
      padc = ' ';
      width = 0; 
      precision = -1;
      leftjustify = FALSE;
      while ((ch = *fmt++) != '%') {
	if (ch == '\0') {
	  goto end;
	}
	cs_putchar(ch, cs);
      }
      lflag = 0;
reswitch:
      switch (ch = *fmt++) {
      case '\0':
	/*
	 * PORT (AmiTCP_NG) fix: a format string ending in a lone '%'. Without
	 * this case the NUL fell through `default:` into `case '%'`, and since
	 * `ch = *fmt++` had already stepped fmt PAST the terminator, the outer
	 * loop resumed scanning beyond the end of the string -- an unbounded
	 * read until it chanced on a '%' or NUL in adjacent memory (and any
	 * stray '%' would then pull further va_args). No current call site can
	 * trigger it (every format is a literal), but the formatter should not
	 * depend on that.
	 */
	goto end;
      case '-':
	leftjustify = TRUE;
	goto reswitch;
      case '0':
	padc = '0';
	goto reswitch;
      case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
	for (width = 0;; ++fmt) {
	  width = width * 10 + ch - '0';
	  ch = *fmt;
	  if (ch < '0' || ch > '9')
	    break;
	}
	goto reswitch;
      case '.': /* precision */
	for (precision = 0; (ch = *fmt) >= '0' && ch <= '9'; ++fmt) {
	  precision = precision * 10 + ch - '0';
	}
	goto reswitch;
      case 'l':
	lflag = 1;
	goto reswitch;
      case 'b':
	{
	  char *tp;

	  ul = va_arg(ap, int);
	  p = va_arg(ap, char *);
	  for (tp = csprintn(ul, *p++, buf, sizeof(buf)); (ch = *tp++);)
	    cs_putchar(ch, cs);
	
	  if (!ul)
	    break;

	  for (tmp = 0; (n = *p++);) {
	    if (ul & (1 << (n - 1))) {
	      cs_putchar(tmp ? ',' : '<', cs);
	      for (; (n = *p) > ' '; ++p)
		cs_putchar(n, cs);
	      tmp = 1;
	    } else
	      for (; *p > ' '; ++p)
		;
	  }
	  if (tmp)
	    cs_putchar('>', cs);
	}
	break;
      case 'r':
	p = va_arg(ap, char *);
	vcsprintf(cs, p, va_arg(ap, va_list));
	break;
      case 'c':
	*buf = va_arg(ap, int);
	buf[1] = '\0';
	p = buf;
	goto textout;
      case 's':
	p = va_arg(ap, char *);
textout:
	/*
	 * Set width to the maximum width, if maximum width is set, and
	 * width exceeds it.
	 */
	if (precision > 0 && width > precision)
	  width = precision;
	/*
	 * spit out necessary pad characters to fill on left, if necessary
	 */
	if (width > 0 && !leftjustify && (width -= strlen(p)) > 0)
	  while (width--)
	    cs_putchar(padc, cs);
	/* take a copy of the original pointer */
	ul = (u_long)p;
	/*
	 * Copy the characters, pay attention to the fact that precision
         * may not be exceeded.
	 */
	while ((ch = *p++) && precision-- != 0)
	  cs_putchar(ch, cs);
	/*
	 * spit out necessary pad characters to fill on right, if necessary
	 */
	if (width > 0 && leftjustify && (width -= ((u_long)p - ul - 1)) > 0)
	  while (width--)
	    cs_putchar(' ', cs);
	break;
      case 'd':
      case 'i':
        ul = lflag ? va_arg(ap, long) : va_arg(ap, int);
        if ((long)ul < 0) {
	  cs_putchar('-', cs);
	  ul = -(long)ul;
	}
	base = 10;
	goto number;
      case 'o':
	ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
	base = 8;
	goto number;
      case 'u':
	ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
	base = 10;
	goto number;
      case 'p': /* pointers */
      case 'P':
	width = 8;
	padc = '0';
	/* FALLTHROUGH */
      case 'x':
      case 'X':
	ul = lflag ? va_arg(ap, u_long) : va_arg(ap, u_int);
	base = 16;
number:
	p = csprintn(ul, base, buf, sizeof(buf));
	tmp = (buf + sizeof(buf) - 1) - p; /* length */
	if (width > 0 && !leftjustify && (width -= tmp) > 0)
	  while (width--)
	    cs_putchar(padc, cs);
	while ((ch = *p++))
	  cs_putchar(ch, cs);
	if (width > 0 && leftjustify && (width -= tmp) > 0)
	  while (width--)
	    cs_putchar(' ', cs);
	break;
      default:
	cs_putchar('%', cs);
	if (lflag)
	  cs_putchar('l', cs);
	/* FALLTHROUGH */
      case '%':
	cs_putchar(ch, cs);
      }
    }

end:

    if (cs->CS_CurChr == cs->CS_Length) {
      cs->CS_CurChr--;			/* must NUL terminate */
    }      
    cs->CS_Buffer[cs->CS_CurChr] = '\0';
    
    return cs->CS_CurChr - start;
  } else {
    /* A pathological case */
    return 0;
  }
}

ULONG csprintf(struct CSource *cs, const char *fmt, ...)
{
  va_list ap;
  ULONG len;

  va_start(ap, fmt);
  len = vcsprintf(cs, fmt, ap);
  va_end(ap);
  return len;
}

ULONG vsprintf(char *buf, const char *fmt, va_list ap)
{
  struct CSource cs;

  cs.CS_Buffer = (UBYTE *)buf;
  cs.CS_CurChr = 0;
  cs.CS_Length = 1024;	  /* This is not probably the cleanest way :-) */

  return vcsprintf(&cs, fmt, ap);
}

ULONG sprintf(char *buf, const char *fmt, ...)
{
  va_list ap;
  ULONG len;

  va_start(ap, fmt);
  len = vsprintf(buf, fmt, ap);
  va_end(ap);
  return len;
}

char *
csprintn(u_long n, int base, char *buf, int buflen)
{
  register char *cp = buf + buflen - 1;

  *cp = 0;
  do {
    cp--;
    *cp = "0123456789abcdef"[n % base];
  } while (n /= base);
  return (cp);
}
