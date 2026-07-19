#ifndef NET_A_IF_H
#define NET_A_IF_H
/*
**      $Filename: net/a_if.h $
**	$Release$
**      $Revision: 3.1 $
**      $Date: 1994/02/03 11:56:05 $
**
**	Old style ioctl structures to network interface 
**
**	Copyright © 1993,1994 AmiTCP/IP Group, <AmiTCP-Group@hut.fi>
**                  Helsinki University of Technology, Finland.
**                  All rights reserved.
*/

#ifndef NET_IF_H
#include <net/if.h>
#endif

/*
 * Compatibility interface structures
 */
struct	aifreq {
#define	AIFNAMSIZ	64
	char	ifr_name[AIFNAMSIZ]; /* sana-II name, e.g. "slip.device/0" */
	union {
		struct	sockaddr ifru_addr;
		struct	sockaddr ifru_dstaddr;
		struct	sockaddr ifru_broadaddr;
		short	ifru_flags;
		int	ifru_metric;
		caddr_t	ifru_data;
	} ifr_ifru;
};

struct aifaliasreq {
	char	ifra_name[AIFNAMSIZ]; /* sana-II name, e.g. "slip.device/0" */
	struct	sockaddr ifra_addr;
	struct	sockaddr ifra_broadaddr;
	struct	sockaddr ifra_mask;
};

#endif /* NET_A_IF_H */
