/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

#ifndef AMITCP_SOCKETBASETAGS_H
#define AMITCP_SOCKETBASETAGS_H
/*
 * $Id: socketbasetags.h,v 3.3 1994/04/07 20:33:07 jraja Exp $
 *
 * Copyright (c) 1994 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * Created: Sun Jan 9 14:33:12 1994 jraja
 * Last modified: Thu Apr  7 23:32:25 1994 jraja
 *
 * HISTORY
 * $Log: socketbasetags.h,v $
 * Revision 3.3  1994/04/07  20:33:07  jraja
 * Put SBTC_COMPAT43 inside #ifdef notyet.
 *
 * Revision 3.2  1994/04/02  10:22:38  jraja
 * Added tag code SBTC_HERRNOLONGPTR for h_errno pointer.
 *
 * Revision 3.1  1994/03/29  12:56:35  ppessi
 * Added SBTC_COMPAT43 tag
 *
 * Revision 1.5  1994/03/22  07:17:13  jraja
 * Added SBTC_FDCALLBACK & definitions for its actions.
 *
 * Revision 1.4  1994/02/26  18:03:28  jraja
 * Moved from netinclude to netinclude/amitcp.
 *
 * Revision 1.3  1994/02/15  21:13:47  jraja
 * fixed the SBTC_ERRNOPTR(size) macro.
 *
 * Revision 1.2  1994/01/20  02:38:00  jraja
 * Reorganized the tags, added rest of the error string table tags and
 * changed the errnoPtr setting tags.
 *
 * Revision 1.1  1994/01/12  06:59:54  jraja
 * Initial revision
 *
 */

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

/*
 * utility/tagitem.h specifies that bits 16-30 in tags are reserved. So we 
 * don't use them for maximum compatability.
 */

/*
 * Argument passing convention (bit 15)
 */
#define SBTF_REF 0x8000		/* 0x0000 == VAL */

/*
 * Code (bits 1-14)
 */
#define SBTB_CODE 1
#define SBTS_CODE 0x3FFF
#define SBTM_CODE(tag) (((UWORD)(tag) >> SBTB_CODE) & SBTS_CODE)

/* 
 * Direction (bit 0)
 */
#define SBTF_SET  0x1		/* 0 == GET */

/*
 * Macros to set things up
 * We keep the TAG_USER (bit 31) set to be compatible with tagitem.h
 * conventions.
 */
#define SBTM_GETREF(code) \
  (TAG_USER | SBTF_REF | (((code) & SBTS_CODE) << SBTB_CODE))
#define SBTM_GETVAL(code) \
  (TAG_USER | (((code) & SBTS_CODE) << SBTB_CODE))
#define SBTM_SETREF(code) \
  (TAG_USER | SBTF_REF | (((code) & SBTS_CODE) << SBTB_CODE) | SBTF_SET)
#define SBTM_SETVAL(code) \
  (TAG_USER | (((code) & SBTS_CODE) << SBTB_CODE) | SBTF_SET)

/*
 * Tag code definitions. These codes are used with one of the above macros.
 *
 * All arguments are ULONG's or pointers (PTR suffix).
 *
 * NOTE: Tag code 0 is not used (see utility/tagitem.h).
 */

/* signal masks */
#define SBTC_BREAKMASK		1
#define SBTC_SIGIOMASK		2
#define SBTC_SIGURGMASK		3

/* error code handling */
#define SBTC_ERRNO		6
#define SBTC_HERRNO		7

/* socket descriptor table related tags */
#define SBTC_DTABLESIZE         8

/* link library fd allocation callback
 * 
 * Argument is a callback function with following prototype
 *
 * int fd = fdCallback(int fd, int action);
 *     D0                  D0      D1
 *
 * see net.lib sources for an example
 */
#define SBTC_FDCALLBACK         9
/*
 * "action" values:
 */
#define FDCB_FREE  0
#define FDCB_ALLOC 1
#define FDCB_CHECK 2

/* syslog variables (see netinclude:sys/syslog.h for values) */
#define SBTC_LOGSTAT		10
#define SBTC_LOGTAGPTR		11
#define SBTC_LOGFACILITY	12
#define SBTC_LOGMASK		13

