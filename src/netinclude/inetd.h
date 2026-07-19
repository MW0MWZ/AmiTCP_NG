#ifndef INETD_H
#define INETD_H

/* 
 *	$Filename: inetd.h $
 *	$Release$
 *	$Revision: 1.1 $
 *	$Date: 1993/08/12 08:19:34 $
 *
 * Internet daemon interface definitions
 *
 * Copyright © 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                  Helsinki University of Technology, Finland.
 *                  All rights reserved.
 *
 * Author: ppessi <Pekka.Pessi@hut.fi>
 *
 * $Id: inetd.h,v 1.1 1993/08/12 08:19:34 jraja Exp $
 *
 * $Log: inetd.h,v $
 * Revision 1.1  1993/08/12  08:19:34  jraja
 * Initial revision
 *
 * Revision 1.3  1993/06/03  20:12:30  ppessi
 * Changed the daemon naming scheme, removed template from inetd.conf
 *
 * Revision 1.2  93/05/28  20:13:47  ppessi
 * Added Segment pointer to the DaemonMessage.
 * 
 * Revision 1.1  93/05/24  21:42:54  ppessi
 * Initial revision
 */

/* 
 * The DaemonPort structure and its associated memory
 * must be allocated with AllocVec call 
 */
struct DaemonPort {
  struct MsgPort dp_Port;
  void         (*dp_ExitCode)();
};

#define DAEMONPORTNAME "inetd.ipc"

/*
 * A message associated with each launched process 
 */
struct DaemonMessage {
  struct Message dm_Msg;	/* Message name is FreeVec()'ed by inetd */
  struct Process*dm_Pid;	/* set by the launcher */
  struct Segment*dm_Seg;	/* used only if resident segment */
  LONG           dm_Id;		/* socket id */
  LONG		 dm_Retval;	/* non-zero errorcode */
  UBYTE          dm_Family;	/* address/protocol family */
  UBYTE          dm_Type;
};

/* Daemon types, used as socket types */
#define DMTYPE_UNKNOWN   -1
#define DMTYPE_INTERNAL  0		/* type is within builtin struct */
#define DMTYPE_STREAM    SOCK_STREAM	/* stream socket */
#define DMTYPE_DGRAM     SOCK_DGRAM	/* datagram socket */
#define DMTYPE_RAW       SOCK_RAW	/* raw-protocol interface */
#define DMTYPE_RDM       SOCK_RDM	/* reliably-delivered message */
#define DMTYPE_SEQPACKET SOCK_SEQPACKET /* sequenced packet stream */
 
/* Return values from the startup code */
#define DERR_LIB    0xA0
#define DERR_OBTAIN 0xA1

#endif /* INETD_H */
