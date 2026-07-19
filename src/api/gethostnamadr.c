/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

/*
 * $Id: gethostnamadr.c,v 3.9 1994/04/02 11:06:28 jraja Exp $
 *
 * Last modified: Sat Feb 26 20:10:41 1994 jraja
 *
 * HISTORY
 * $Log: gethostnamadr.c,v $
 * Revision 3.9  1994/04/02  11:06:28  jraja
 * Moved global resolver variables to SocketBase, removed res_lock.
 *
 * Revision 3.8  1994/02/26  18:11:04  jraja
 * Fixed hethostid() name on the autodoc entry.
 *
 * Revision 3.7  1994/02/15  23:22:21  jraja
 * Added prototype for the findid().
 *
 * Revision 3.6  1994/01/20  02:18:33  jraja
 * Changed include <errno.h> to <sys/errno.h>, removed errno definition,
 * added <conf.h> as the first include.
 *
 * Revision 3.5  1994/01/18  19:21:17  jraja
 * Changed errno macro to use baseErrno() macro.
 *
 * Revision 3.4  1994/01/18  02:10:09  jraja
 * Implemented gethostname(). Added internal sethostname(). Added autodoc
 * for the gethostid().
 *
 * Revision 3.3  1994/01/13  07:35:25  jraja
 * Added implementation for the gethostid().
 *
 * Revision 3.2  1994/01/09  21:06:45  too
 * Added setting of h_errno to rest returns
 *
 * Revision 3.1  1994/01/04  14:10:25  too
 * Removed "extern int h_errno". Now it is defined to
 * libPtr->hErrno in resolv.h. Added cores for gethostname() and
 * gethostid()
 *
 * Revision 1.7  1993/09/08  14:26:47  too
 * Fixed getanswer (from gethostbyaddr) further. Now alias and addr lists
 * are NULL terminated. question is no longer copied. Removed that
 * unnecessary aligment by accident.
 *
 * Revision 1.6  1993/08/20  17:26:34  too
 * Fixed bug in getanswer that caused gethostbyaddr() to return
 * hostname w/ garpage at the end.
 * Added some comments w/ wponders
 * now answer buffer is aligned before host addresses are written.
 * this is unnecessary though.
 * getanswer still writes the question to the answer buffer. This will
 * be fixed later since it doesn't affect functionality
 *
 * Revision 1.5  1993/06/07  12:37:20  too
 * Changed inet_ntoa, netdatabase functions and WaitSelect() use
 * separate buffers for their dynamic buffers
 *
 */
/*
 * Copyright (c) 1985, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)gethostnamadr.c	6.45 (Berkeley) 2/24/91";
#endif /* LIBC_SCCS and not lint */

/*
 * gethostnamadr.c --- gethostbyname() / gethostbyaddr(): name <-> address lookup.
 *
 * The public host-lookup entry points. gethostbyname() first consults the local
 * network database (kern/amiga_netdb.c: the "hosts" table), and if not found and a
 * nameserver is configured, issues a DNS query through the resolver (res_search,
 * api/res_query.c). gethostbyaddr() does the reverse (a PTR query). The result is
 * built into a `struct hostent` in this opener's private buffer (allocdatabuffer.c).
 * This is where the netdb and the DNS resolver meet. (Berkeley-derived.)
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <netinet/in.h>
#include <net/if.h> /* for the gethostid() needs */
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/errno.h>

#include <api/arpa_nameser.h>
#include <api/resolv.h>
#include <kern/amiga_includes.h>
#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <api/amiga_raf.h>
#include <api/allocdatabuffer.h>     
#include <kern/amiga_subr.h>

#include <api/gethtbynamadr.h>     /* prototypes (NO MORE BUGS HERE) */
#include <api/apicalls.h>
#include <api/dns_cache.h>

/* memcpy() is not declared by any reachable header in the GNUC/-noixemul
 * build (see the strcmp() note in amiga_roadshow.c for why plain
 * <string.h> cannot be included here). Still provided by libnix at link
 * time, so just declare it. */
extern void *memcpy(void *, const void *, size_t);
     
#define	MAXALIASES	35
#define	MAXADDRS	35

#if PACKETSZ > 1024
#define	MAXPACKET	PACKETSZ
#else
#define	MAXPACKET	1024
#endif