/*
 * The argument of following error string tags is a ULONG,
 * where the error number is stored. On return the string pointer is 
 * returned on this same ULONG. (GET ONLY)
 *
 * NOTE: error numbers defined in <exec/errors.h> are negative and must be
 * negated (turned to positive) before passing to the SocketBaseTagList().
 */
#define SBTC_ERRNOSTRPTR	14 /* <sys/errno.h> */
#define SBTC_HERRNOSTRPTR	15 /* <netdb.h> */
#define SBTC_IOERRNOSTRPTR	16 /* <exec/errors.h> SEE NOTE ABOVE */
#define SBTC_S2ERRNOSTRPTR	17 /* <devices/sana2.h> */
#define SBTC_S2WERRNOSTRPTR	18 /* <devices/sana2.h> */


/* errno pointer & size SETTING (only) */
#define SBTC_ERRNOBYTEPTR	21
#define SBTC_ERRNOWORDPTR	22
#define SBTC_ERRNOLONGPTR	24
/*
 * Macro for generating the errnoptr tag code from a (constant) size.
 * only 1,2 & 4 are legal 'size' values. If the 'size' value is illegal,
 * the tag is set to 0, which causes SocketBaseTagList() to fail.
 */
#define SBTC_ERRNOPTR(size)    ((size == sizeof(long)) ? SBTC_ERRNOLONGPTR   :\
				((size == sizeof(short)) ? SBTC_ERRNOWORDPTR :\
				 ((size == sizeof(char)) ? SBTC_ERRNOBYTEPTR :\
				  0)))

/* h_errno pointer */
#define SBTC_HERRNOLONGPTR	25

#ifdef notyet
/*
 * Different boolean variables
 */
/* use 4.3BSD compatible sockaddr structures */
#define SBTC_COMPAT43           29
#endif

/*
 * PORT (AmiTCP_NG): Roadshow extension capability codes (from the Roadshow
 * <libraries/bsdsocket.h>). A Roadshow-aware client queries these through
 * SocketBaseTagList BEFORE calling an extension family, so it can gracefully
 * skip families the library does not provide. AmiTCP 3.0b2 knew nothing of them
 * -- an unknown code failed the whole SocketBaseTagList call, which some tools
 * read as a hard error. We now answer every one honestly (see amiga_generic2.c):
 * 1 for families we implement, 0 for those still stubbed. As tranches land, flip
 * the corresponding answer from 0 to 1. Codes 40-69 do not collide with AmiTCP's
 * own 1-29.
 */
#define SBTC_NUM_PACKET_FILTER_CHANNELS		40 /* BPF: # of capture channels */
#define SBTC_HAVE_ROUTING_API			41
#define SBTC_HAVE_INTERFACE_API			47
#define SBTC_HAVE_MONITORING_API		50
#define SBTC_HAVE_STATUS_API			53
#define SBTC_HAVE_DNS_API			54
#define SBTC_HAVE_LOCAL_DATABASE_API		59
#define SBTC_HAVE_ADDRESS_CONVERSION_API	60
#define SBTC_HAVE_KERNEL_MEMORY_API		61
#define SBTC_HAVE_SERVER_API			63
#define SBTC_HAVE_ROADSHOWDATA_API		67
#define SBTC_HAVE_GETHOSTADDR_R_API		69

/*
 * SBTC_SYSTEM_STATUS (GET-only) reports an SBSYSSTAT_* bitmask describing what the
 * stack currently has configured. Roadshow's GetNetStatus tool reads this to decide
 * whether the machine is "online" and which facilities are up. (Roadshow value 56.)
 */
#define SBTC_SYSTEM_STATUS			56

#define SBSYSSTAT_Interfaces		(1L<<0)	/* >=1 non-loopback interface up */
#define SBSYSSTAT_PTP_Interfaces	(1L<<1)	/* >=1 point-to-point interface up */
#define SBSYSSTAT_BCast_Interfaces	(1L<<2)	/* >=1 broadcast interface up */
#define SBSYSSTAT_Resolver		(1L<<3)	/* >=1 domain name server set */
#define SBSYSSTAT_Routes		(1L<<4)	/* >=1 route configured */
#define SBSYSSTAT_DefaultRoute		(1L<<5)	/* a default route is present */

#endif /* !AMITCP_SOCKETBASETAGS_H */
