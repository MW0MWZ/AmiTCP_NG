/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: amiga_netdb.c,v 3.2 1994/04/06 15:37:29 too Exp $";
/* 
 * amiga_netdb.c --- NetDB Parse Functions
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created      : Tue Apr 27 10:18:58 1993 ppessi
 * Last modified: Wed Apr  6 18:14:49 1994 too
 *
 * $Log: amiga_netdb.c,v $
 * Revision 3.2  1994/04/06  15:37:29  too
 * Added parsing of '@' as match for all private ports (# < 1024)
 * in addaccessent()
 *
 * Revision 3.1  1994/03/26  09:41:13  too
 * Added netdb parsing code for ACCESS control
 *
 * Revision 1.14  1993/07/28  16:00:43  jraja
 * Fixed two inet_aton() calls (return value was misunderstood).
 *
 * Revision 1.13  1993/06/14  15:37:48  jraja
 * Changed file line handling to somewhat better.
 * Added check for too deep recursion of the netdb files.
 *
 * Revision 1.12  1993/06/13  23:39:27  jraja
 * Changed HOST to use Unix compatible format.
 * Added WITH to include other files.
 * '#' as a comment introducer now fully supported.
 * Uses parse function table instead of a switch-case structure.
 * read_netdb() now supports 'prefixes'.
 * read_netdb() now CD's to the AmiTCP:db before opening the file.
 *
 * Revision 1.11  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.10  1993/05/29  20:54:12  jraja
 * Changed to use _PATH_NETDB.
 *
 * Revision 1.9  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.8  1993/05/16  19:24:03  jraja
 * Changed structure names:
 * NameServerNode => NameserventNode, nameserver => nameservent,
 * DomainNode => DomainentNode, domainname => domainent.
 *
 * Revision 1.7  1993/05/16  00:48:14  jraja
 * Changed init_netdb() to forgive file errors.
 *
 * Revision 1.6  1993/05/16  00:17:32  jraja
 * Changed return values from explicit numbers to RETURN_XXXX.
 * Changed syntax errors to be logged and bypassed.
 * Implemented nameserver and domain parsing.
 *
 * Revision 1.5  93/05/14  11:37:11  11:37:11  ppessi (Pekka Pessi)
 * Cleaned Arexx interface a bit. Private information removed from
 * header files, added to this file.
 * 
 * Revision 1.4  93/05/05  16:10:10  16:10:10  puhuri (Markus Peuhkuri)
 * Fixes for final demo.
 * 
 * Revision 1.3  93/05/04  16:53:28  16:53:28  ppessi (Pekka Pessi)
 * Added net and host entry parsing.
 * 
 * Revision 1.2  93/05/04  12:38:32  12:38:32  jraja (Jarno Tapio Rajahalme)
 * _Minor_ fixes...
 * 
 * Revision 1.1  93/04/28  21:56:49  21:56:49  ppessi (Pekka Pessi)
 * Initial revision
 * 
 */

/*
 * amiga_netdb.c --- the network database (hosts, networks, protocols, services).
 *
 * This is the Amiga equivalent of /etc/hosts, /etc/services, /etc/protocols and
 * friends: the static name<->number mappings the resolver falls back on. It is
 * loaded from AmiTCP:db/netdb (which may WITH-include hosts, protocols, services,
 * networks and a per-host netdb-myhost file) into in-memory tables, and served to
 * applications through gethostbyname/getservbyname/getprotobyname (api/getxbyy.c,
 * api/gethostnamadr.c).
 *   init_netdb()   called from init_all(): parse AmiTCP:db/netdb into the tables.
 *   read_netdb()/do_netdb()   the parser (over a CSource), also reachable from the
 *                  ARexx interface so entries can be added to a running stack.
 * NOTE: a missing WITH-included file is only a warning, but a self-contained netdb
 * (localhost + this host) is enough to run -- see the runtime notes in PORTING.md.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <kern/amiga_includes.h>
#include <kern/amiga_config.h>
#include <kern/amiga_netdb.h>
#include <kern/accesscontrol.h>

#include <netinet/in.h>

#include <dos/dos.h>
#include <dos/rdargs.h>

#if __SASC
#include <proto/dos.h>
#elif __GNUC__
#include <inline/dos.h>
#else
#error Your compiler is not supported in this release.
#endif

int inet_aton(register const char *cp, struct in_addr *addr);
int strcmp(const char *s1, const char *s2);

LONG read_netdb(struct NetDataBase *ndb, UBYTE *fname, const UBYTE** errstrp, struct CSource *res, int prefixindex);

/*
 * Global pointer for the NetDataBase
 */