typedef union {
    HEADER hdr;
    u_char buf[MAXPACKET];
} querybuf;

typedef union {
    long al;
    char ac;
} align;

/*
 * macro for getting error value from another library base function
 * ( which is called directly here )
 */

/*
 * hostent structure in SocketBase
 */
#define HOSTENT ((struct hostent *)libPtr->hostents.db_Addr)

/*
 * longword align given pointer (i.e. divides by 4)
 */
#define ALIGN(p) (((u_int)(p) + (sizeof(long) - 1)) &~ (sizeof (long) -1))

#define MAXALIASES	35
#define MAXADDRS	35  
  
typedef char hostbuf_t[512];

struct hoststruct {
  char * host_aliases[MAXALIASES + 1];
  char * h_addr_ptrs[MAXADDRS + 1];
  short host_alias_count;
  short h_addr_count;
  u_long min_ttl;		/* smallest RR TTL in the answer (RFC 1035); for the DNS cache */
  struct hostent host;
  hostbuf_t hostbuf;
};

static struct hostent * makehostent(struct SocketBase * libPtr,
				    struct hoststruct * HS,
				    char * ptr);

LONG usens = 1;

static char *
 getanswer(struct SocketBase * libPtr, querybuf *answer,int anslen, int iquery,
	    struct hoststruct * HS)
{
  register HEADER *hp;
  register u_char *cp; /* pointer to traverse in 'answer' */
  register int n;
  u_char *eom;
  int buflen = sizeof HS->hostbuf;
  char *bp; /* bp -- answer buffer pointer */
  int type, class, ancount, qdcount;
  int haveanswer, getclass = C_ANY;
  char **ap, **hap;
  u_long ttl, min_ttl = ~0UL;		/* per-message minimum RR TTL (for the DNS cache) */
  
  eom = answer->buf + anslen;
  /*
   * find first satisfactory answer
   */
  hp = &answer->hdr;
  ancount = ntohs(hp->ancount); /* how many answers returned from nameserver */
  qdcount = ntohs(hp->qdcount); /* how many questions in nameserver query */

  /*
   *  bp, points to start of buffer space where new resolved answer is to 
   *  be written. the bp is moved to next free space. Initially it is
   *  set below, to start of buffer allocated for it-
   */
  bp = HS->hostbuf;
  /*
   * address cp to start of nameserver answers (after static sized header)
   */
  cp = answer->buf + sizeof(HEADER);

  /*
   * Any questions asked..hmm this should always be the case
   */
  if (qdcount) {
#if 0			/* added by too 8.Sep.1993: skipping strange parts */
    /*
     * gethostbyaddr uses inverse query...
     */
    if (iquery) {
      if ((n = dn_expand((u_char *)answer->buf,
			 (u_char *)eom, (u_char *)cp, (u_char *)bp,
			 buflen)) < 0) {
	h_errno = NO_RECOVERY;
	return NULL;
      }
      cp += n + QFIXEDSZ;
      /*
       * Hostname in final hostent structure is written here in case
       * of gethostbyaddr. (from question section ???)
       */
      HS->host.h_name = bp;
      n = strlen(bp) + 1;
      bp += n;
      buflen -= n;
    }
    else
#endif	/* 0 */		/* 8Sep93: now code below skips all question strings */
      /*
       * here is normal query (gethostbyname). skipping query section
       * hmm, wondering why is it originally implemented as 2
       * __dn_skipname function calls ?
       */
      cp += __dn_skipname(cp, eom) + QFIXEDSZ;
    while (--qdcount > 0)
      cp += __dn_skipname(cp, eom) + QFIXEDSZ;
  }
  else if (iquery) {
    /*
     * no questions and inverse query :o
     */
    if (hp->aa)
      h_errno = HOST_NOT_FOUND;
    else
      h_errno = TRY_AGAIN;
    return NULL;
  }
  ap = HS->host_aliases;
  HS->host_alias_count = 1; /* there is always NULL as last pointer */
  hap = HS->h_addr_ptrs;
  HS->h_addr_count = 1; /* there is always NULL as last pointer */
  
