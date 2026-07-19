#ifndef ERRNO_H
#define ERRNO_H
/*
 * $Id: errno.h,v 1.4 1994/05/02 20:52:01 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *	              Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * errno.h -- errno.h for network programs
 *
 */

#include <sys/errno.h>

extern int errno;

extern int _OSERR;

#define __sys_nerr 80

#ifndef STRICT_ANSI
#define sys_nerr __sys_nerr
#endif

#endif /* ERRNO_H */