struct NetDataBase *NDB = NULL;

/*
 * Default netdatabase name
 */
STRPTR netdbname = _PATH_NETDB;

/* 
 * Templates for Arexx commands and DB files
 */
STRPTR NETDBENTRY =
  (STRPTR)"WITH,H=HOST,N=NET,S=SERVICE,P=PROTOCOL,NS=NAMESERVER,DO=DOMAIN,ACC=ACCESS";

enum ndbtype { KNDB_WITH, KNDB_HOST, KNDB_NET, KNDB_SERV, KNDB_PROTO, 
	       KNDB_DNS, KNDB_DOM, KNDB_ACC };

STRPTR NETDBTEMPLATE =
  (STRPTR)"$NAME$/A,$ENTRY$/A,$ALIAS$/M";

STRPTR PROTOCOL_TEMPLATE =
  (STRPTR)"$NAME$/A,$NUMBER$/A/N,$ALIAS$/M";

enum ndbarg { KNDB_NAME, KNDB_DATA, KNDB_ALIAS };

#define NDBARGS 3

STRPTR ACCESS_TEMPLATE =
  (STRPTR)"$PORT$/A,$HOSTMASK$/A,$ACCESS$/A,LOG/S";

enum accarg { KACC_PORT, KACC_HOSTMASK, KACC_ACCESS, KACC_LOG };

#define ACCARGS 4

STRPTR WITH_TEMPLATE =
  (STRPTR)"$FILE$/A,PREFIX/K";
#define WITHARGS 2
enum witharg { WITH_FILE, WITH_PREFIX };

/* prototypes for the netdb parsing functions */

LONG addwith(struct NetDataBase *ndb,
	     struct RDArgs *rdargs, UBYTE **errstrp);
LONG addhostent(struct NetDataBase *ndb,
		struct RDArgs *rdargs, UBYTE **errstrp);
LONG addnetent(struct NetDataBase *ndb,
	       struct RDArgs *rdargs, UBYTE **errstrp);
LONG addservent(struct NetDataBase *ndb,
		struct RDArgs *rdargs, UBYTE **errstrp);
LONG addprotoent(struct NetDataBase *ndb,
		 struct RDArgs *rdargs, UBYTE **errstrp);
LONG addnameservent(struct NetDataBase *ndb,
		    struct RDArgs *rdargs, UBYTE **errstrp);
LONG adddomainent(struct NetDataBase *ndb,
		  struct RDArgs *rdargs, UBYTE **errstrp);
LONG addaccessent(struct NetDataBase *ndb,
		  struct RDArgs *rdargs, const UBYTE **errstrp);
LONG addndbent(struct NetDataBase *ndb,
	       struct RDArgs *rdargs, UBYTE **errstrp);

typedef LONG (*ndb_parse_f)(struct NetDataBase *ndb,
	    struct RDArgs *rdargs, UBYTE **errstrp);

/* Array of parsing functions. Note that the order is same as in the
 * NETDBENTRY.
 */
ndb_parse_f ndb_parse_funs[] = {
  addwith,
  addhostent,
  addnetent,
  addservent,
  addprotoent,
  addnameservent,
  adddomainent,
  (ndb_parse_f)addaccessent
};

/*
 * Alloc a NetDataBase
 */
struct NetDataBase *
alloc_netdb(struct NetDataBase *ndb)
{
  if (ndb || 
      (ndb = bsd_malloc(sizeof (*NDB), M_NETDB, M_WAITOK))) {  
    struct MinList *gl;

    InitSemaphore(&ndb->ndb_Lock);
    for (gl = (struct MinList *)&ndb->ndb_Hosts;
	 gl <= (struct MinList *)&ndb->ndb_Domains;        
	 gl++)
      NewList((struct List *)gl);
    
  }
  
  ndb->ndb_AccessCount = 0;
  if ((ndb->ndb_AccessTable =
       bsd_malloc(TMPACTSIZE, M_NETDB, M_WAITOK)) == NULL) {
    bsd_free(ndb, M_NETDB);
    ndb = NULL;
  }
  return ndb;
}

/*
 * Free a NetDataBase
 * Caller must have a write lock on NDB
 */
void
free_netdb(struct NetDataBase *ndb)
{
  struct GenentNode *gn;
  struct MinList *gl;

  for (gl = (struct MinList *)&ndb->ndb_Hosts;
       gl <= (struct MinList *)&ndb->ndb_Domains;        
       gl++)
    while (gn = (struct GenentNode *)RemHead((struct List *)gl)) 
      bsd_free(gn, M_NETDB);

  if (ndb->ndb_AccessTable != NULL) {
    bsd_free(ndb->ndb_AccessTable, M_NETDB);
    ndb->ndb_AccessTable = NULL;
  }
  bsd_free(ndb, M_NETDB);
}

