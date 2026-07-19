/* $Id: netisr.h,v 1.4 1993/06/04 11:16:15 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * netisr.h - Network protocol input queue scheduling
 * 
 * Last modified: Fri Jun  4 00:38:40 1993 jraja
 * 
 * HISTORY
 * $Log: netisr.h,v $
 * Revision 1.4  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.3  1993/05/16  21:09:43  ppessi
 * RCS version changed.
 *
 * Revision 1.2  1993/02/28  21:41:50  ppessi
 * Revised version.
 *
 * Revision 1.1  93/02/20  09:18:22  09:18:22  ppessi (Pekka Pessi)
 * Initial revision
 */

/*
 * Frame dispatchers set an appropriate protocol input queue bit 
 */

#define	NETISR_RAW	0		/* same as AF_UNSPEC */
#define	NETISR_IP	2		/* same as AF_INET */
#define	NETISR_IMP	3		/* same as AF_IMPLINK */
#define	NETISR_NS	6		/* same as AF_NS */
#define	NETISR_ISO	7		/* same as AF_ISO */
#define	NETISR_CCITT	10		/* same as AF_CCITT */
#define NETISR_ARP      31

void schednetisr(int isr);
void schednetisr_nosignal(int isr);
void net_poll(void);