  haveanswer = 0;
  while (--ancount >= 0 && cp < eom) {
    if ((n = dn_expand((u_char *)answer->buf, (u_char *)eom,
		       (u_char *)cp, (u_char *)bp, buflen)) < 0)
      break;
    cp += n;
    /*
     * PORT (AmiTCP_NG) security fix: before reading the fixed
     * resource-record header (type+class+ttl+rdlength = 10 bytes) make sure it
     * is actually present -- the loop condition only guaranteed cp < eom, so a
     * truncated or hostile reply could otherwise make us read past the message.
     */
    if (cp + 2 * sizeof(u_short) + sizeof(u_long) + sizeof(u_short) > (u_char *)eom)
      break;
    /*
     * Type and class are type and class of answer in returned resource
     * record. see arpa[_/]nameserver.h for more information.
     */
    type = _getshort(cp);
    cp += sizeof(u_short);
    class = _getshort(cp);
    cp += sizeof(u_short);
    ttl = _getlong(cp);			/* RR TTL (RFC 1035) -- was previously skipped */
    cp += sizeof(u_long);
    if (ttl < min_ttl)			/* track the answer's minimum TTL for the cache */
      min_ttl = ttl;
    n = _getshort(cp);
    cp += sizeof(u_short);
    /*
     * PORT (AmiTCP_NG) security fix: RDLENGTH must not claim more
     * data than actually remains in the message, or the bcopy/dn_expand below
     * would over-read past eom (info leak of adjacent heap into the hostent).
     */
    if (n < 0 || cp + n > (u_char *)eom)
      break;
    
    if (type == T_CNAME) {   /* canonical name (add alias names)*/
      cp += n;
      if (HS->host_alias_count >= MAXALIASES)
	continue;
      *ap++ = bp;
      HS->host_alias_count++;
      n = strlen(bp) + 1;
      bp += n;
      buflen -= n;
      continue;
    }
    if (iquery && type == T_PTR) {  /* domain name pointer (get domain
				       name and return) */
      if ((n = dn_expand((u_char *)answer->buf,
			 (u_char *)eom, (u_char *)cp, (u_char *)bp,
			 buflen)) < 0) {
	cp += n;
	continue;
      }
      cp += n;
      HS->host.h_name = bp;   /* well, rewrites name pointer if there were
				 returned questions also... */
      haveanswer = 1;
      bp+= (strlen(bp) + 1);
      break;
    }
    if (iquery || type != T_A)  {
      /*
       * here is strange answer from nameserver: inverse query should have
       * been handled earlyer and there should not be any other types
       * left than "host address"
       */
#ifdef RES_DEBUG
      printf("unexpected answer type %d, size %d\n",
	     type, n);
#endif
      cp += n;
      continue;
    }
    /*
     * PORT (AmiTCP_NG) security fix: type == T_A here. An IPv4 A
     * record's RDATA is exactly a 4-byte address; reject any other length so a
     * hostile RDLENGTH can neither over-copy into the address slot nor set a
     * bogus h_length that downstream code (assuming 4) would misread.
     */
    if (n != sizeof(struct in_addr)) {
      cp += n;
      continue;
    }
    if (haveanswer) {
      /*
       * Here if one host address answer is already returned (rather odd...)
       */
      if (n != HS->host.h_length) {
	cp += n;
	continue;
      }
      if (class != getclass) {
	cp += n;
	continue;
      }
    }
    else {
      /*
       * Fill in host address data and comparing info for next cycle (if any)
       */
      HS->host.h_length = n;
      getclass = class;
      HS->host.h_addrtype = (class == C_IN) ?
	AF_INET : AF_UNSPEC;
      if (!iquery) {
	/*
	 * if not inverse query and haveanswer = 0 host name is first in
	 * bp pointed buffer. (rather strange if answer already returned
	 * and new addresses are to be added since aren't in that case
	 * also names returned or is it inconsistent or have i missed
	 * something ?
	 */
	int n1;

	HS->host.h_name = bp;
	n1 = strlen(bp) + 1;
	bp += n1;
	buflen -= n1;
      }
    }

/*    bp = (char *)ALIGN(bp); -- align answer buffer for next host address */
		
    if (HS->host.h_length >= buflen) {
#ifdef RES_DEBUG
      printf("size (%d) too big\n", host->h_length);
#endif
      break;
    }
    /*
     * PORT (AmiTCP_NG) security fix: the alias path above guards
     * against overflowing host_aliases[]; the address path did NOT. A reply
     * packed with many compressed A records would otherwise walk *hap past the
     * end of h_addr_ptrs[MAXADDRS+1] and corrupt the adjacent hoststruct.
     */
    if (HS->h_addr_count >= MAXADDRS) {
      cp += n;
      continue;
    }
    /*
     * Fill next host address in address list
     */
    bcopy(cp, *hap++ = bp, n);
    HS->h_addr_count++;
    bp += n;
    buflen -= n;
    cp += n;
    haveanswer++;
  } /* while (--ancount ...) */
  
