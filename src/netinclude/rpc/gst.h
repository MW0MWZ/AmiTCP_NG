/*
 * $Id: gst.h,v 1.2 1994/03/09 01:54:02 jraja Exp $
 *
 * Include file for the SAS/C Global Symbol Table generation
 *
 * $Log: gst.h,v $
 * Revision 1.2  1994/03/09  01:54:02  jraja
 * Added few files.
 *
 * Revision 1.1  1993/11/10  01:11:15  jraja
 * Initial revision
 *
 */
struct sockaddr_in;

#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
/* #include <sys/socket.h> \* includes inline functions */
#include <sys/time.h>
/* #include <netdb.h> \* includes inline functions */
#include <errno.h>
/* #include <arpa/inet.h> \* includes inline functions */
/* #include <net/if.h> \* depends on sys/socket.h */
#include <unistd.h>
#include <proto/usergroup.h>
#include <pragmas/socket_pragmas.h>

#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_rmt.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <proto/exec.h>
