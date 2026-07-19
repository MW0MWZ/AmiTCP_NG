#ifndef AMIGA_NETDB_H
#define AMIGA_NETDB_H
/* $Id: amiga_netdb.h,v 3.2 1994/04/06 15:37:12 too Exp $
 *
 * kern/amiga_netdb.h --- local NetDB functions
 *
 * Author: ppessi <Pekka.Pessi@hut.fi>
 *
 * Copyright (c) 1993 OHT-AmiTCP/IP Group,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created      : Thu Apr 22 12:05:05 1993 ppessi
 * Last modified: Wed Apr  6 18:06:05 1994 too
 *
 * $Log: amiga_netdb.h,v $
 * Revision 3.2  1994/04/06  15:37:12  too
 * Added flag ACF_PRIVONLY
 *
 * Revision 3.1  1994/03/26  09:44:06  too
 * Added ACCESS control data items and defines.
 * Added function prototype for (fixed) service entry query
 * and netdb deinitialization placeholder.
 *
 * Revision 1.11  1993/05/29  20:55:59  jraja
 * Removed declaration of netdb path name.
 *
 * Revision 1.10  1993/05/16  19:24:03  jraja
 * Changed structure names:
 * NameServerNode => NameserventNode, nameserver => nameservent,
 * DomainNode => DomainentNode, domainname => domainent.
 *
 * Revision 1.9  1993/05/16  00:14:28  jraja
 * Changed the nameserver ent value from sockaddr_in to in_addr.
 *
 * Revision 1.8  1993/05/15  12:57:25  ppessi
 * Added netinet/in.h into includes. Changed struct domain to struct domainname
 * (there was a name clash)
 *
 * Revision 1.7  93/05/15  10:48:45  10:48:45  too (Tomi Ollila)
 * Silly style error fixed (nsn_addr -> ns_addr)
 * 
 * Revision 1.6  93/05/15  10:36:57  10:36:57  too (Tomi Ollila)
 * Some cosmetic changes for consistency
 * 
 * Revision 1.5  1993/05/14  23:07:56  ppessi
 * Fixed bugs. Added a NameserverEnt node type
 *
 * Revision 1.4  93/05/14  11:38:10  11:38:10  ppessi (Pekka Pessi)
 * Removed private information. Added DNS and domain lists. 
 * Changed locking mechanism. Added (untested) RESET feature.
 * 
 * Revision 1.3  93/05/05  16:10:12  16:10:12  puhuri (Markus Peuhkuri)
 * Fixes for final demo.
 * 
 * Revision 1.2  93/05/04  12:45:46  12:45:46  jraja (Jarno Tapio Rajahalme)
 * Changed keyword names to avoid collisions with data names(e.g. host name).
 * 
 * Revision 1.1  93/04/28  21:57:22  21:57:22  ppessi (Pekka Pessi)
 * Initial revision
 * 
 */

#ifndef NETDB_H
#include <netdb.h>
#endif

#ifndef IN_H
#include <netinet/in.h>
#endif

/* Access control table item */
struct AccessItem {
  UWORD	ai_flags;
  UWORD	ai_port;
  ULONG	ai_host;
  ULONG ai_mask;
};

/* Access control flags */
#define ACF_ALLOW	0x01
#define ACF_LOG		0x02
#define ACF_PRIVONLY	0x04 /* 0 as port matches for privileged ports only */
#define ACF_CONTINUE	(1 << 15)

/* AC table temporary buffer size */
#define TMPACTSIZE	0x4000 

/* NetDataBase */
struct NetDataBase {
  struct SignalSemaphore ndb_Lock;
  struct MinList         ndb_Hosts;
  struct MinList         ndb_Networks;
  struct MinList         ndb_Services;
  struct MinList         ndb_Protocols;
  struct MinList         ndb_NameServers;
  struct MinList         ndb_Domains;
  LONG			 ndb_AccessCount; /* tmp var, but reduces code size */
  struct AccessItem *	 ndb_AccessTable;
};

extern struct NetDataBase *NDB;

/* Locking macros for NDB */
#define LOCK_W_NDB(ndb) (ObtainSemaphore(&(ndb)->ndb_Lock))
#define LOCK_R_NDB(ndb) (ObtainSemaphoreShared(&(ndb)->ndb_Lock))
#define UNLOCK_NDB(ndb) (ReleaseSemaphore(&(ndb)->ndb_Lock))
#define ATTEMPT_NDB(ndb) (AttemptSemaphore(&(ndb)->ndb_Lock))

/*
 * GenEnt has the common part of all NetDataBase Nodes below
 */
struct GenentNode {
  struct MinNode gn_Node;
  short          gn_EntSize;
  struct {
    char dummy[0];
  }              gn_Ent;
};

/* NetDataBase Nodes */
struct HostentNode {
  struct MinNode hn_Node;
  short          hn_EntSize;
  struct hostent hn_Ent;
};

struct NetentNode {
  struct MinNode nn_Node;
  short          nn_EntSize;
  struct netent  nn_Ent;
};

struct ServentNode {
  struct MinNode  sn_Node;
  short           sn_EntSize;
  struct servent  sn_Ent;
};

struct ProtoentNode {
  struct MinNode  pn_Node;
  short           pn_EntSize;
  struct protoent pn_Ent;
};

struct NameserventNode {
  struct MinNode  nsn_Node;
  short           nsn_EntSize;
  short           nsn_Dynamic;	/* 0 = static (config file); 1 = runtime/DHCP-added */
  struct nameservent {
    struct in_addr ns_addr;
  } nsn_Ent;
};

struct DomainentNode {
  struct MinNode  dn_Node;
  short           dn_EntSize;
  struct domainent {
    char *d_name;
  } dn_Ent;
};
/*
 * netdatabase calls for some AmiTCP/IP functions
 */
struct ServentNode * findServentNode(struct NetDataBase * ndb,
				     const char * name, const char * proto);

/*
 * Read NetDB...
 */
LONG do_netdb(struct CSource *cs, UBYTE **errstrp, struct CSource *res);
LONG reset_netdb(struct CSource *cs, UBYTE **errstrp, struct CSource *res);
LONG init_netdb(void);
void netdb_deinit(void);
     
#endif /* AMIGA_NETDB_H */