  if (haveanswer) {
    *ap = NULL;
    *hap = NULL;
    HS->min_ttl = min_ttl;		/* captured for the DNS cache (no reader until it lands) */
    return bp;
  }
  else {
    h_errno = TRY_AGAIN;
    return NULL;
  }
}

/*
 * PORT (AmiTCP_NG): the host-resolution core of gethostbyname(), factored out of
 * the register-argument library entry so getaddrinfo() (api/amiga_roadshow.c) can
 * reuse the FULL numeric + local-database + DNS lookup directly. Assumes the
 * caller already validated the task context (the entry points do CHECK_TASK).
 */
struct hostent *
ng_gethostbyname_impl(struct SocketBase *libPtr, const char *name)
{
  querybuf *buf;
  int n;
  extern int inet_aton(const char *name, struct in_addr *ia);
  struct hostent * anshost = NULL;

  /*
   * check if name consists only dots and digits.
   */
  if (isdigit(name[0])) {
    struct in_addr inaddr;
    u_long * lptr;
    
    if (!inet_aton(name, &inaddr)) {
      writeErrnoValue(libPtr, EINVAL);
      h_errno = 0;
      return NULL;
    }
    
    /*
     * PORT (AmiTCP_NG) security fix: the tail buffer holds the
     * address (4) + its pointer (4) + a NULL (4) + the name string, and the
     * name is strcpy()'d in below. inet_aton() is deliberately lenient (it
     * accepts leading zeros / octal / hex, so a "digit-leading" name can be far
     * longer than the 15 chars of "255.255.255.255"). Size the allocation from
     * the real name length instead of a fixed 28 so the strcpy can never overrun.
     */
    if (allocDataBuffer(&libPtr->hostents,
			sizeof (struct hostent) + 12 + strlen(name) + 1) == FALSE) {
      writeErrnoValue(libPtr, ENOMEM);
      h_errno = 0;
      return NULL;
    }
    HOSTENT->h_addrtype = AF_INET;
    HOSTENT->h_length = sizeof (struct in_addr);
    lptr = (u_long *)(HOSTENT + 1);
    *lptr++ = inaddr.s_addr;
    *(u_long **)(lptr) = lptr - 1;
    HOSTENT->h_addr_list = (char **)lptr;
    *++lptr = NULL;
    HOSTENT->h_aliases = (char **)lptr;
    HOSTENT->h_name = strcpy((char *)++lptr, name);
    
    return HOSTENT;
  }
  /*
   * Search local database (first) is usens not FIRST
   */
  if (usens != 1)
    if ((anshost =_gethtbyname(libPtr, name)) != NULL || usens == 0)
      return anshost;
  /*
   * Here if usens is FIRST or host not in local database and usens is SECOND
   */
  /*
   * DNS response cache: front the network query. A fresh positive hit is
   * materialised into libPtr's per-opener buffer for us and returned as-is.
   */
  {
    int neg = 0;
    struct hostent *hit = ng_dnscache_get(libPtr, name, T_A, &neg);
    if (hit != NULL)
      return hit;
    if (neg) {			/* cached "not found": fail fast, no query */
      h_errno = HOST_NOT_FOUND;
      return NULL;
    }
  }

  if ((buf = bsd_malloc(sizeof (querybuf), M_TEMP, M_WAITOK)) == NULL) {
    writeErrnoValue(libPtr, ENOMEM);
    return NULL;
  }

  n = res_search(libPtr, name, C_IN, T_A, buf->buf, sizeof (querybuf));
  if (n >= 0) {
    unsigned long ttl = 0;
    anshost = ng_hostent_from_response(libPtr, buf, n, 0, &ttl);
    if (anshost != NULL)
      ng_dnscache_put(name, T_A, buf->buf, n, ttl);
  }
  else {
    int herr = h_errno;		/* resolver's verdict, before any local-db fallback */
#ifdef RES_DEBUG
    printf("res_search failed\n");
#endif
    /*
     * If usens is FIRST and host not found using resolver.
     */
    if (usens != 2)
      anshost =_gethtbyname(libPtr, name);
    /*
     * Negative-cache only a definitive "no such name" (NXDOMAIN), and only if
     * nothing turned it up: a transient failure (TRY_AGAIN / timeout) must be
     * retried, not remembered.
     */
    if (anshost == NULL && herr == HOST_NOT_FOUND)
      ng_dnscache_put_negative(name, T_A);
  }
  bsd_free(buf, M_TEMP);
  return anshost;

}

