/*
 * allocdatabuffer.h
 *
 * 	Copyright (c) 1993 OHT-ATCP Group
 * 	    All rights reserved
 *
 * Created: Mon Apr 26 11:58:58 1993 too
 * Last modified: Sun Jun 13 01:23:43 1993 too
 *
 * $Id: allocdatabuffer.h,v 1.3 1993/06/12 22:58:57 too Exp $
 *
 * HISTORY
 * $Log: allocdatabuffer.h,v $
 * Revision 1.3  1993/06/12  22:58:57  too
 * Added prototype for freeDataBuffer. Now part of allocDataBuffer is
 * inline function so it is faster.
 *
 * Revision 1.2  1993/06/07  12:37:20  too
 * Changed inet_ntoa, netdatabase functions and WaitSelect() use
 * separate buffers for their dynamic buffers
 *
 * Revision 1.1  1993/04/27  10:24:18  too
 * Initial revision
 *
 *
 */

#ifndef ALLOCDATABUFFER_H
#define ALLOCDATABUFFER_H

VOID freeDataBuffer(struct DataBuffer * DB);
BOOL doAllocDataBuffer(struct DataBuffer * DB, int size);

static inline BOOL allocDataBuffer(struct DataBuffer * DB, int size)
{
  if (DB->db_Size < size)
    return doAllocDataBuffer(DB, size);
  else
    return TRUE;
}

#endif /* ALLOCDATABUFFER_H */
