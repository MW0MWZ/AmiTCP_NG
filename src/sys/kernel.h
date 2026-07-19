/*
 * $Id: kernel.h,v 1.7 1993/06/04 11:16:15 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: kernel.h,v $
 * Revision 1.7  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.6  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.5  1993/03/03  19:43:23  jraja
 * Major Cleanup. Changed hz & tick to preprocessor defines.
 *
 * Revision 1.4  93/02/04  18:14:21  18:14:21  jraja (Jarno Tapio Rajahalme)
 * fixed bug in #if -statement.
 * 
 * Revision 1.3  92/12/22  00:11:52  00:11:52  jraja (Jarno Tapio Rajahalme)
 * made 'tick' visible.
 * 
 * Revision 1.2  92/11/20  15:56:54  15:56:54  jraja (Jarno Tapio Rajahalme)
 * Added #ifndef AMITCP's to make this compile.
 * 
 * Revision 1.1  92/11/20  15:47:56  15:47:56  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 *
 */

#ifndef SYS_KERNEL_H
#define SYS_KERNEL_H

#define hz   (50)		/* computational clock frequency */
#define tick (1000000/hz)	/* microseconds / hz */

#endif /* !SYS_KERNEL_H */
