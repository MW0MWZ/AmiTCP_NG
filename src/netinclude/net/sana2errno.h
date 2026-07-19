#ifndef NET_SANA2ERRNO_H
#define NET_SANA2ERRNO_H
/* $Id: sana2errno.h,v 1.3 1993/06/04 11:16:15 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * sana2errno.h --- SANA-II error lists and printing
 *
 * Created      : Sun Apr 18 17:27:33 1993 ppessi
 * Last modified: Fri Jun  4 00:40:27 1993 jraja
 */

extern const short io_nerr;
extern const char * const io_errlist[]; 
extern const short sana2io_nerr;
extern const char * const sana2io_errlist[];
extern const short sana2wire_nerr;
extern const char * const sana2wire_errlist[];

void sana2perror(const char *banner, struct IOSana2Req *ios2);
void Sana2PrintFault(const char *banner, struct IOSana2Req *ios2);

#endif
