#ifndef	SYS_A_IOCTL_H
#define	SYS_A_IOCTL_H
/*
**      $Filename: sys/a_ioctl.h $
**	$Release$
**      $Revision: 3.1 $
**      $Date: 1994/02/03 11:46:16 $
**
**	AmiTCP/IP 2 compatible ioctl codes to network interfaces
**
**	Copyright © 1993,1994 AmiTCP/IP Group, <AmiTCP-Group@hut.fi>
**                  Helsinki University of Technology, Finland.
**                  All rights reserved.
*/

#ifndef SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#define COMPAT_AMITCP2  1

/*
 * Compatiblity ioctl's
 */
#define	ASIOCSIFADDR	_IOW ('I',12,struct aifreq)	 /* set ifnet address */
#define	ASIOCGIFADDR	_IOWR('I',33,struct aifreq)	 /* get ifnet address */
#define	ASIOCSIFDSTADDR	_IOW ('I',14,struct aifreq)	 /* set p-p address */
#define	ASIOCGIFDSTADDR	_IOWR('I',34,struct aifreq)	 /* get p-p address */
#define	ASIOCSIFFLAGS	_IOW ('I',16,struct aifreq)	 /* set ifnet flags */
#define	ASIOCGIFFLAGS	_IOWR('I',17,struct aifreq)	 /* get ifnet flags */
#define	ASIOCGIFBRDADDR	_IOWR('I',35,struct aifreq)	 /* get bdcast addr */
#define	ASIOCSIFBRDADDR	_IOW ('I',19,struct aifreq)	 /* set bcast addr */
#define	ASIOCGIFNETMASK	_IOWR('I',37,struct aifreq)	 /* get net addr mask */
#define	ASIOCSIFNETMASK	_IOW ('I',22,struct aifreq)	 /* set net addr mask */
#define	ASIOCGIFMETRIC	_IOWR('I',23,struct aifreq)	 /* get IF metric */
#define	ASIOCSIFMETRIC	_IOW ('I',24,struct aifreq)	 /* set IF metric */
#define	ASIOCDIFADDR	_IOW ('I',25,struct aifreq)	 /* delete IF addr */

#define	ASIOCGIFCONF	_IOWR('I',36,struct ifconf)	 /* get ifnet list */
#define	ASIOCAIFADDR	_IOW ('I',26,struct aifaliasreq) /* add/chg IF alias */

#define	ASIOCSARP	_IOW('I', 30, struct aarpreq)    /* set arp entry */
#define	ASIOCGARP	_IOWR('I',38, struct aarpreq)    /* get arp entry */
#define	ASIOCDARP	_IOW('I', 32, struct aarpreq)    /* delete arp entry */

#endif /* !SYS_A_IOCTL_H */
