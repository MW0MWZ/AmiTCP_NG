#ifndef ACCESSCONTROL_H
#define ACCESSCONTROL_H
/*
 * $Id: accesscontrol.h,v 3.1 1994/03/26 09:39:45 too Exp $
 *
 * Copyright (c) 1993, 1994	AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    		Helsinki University of Technology, Finland.
 *                    		All rights reserved.
 *
 * Created: Tue Mar 22 22:17:57 1994 too
 * Last modified: Fri Mar 25 20:39:11 1994 too
 *
 * HISTORY
 * $Log: accesscontrol.h,v $
 * Revision 3.1  1994/03/26  09:39:45  too
 * Initial release
 *
 */


#ifndef AMIGA_NETDB_H
#include <kern/amiga_netdb.h>
#endif

int controlaccess(struct in_addr shost, unsigned short sport);

static inline void setup_accesscontroltable(struct NetDataBase * ndb)
{
  *((ULONG *)&ndb->ndb_AccessTable[ndb->ndb_AccessCount]) = 0; /*mark default*/

  /*
   * PORT (AmiTCP_NG): do NOT overwrite the live pointer with the return value
   * unconditionally. bsd_realloc() leaves the OLD block intact and returns NULL
   * when it cannot allocate (kern/kern_malloc.c), so assigning the NULL both
   * leaked that block and left controlaccess() walking through a NULL table.
   * Keeping the old, larger block on failure is harmless -- it is merely bigger
   * than needed -- so ndb_AccessMax stays as it was in that case.
   */
  {
    struct AccessItem *shrunk =
      bsd_realloc(ndb->ndb_AccessTable,
		  ndb->ndb_AccessCount * sizeof (struct AccessItem) +
		  sizeof (ULONG), M_NETDB, M_WAITOK);

    if (shrunk != NULL) {
      ndb->ndb_AccessTable = shrunk;
      /* The table is now an EXACT fit: no spare capacity at all. This is what
       * made the old constant bound in addaccessent() wrong -- see there. */
      ndb->ndb_AccessMax = ndb->ndb_AccessCount;
    }
  }

#if 0
  {
    int i;
#define host ndb->ndb_AccessTable[i].ai_host
#define mask ndb->ndb_AccessTable[i].ai_mask
    for (i = 0; i < ndb->ndb_AccessCount; i++)
      log(7, "%ld %ld.%ld.%ld.%ld/%ld.%ld.%ld.%ld %lx",
	  ndb->ndb_AccessTable[i].ai_port, 
	  host>>24 & 0xff, host>>16 & 0xff, host>>8 & 0xff, host & 0xff,
	  mask>>24 & 0xff, mask>>16 & 0xff, mask>>8 & 0xff, mask & 0xff,
	  ndb->ndb_AccessTable[i].ai_flags);
    
    log(7, "%ld %ld", ndb->ndb_AccessTable[i].ai_flags,
	/*                 */ ndb->ndb_AccessTable[i].ai_port);
#undef mask
#undef host    
  }
#endif	  
}
#endif /* ACCESSCONTROL_H */

