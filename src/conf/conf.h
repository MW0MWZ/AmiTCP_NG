/*
 * $Id: conf.h,v 1.3 1993/06/04 11:16:15 jraja Exp $
 * 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * This file contains several definitions which affect the compilation of the 
 * AmiTCP/IP code. Normally they are boolean switches and the comments tell
 * what happens if the value is TRUE (eg. non zero)
 *
 * HISTORY
 * $Log: conf.h,v $
 * Revision 1.3  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.2  1993/05/29  20:48:24  jraja
 * Added default values for IP configurable flags, removed GATEWAY.
 *
 * Revision 1.1  1993/05/17  00:50:39  ppessi
 * General configuration file.
 *
 */

/*
 * Do diagnostic tests which are not necessary in production version
 */
#define DIAGNOSTIC 1

/*
 * Be compatible with BSD 4.2. Affects only checksumming of UDP data. If true
 * the checksum is NOT calculated by default.
 */
#define COMPAT_42 0

/*
 * Make TCP compatible with BSD 4.2
 */
#define TCP_COMPAT_42 0

/*
 * protocol families
 */
#define INET 1
#define CCITT 0
#define NHY 0			/* HYPERchannel */
#define NIMP 0
#define ISO 0
#define NS 0
#define RMP 0

/*
 * optional protocols over IP
 */
#define NSIP 0
#define EON 0
#define TPIP 0

/*
 * default values for IP configurable flags
 */
#define IPFORWARDING    0
#define IPSENDREDIRECTS 1
#define IPPRINTFS       0

/*
 * Network level
 */
#define NETHER 1		/* Call ARP ioctl */
