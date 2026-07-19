/*
 * $Id: sockargs.h,v 3.1 1994/01/06 13:39:42 too Exp $
 * 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created: Wed Jan  5 19:25:16 1994
 * Last modified: Wed Jan  5 19:27:36 1994 too
 * 
 * $Log: sockargs.h,v $
 * Revision 3.1  1994/01/06  13:39:42  too
 * extern prototype for sockArgs() which resides in amiga_syscalls.c
 *
 */

#ifndef API_SOCKARGS_H
#define API_SOCKARGS_H

/*
 * sockArgs code in amiga_syscalls.c
 */
LONG sockArgs(struct mbuf **mp, caddr_t buf, LONG buflen, LONG type);

#endif /* API_SOCKARGS_H */
