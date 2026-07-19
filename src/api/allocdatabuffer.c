RCS_ID_C="$Id: allocdatabuffer.c,v 1.4 1994/02/16 06:31:55 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created: Sun Jun 13 01:09:17 1993 too
 * Last modified: Wed Feb 16 08:31:10 1994 jraja
 *
 * HISTORY
 * $Log: allocdatabuffer.c,v $
 * Revision 1.4  1994/02/16  06:31:55  jraja
 * Eliminated unnecessary checks :-) (see api/allocdatabuffer.h).
 *
 * Revision 1.3  1994/02/14  00:42:52  ppessi
 * Eliminated unnecessary allocations.
 *
 * Revision 1.2  1994/01/20  02:18:00  jraja
 * Added #include <conf.h> as the first include.
 *
 * Revision 1.1  1993/06/12  22:57:18  too
 * Initial revision
 *
 *
 */

/*
 * allocdatabuffer.c --- per-opener dynamic result buffers.
 *
 * Some API calls return pointers into storage the library owns -- inet_ntoa's
 * static string, the gethostbyname() hostent and its address list, WaitSelect's
 * working set. To keep programs isolated (and thread-safe across openers) each
 * SocketBase carries its OWN such buffers; this file allocates/reallocates/frees
 * them on demand and releases them when the program closes the library (UL_Close,
 * amiga_api.c). A small but important piece of the per-opener isolation model.
 */

#include <conf.h>

#include <exec/types.h>

#include <sys/malloc.h>

#include <api/amiga_api.h>
#include <api/allocdatabuffer.h>

BOOL doAllocDataBuffer(struct DataBuffer * DB, int size)
{
  if (DB->db_Addr)
    bsd_free(DB->db_Addr, M_TEMP);
  
  if ((DB->db_Addr = bsd_malloc(size, M_TEMP, M_WAITOK)) == NULL) {
    DB->db_Size = 0;
    return FALSE;
  }
  DB->db_Size = size;
  return TRUE;
}

VOID freeDataBuffer(struct DataBuffer * DB)
{
  if (DB->db_Addr)
    bsd_free(DB->db_Addr, M_TEMP);
  DB->db_Size = 0;
  DB->db_Addr = NULL;
}

