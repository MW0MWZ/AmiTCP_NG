RCS_ID_C="$Id: res_init.c,v 3.1 1994/04/02 11:06:28 jraja Exp $";
/*
 * Author: Tomi Ollila <too@cs.hut.fi>
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created: Mon May 10 10:40:20 1993 too
 * Last modified: Sat Apr  2 13:52:04 1994 jraja
 *
 * HISTORY
 * $Log: res_init.c,v $
 * Revision 3.1  1994/04/02  11:06:28  jraja
 * Moved global resolver variables to SocketBase, removed res_lock.
 *
 * Revision 1.4  1994/01/20  02:16:05  jraja
 * Added #include <conf.h> as the first include.
 *
 * Revision 1.3  1993/06/03  19:05:48  too
 * Fixed global res_lock to be zero after compilation (for sure)
 *
 * Revision 1.2  1993/06/02  19:26:17  too
 * Moved resolver stuff here from kern/ -directory
 *
 * Revision 1.1  1993/06/01  16:30:33  too
 * Initial revision
 *
 */

/*
 * res_init.c --- initialise the DNS resolver.  (Berkeley BIND, see copyright.)
 *
 * The RESOLVER is the DNS client built into the stack: it turns a name like
 * "www.example.com" into an IP address by querying a nameserver. It is the same
 * BIND 4 resolver found in every Unix of the era, so these api/res_*.c files are
 * near-verbatim BIND and the flow is standard:
 *   res_init()      (this file) load the resolver state `_res` -- the list of
 *                   nameserver addresses, the local domain, and the search list --
 *                   from the network database (AmiTCP:db/netdb DOMAIN/NAMESERVER
 *                   lines; see kern/amiga_netdb.c).
 *   res_mkquery()   (res_mkquery.c) build a DNS query message.
 *   res_search()/res_query() (res_query.c) apply the domain search list and issue
 *                   the query.
 *   res_send()      (res_send.c) send the datagram to a nameserver and read the
 *                   reply (UDP, retrying / falling back to TCP for big answers) --
 *                   the only part that touches sockets.
 *   dn_comp()/dn_expand() (res_comp.c) DNS name (de)compression.
 * gethostbyname() (api/gethostnamadr.c) is the public entry that drives all this.
 *
 * AmiTCP note: the resolver state is per-SocketBase (each program gets its own),
 * which is why _res is a macro onto the library base (see api/resolv.h).
 */

#include <conf.h>

#include <sys/param.h>
#include <kern/amiga_includes.h>

#include <api/resolv.h>

#ifndef AMITCP /* AmiTCP has this in the SocketBase */
struct state _res;
#endif

void
res_init(struct state *state)
{
  state->retrans = RES_TIMEOUT;
  state->retry   = 4;
  state->options = RES_DEFAULT;
}

