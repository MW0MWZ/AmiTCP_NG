/*
 * $Id: cdefs.h,v 1.12 1994/03/22 07:18:15 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: cdefs.h,v $
 * Revision 1.12  1994/03/22  07:18:15  jraja
 * Added compiler dependent definitions ASM and REG(x).
 *
 * Revision 1.11  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.10  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.9  1993/05/05  16:10:47  puhuri
 * Fixes for final demo.
 *
 * Revision 1.8  93/04/13  21:56:30  21:56:30  jraja (Jarno Tapio Rajahalme)
 * Added new keyword: ALIGNED.
 * 
 * Revision 1.7  93/04/11  22:21:36  22:21:36  jraja (Jarno Tapio Rajahalme)
 * Added function modifier STKARGFUN to be used with functions whose arguments
 * MUST be passed in stack.
 * 
 * Revision 1.6  93/03/07  00:57:12  00:57:12  jraja (Jarno Tapio Rajahalme)
 * Added definition for REGARGFUN keyword.
 * 
 * Revision 1.5  93/03/03  20:08:54  20:08:54  jraja (Jarno Tapio Rajahalme)
 * Removed redundant copyright message.
 * 
 * Revision 1.4  93/03/03  20:06:32  20:06:32  jraja (Jarno Tapio Rajahalme)
 * Cleanup. Changed _CDEFS_H_ to SYS_CDEFS_H.
 * 
 * Revision 1.3  93/03/03  12:32:04  12:32:04  jraja (Jarno Tapio Rajahalme)
 * Fixed _SASC_60 to __SASC_60.
 * Added SAVEDS definition.
 * 
 * Revision 1.2  92/12/22  00:26:32  00:26:32  jraja (Jarno Tapio Rajahalme)
 * Added trailing underscores to __GNUC__ keywords and SASC6.0 inline
 * definition
 * 
 * Revision 1.1  92/11/20  15:41:43  15:41:43  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 *
 */

/*
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	@(#)cdefs.h	7.6 (Berkeley) 5/4/91
 */

#ifndef	SYS_CDEFS_H
#define	SYS_CDEFS_H

#if defined(__cplusplus)
#define	__BEGIN_DECLS	extern "C" {
#define	__END_DECLS	};
#else
#define	__BEGIN_DECLS
#define	__END_DECLS
#endif

/*
 * SAVEDS should be used in all function definitions which will be called 
 * from other tasks than AmiTCP/IP. Is restores the global data base pointer
 * as the first thing in the function body.
 *
 * REGARGFUN contains special keywords which should be used when functions
 * used through shared library are referenced.
 */
#if __SASC_60
#define SAVEDS __saveds
#define REGARGFUN __asm
#define STKARGFUN __stdargs
#define ALIGNED __aligned
#define ASM __asm
#define REG(x) register __##x
#else
#define SAVEDS
#define REGARGFUN
#define STKARGFUN
#define ALIGNED
#define ASM
#define REG(x)
#endif

/*
 * The __CONCAT macro is used to concatenate parts of symbol names, e.g.
 * with "#define OLD(foo) __CONCAT(old,foo)", OLD(foo) produces oldfoo.
 * The __CONCAT macro is a bit tricky -- make sure you don't put spaces
 * in between its arguments.  __CONCAT can also concatenate double-quoted
 * strings produced by the __STRING macro, but this only works with ANSI C.
 */
#if defined(__STDC__) || defined(__cplusplus)
#define	__P(protos)	protos		/* full-blown ANSI C */
#define	__CONCAT(x,y)	x ## y
#define	__STRING(x)	#x

#if __SASC_60
#define inline          __inline
#endif

#else	/* !(__STDC__ || __cplusplus) */
#define	__P(protos)	()		/* traditional C preprocessor */
#define	__CONCAT(x,y)	x/**/y
#define	__STRING(x)	"x"

#if __GNUC__
#define	const		__const__	/* GCC: ANSI C with -traditional */
#define	inline		__inline__
#define	signed		__signed__
#define	volatile	__volatile__

#else	/* !__GNUC__ */
#define	const				/* delete ANSI C keywords */
#define	inline
#define	signed
#define	volatile
#endif	/* !__GNUC__ */
#endif	/* !(__STDC__ || __cplusplus) */

#endif /* !SYS_CDEFS_H */
