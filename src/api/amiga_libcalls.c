RCS_ID_C="$Id: amiga_libcalls.c,v 1.19 1993/06/11 19:01:41 too Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * HISTORY
 * $Log: amiga_libcalls.c,v $
 * Revision 1.19  1993/06/11  19:01:41  too
 * Changed register argument from a0 to d0 in functions
 * Inet_NtoA, Inet_LnaOf and Inet_NetOf
 *
 * Revision 1.18  1993/06/07  12:37:20  too
 * Changed inet_ntoa, netdatabase functions and WaitSelect() use
 * separate buffers for their dynamic buffers
 *
 * Revision 1.17  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.16  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.15  1993/05/04  14:08:05  too
 * Changed local api function names to the same as the names given
 * to API programmer, but w/ starting _
 *
 * Revision 1.14  93/05/04  12:05:28  12:05:28  jraja (Jarno Tapio Rajahalme)
 * fix.
 * 
 * Revision 1.13  93/04/29  02:21:17  02:21:17  jraja (Jarno Tapio Rajahalme)
 * Added function rresvport().
 * 
 * Revision 1.12  93/04/26  13:18:50  13:18:50  jraja (Jarno Tapio Rajahalme)
 * Changed inet_ntoa() to use return buffer as defined in amiga_api.h.
 * 
 * Revision 1.11  93/04/26  12:32:51  12:32:51  too (Tomi Ollila)
 * Added #Include <allocdatabinclude <api/allocdatabuffer.h>
 * 
 * Revision 1.10  93/04/26  11:52:35  11:52:35  too (Tomi Ollila)
 * Changed include paths of amiga_api.h, amiga_libcallentry.h and amiga_raf.h
 * from kern to api
 * 
 * Revision 1.9  93/04/26  11:06:20  11:06:20  too (Tomi Ollila)
 * Added CHECK_TASK2()
 * 
 * Revision 1.8  93/04/25  19:58:43  19:58:43  too (Tomi Ollila)
 * Changed inet_ntoa to use dynamic buffers for return string
 * 
 * Revision 1.1  93/04/25  19:56:18  19:56:18  too (Tomi Ollila)
 * Initial revision
 * 
 * Revision 1.7  93/04/06  15:15:18  15:15:18  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.6  93/03/16  08:30:05  08:30:05  jraja (Jarno Tapio Rajahalme)
 * Removed redundant include of sys/stat.h.
 * 
 * Revision 1.5  93/03/12  23:59:14  23:59:14  ppessi (Pekka Pessi)
 * Added inte_addr() return tyep.
 * 
 * Revision 1.4  93/03/10  21:06:18  21:06:18  too (Tomi Ollila)
 * Changed use of libPtr->buffer to the new union member
 * 
 * Revision 1.3  93/03/09  17:52:22  17:52:22  ppessi (Pekka Pessi)
 * Removed passign struct in_addr passing, now passes only a LONG;
 * fixed inet_ntoa not to print excessive spaces
 * 
 * Revision 1.2  93/03/07  00:46:15  00:46:15  jraja (Jarno Tapio Rajahalme)
 * Added SAVEDS to API function definitions.
 * 
 * Revision 1.1  93/03/06  02:08:48  02:08:48  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 */

/*
 * Copyright (c) 1983, 1990 Regents of the University of California.
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
 */

/*
 * amiga_libcalls.c --- address-conversion and other self-contained library calls.
 *
 * The vectors that do NOT need the socket layer -- pure utility functions the
 * library exports: inet_addr()/inet_aton() (dotted-decimal string -> 32-bit
 * address), inet_ntoa()/Inet_NtoA() (the reverse), Inet_LnaOf/Inet_NetOf, and
 * similar. inet_aton's isdigit()/isspace() use is the reason this file pulls in
 * <ctype.h> -- see the -noixemul/libnix ctype note in PORTING.md.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/ioctl.h>
#include <sys/protosw.h>
#include <sys/malloc.h>
#include <sys/synch.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <netinet/in.h>

#include <kern/amiga_includes.h>

#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <api/allocdatabuffer.h>

#include <ctype.h>

/*
 * Functions which are defined in link library in unix systems
 */

/* from inet_ntoa.c */
/*
 * Convert network-format internet address
 * to base 256 d.d.d.d representation.
 */
char *SAVEDS RAF2(_Inet_NtoA,
		 struct SocketBase *,	libPtr,	a6,
		 ULONG,			s_addr,	d0)
#if 0
{
#endif
  (void)NTOHL(s_addr);

  CHECK_TASK2();

  sprintf(libPtr->inet_ntoa,
	  "%ld.%ld.%ld.%ld", 
	  (s_addr>>24) & 0xff, 
	  (s_addr>>16) & 0xff, 
	  (s_addr>>8) & 0xff, 
	  s_addr & 0xff);
  return ((char *)libPtr->inet_ntoa);
}

/* from inet_addr.c */
/* 
 * Check whether "cp" is a valid ascii representation
 * of an Internet address and convert to a binary address.
 * Returns 1 if the address is valid, 0 if not.
 * This replaces inet_addr, the return value from which
 * cannot distinguish between failure and a local broadcast address.
 */

