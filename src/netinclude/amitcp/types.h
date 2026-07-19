/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

#ifndef AMITCP_TYPES_H
#define AMITCP_TYPES_H
/*
 * $Id: types.h,v 3.1 1994/04/07 20:20:16 jraja Exp $
 *
 * Common types previously defined in multiple headers.
 *
 * Copyright (c) 1994 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 *       Created: Thu Apr  7 22:50:12 1994 jraja
 * Last modified: Thu Apr  7 23:19:57 1994 jraja
 *
 * HISTORY
 * $Log: types.h,v $
 * Revision 3.1  1994/04/07  20:20:16  jraja
 * Initial revision.
 *
 */

#ifndef _UID_T
#define _UID_T long
typedef _UID_T uid_t;
#endif

#ifndef _GID_T
#define _GID_T long
typedef _GID_T gid_t;
#endif

#ifndef _PID_T
#define _PID_T struct Task *
typedef	_PID_T pid_t;			/* process id */
#endif

#ifndef _MODE_T
#define _MODE_T unsigned short 
typedef _MODE_T mode_t;
#endif

#ifndef _TIME_T
#define _TIME_T long
typedef _TIME_T time_t;
#endif

#ifndef NULL
/* PORT (AmiTCP_NG, bebbo gcc 6.5): modern gcc's __VERSION__ is a string literal,
 * illegal in a #if expression. The original `__SASC && (__VERSION__...)` still
 * parses the RHS even though __SASC is 0, so nest the SAS/C test to keep
 * __VERSION__ out of the preprocessor's sight under gcc. */
#ifdef __SASC
#if (__VERSION__ > 6 || __REVISION__ >= 50)
#include <sys/commnull.h>
#else
#define	NULL 0L
#endif
#else
#define	NULL 0L
#endif
#endif

#endif /* !AMITCP_TYPES_H */
