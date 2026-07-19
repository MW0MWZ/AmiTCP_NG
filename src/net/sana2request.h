/* $Id: sana2request.h,v 1.11 1993/06/04 11:16:15 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * sana2request.h - Sana-II IO Request Management routines
 *
 * Last modified: Fri Jun  4 00:40:53 1993 jraja
 *
 * HISTORY
 * $Log: sana2request.h,v $
 * Revision 1.11  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.10  1993/05/16  21:09:43  ppessi
 * RCS version changed.
 *
 * Revision 1.9  1993/05/04  15:45:12  puhuri
 * Changed ss_addr to ss_hwaddr...
 *
 * Revision 1.8  93/04/19  02:19:05  02:19:05  ppessi (Pekka Pessi)
 * ssss_addrbytes replaced with ss?iss_if.if_addrlen
 * 
 * Revision 1.7  93/04/06  08:54:22  08:54:22  jraja (Jarno Tapio Rajahalme)
 * Changed bcopy's to aligned_bcopy[_const] when appropriate.
 * 
 * Revision 1.6  93/03/20  07:12:49  07:12:49  ppessi (Pekka Pessi)
 * Fixed mbuf allocating for headers
 * 
 * Revision 1.5  93/03/16  19:38:42  19:38:42  too (Tomi Ollila)
 * code fixes
 * 
 * Revision 1.4  93/03/12  23:55:50  23:55:50  ppessi (Pekka Pessi)
 * Added IOSana2Request handling functions...
 * 
 * Revision 1.1  93/03/12  03:21:25  03:21:25  ppessi (Pekka Pessi)
 * moved DeleteIOSana2Req and CreateIOSana2Req here.
 * 
 * Revision 1.3  93/03/05  19:51:21  19:51:21  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.2  93/02/28  22:30:28  22:30:28  ppessi (Pekka Pessi)
 * Made a much fancier banner... 
 * Transfered buffermanagement taglist here.
 * 
 * Revision 1.1  93/02/25  14:35:21  14:35:21  ppessi (Pekka Pessi)
 * Initial revision
 * 
 */

extern struct TagItem buffermanagement[];

#ifndef SYS_PARAM_H
#include <sys/param.h>
#endif

BOOL
ioip_alloc_mbuf(struct IOIPReq *s2rp, ULONG MTU);

/*
 * Allocate a new Sana-II IORequest for this task
 */
static inline struct IOSana2Req *
CreateIOSana2Req(register struct sana_softc *ssc)
{
  register struct IOSana2Req *req;
  register struct MsgPort *port;

  port = CreateMsgPort();
  if (!port) return NULL;

  req = CreateIORequest(port, sizeof(*req));
  if (!req) {
    DeleteMsgPort(port);
    return NULL;
  }

  if (ssc) {
    req->ios2_Req.io_Device    = ssc->ss_dev;
    req->ios2_Req.io_Unit      = ssc->ss_unit;
    req->ios2_BufferManagement = ssc->ss_bufmgnt;

    aligned_bcopy(ssc->ss_hwaddr, req->ios2_SrcAddr, ssc->ss_if.if_addrlen);
  }
  return req;
}

/*
 * Delete a Sana-II IORequest 
 */
static inline VOID 
DeleteIOSana2Req(register struct IOSana2Req *open)
{
  register struct MsgPort *port = open->ios2_Req.io_Message.mn_ReplyPort;

  DeleteIORequest((struct IORequest*) open);
  if (port)
    DeleteMsgPort(port);
}