#ifdef DEBUG
static char * zap;
static size_t zap_size;
#endif

/* 
 * Copy alias list to ato, alias strings and name to cto
 */
static void
aliascpy(UBYTE *cto, UBYTE *name, UBYTE**ato, UBYTE **afrom)
{ 
#ifdef DEBUG
  UBYTE *logname = name;
#endif
  do {
    while((*cto++ = *name++));
  } while (afrom && (name = *afrom++) && (*ato++ = cto));

  *ato = NULL;

#ifdef DEBUG
  if (cto != zap + zap_size) {
    log(LOG_ERR, "%s: mismatch in size %ld != expected %ld\n",
	logname, cto - zap, zap_size);
  } 
#endif
}

/*
 * Allocate a netdb node
 *
 * nodesize is the size of the base structure, additional space
 * is allocated for the name and the aliases.
 * alias is NULL terminated array of alias name pointers.
 * Number of aliases is returned via aliasp.
 *
 * size field of the allocated node is set to the total size - size for 
 * the MinNode and the size field itself.
 */
static void *
node_alloc(size_t nodesize, UBYTE *name, UBYTE **alias, int *aliasp)
{
  struct GenentNode *gn;

  nodesize += strlen((char *)name) + 1;	/* Add space needed for the name */

  *aliasp = 1;
  nodesize += sizeof (char*);	/* Alias list NULL terminator */

  /* Calculate the size of the aliases */
  if (alias) {
    while (*alias) {
      (*aliasp)++;
      nodesize += strlen((char *)*alias++) + 1 + sizeof (char*);
    }
  }
  gn = bsd_malloc(nodesize, M_NETDB, M_WAITOK);
  /*
   * set the ent size
   */
  if (gn)
    gn->gn_EntSize = nodesize - sizeof (struct GenentNode);

#ifdef DEBUG  
  zap_size = nodesize;
  zap = (char *)gn;
#endif
  return gn;
}

/*
 * Parse a service entry.
 */
LONG
addwith(struct NetDataBase *ndb,
	struct RDArgs *rdargs,
	UBYTE **errstrp)
{
  UBYTE result[REPLYBUFLEN + 1];
  struct CSource res;
  LONG retval = RETURN_OK;
  LONG Args[WITHARGS] = { 0 };
  int which;

  res.CS_Buffer = result; 
  res.CS_Length = sizeof (result);
  res.CS_CurChr = 0;

  if (rdargs = ReadArgs(WITH_TEMPLATE, Args, rdargs)) {
    if (Args[WITH_PREFIX] == 0)	/* no prefix given */
      which = -1;
    else {
      /* match given prefix */
      which = FindArg(NETDBENTRY, (UBYTE *)Args[WITH_PREFIX]);
      if (which < 0) {
	*errstrp = ERR_VALUE;
	retval = RETURN_WARN;
      }
    }
    if (retval == RETURN_OK) {

      retval = read_netdb(ndb, (UBYTE *)Args[WITH_FILE], (const UBYTE **)errstrp, &res,
			  which);

      if (retval) {
	log(LOG_WARNING, "netdb: WITH file %s: %s", 
	    (UBYTE *)Args[WITH_FILE], *errstrp);
#if 0
	if (retval <= RETURN_ERROR)
	  retval = RETURN_OK;	/* forgive */
#endif	
      }
    }
    FreeArgs(rdargs);
  } else {
    *errstrp = ERR_SYNTAX; retval = RETURN_WARN;
  }
  return retval;
}

/*
 * Parse a service entry.
 */
