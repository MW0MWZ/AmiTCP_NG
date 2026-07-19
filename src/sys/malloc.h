/*
 * $Id: malloc.h,v 1.15 1994/03/29 20:06:10 jraja Exp $
 *
 * HISTORY
 * $Log: malloc.h,v $
 * Revision 1.15  1994/03/29  20:06:10  jraja
 * Added macros to prevent passing of unnecessary arguments.
 *
 * Revision 1.14  1994/03/26  10:04:07  too
 * Moved bsd_malloc() and bsd_free() inline functions to kern/kern_malloc.c
 * (now normal functions). Added prototype for bsd_realloc().
 *
 * Revision 1.13  1993/05/16  23:15:36  ppessi
 * Added M_ARPENT
 *
 * Revision 1.12  1993/04/28  22:09:44  ppessi
 * Added M_CFGVAR and M_NETDB as new memory types.
 *
 * Revision 1.11  93/03/11  19:40:33  19:40:33  jraja (Jarno Tapio Rajahalme)
 * Changed mallocSemaphore to malloc_semaphore.
 * 
 * Revision 1.10  93/03/11  12:27:44  12:27:44  jraja (Jarno Tapio Rajahalme)
 * Minor Clean up.
 * 
 * Revision 1.9  93/03/03  19:35:24  19:35:24  jraja (Jarno Tapio Rajahalme)
 * Cleanup. Removed redundant memory types, changed _MALLOC_H_ to SYS_MALLOC_H.
 * 
 * Revision 1.8  93/02/27  12:07:44  12:07:44  ppessi (Pekka Pessi)
 * Added memory type for network interface (M_IFNET)
 * 
 * Revision 1.7  93/02/27  12:05:52  12:05:52  jraja (Jarno Tapio Rajahalme)
 * Added #include <cdefs.h> for inline functions.
 * 
 * Revision 1.6  93/02/24  12:56:09  12:56:09  jraja (Jarno Tapio Rajahalme)
 * Changed malloc_init prototype.
 * 
 * Revision 1.5  93/02/24  11:15:41  11:15:41  jraja (Jarno Tapio Rajahalme)
 * Changed path uxkern to kern.
 * 
 * Revision 1.4  93/01/23  22:21:32  22:21:32  jraja (Jarno Tapio Rajahalme)
 * Protected calls to malloc() & free() with a semaphore (because they are
 * not re-entrant).
 * 
 * Revision 1.3  93/01/06  19:11:56  19:11:56  jraja (Jarno Tapio Rajahalme)
 * Removed all definitions related to virtual memory subsystem.
 * Added inline definitions for bsd_malloc and bsd_free.
 * 
 * Revision 1.2  92/11/20  15:56:56  15:56:56  jraja (Jarno Tapio Rajahalme)
 * Added #ifndef AMITCP's to make this compile.
 * 
 * Revision 1.1  92/11/20  15:47:59  15:47:59  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 */

/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)malloc.h	7.25 (Berkeley) 5/15/91
 */

#ifndef SYS_MALLOC_H
#define	SYS_MALLOC_H

/*
 * flags to malloc
 */
#define	M_WAITOK	0x0000
#define	M_NOWAIT	0x0001

/*
 * Types of memory to be allocated
 */
#define	M_FREE		0	/* should be on free list */
#define	M_MBUF		1	/* mbuf */
#define	M_SOCKET	2	/* socket structure */
#define	M_PCB		3	/* protocol control block */
#define	M_RTABLE	4	/* routing tables */
#define	M_HTABLE	5	/* IMP host tables */
#define	M_FTABLE	6	/* fragment reassembly header */
#define	M_IFADDR	7	/* interface address */
#define	M_SOOPTS	8 	/* socket options */
#define	M_SONAME	9 	/* socket name */
#define	M_IOCTLOPS	10	/* ioctl data buffer */
#define	M_FILEDESC	11	/* Open file descriptor table */
#define	M_TEMP		12	/* misc temporary data buffers */
#define	M_IFNET		13	/* network interface structure */
#define M_CFGVAR 	14	/* configureable variable */
#define M_NETDB  	15	/* netdb node */
#define M_ARPENT        16	/* ARP entry */
#define	M_LAST		17

#ifdef KERNEL

/*
 * Use malloc & free of the standard library.
 *
 * NOTE: Because these are called from different tasks concurrently, these
 * have to be protected with a mutex.
 */
#ifndef AMIGA_INCLUDES_H
#include <kern/amiga_includes.h>
#endif

#ifndef SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

/*
 * prototype for the initialization function
 */
BOOL malloc_init(void);

/*
 * prototypes for BSD malloc wrapper functions.
 */

#ifndef USE_MALLOC_TYPE_TRACKING
/*
 * These macros are used to prevent unnecessary passing of arguments, which 
 * are currently not used.
 */
#define bsd_malloc(size, type, flags) bsd_malloc(size)
#define bsd_free(addr, type) bsd_free(addr)
#define bsd_realloc(mem, size, type, flags) bsd_realloc(mem, size)
#endif

void *	bsd_malloc(unsigned long size, int type, int flags);
void	bsd_free(void *addr, int type);
void *	bsd_realloc(void * mem, unsigned long size, int type, int flags);

/*
 * Macro versions for the usual cases of malloc/free
 */
#define	MALLOC(space, cast, size, type, flags) \
	(space) = (cast)bsd_malloc((u_long)(size), type, flags)
#define FREE(addr, type) bsd_free((caddr_t)(addr), type)

#endif /* KERNEL */
#endif /* !SYS_MALLOC_H */