struct hostent * SAVEDS RAF2 (_gethostbyname,
			      struct SocketBase *,	libPtr,	a6,
			      const char *,		name,	a0)
#if 0
{
#endif
  CHECK_TASK2();
  return ng_gethostbyname_impl(libPtr, name);
}

/*
 * PORT (AmiTCP_NG): the reverse-lookup core of gethostbyaddr(), factored out (as
 * with ng_gethostbyname_impl) so getnameinfo() can reuse the full local-database
 * + reverse-DNS (in-addr.arpa PTR) lookup. Assumes the caller already validated
 * the task context.
 */
struct hostent *
ng_gethostbyaddr_impl(struct SocketBase *libPtr, const UBYTE *addr, int len, int type)
{
  querybuf * buf;
  int n;
  char * ptr;
  struct hoststruct * HS = NULL;
  char * qbuf;
  struct hostent * anshost = NULL;

  if (type != AF_INET)
    return ((struct hostent *) NULL);
  
  /*
   * Search local database (first) is usens not FIRST
   */
  if (usens != 1)
    if ((anshost =_gethtbyaddr(libPtr, (const char *)addr, len, type)) != NULL || usens == 0)
      return anshost;

  /*
   * Here if usens is FIRST or host not in local database and usens is SECOND
   */
  if ((HS = bsd_malloc(sizeof (querybuf) + MAXDNAME + 1 +
		       sizeof (struct hoststruct),  M_TEMP, M_WAITOK))
      == NULL) {
    writeErrnoValue(libPtr, ENOMEM);
    return NULL;
  }
  buf = (querybuf *)(HS + 1);
  qbuf = (caddr_t)(buf + 1);
  
  (void)sprintf(qbuf, "%lu.%lu.%lu.%lu.in-addr.arpa",
		((unsigned)addr[3] & 0xff),
		((unsigned)addr[2] & 0xff),
		((unsigned)addr[1] & 0xff),
		((unsigned)addr[0] & 0xff));
  n = res_query(libPtr, qbuf, C_IN, T_PTR, (u_char *)buf, sizeof (querybuf));

  if (n >= 0) {
    ptr = getanswer(libPtr, buf, n, 1, HS);
    if (ptr != NULL) {
      if (HS->h_addr_count == 1) {
	HS->h_addr_count++;
	bcopy(addr, ptr, len);
	HS->h_addr_ptrs[0] = ptr;
	ptr += len;
      }      
      else
	bcopy(addr, &HS->h_addr_ptrs[0], len);
      HS->h_addr_ptrs[1] = NULL;
      if ((anshost = makehostent(libPtr, HS, ptr)) != NULL) {
	anshost->h_addrtype = type;
	anshost->h_length = len;
      }
    }
  }
  else {
#ifdef RES_DEBUG
    printf("res_query failed\n");
#endif
    /*
     * If usens is FIRST and host not found using resolver.
     */
    if (usens != 2)
      anshost = _gethtbyaddr(libPtr, (const char *)addr, len, type);
  }
  if (HS)
    bsd_free(HS, M_TEMP);
  return anshost;
}

struct hostent * SAVEDS RAF4 (_gethostbyaddr,
			      struct SocketBase *,	libPtr,	a6,
			      const UBYTE *,		addr,	a0,
			      int,			len,	d0,
			      int,			type,	d1)
#if 0
{
#endif
  CHECK_TASK2();
  return ng_gethostbyaddr_impl(libPtr, addr, len, type);
}