LONG
addservent(struct NetDataBase *ndb,
	    struct RDArgs *rdargs,
	    UBYTE **errstrp)
{
  LONG retval;
  LONG Args[NDBARGS] = { 0 };
  struct ServentNode *sn;     
  int aliases, plen;

  if (rdargs = ReadArgs(NETDBTEMPLATE, Args, rdargs)) {
    /* convert port number */
    UBYTE *s_proto = (UBYTE*)Args[KNDB_DATA];
    if ((plen = StrToLong(s_proto, &Args[KNDB_DATA])) > 0 &&
	s_proto[plen++] == '/') {
      int protonamelen = strlen((char *)(s_proto = s_proto + plen)) + 1;
      sn = node_alloc(sizeof (*sn) + protonamelen,
		      (UBYTE*)Args[KNDB_NAME],
		      (UBYTE **)Args[KNDB_ALIAS], &aliases);
      if (sn) {
	UBYTE **alias = (UBYTE **)(sn+1);
	UBYTE *name = (UBYTE *)(alias + aliases);

	sn->sn_Ent.s_port = Args[KNDB_DATA];
	sn->sn_Ent.s_proto = strcpy((char *)name, (char *)s_proto);
	sn->sn_Ent.s_name = (char *)(name + protonamelen);
	sn->sn_Ent.s_aliases = (char **)alias;

	/* Copy aliases */
	aliascpy((UBYTE *)sn->sn_Ent.s_name, (UBYTE*)Args[KNDB_NAME],
		 alias, (UBYTE **)Args[KNDB_ALIAS]);
	AddTail((struct List*)&ndb->ndb_Services, (struct Node*)sn);
	retval = RETURN_OK;
      } else {
	*errstrp = ERR_MEMORY; retval = RETURN_FAIL;
      }
    } else { 
      *errstrp = ERR_VALUE; retval = RETURN_WARN; 
    } 
    FreeArgs(rdargs);
  } else {
    *errstrp = ERR_SYNTAX; retval = RETURN_WARN;
  }
  return retval;
}

/*
 * Parse a host entry.
 *
 * NOTE: The host entry has the address in the 'name' and the official name
 * in the 'data'.
 */
LONG
addhostent(struct NetDataBase *ndb,
	    struct RDArgs *rdargs,
	    UBYTE **errstrp)
{
  LONG retval;
  LONG Args[NDBARGS] = { 0 };
  struct HostentNode *hn;
  struct in_addr addr;
  int aliases;

  if (rdargs = ReadArgs(NETDBTEMPLATE, Args, rdargs)) {
    /* convert ip address */
    if (inet_aton((char*)Args[KNDB_NAME], &addr)) {
      hn = node_alloc(sizeof (*hn) + 2*sizeof (&addr) + sizeof (addr),
		      (UBYTE*)Args[KNDB_DATA], 
		      (UBYTE **)Args[KNDB_ALIAS], &aliases);
      if (hn) {
	struct in_addr **addrtbl = (struct in_addr **)(hn + 1);
	UBYTE **alias = (UBYTE **)((UBYTE*)(addrtbl + 2) + sizeof (addr));
	UBYTE *name = (UBYTE *)(alias + aliases);

	hn->hn_Ent.h_addrtype = AF_INET;
	hn->hn_Ent.h_length = sizeof (addr);
	hn->hn_Ent.h_addr_list = (char **)addrtbl;
	hn->hn_Ent.h_name = (char *)name;
	hn->hn_Ent.h_aliases = (char **)alias;

	/* Make address list */
	addrtbl[0] = (struct in_addr *)(addrtbl + 2);
	addrtbl[1] = NULL;
	bcopy(&addr, addrtbl[0], sizeof (addr));

	/* Copy aliases */
	aliascpy((UBYTE *)hn->hn_Ent.h_name, (UBYTE*)Args[KNDB_DATA],
		 alias, (UBYTE **)Args[KNDB_ALIAS]);
	AddTail((struct List*)&ndb->ndb_Hosts, (struct Node*)hn);
	retval = RETURN_OK;
      } else {
	*errstrp = ERR_MEMORY; retval = RETURN_FAIL;
      }
    } else { 
      *errstrp = ERR_VALUE; retval = RETURN_WARN; 
    } 
    FreeArgs(rdargs);
  } else {
    *errstrp = ERR_SYNTAX; retval = RETURN_WARN;
  }
  return retval;
}

/*
 * Parse a net entry.
 */
LONG
addnetent(struct NetDataBase *ndb,
	  struct RDArgs *rdargs,
	  UBYTE **errstrp)
{
  LONG retval;
  LONG Args[NDBARGS] = { 0 };
  struct NetentNode *nn;
  struct in_addr addr;
  int aliases;

  if (rdargs = ReadArgs(NETDBTEMPLATE, Args, rdargs)) {
    /* convert ip address */
    if (inet_aton((char*)Args[KNDB_DATA], &addr)) {
      nn = node_alloc(sizeof (*nn),
		      (UBYTE*)Args[KNDB_NAME], 
		      (UBYTE **)Args[KNDB_ALIAS], &aliases);
      if (nn) {
	UBYTE **alias = (UBYTE **)(nn + 1);
	UBYTE *name = (UBYTE *)(alias + aliases);

	nn->nn_Ent.n_addrtype = AF_INET;
	nn->nn_Ent.n_name = (char *)name;
	nn->nn_Ent.n_aliases = (char **)alias;
	bcopy(&addr, &nn->nn_Ent.n_net, sizeof (unsigned long));

	/* Copy aliases */
	aliascpy((UBYTE *)nn->nn_Ent.n_name, (UBYTE*)Args[KNDB_NAME],
		 alias, (UBYTE **)Args[KNDB_ALIAS]);
	AddTail((struct List*)&ndb->ndb_Networks, (struct Node*)nn);
	retval = RETURN_OK;
      } else {
	*errstrp = ERR_MEMORY; retval = RETURN_FAIL;
      }
    } else { 
      *errstrp = ERR_VALUE; retval = RETURN_WARN;
    } 
    FreeArgs(rdargs);
  } else {
    *errstrp = ERR_SYNTAX; retval = RETURN_WARN;
  }
  return retval;
}

