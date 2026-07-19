RCS_ID_C="$Id: amiga_rexx.c,v 1.17 1994/01/05 10:23:02 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * $Log: amiga_rexx.c,v $
 * Revision 1.17  1994/01/05  10:23:02  jraja
 * Added new functions: rexx_show() and rexx_hide().
 * Removed AddPort() and RemPort() calls from other functions.
 *
 * Revision 1.16  1993/07/26  08:22:50  jraja
 * Changed ARexx port name from 'NETTRACE' to 'AMITCP'.
 *
 * Revision 1.15  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.14  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.13  1993/04/25  03:04:51  jraja
 * Moved RemPort() upper in function rexx_deinit(). Changed bsd_free() type
 * argument to the same as the corresponding bsd_malloc().
 *
 * Revision 1.12  93/04/23  21:03:08  21:03:08  puhuri (Markus Peuhkuri)
 * Add code to check if we had to alloc more buffer and free it.
 * 
 * Revision 1.11  93/04/23  00:28:34  00:28:34  ppessi (Pekka Pessi)
 * using now parseline() (in amiga_config.c) to parse Arexx messages
 * 
 * Revision 1.10  93/04/21  19:08:04  19:08:04  puhuri (Markus Peuhkuri)
 * Add new feature: Connections. UNFINISHED.
 *
 * Revision 1.9  93/04/06  15:15:39  15:15:39  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.8  93/03/20  07:06:21  07:06:21  ppessi (Pekka Pessi)
 * Support multiple instances. Library name, rexx port name and task name fixed.
 * 
 * Revision 1.7  93/03/05  21:11:07  21:11:07  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.6  93/03/05  12:28:33  12:28:33  jraja (Jarno Tapio Rajahalme)
 * Changed RexxSysBase and UtilityBase to struct Library in GCC to 
 * follow SASC convention.
 * Removed __stdargs in GCC.
 * 
 * Revision 1.5  93/03/05  03:25:59  03:25:59  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.4  93/03/04  10:59:50  10:59:50  jraja (Jarno Tapio Rajahalme)
 * Minor cosmetic changes.
 * 
 * Revision 1.3  93/03/02  18:23:12  18:23:12  puhuri (Markus Peuhkuri)
 * Transformed toupper to utility/ToUpper and add open&close for utility.lib
 * 
 * Revision 1.2  93/03/01  19:11:56  19:11:56  puhuri (Markus Peuhkuri)
 * Major rewrite to remove simplerexx.c and to make arexx-interface
 * more efficient.
 * 
 * Revision 1.1  93/02/25  19:59:18  19:59:18  puhuri (Markus Peuhkuri)
 * Initial revision
 * 
 */

/*
 * amiga_rexx.c --- the ARexx control interface (the "AMITCP" port).
 *
 * ARexx is AmigaOS's inter-application scripting/command bus. This file gives the
 * running stack an ARexx port named "AMITCP" so external tools can query and
 * control it without any custom IPC: online/offline bring interfaces up/down,
 * ShowNetStatus reads statistics, stopnet sends "KILL". The port is owned by the
 * NETTRACE process (kern/amiga_log.c), which is why rexx_init() is called from
 * log_task().
 *   rexx_init()          create the port, return its signal bit.
 *   the dispatch loop     receive a RexxMsg, look the command up, run it (most
 *                         commands are handled by the same config/netdb parsing
 *                         machinery as the config file -- amiga_config.c), reply.
 * This is the stack's side of the config-tool conversation, and thus part of the
 * Roadshow-compatibility surface. docs/ARCHITECTURE.md section 6.
 */

#include <conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <kern/amiga_config.h>
#include <kern/amiga_includes.h>
#include <dos/rdargs.h>

#if __GNUC__
char *strtok(char *,char *);
#endif

#include <rexx/storage.h>
#include <rexx/rxslib.h>

#if __SASC
#include <proto/rexxsyslib.h>
#include <proto/utility.h>
#elif __GNUC__
/*
 * predefine RexxSysBase and UtilityBase to Library to follow SASC convention.
 */
#undef BASE_EXT_DECL
#undef BASE_NAME
#define BASE_EXT_DECL extern struct Library * RexxSysBase;
#define BASE_NAME (struct RexxSysBase *)RexxSysBase
#include <inline/rexxsyslib.h>
struct ClockData;
#undef BASE_EXT_DECL
#undef BASE_NAME
#define BASE_EXT_DECL extern struct Library * UtilityBase;
#define BASE_NAME (struct UtilityBase *)UtilityBase
#include <inline/utility.h>
#endif

/*
 * The rexx port and error name may change, if there is already an 
 * AmiTCP/IP task running
 */ 
#define D_REXX_PORT_NAME     "AMITCP"
#define REXX_ERROR_POSTFIX  ".LASTERROR"
#define D_REXX_ERROR_NAME     D_REXX_PORT_NAME REXX_ERROR_POSTFIX

UBYTE   T_REXX_PORT_NAME[]  = D_REXX_PORT_NAME;
UBYTE  *REXX_PORT_NAME      = T_REXX_PORT_NAME;

UBYTE   T_REXX_ERROR_NAME[] = D_REXX_ERROR_NAME;
UBYTE  *REXX_ERROR_NAME     = T_REXX_ERROR_NAME;

#define	REXX_RETURN_ERROR   ((struct RexxMsg *)-1L)

extern LONG nthLibrary;

#include <kern/amiga_rexx.h>

/*
 * This function returns a structure that contains the commands sent from
 * ARexx...  You will need to parse it and return the structure back
 * so that the memory can be freed...
 *
 * This returns NULL if there was no message...
 */
