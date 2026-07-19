RCS_ID_C="$Id: accesscontrol.c,v 3.2 1994/04/06 15:36:50 too Exp $";
/*
 * Copyright (c) 1993, 1994	AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    		Helsinki University of Technology, Finland.
 *                    		All rights reserved.
 *
 * Created: Tue Mar 22 22:24:17 1994 too
 * Last modified: Wed Apr  6 18:36:22 1994 too
 *
 * HISTORY
 * $Log: accesscontrol.c,v $
 * Revision 3.2  1994/04/06  15:36:50  too
 * Added checking for private ports
 *
 * Revision 3.1  1994/03/26  09:37:18  too
 * Initial relelase
 */

/*
 * accesscontrol.c --- who is allowed to use the stack.
 *
 * An AmiTCP feature (not part of stock BSD): a simple access-control list checked
 * when a program opens bsdsocket.library, so a machine can restrict which local
 * users/tasks may use the network. Provides the list management and the yes/no
 * check that api/amiga_api.c's ELL_Open consults. Small and self-contained.
 */

#include <exec/types.h>

#include <sys/systm.h>
#include <sys/malloc.h>
#include <kern/amiga_config.h>
#include <kern/amiga_netdb.h>
#include <kern/accesscontrol.h>

#include <sys/syslog.h>

int controlaccess(struct in_addr shost, unsigned short dport)
{
  int i;

  LOCK_R_NDB(NDB);		
  for (i = 0; NDB->ndb_AccessTable[i].ai_flags; i++) 
#define AT NDB->ndb_AccessTable[i]
#define host (*(ULONG *)&shost)				/* XXX */
    if (((AT.ai_port == 0 && (!(AT.ai_flags & ACF_PRIVONLY) || dport < 1024))
	 || AT.ai_port == dport) &&
	((host ^ AT.ai_host) & AT.ai_mask) == 0) {
      /*
       * match
       */
      int allow = AT.ai_flags & ACF_ALLOW;

      if (AT.ai_flags & ACF_LOG)
	log(allow? LOG_INFO: LOG_NOTICE,
	    "Access from host %ld.%ld.%ld.%ld to port %ld %s\n",
	    host>>24 & 0xff, host>>16 & 0xff, host>>8 & 0xff, host & 0xff,
	    dport, allow? "allowed": "denied");

      UNLOCK_NDB(NDB);
      return allow;
#undef allow
#undef host
#undef AT
    }
  /*
   * No match. allow by default.
   */
  UNLOCK_NDB(NDB);
  return ACF_ALLOW;
}
