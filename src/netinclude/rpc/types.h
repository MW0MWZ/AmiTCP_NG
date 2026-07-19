/*
 * $Id: types.h,v 1.4 1994/04/07 20:28:04 jraja Exp $
 *
 * $Log: types.h,v $
 * Revision 1.4  1994/04/07  20:28:04  jraja
 * Changed _UNISTD_H_ to UNISTD_H
 *
 * Revision 1.3  1994/03/09  01:55:46  jraja
 * Added unistd.h, cleaned up a little.
 *
 * Revision 1.2  1993/11/10  01:02:57  jraja
 * ANSI prototypes.
 * Added few misc. prototypes.
 *
 */
/* @(#)types.h	2.3 88/08/15 4.0 RPCSRC */
/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */
/*      @(#)types.h 1.18 87/07/24 SMI      */

/*
 * Rpc additions to <sys/types.h>
 */
#ifndef __TYPES_RPC_HEADER__
#define __TYPES_RPC_HEADER__

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif
#ifndef SYS_TIME_H
#include <sys/time.h>
#endif

#include <stddef.h>

#ifndef UNISTD_H
#include <unistd.h>
#endif

/*
 * Check against short integers
 */
#if __SASC && defined(_SHORTINT)
#error RPC library works with 32-bit integers only.
#endif

/*
 * Support for Amiga SAS/C 6.x (add others)
 */
#if __SASC
#define XDRFUN __regargs
#else
#define XDRFUN
#endif /* __SASC */

#define	bool_t  int
#define	enum_t	int
#ifndef FALSE
#	define	FALSE	(0)
#endif
#ifndef TRUE
#	define	TRUE	(1)
#endif
#define __dontcare__	-1
#ifndef NULL
#	define NULL 0L
#endif

extern void exit(int code);
#ifndef USE_MEM_STUBS
extern void *malloc(size_t n);
extern void *calloc(size_t c, size_t n);
extern void free(void *p);
#define mem_alloc malloc
#define mem_calloc calloc
#define mem_free(ptr, bsize)	free(ptr)
#else
extern void *mem_alloc(size_t bsize);
extern void *mem_calloc(size_t nelt, size_t esize);
extern void mem_free(void *ptr, size_t bsize);
#endif

#ifndef INADDR_LOOPBACK
#define       INADDR_LOOPBACK         (u_long)0x7F000001
#endif
#ifndef MAXHOSTNAMELEN
#define        MAXHOSTNAMELEN  64
#endif

extern int _rpc_dtablesize(void);
extern int bindresvport(int sd, struct sockaddr_in * sin);
extern void get_myaddress(struct sockaddr_in * addr);

#endif /* ndef __TYPES_RPC_HEADER__ */