static struct hostent * makehostent(struct SocketBase * libPtr,
				    struct hoststruct * HS,
				    char * ptr)
{
  int n, i;

  i = (caddr_t)ALIGN(ptr) - (caddr_t)&HS->hostbuf;
  n = i + sizeof (struct hostent) + HS->h_addr_count * sizeof (char *) +
    HS->host_alias_count * sizeof (char *);
    
  if (allocDataBuffer(&libPtr->hostents, n) == FALSE) {
    writeErrnoValue(libPtr, ENOMEM);    
    return NULL;
  }
  /*
   * copy ent data to user buffer (pointers will be set later)
   */
  bcopy(HS->hostbuf, (caddr_t)(HOSTENT + 1), i);

  /*
   * how much to add to old pointers
   */
  n = (caddr_t)HOSTENT + sizeof(struct hostent) - (caddr_t)&HS->hostbuf;
  
  /*
   * fill vital fields in user hostent structure
   */
  HOSTENT->h_name = HS->host.h_name + n;

  HOSTENT->h_aliases = (char **)((char *)(HOSTENT + 1) + i);
  for (i = 0; HS->host_aliases[i]; i++)
    HOSTENT->h_aliases[i] = HS->host_aliases[i] + n;
  HOSTENT->h_aliases[i++] = NULL;

  HOSTENT->h_addr_list = HOSTENT->h_aliases + i;
  for (i = 0; HS->h_addr_ptrs[i]; i++)
    HOSTENT->h_addr_list[i] = HS->h_addr_ptrs[i] + n;
  HOSTENT->h_addr_list[i] = NULL;

  return HOSTENT;
}

/*
 * Replay a raw DNS response into libPtr's per-opener hostent buffer via the
 * normal getanswer()/makehostent() path. Used for both a freshly received
 * answer and a DNS-cache hit, so a cached answer materialises identically to a
 * fresh one (no separate re-serialisation to get wrong). iquery is nonzero for
 * a reverse (PTR) lookup. If min_ttl_out is non-NULL it receives the answer's
 * minimum RR TTL (for the cache). Returns NULL on parse failure. Declared in
 * dns_cache.h; non-static so the cache module can reach it.
 */
struct hostent *
ng_hostent_from_response(struct SocketBase *libPtr, void *response, int len,
			 int iquery, unsigned long *min_ttl_out)
{
  struct hoststruct *HS;
  char *ptr;
  struct hostent *anshost = NULL;

  if ((HS = bsd_malloc(sizeof (struct hoststruct), M_TEMP, M_WAITOK)) == NULL) {
    writeErrnoValue(libPtr, ENOMEM);
    return NULL;
  }
  ptr = getanswer(libPtr, (querybuf *)response, len, iquery, HS);
  if (ptr != NULL && (anshost = makehostent(libPtr, HS, ptr)) != NULL) {
    anshost->h_addrtype = HS->host.h_addrtype;
    anshost->h_length = HS->host.h_length;
    if (min_ttl_out)
      *min_ttl_out = HS->min_ttl;
  }
  bsd_free(HS, M_TEMP);
  return anshost;
}

/*
 * id_addr variable is used by both the gethostname() and gethostid().
 * 
 * host_name is the host_name configuration variable.
 */
static ULONG id_addr = 0;

void findid(ULONG *); /* defined in net/if.c */

/*
 * Global host name and name length
 */
char host_name[MAXHOSTNAMELEN+1] = { 0 };
size_t host_namelen = 0;

/****i* AmiTCP/sethostname *********************************************
*
*   NAME   
*       sethostname -- set the name of the host
*
*   SYNOPSIS
*       error = sethostname(name, namelen);
*
*       int sethostname(const char *, size_t);
*
*   FUNCTION
*       Set the name of the host to the given 'name' of length 'namelen'.
*
*   INPUTS
*       name    - Pointer to the name string.
*       namelen - Length of the name.
*
*   RESULT
*       error   - 0 on success.
*  
*   EXAMPLE
*
*   NOTES
*       This function is not intended to be provided to the applications,
*       this is for AmiTCP internal use only (at least for now).
*
*   BUGS
*
*   SEE ALSO
*****************************************************************************
*
*/
int
sethostname(const char * name, size_t namelen)
{
  if (namelen > MAXHOSTNAMELEN)
    namelen = MAXHOSTNAMELEN;

  memcpy(host_name, name, namelen);
  host_name[namelen] = '\0';
  host_namelen = namelen;

  return 0;
}