/*
 * Parse a protocol entry.
 */
LONG
addprotoent(struct NetDataBase *ndb,
	    struct RDArgs *rdargs,
	    UBYTE **errstrp)
{
  LONG retval;
  LONG Args[NDBARGS] = { 0 };
  struct ProtoentNode *pn;     
  int aliases;

  if (rdargs = ReadArgs(PROTOCOL_TEMPLATE, Args, rdargs)) {
    
    if (Args[KNDB_DATA]) {
      pn = node_alloc(sizeof (*pn), (UBYTE*)Args[KNDB_NAME], 
		      (UBYTE **)Args[KNDB_ALIAS], &aliases);
      if (pn) {
	UBYTE **alias = (UBYTE **)(pn+1);
	UBYTE *name = (UBYTE *)(alias + aliases);

	pn->pn_Ent.p_name = (char *)name;
	pn->pn_Ent.p_aliases = (char **)alias;
	pn->pn_Ent.p_proto = *(LONG *)Args[KNDB_DATA];

	/* Copy name and aliases */
	aliascpy(name, (UBYTE*)Args[KNDB_NAME], 
		 alias, (UBYTE **)Args[KNDB_ALIAS]);
	AddTail((struct List*)&ndb->ndb_Protocols, (struct Node*)pn);
	retval = RETURN_OK;
      } else {
	*errstrp = ERR_MEMORY; retval = RETURN_FAIL;
      }
    } else { 
      *errstrp = ERR_VALUE; retval = RETURN_WARN; 
    } 
    FreeArgs(rdargs);
  } else {
    *errstrp = ERR_SYNTAX; retval = RETURN_WARN;
  }
  return retval;
}

/*
 * Parse a Name Server entry
 */
LONG
addnameservent(struct NetDataBase *ndb,
	       struct RDArgs *rdargs,
	       UBYTE **errstrp)
{
  UBYTE Buffer[KEYWORDLEN];
  LONG  BufLen = sizeof (Buffer);
  struct in_addr ns_addr;
  struct NameserventNode *nsn;

  if (ReadItem(Buffer, BufLen, &rdargs->RDA_Source) <= 0) {
    *errstrp = ERR_SYNTAX; 
    return RETURN_WARN;
  }
  if (!inet_aton((char *)Buffer, &ns_addr)) {
    *errstrp = ERR_VALUE;
    return RETURN_WARN; 
  }
  if ((nsn = bsd_malloc(sizeof (*nsn), M_NETDB, M_WAITOK)) == NULL) {
    *errstrp = ERR_MEMORY;
    return RETURN_FAIL;
  }
  nsn->nsn_EntSize = sizeof (nsn->nsn_Ent);
  nsn->nsn_Dynamic = 0;			/* statically configured (from the config file) */
  nsn->nsn_Ent.ns_addr = ns_addr;

  AddTail((struct List*)&ndb->ndb_NameServers, (struct Node*)nsn);
  return RETURN_OK;
}

/*
 * Parse a Domain Name entry
 */
LONG
adddomainent(struct NetDataBase *ndb,
	       struct RDArgs *rdargs,
	       UBYTE **errstrp)
{
  UBYTE Buffer[REPLYBUFLEN];
  LONG  BufLen = sizeof (Buffer);
  struct DomainentNode *dn;
  short  nodesize;

  if (ReadItem(Buffer, BufLen, &rdargs->RDA_Source) <= 0) {
    *errstrp = ERR_SYNTAX; 
    return RETURN_WARN;
  }
  nodesize = sizeof (*dn) + strlen((char *)Buffer) + 1;
  if ((dn = bsd_malloc(nodesize, M_NETDB, M_WAITOK)) == NULL) {
    *errstrp = ERR_MEMORY;
    return RETURN_FAIL;
  }
  dn->dn_EntSize = nodesize - sizeof (struct GenentNode);
  dn->dn_Ent.d_name = (char *)(dn + 1);

  strcpy((char *)(dn + 1), (char *)Buffer);

  AddTail((struct List*)&ndb->ndb_Domains, (struct Node*)dn);
  return RETURN_OK;
}

