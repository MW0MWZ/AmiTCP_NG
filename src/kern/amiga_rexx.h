/*
 * $Id: amiga_rexx.h,v 1.4 1994/01/05 10:20:38 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: amiga_rexx.h,v $
 * Revision 1.4  1994/01/05  10:20:38  jraja
 * Added prototypes for the new functions (rexx_show()&rexx_hide()).
 *
 * Revision 1.3  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.2  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.1  1993/03/05  03:26:02  ppessi
 * Initial revision
 *
 */

ULONG rexx_init(void);
BOOL rexx_show(void);
BOOL rexx_hide(void);
void rexx_deinit(void);
BOOL rexx_poll(void);

