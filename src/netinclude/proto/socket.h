#ifndef PROTO_SOCKET_H
#define PROTO_SOCKET_H
/*
**      $Filename: proto/socket.h $
**	$Release$
**      $Revision: 3.3 $
**      $Date: 1994/02/26 18:52:01 $
**
**	SAS C Prototypes for bsdsocket.library
**
**	Copyright © 1993 AmiTCP/IP Group, <AmiTCP-Group@hut.fi>
**                  Helsinki University of Technology, Finland.
**                  All rights reserved.
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif

extern struct Library *SocketBase;

#include <clib/socket_protos.h>
#include <pragmas/socket_pragmas.h>
#ifdef _OPTINLINE		/* for SAS C 6.3 and later */
#include <clib/socket_inlines.h>
#endif
#endif /* PROTO_SOCKET_H */