/*
 * Parse a access control entry.. after reading the whole netdatabase
 * access list must be reorganized;
 */
LONG
addaccessent(struct NetDataBase *ndb,
	     struct RDArgs *rdargs,
	     const UBYTE **errstrp)
{
  LONG retval = RETURN_WARN;
  LONG Args[ACCARGS] = { 0 };

  ULONG host, mask;
  UWORD port, flags = ACF_CONTINUE;

  if (ndb->ndb_AccessCount >= TMPACTSIZE / sizeof (struct AccessItem)) {
    *errstrp = (const UBYTE *)"Too many access control items\n";
    return retval; /* copy propagation expected */
  }
  
  if ((rdargs = ReadArgs(ACCESS_TEMPLATE, Args, rdargs)) != NULL) {
    
    if (strcmp((char *)Args[KACC_PORT], "*") == 0)
      port = 0;
    else if (strcmp((char *)Args[KACC_PORT], "@") == 0) {
      port = 0; flags |= ACF_PRIVONLY;
    }
    else if (StrToLong((UBYTE *)Args[KACC_PORT], (LONG *)&host) > 0
	     && host != 0) {
      if (host > 0xffff) {
	*errstrp = (const UBYTE *)"Illegal port value\n";
	goto exit;
      }
      port = host;
    }
    else {
      struct ServentNode * entNode;

      if ((entNode =
	   findServentNode(ndb, (char *)Args[KACC_PORT], "tcp")) != NULL)
	  port = entNode->sn_Ent.s_port;
      else {
	*errstrp = (const UBYTE *)"Illegal port value\n";
	goto exit;
      }
    }
    {
      int zmask = 0xFFFFFFFF;
      int i = 0, ls = 0, dots = 0;

#define hm ((char *)Args[KACC_HOSTMASK])
      
      while ((hm[i] >= '0' && hm[i] <= '9') || hm[i] == '.' || hm[i] == '*') {
	if (hm[i] == '.') {
	  ls = 0;
	  /*
	   * PORT (AmiTCP_NG) fix: an IPv4 host mask has at most 3 dots. Cap the
	   * counter so the `0xFF000000 >> 8 * dots` shifts below never shift by
	   * 32 bits or more -- which is undefined behaviour -- when the config
	   * supplies a malformed mask with a long run of dots.
	   */
	  if (dots < 3)
	    dots++;
	}
	else if (hm[i] == '*') {
	  hm[i] = '0';
	  ls = 1;
	  zmask ^= (0xFF000000 >> 8 * dots);
	}
	i++;
      }
      if (ls == 1)
	while (dots++ < 3)
	  zmask ^= (0xFF000000 >> 8 * dots);

      if (hm[i] == '/') {
	hm[i++] = '\0';
	if (inet_aton(&hm[i], (struct in_addr *)&mask) == 0) {
	  *errstrp = (const UBYTE *)"Illegal mask value\n";
	  goto exit;
	}
      }
      else
	mask = 0xffffffff;

      mask &= zmask;
      
      if (inet_aton(hm, (struct in_addr *)&host) == 0) {
	*errstrp = (const UBYTE *)"Illegal host value\n";
	goto exit;
      }
#undef hm      
    }
    if (strcmp((char *)Args[KACC_ACCESS], "allow") == 0)
      flags |= ACF_ALLOW;
    else if (strcmp((char *)Args[KACC_ACCESS], "deny") != 0) {
      *errstrp = (const UBYTE *)"Illegal access value\n";
      goto exit;
    }

    if (Args[KACC_LOG])
      flags |= ACF_LOG;
    
    ndb->ndb_AccessTable[ndb->ndb_AccessCount].ai_port = port;
    ndb->ndb_AccessTable[ndb->ndb_AccessCount].ai_host = host;
    ndb->ndb_AccessTable[ndb->ndb_AccessCount].ai_mask = mask;
    ndb->ndb_AccessTable[ndb->ndb_AccessCount].ai_flags = flags;
    ndb->ndb_AccessCount++;

    retval = 0;
  exit:
    FreeArgs(rdargs);
  }
  else
    *errstrp = ERR_SYNTAX;

  return retval;
}

/*
 * Add an entry into NetDB. 
 * Caller must have a write lock on ndb 
 */
