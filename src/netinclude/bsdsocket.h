#ifndef BSDSOCKET_H
#define BSDSOCKET_H
/*
 * $Id: bsdsocket.h,v 1.5 1994/02/26 18:47:23 jraja Exp $
 *
 * Compiler dependent prototypes and inlines for bsdsocket.library
 *
 * Copyright © 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                  Helsinki University of Technology, Finland.
 *                  All rights reserved.
 */

/* These are compiler independent */
#include <unistd.h>
#include <clib/netlib_protos.h>

#if __SASC
#include <proto/socket.h>
#include <proto/usergroup.h>
#elif __GNUC__
#include <inline/socket.h>
#else
#include <clib/socket_protos.h>
#include <clib/usergroup_protos.h>
#endif

#endif /* !BSDSOCKET_H */