/****** bsdsocket.library/gethostname *************************************
*
*   NAME   
*       gethostname -- get the name of the host
*
*   SYNOPSIS
*       error = gethostname(name, namelen);
*
*       long gethostname(char *, long);
*
*   FUNCTION
*       Get the name of the host to the buffer name of length namelen.
*       The name is queried from the netdb and/or the name server if 
*       it is not explicitly configured (configuration variable
*       HOSTNAME).
*
*   INPUTS
*       name    - Pointer to the buffer where the name should be
*                 stored.
*       namelen - Length of the buffer name.
*
*   RESULT
*       error   - 0 on success.
*  
*   EXAMPLE
*       char hostname[MAXHOSTNAMELEN];
*       long error;
*       
*       error = gethostname(hostname, sizeof(hostname));
*       if (error < 0)
*         exit(10);
*       
*       printf("My name is \"%s\".\n", hostname);
*
*   NOTES
*
*   BUGS
*       Unlike the Unix version, this version assures that the
*       resulting string is always NULL-terminated.
*
*   SEE ALSO
*       gethostid()
*****************************************************************************
*
*/
LONG SAVEDS RAF3(_gethostname,
		 struct SocketBase *,	libPtr,		a6,
		 STRPTR,		name,		a0,
		 LONG,			namelen,	d0)
#if 0     
{
#endif
  CHECK_TASK();

  /*
   * Get the name with the gethostbyaddr(), if the name is not set yet.
   */
  if (*host_name == '\0') {
    struct hostent * hent;
    /* gethostid() */
    if (id_addr == 0)
      findid(&id_addr);
    if (id_addr != 0) { /* query if we have an address */
      hent = gethostbyaddr(libPtr, (const char *)&id_addr,
			   sizeof(id_addr), AF_INET);
      if (hent != NULL) {
	sethostname(hent->h_name, strlen(hent->h_name));
      }
    }
  }
  
  /*
   * PORT (AmiTCP_NG) security fix: a caller passing namelen <= 0
   * must be rejected here. Otherwise the `else namelen--` below underflows the
   * signed LONG to -1, which becomes a ~4GB size_t for memcpy() and an
   * out-of-bounds name[-1] write -- machine corruption on this MMU-less target.
   */
  if (namelen <= 0)
    API_STD_RETURN(EINVAL, 0);

  /*
   * Copy the name to the user buffer. stccpy() ensures that the buffer
   * is not written over and that it will be null-terminated.
   */
  if (namelen > host_namelen)
    namelen = host_namelen;
  else
    namelen--;			/* make space for the trailing '\0' */

  memcpy(name, host_name, namelen);
  name[namelen] = '\0';

  API_STD_RETURN(0, 0);
}

/****** bsdsocket.library/gethostid ***************************************
*
*   NAME   
*       gethostid -- get an unique 32-bit id to this host
*
*   SYNOPSIS
*       id = gethostid();
*
*       ULONG gethostid(void);
*
*   FUNCTION
*       Return the 32-bit unique id for this host. The Internet
*       address if the primary interface is used as the unique id.
*       This means that this function is also a supported way to get
*       the hosts IP address in AmiTCP/IP. If no interfaces are
*       configured, zero is returned. Any non-loobpack interface with
*       is preferred. Only if no other interfaces are present, is the
*       loopback address returned.
*       
*   INPUTS
*
*   RESULT
*       id  - non-zero on success.
*  
*   EXAMPLE
*       ULONG id;
*       
*       id = gethostid();
*       if (id == 0)
*         exit(10);
*       
*       printf("My primary IP address is: %s.\n", Inet_NtoA(id));
*
*   NOTES
*       Non-zero id is returned as soon as a interface is configured.
*       After that the id will not change, not even if the id is the
*       address of the loopback interface.
*
*   BUGS
*
*   SEE ALSO
*****************************************************************************
*
*/
ULONG SAVEDS RAF1(_gethostid,
		  struct SocketBase *,	libPtr,	a6)
#if 0     
{
#endif
  (void)libPtr;
  if (id_addr == 0)
    findid(&id_addr);
  return id_addr;
}