LONG 
addndbent(struct NetDataBase *ndb,
	  struct RDArgs *rdargs, 
	  UBYTE **errstrp)
{
  if (NDB) {
    LONG item;
    enum ndbtype which;
    UBYTE Buffer[KEYWORDLEN];

    /* Get entry type */
    item = ReadItem(Buffer, sizeof (Buffer), &rdargs->RDA_Source);

    if (item == 0)
      return RETURN_OK;		/* empty line */
    if (item < 0) {
      *errstrp = ERR_SYNTAX;
      return RETURN_WARN;
    }
    if ((which = FindArg(NETDBENTRY, Buffer)) < 0) {
      *errstrp = ERR_UNKNOWN;
      return RETURN_WARN;
    } 

    return ndb_parse_funs[which](ndb, rdargs, errstrp);

  } else {
    *errstrp = ERR_NONETDB;
    return RETURN_FAIL;
  }
}

/* 
 * Read in a NetDataBase file
 */
LONG 
read_netdb(struct NetDataBase *ndb, UBYTE *fname, 
	  const UBYTE** errstrp, struct CSource *res, int prefixindex)
{
  LONG warnval = RETURN_OK;
  LONG retval = RETURN_OK, ioerr = 0;
  UBYTE *p, *buf = AllocMem(CONFIGLINELEN, MEMF_PUBLIC);
  struct RDArgs *rdargs;
  BPTR fh;
  short line = 0;
  ndb_parse_f parser;
  BPTR lock, oldcd;

  /* Get an exclusive lock on the database.
   * Multiple locks are OK (when this function is called recursively)
   */
  LOCK_W_NDB(ndb);		
  if (ndb->ndb_Lock.ss_NestCount > 10) {
    UNLOCK_NDB(ndb);
    *errstrp = (const UBYTE *)"Too many files included";
    return RETURN_ERROR;
  }
  if (buf) {

    /* CD to netdb directory */
    lock = Lock((CONST_STRPTR)_PATH_DB, ACCESS_READ);
    if (lock)
      oldcd = CurrentDir(lock);

    if ((fh = Open(fname, MODE_OLDFILE))) {
      if (rdargs = AllocDosObject(DOS_RDARGS, NULL)) {
	/* initialize CSource of the rdargs */
	rdargs->RDA_Source.CS_Buffer = buf;
	/* initialize rest fields (see dos/rdargs.h) */
	rdargs->RDA_DAList = NULL;
	rdargs->RDA_ExtHelp = NULL;
	rdargs->RDA_Flags = 0;
	
	if (prefixindex < 0)
	  parser = addndbent;	/* no prefix */
	else
	  parser = ndb_parse_funs[prefixindex];
	
	while (FGets(fh, buf, CONFIGLINELEN - 1)) {
	  line++;		/* maintain line number */
	  /* pass by white space */
	  for (p = buf; *p == ' ' || *p == '\t' || *p == '\r'; p++)
	    ;
	  rdargs->RDA_Source.CS_CurChr = p - buf;
	  if (*p == '#' || *p == ';' || *p == '\n') /* only a comment line */
	    continue;
	  /* remove comments & calc length */
	  for (; *p; p++) { 
	    if (*p == '#' || *p == ';') {
	      *p++ = '\n';
	      *p   = '\0';	/* terminate line */
	      break;
	    }
	  }
	  /* ensure that line ends with '\n' (ReadArgs() depends on it) */
	  if (*(p - 1) != '\n') {
	    *p++ = '\n';
	    *p   = '\0';
	  }
	  rdargs->RDA_Source.CS_Length = p - buf;
	  rdargs->RDA_Buffer = NULL;
	  rdargs->RDA_BufSiz = 0;
	  retval = parser(ndb, rdargs, (UBYTE **)errstrp);
	  if (retval == RETURN_OK)
	    continue;
	  if (retval != RETURN_WARN) /* severe error */
	    break;
	  
	  /* Log the error */
	  log(LOG_NOTICE, "NetDB(%s) line %ld: %s before col %ld\n",
	      fname, line, *errstrp, rdargs->RDA_Source.CS_CurChr);

	  warnval = retval;
	}
	/* Check file error */ 
	ioerr = IoErr();
	
	FreeDosObject(DOS_RDARGS, rdargs);
      }
      Close(fh);
    } else {
      ioerr = IoErr();
    }
    
    if (ioerr) {
      Fault(ioerr, (STRPTR)"readnetdb", res->CS_Buffer, res->CS_Length);
      *errstrp = res->CS_Buffer;
      retval = RETURN_ERROR;
    }
    
    /* return old current directory */
    if (lock) {
      CurrentDir(oldcd);
      UnLock(lock);
    }

    FreeMem(buf, CONFIGLINELEN);
  } else {
    *errstrp = ERR_MEMORY;
    retval = RETURN_FAIL;
  }

  UNLOCK_NDB(ndb);

  return retval > warnval? retval: warnval;
}

