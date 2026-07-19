#ifndef PROTO_USERGROUP_H
#define PROTO_USERGROUP_H
/*
**      $Filename: proto/usergroup.h $
**	$Release$
**      $Revision: 1.1 $
**      $Date: 1994/01/20 16:21:31 $
**
**	SAS C prototypes for usergroup.library
**
**	Copyright © 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
**                  Helsinki University of Technology, Finland.
**                  All rights reserved.
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

extern struct Library *UserGroupBase;

#include <libraries/usergroup.h>
#include <clib/usergroup_protos.h>
#include <pragmas/usergroup_pragmas.h>

#endif /* PROTO_USERGROUP_H */
