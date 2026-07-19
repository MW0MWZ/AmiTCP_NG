/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

/*
 * Copyright (c) 1985 Regents of the University of California.
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
static char sccsid[] = "@(#)res_mkquery.c	6.16 (Berkeley) 3/6/91";
#endif /* LIBC_SCCS and not lint */

/*
 * res_mkquery.c --- build a DNS query message. (BIND; see res_init.c.)
 *
 * res_mkquery() formats a DNS request packet in wire form: the 12-byte header
 * (a query id, flags, counts) followed by the question -- the name encoded as
 * length-prefixed labels (via dn_comp, res_comp.c), the query type (T_A for an
 * address, T_PTR for a reverse lookup, ...) and class (C_IN). Pure formatting; no
 * I/O. The result is handed to res_send().
 */

#include <conf.h>

#include <sys/param.h>
#include <api/arpa_nameser.h>
#include <api/resolv.h>
#include <kern/amiga_includes.h>
#include <api/amiga_api.h>
#include <kern/amiga_subr.h>     

/*
 * Form all types of queries.
 * Returns the size of the result or -1.
 */

int res_mkquery(struct SocketBase *libPtr,
		int op,			/* opcode of query */
		const char *dname,	/* domain name */
		int class,
		int type,		/* class and type of query */
		const char *data,	/* resource record data */
		int datalen,		/* length of data */
		const struct rrec *newrr, /* new rr for modify or append */
		char *buf,		/* buffer to put query */
		int buflen)		/* size of buffer */
{
	register HEADER *hp;
	register char *cp;
	register int n;
	char *dnptrs[10], **dpp, **lastdnptr;

#ifdef RES_DEBUG
	       printf("res_mkquery(%d, %s, %d, %d)\n", op, dname, class, type);
#endif /* RES_DEBUG */
	/*
	 * Initialize header fields.
	 */
	if ((buf == NULL) || (buflen < sizeof(HEADER)))
		return(-1);
	bzero(buf, sizeof(HEADER));
	hp = (HEADER *) buf;
	/*
	 * PORT (AmiTCP_NG) hardening: the original derived the query id from a bare,
	 * unseeded sequential counter (++_res.id) -- entirely predictable. Combined
	 * with the (now fixed) lack of a reply-source check in res_send.c that made
	 * off-path DNS cache poisoning nearly free. Stir in the microsecond field of
	 * the system clock, which carries real interrupt/scheduling jitter, so the id
	 * is hard to predict. HONESTY NOTE: this is a WEAK source -- there is no
	 * RTC-grade entropy or CSPRNG on this hardware -- so it lifts the bar well
	 * above a bare counter but is NOT cryptographically strong (the same
	 * pragmatic posture this stack already takes for the TCP initial sequence).
	 * The running counter is folded in too, so two queries issued within the same
	 * microsecond still get distinct ids.
	 */
	{
		struct timeval now;
		static u_short res_id_ctr = 0;

		GetSysTime(&now);
		_res.id = (u_short)((u_short)now.tv_micro
				    ^ (u_short)(now.tv_secs << 5)
				    ^ (u_short)(++res_id_ctr * 40503U));
	}
	hp->id = htons(_res.id);
	hp->opcode = op;
	hp->pr = (_res.options & RES_PRIMARY) != 0;
	hp->rd = (_res.options & RES_RECURSE) != 0;
	hp->rcode = NOERROR;
	cp = buf + sizeof(HEADER);
	buflen -= sizeof(HEADER);
	dpp = dnptrs;
	*dpp++ = buf;
	*dpp++ = NULL;
	lastdnptr = dnptrs + sizeof(dnptrs)/sizeof(dnptrs[0]);
	/*
	 * perform opcode specific processing
	 */
	switch (op) {
	case QUERY:
		if ((buflen -= QFIXEDSZ) < 0)
			return(-1);
		if ((n = dn_comp((u_char *)dname, (u_char *)cp, buflen,
		    (u_char **)dnptrs, (u_char **)lastdnptr)) < 0)
			return (-1);
		cp += n;
		buflen -= n;
		__putshort(type, (u_char *)cp);
		cp += sizeof(u_short);
		__putshort(class, (u_char *)cp);
		cp += sizeof(u_short);
		hp->qdcount = htons(1);
		if (op == QUERY || data == NULL)
			break;
		/*
		 * Make an additional record for completion domain.
		 */
		buflen -= RRFIXEDSZ;
		if ((n = dn_comp((u_char *)data, (u_char *)cp, buflen,
		    (u_char **)dnptrs, (u_char **)lastdnptr)) < 0)
			return (-1);
		cp += n;
		buflen -= n;
		__putshort(T_NULL, (u_char *)cp);
		cp += sizeof(u_short);
		__putshort(class, (u_char *)cp);
		cp += sizeof(u_short);
		__putlong(0, (u_char *)cp);
		cp += sizeof(u_long);
		__putshort(0, (u_char *)cp);
		cp += sizeof(u_short);
		hp->arcount = htons(1);
		break;
	default:
		return (-1); /* is call initially comes from gethostname()
				no other opcodes are used */
	}
	return (cp - buf);
}