/*
 * Parse the 'ADD' command
 */
LONG
do_netdb(struct CSource *csarg, UBYTE **errstrp, struct CSource *res)
{
  struct RDArgs *rdargs;
  LONG retval;
  
  if (rdargs = AllocDosObject(DOS_RDARGS, NULL)) {
    /* initialize CSource of the rdargs */
    rdargs->RDA_Source = *csarg;
    /* initialize rest fields (see <dos/rdargs.h>) */
    rdargs->RDA_DAList = NULL;
    rdargs->RDA_Buffer = NULL;
    rdargs->RDA_BufSiz = 0;
    rdargs->RDA_ExtHelp = NULL;
    rdargs->RDA_Flags = 0;

    LOCK_W_NDB(NDB);

    retval = addndbent(NDB, rdargs, errstrp);

    UNLOCK_NDB(NDB);
    
    FreeDosObject(DOS_RDARGS, rdargs);
  }
  else 
    retval = RETURN_FAIL;
	
  return retval;
}

/*
 * Initialize the Network Data Base
 */
LONG 
init_netdb(void)
{
  UBYTE result[REPLYBUFLEN + 1]; /* for error returns */
  struct CSource res;
  UBYTE *errstr;
  LONG   retval;

  res.CS_Buffer = result;      
  res.CS_Length = sizeof (result); 
  res.CS_CurChr = 0;
  
  /* Allocate the NetDataBase */
  if (!(NDB = alloc_netdb(NULL))) {
    return RETURN_FAIL;
  }

  /* Read in the default data base file */
  retval = read_netdb(NDB, netdbname, (const UBYTE **)&errstr, &res, -1);
  if (retval)
    log(LOG_WARNING, "init_netdb: file %s: %s", netdbname, errstr);

  /*
   * PORT (AmiTCP_NG): forgive a missing/unreadable netdb file -- honour this
   * function's own long-standing documented intent ("Changed init_netdb() to
   * forgive file errors", RCS 1.7). The self-starting LIBS:bsdsocket.library must
   * come up even on a system that has NO AmiTCP:db/netdb at all (e.g. when it is
   * dropped in over Roadshow, whose config lives elsewhere): an empty NDB is
   * enough to run -- literal-name lookups just fall through to DNS, and name
   * servers can be added at runtime (AddDomainNameServer / DHCP). Only a genuine
   * out-of-memory alloc_netdb() failure above is fatal. setup_accesscontroltable()
   * is safe to run on the (possibly empty) NDB either way.
   */
  setup_accesscontroltable(NDB);

  return RETURN_OK;
}


void netdb_deinit(void)
{
  /* A Placeholder for possible future deinitializations */
}
  
/*
 * Reset the NetDataBase
 */
LONG reset_netdb(struct CSource *cs,
		 UBYTE **errstrp,
		 struct CSource *res)
{
  LONG retval;
  struct NetDataBase *newnetdb;

  /* Allocate a temporary NetDataBase */
  if (!(newnetdb = alloc_netdb(NULL))) {
    *errstrp = ERR_MEMORY;
    return RETURN_FAIL;
  }

  retval = read_netdb(newnetdb, netdbname, (const UBYTE **)errstrp, res, -1);

  if (retval == RETURN_OK) {
    /*
     * Success
     */
    struct MinList *gl, *ol;

    setup_accesscontroltable(NDB);

    /* Now clear the old lists of the NDB */
    LOCK_W_NDB(NDB);
    free_netdb(NDB);

    /*
     * Transfer the lists of the new (temporary) database
     * to the NDB.
     */
    for (gl = (struct MinList *)&newnetdb->ndb_Hosts,
	 ol = (struct MinList *)&NDB->ndb_Hosts;
	 gl <= (struct MinList *)&newnetdb->ndb_Domains;        
	 gl++, ol++) {
      if (gl->mlh_Head->mln_Succ) {
	/* There is a non-empty list */
        *ol = *gl;
	ol->mlh_Head->mln_Pred = (struct MinNode*)&ol->mlh_Head;
	ol->mlh_TailPred->mln_Succ = (struct MinNode*)&ol->mlh_Tail;
      }
    }
    NDB->ndb_AccessTable = newnetdb->ndb_AccessTable;
    /*
     * Perhaps ugly...
     */
    newnetdb->ndb_AccessTable = NULL; 

    UNLOCK_NDB(NDB);
  } else {
    free_netdb(newnetdb);
  }

  bsd_free(newnetdb, M_NETDB);	/* free the temporary database */
  return retval;
}