int
inet_aton(register const char *cp, struct in_addr *addr)
{
	register u_long val, base, n;
	register char c;
	u_long parts[4], *pp = parts;
	
	for (;;) {
		/*
		 * Collect number up to ``.''.
		 * Values are specified as for C:
		 * 0x=hex, 0=octal, other=decimal.
		 */
		val = 0; base = 10;
		if (*cp == '0') {
			if (*++cp == 'x' || *cp == 'X')
				base = 16, cp++;
			else
				base = 8;
		}
		while ((c = *cp) != '\0') {
			if (isascii(c) && isdigit(c)) {
				val = (val * base) + (c - '0');
				cp++;
				continue;
			}
			if (base == 16 && isascii(c) && isxdigit(c)) {
				val = (val << 4) + 
					(c + 10 - (islower(c) ? 'a' : 'A'));
				cp++;
				continue;
			}
			break;
		}
		if (*cp == '.') {
			/*
			 * Internet format:
			 *	a.b.c.d
			 *	a.b.c	(with c treated as 16-bits)
			 *	a.b	(with b treated as 24 bits)
			 */
			if (pp >= parts + 3 || val > 0xff)
				return (0);
			*pp++ = val, cp++;
		} else
			break;
	}
	/*
	 * Check for trailing characters.
	 */
	if (*cp && (!isascii(*cp) || !isspace(*cp)))
		return (0);
	/*
	 * Concoct the address according to
	 * the number of parts specified.
	 */
	n = pp - parts + 1;
	switch (n) {

	case 1:				/* a -- 32 bits */
		break;

	case 2:				/* a.b -- 8.24 bits */
		if (val > 0xffffff)
			return (0);
		val |= parts[0] << 24;
		break;

	case 3:				/* a.b.c -- 8.8.16 bits */
		if (val > 0xffff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16);
		break;

	case 4:				/* a.b.c.d -- 8.8.8.8 bits */
		if (val > 0xff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
		break;
	}
	if (addr)
		addr->s_addr = htonl(val);
	return (1);
}

/*
 * Ascii internet address interpretation routine.
 * The value returned is in network order.
 */

ULONG SAVEDS RAF2(_inet_addr,
		  struct SocketBase *,	libPtr,	a6,
		  const char *,		cp,	a0)
#if 0
{
#endif
	struct in_addr val;
	(void)libPtr;

	if (inet_aton(cp, &val))
		return (val.s_addr);
	return (INADDR_NONE);
}

/* from inet_lnaof.c */
/*
 * Return the local network address portion of an
 * internet address; handles class a/b/c network
 * number formats.
 */
ULONG SAVEDS RAF2(_Inet_LnaOf,
		  struct SocketBase *,	libPtr,	a6,
		  ULONG,	        s_addr, d0)
#if 0
{
#endif
	(void)libPtr;
	(void)NTOHL(s_addr);

	if (IN_CLASSA(s_addr))
		return ((s_addr)&IN_CLASSA_HOST);
	else if (IN_CLASSB(s_addr))
		return ((s_addr)&IN_CLASSB_HOST);
	else
		return ((s_addr)&IN_CLASSC_HOST);
}

/* from inet_netof.c */
/*
 * Return the network number from an internet
 * address; handles class a/b/c network #'s.
 */
ULONG SAVEDS RAF2(_Inet_NetOf,
		  struct SocketBase *,	libPtr,	a6,
		  ULONG,	        s_addr, d0)
#if 0
{
#endif
	(void)libPtr;
	(void)NTOHL(s_addr);

	if (IN_CLASSA(s_addr))
		return (((s_addr)&IN_CLASSA_NET) >> IN_CLASSA_NSHIFT);
	else if (IN_CLASSB(s_addr))
		return (((s_addr)&IN_CLASSB_NET) >> IN_CLASSB_NSHIFT);
	else
		return (((s_addr)&IN_CLASSC_NET) >> IN_CLASSC_NSHIFT);
}

/* from inet_makeaddr.c */
/*
 * Formulate an Internet address from network + host.  Used in
 * building addresses stored in the ifnet structure.
 */
ULONG SAVEDS RAF3(_Inet_MakeAddr,
		  struct SocketBase *,	libPtr,	a6,
		  ULONG,	        net,	d0,
		  ULONG,	        host,	d1)
#if 0
{
#endif
	u_long addr;
	(void)libPtr;

	if (net < 128)
		addr = (net << IN_CLASSA_NSHIFT) | (host & IN_CLASSA_HOST);
	else if (net < 65536)
		addr = (net << IN_CLASSB_NSHIFT) | (host & IN_CLASSB_HOST);
	else if (net < 16777216L)
		addr = (net << IN_CLASSC_NSHIFT) | (host & IN_CLASSC_HOST);
	else
		addr = net | host;

	return htonl(addr);
}

/* from inet_network.c */
/*
 * Internet network address interpretation routine.
 * The library routines call this routine to interpret
 * network numbers.
 */
ULONG SAVEDS RAF2(_inet_network,
		  struct SocketBase *,	libPtr,	a6,
		  const char *,		cp,	a0)
#if 0
{
#endif
	register u_long val, base, n;
	register char c;
	u_long parts[4], *pp = parts;
	register int i;
	(void)libPtr;

again:
	val = 0; base = 10;
	if (*cp == '0')
		base = 8, cp++;
	if (*cp == 'x' || *cp == 'X')
		base = 16, cp++;
	while ((c = *cp)) {
		if (isdigit(c)) {
			val = (val * base) + (c - '0');
			cp++;
			continue;
		}
		if (base == 16 && isxdigit(c)) {
			val = (val << 4) + (c + 10 - (islower(c) ? 'a' : 'A'));
			cp++;
			continue;
		}
		break;
	}
	if (*cp == '.') {
		if (pp >= parts + 4)
			return (INADDR_NONE);
		*pp++ = val, cp++;
		goto again;
	}
	if (*cp && !isspace(*cp))
		return (INADDR_NONE);
	*pp++ = val;
	n = pp - parts;
	if (n > 4)
		return (INADDR_NONE);
	for (val = 0, i = 0; i < n; i++) {
		val <<= 8;
		val |= parts[i] & 0xff;
	}
	return (val);
}