STKARGFUN long SetRexxVar(struct Message *, char *, char *, long);

struct Library *UtilityBase = NULL;
struct Library *RexxSysBase = NULL;

struct MsgPort *ARexxPort = NULL;

ULONG 
rexx_init(void)
{
  if (
#ifdef DEBUG
      (REXX_PORT_NAME = 
       bsd_malloc(sizeof(D_REXX_PORT_NAME) + 3, M_CFGVAR, M_WAITOK)) &&
      (REXX_ERROR_NAME = 
       bsd_malloc(sizeof(D_REXX_ERROR_NAME) + 3, M_CFGVAR, M_WAITOK)) &&
#endif
      (UtilityBase = OpenLibrary((STRPTR)"utility.library", 37)) &&
      (RexxSysBase = OpenLibrary((STRPTR)"rexxsyslib.library", 0L))) {
    ARexxPort = CreateMsgPort();
    if (ARexxPort) {
#ifdef DEBUG
      strcpy(REXX_PORT_NAME, D_REXX_PORT_NAME);
      strcpy(REXX_ERROR_NAME, D_REXX_PORT_NAME);
      if (nthLibrary) {
	REXX_PORT_NAME[sizeof(D_REXX_PORT_NAME)-1] = '.'; 
	REXX_PORT_NAME[sizeof(D_REXX_PORT_NAME)] = '0' + nthLibrary;
	REXX_PORT_NAME[sizeof(D_REXX_PORT_NAME)+1] = '\0';
	REXX_ERROR_NAME[sizeof(D_REXX_PORT_NAME)-1] = '.'; 
	REXX_ERROR_NAME[sizeof(D_REXX_PORT_NAME)] = '0' + nthLibrary;
	REXX_ERROR_NAME[sizeof(D_REXX_PORT_NAME)+1] = '\0';
      }
      strcat(REXX_ERROR_NAME, REXX_ERROR_POSTFIX);
#endif

      ARexxPort->mp_Node.ln_Name = (char *)REXX_PORT_NAME;
      return (ULONG)1 << ARexxPort->mp_SigBit;
    }
  }
  rexx_deinit();
  return (0);
}

static BOOL rexx_shown = FALSE;

BOOL
rexx_show(void)
{
  if (!rexx_shown && ARexxPort) {
    AddPort(ARexxPort);	/* No return value! */
    rexx_shown = TRUE;
    return TRUE;
  }
  return FALSE;
}

BOOL
rexx_hide(void)
{
  if (rexx_shown) {
    if (ARexxPort) {
      /*
       * Remove the port from the system's message port list so that the
       * port cannot be found any more
       */
      RemPort(ARexxPort);
    }
    rexx_shown = FALSE;
  }
  return TRUE;
}

void rexx_deinit(void)
{
  struct RexxMsg *rmsg;
  static STRPTR errstr = (STRPTR)"99: Port Closed!";

  if (RexxSysBase) {
    if (ARexxPort) {
      rexx_hide();
      /*
       * Reply to all messages received with error code set.
       */
      while(rmsg = (struct RexxMsg *)GetMsg(ARexxPort)){
	SetRexxVar((struct Message *)rmsg, (char *)REXX_ERROR_NAME,
		   (char *)errstr, strlen((char *)errstr));
	if (rmsg != REXX_RETURN_ERROR){
	  rmsg->rm_Result2 = 0;
	  rmsg->rm_Result1 = 100;
	  ReplyMsg((struct Message *)rmsg);
	}
      }
      DeleteMsgPort(ARexxPort);
      ARexxPort = NULL;
    }
    CloseLibrary(RexxSysBase);
    RexxSysBase = NULL;
  }
  if (UtilityBase) {
    CloseLibrary(UtilityBase);
    UtilityBase = NULL;
  }
}

BOOL rexx_poll(void)
{
  struct RexxMsg *rmsg;
	  
  if ((rmsg = (struct RexxMsg *)GetMsg(ARexxPort))
      && rmsg != REXX_RETURN_ERROR 
      && IsRexxMsg(rmsg)) {
    UBYTE rbuf[REPLYBUFLEN];
    struct CSource result;
    struct CSource csarg;
    UBYTE *errstr = NULL;
    LONG error = 0;

    result.CS_Buffer = rbuf; 
    result.CS_Length = REPLYBUFLEN - 1;
    result.CS_CurChr = 0;

    csarg.CS_Buffer = ARG0(rmsg);
    csarg.CS_Length = LengthArgstring(ARG0(rmsg)) + 1;
    csarg.CS_CurChr = 0;
    csarg.CS_Buffer[csarg.CS_Length - 1] = '\n'; /* Sentinel */

    rmsg->rm_Result1 = rmsg->rm_Result2 = 0;

    if ((error = parseline(&csarg, &errstr, &result))) {
      SetRexxVar((struct Message*)rmsg, (char *)REXX_ERROR_NAME,
		 (char *)errstr, (long)strlen((char *)errstr));
      rmsg->rm_Result1 = error;
    } else {
      if (rmsg->rm_Action & (1L << RXFB_RESULT)) {
	rmsg->rm_Result2 = (LONG)
	  CreateArgstring(result.CS_Buffer, (LONG)strlen((char *)result.CS_Buffer));
      }
    }

    csarg.CS_Buffer[csarg.CS_Length - 1] = '\0';

    ReplyMsg((struct Message *)rmsg);

    if (result.CS_Buffer != rbuf) /* We've allocated memory */
      bsd_free(result.CS_Buffer, M_TEMP); 

    return TRUE;
  }
  return FALSE;
}
