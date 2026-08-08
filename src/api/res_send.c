/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

/*
 * Copyright (c) 1985, 1989 Regents of the University of California.
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

 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)res_send.c	6.27 (Berkeley) 2/24/91";
#endif /* LIBC_SCCS and not lint */

/*
 * Send query to name server and wait for reply.
 */

/*
 * res_send.c --- transmit a DNS query and read the reply. (BIND; see res_init.c.)
 *
 * The only resolver file that touches sockets. res_send() sends the query datagram
 * to each configured nameserver in turn (UDP), waits (with a timeout) for a reply,
 * and retries the next server on failure. If a reply comes back truncated (too big
 * for a UDP datagram) it re-asks over TCP. Returns the raw DNS response for the
 * caller (res_query) to parse. This is where nameserver selection and timeouts live.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/errno.h>

#include <api/arpa_nameser.h>
#include <api/resolv.h>
#include <kern/amiga_includes.h>
#include <api/apicalls.h>
#include <api/amiga_api.h>
#include <kern/amiga_subr.h>     
#include <kern/amiga_netdb.h>     

/*
 * res_sock is the PER-TASK resolver socket: resolv.h maps `res_sock` to
 * NG_CTX(libPtr)->res_socket, a field of the calling task's scratch context
 * (api/amiga_api.h), initialised to -1 when the base is opened. Each task
 * resolving concurrently uses its own socket -- there is no shared socket to
 * race. (A dead `#ifndef AMITCP` file-
 * static used to sit here; it never compiled in this AMITCP build but made the
 * socket look shared, so it was removed.)
 */

/* constant */
static const struct sockaddr no_addr = { sizeof(struct sockaddr), AF_INET, {0} };

#ifndef FD_SET
#define	NFDBITS		32
#define	FD_SETSIZE	32
#define	FD_SET(n, p)	((p)->fds_bits[(n)/NFDBITS] |= (1 << ((n) % NFDBITS)))
#define	FD_CLR(n, p)	((p)->fds_bits[(n)/NFDBITS] &= ~(1 << ((n) % NFDBITS)))
#define	FD_ISSET(n, p)	((p)->fds_bits[(n)/NFDBITS] & (1 << ((n) % NFDBITS)))
#define FD_ZERO(p)	bzero((char *)(p), sizeof(*(p)))
#endif

int
res_send(struct SocketBase *	libPtr,
	 const char *		buf,
	 int			buflen,
	 char *			answer,
	 int 			anslen)
{
	register int n;
	int try, v_circuit, resplen, nscount;
	int gotsomewhere = 0, connected = 0;
	int connreset = 0;
	u_short id, len;
	char *cp;
	fd_set dsmask;
	struct timeval timeout;
	long waitleft = 0;	/* seconds left for the current server; see
				 * the "goto wait" hardening note below */
	long wtotal = 0;	/* that server's whole budget, in seconds */
	int wrejects = 0;	/* rejected packets left before we give up */
	struct timeval wstart;	/* when the wait on this server began */
	int nsi;			/* index of the nameserver being queried */
	struct sockaddr_in host;
	HEADER *hp = (HEADER *) buf;
	HEADER *anhp = (HEADER *) answer;
	u_char terrno = ETIMEDOUT;
#define MAXREJECTS 32	/* rejected packets tolerated per server, backstopping the clock */
#define JUNK_SIZE 128	/* read-and-discard drain chunk; small to keep the caller-stack frame down */
	char junk[JUNK_SIZE]; /* buffer for trash data */

#ifdef RES_DEBUG
		printf("res_send()\n");
		__p_query(buf);
#endif /* RES_DEBUG */

	v_circuit = (_res.options & RES_USEVC) || buflen > PACKETSZ;
	id = hp->id;

	/* Defensive: the name database is created at stack init and never cleared,
	 * so NDB is non-NULL for the whole life of the API here. Guard anyway (as
	 * ng_flush_dynamic_nameservers does) so a future teardown that frees NDB
	 * could never fault the LOCK_R_NDB(NDB) / ndb_NameServers walk below. */
	if (NDB == NULL) {
		writeErrnoValue(libPtr, ECONNREFUSED);
		return (-1);
	}

	/*
	 * Send request, RETRY times, or until successful
	 */
	for (try = 0; try < _res.retry; try++) {
	  nscount = 0;
	  for (nsi = 0; ; nsi++) {
	    struct NameserventNode *nn;
	    int k;
	    /*
	     * Fetch the nsi-th nameserver's address under LOCK_R_NDB, re-walking from
	     * the list head each pass, so no ndb_NameServers node is ever dereferenced
	     * across the blocking socket I/O below. RemoveDomainNameServer() and
	     * ng_flush_dynamic_nameservers() (DHCP offline/teardown) take the write lock
	     * and Remove()+free() these nodes -- holding a node pointer across a send/recv
	     * would be a use-after-free. Only the copied host.sin_addr crosses the unlock;
	     * a `goto usevc` retry reuses it (nsi unchanged), so it needs no re-fetch.
	     */
	    LOCK_R_NDB(NDB);
	    nn = (struct NameserventNode *)NDB->ndb_NameServers.mlh_Head;
	    for (k = 0; k < nsi && nn->nsn_Node.mln_Succ; k++)
	      nn = (struct NameserventNode *)nn->nsn_Node.mln_Succ;
	    if (nn->nsn_Node.mln_Succ == NULL) {
	      UNLOCK_NDB(NDB);
	      break;			/* reached the end of the nameserver list */
	    }
	    host.sin_addr = nn->nsn_Ent.ns_addr;
	    UNLOCK_NDB(NDB);
	    nscount++;
#ifdef RES_DEBUG
			printf("Querying server #%d address = %s\n", nscount,
			      inet_ntoa(host.sin_addr));
#endif /* RES_DEBUG */
	    host.sin_len = sizeof(host);
	    host.sin_family = AF_INET;
	    host.sin_port = NAMESERVER_PORT;
	    aligned_bzero_const(&host.sin_zero, sizeof(host.sin_zero));
	usevc:
		if (v_circuit) {
			int truncated = 0;

			/*
			 * Use virtual circuit;
			 * at most one attempt per server.
			 */
			try = _res.retry;
			if (res_sock < 0) {
				res_sock = socket(libPtr, AF_INET, SOCK_STREAM, 0);
				if (res_sock < 0) {
					terrno = readErrnoValue(libPtr);
#ifdef RES_DEBUG
					    perror("socket (vc) failed");
#endif /* RES_DEBUG */
					continue;
				}
				if (connect(libPtr, res_sock,
					    (struct sockaddr *)&host,
					    sizeof(struct sockaddr)) < 0) {
				        terrno = readErrnoValue(libPtr);
#ifdef RES_DEBUG
					    perror("connect failed");
#endif /* RES_DEBUG */
					(void) CloseSocket(libPtr, res_sock);
					res_sock = -1;
					continue;
				}
			}
			/*
			 * Send length & message
			 */
			len = htons((u_short)buflen);
			if ((send(libPtr, res_sock, (char *)&len, sizeof(len), 0)
			     != sizeof(len)) ||
			   ((send(libPtr, res_sock, (char *)buf, buflen, 0)
			     != buflen))) {
				terrno = readErrnoValue(libPtr);
#ifdef RES_DEBUG
					perror("write failed");
#endif /* RES_DEBUG */
				(void) CloseSocket(libPtr, res_sock);
				res_sock = -1;
				continue;
			}
			/*
			 * Receive length & response
			 */
			cp = answer;
			len = sizeof(short);
			while (len != 0 &&
			    (n = recv(libPtr, res_sock,
				      (char *)cp, (int)len, 0)) > 0) {
				cp += n;
				len -= n;
			}
			if (n <= 0) {
				terrno = readErrnoValue(libPtr);
#ifdef RES_DEBUG
					perror("read failed");
#endif /* RES_DEBUG */
				(void) CloseSocket(libPtr, res_sock);
				res_sock = -1;
				/*
				 * A long running process might get its TCP
				 * connection reset if the remote server was
				 * restarted.  Requery the server instead of
				 * trying a new one.  When there is only one
				 * server, this means that a query might work
				 * instead of failing.  We only allow one reset
				 * per query to prevent looping.
				 */
				if (terrno == ECONNRESET && !connreset) {
					connreset = 1;
					goto usevc;	/* retry the SAME server once; host.sin_addr
							 * still holds it and nsi is unchanged, so
							 * this reuses the address, no re-fetch. */
				}
				continue;
			}
			cp = answer;
			if ((resplen = ntohs(*(u_short *)cp)) > anslen) {
#ifdef RES_DEBUG
			               fprintf(stderr, "response truncated\n");
#endif /* RES_DEBUG */
				len = anslen;
				truncated = 1;
			} else
				len = resplen;
			while (len != 0 &&
			   (n = recv(libPtr, res_sock,
				     (char *)cp, (int)len, 0)) > 0) {
				cp += n;
				len -= n;
			}
			if (n <= 0) {
				terrno = readErrnoValue(libPtr);
#ifdef RES_DEBUG
					perror("read failed");
#endif /* RES_DEBUG */
				(void) CloseSocket(libPtr, res_sock);
				res_sock = -1;
				continue;
			}
			if (truncated) {
				/*
				 * Flush rest of answer
				 * so connection stays in synch.
				 */
				anhp->tc = 1;
				len = resplen - anslen;
				while (len != 0) {
					n = (len > JUNK_SIZE ? JUNK_SIZE : len);
					if ((n = recv(libPtr, res_sock,
						      junk, n, 0)) > 0)
						len -= n;
					else
						break;
				}
			}
		} else {
			/*
			 * Use datagrams.
			 */
			if (res_sock < 0) {
				res_sock = socket(libPtr, AF_INET, SOCK_DGRAM, 0);
				if (res_sock < 0) {
					terrno = readErrnoValue(libPtr);
#ifdef RES_DEBUG
					    perror("socket (dg) failed");
#endif /* RES_DEBUG */
					continue;
				}
			}
			/*
			 * I'm tired of answering this question, so:
			 * On a 4.3BSD+ machine (client and server,
			 * actually), sending to a nameserver datagram
			 * port with no nameserver will cause an
			 * ICMP port unreachable message to be returned.
			 * If our datagram socket is "connected" to the
			 * server, we get an ECONNREFUSED error on the next
			 * socket operation, and select returns if the
			 * error message is received.  We can thus detect
			 * the absence of a nameserver without timing out.
			 * If we have sent queries to at least two servers,
			 * however, we don't want to remain connected,
			 * as we wish to receive answers from the first
			 * server to respond.
			 */
			if (try == 0 && nscount == 1) {
				/*
				 * Don't use connect if we might
				 * still receive a response
				 * from another server.
				 */
				if (connected == 0) {
				  if (connect(libPtr, res_sock,
					      (struct sockaddr *)&host,
					      sizeof(struct sockaddr)) < 0) {
#ifdef RES_DEBUG
							perror("connect");
#endif /* RES_DEBUG */
						continue;
					}
					connected = 1;
				}
				if (send(libPtr, res_sock,
					 (char *)buf, buflen, 0) != buflen) {
#ifdef RES_DEBUG
						perror("send");
#endif /* RES_DEBUG */
					continue;
				}
			} else {
				/*
				 * Disconnect if we want to listen
				 * for responses from more than one server.
				 */
				if (connected) {
					(void) connect(libPtr, res_sock, (struct sockaddr *)&no_addr,
					    sizeof(no_addr));
					connected = 0;
				}
				if (sendto(libPtr, res_sock, (char *)buf, buflen, 0,
				    (struct sockaddr *)&host,
				    sizeof(struct sockaddr)) != buflen) {
#ifdef RES_DEBUG
						perror("sendto");
#endif /* RES_DEBUG */
					continue;
				}
			}

			/*
			 * Wait for reply
			 */
			timeout.tv_sec = (_res.retrans << try);
			if (try > 0)
				timeout.tv_sec /= nscount;
			if (timeout.tv_sec <= 0)
				timeout.tv_sec = 1;
			timeout.tv_usec = 0;
			/*
			 * PORT (AmiTCP_NG) security fix: remember how much of
			 * this server's budget is left. Every `goto wait` below
			 * (an answer from an unexpected source, or one carrying
			 * a stale query id) used to re-enter WaitSelect with
			 * this same, untouched timeout -- and WaitSelect does
			 * not write back the remaining time -- so each rejected
			 * packet re-armed a FULL-length wait. Anyone able to
			 * send UDP to our ephemeral port with the nameserver's
			 * address and port 53 could hold res_send() here
			 * forever, hanging every gethostbyname() on the machine.
			 * Note this needs no query-id guessing: a WRONG id is
			 * precisely what triggers the retry.
			 *
			 * The budget is charged by ELAPSED TIME, not simply
			 * spent on the first wait: a stale reply from an
			 * earlier retry of this same query is routine DNS and
			 * needs no attacker, so a rejected packet must not
			 * cost us a healthy server whose real answer may be
			 * microseconds away. A rejects counter backstops the
			 * clock, keeping the loop bounded even if the system
			 * time is stepped underneath us (sntp does exactly
			 * that, and on a battery-less Amiga the step can be
			 * years).
			 */
			wtotal = timeout.tv_sec;
			wrejects = MAXREJECTS;
			get_time(&wstart);
wait:
			FD_ZERO(&dsmask);
			FD_SET(res_sock, &dsmask);
			/*
			 * Charge each wait against the remaining budget, and
			 * give up on this server once it is spent (the outer
			 * retry loop then moves on) instead of waiting anew.
			 */
			{
				struct timeval wnow;
				long elapsed;

				get_time(&wnow);
				elapsed = (long)wnow.tv_sec - (long)wstart.tv_sec;
				if (elapsed < 0)
					/* Clock stepped backwards: charge the
					 * whole budget rather than handing out
					 * a fresh one, which would reinstate
					 * the very hang described above. */
					elapsed = wtotal;
				waitleft = wtotal - elapsed;
			}
			if (waitleft <= 0 || wrejects <= 0) {
				/* Budget spent on rejected packets: treat it
				 * exactly as the n == 0 timeout below does, so
				 * the retry loop advances to the next server. */
				gotsomewhere = 1;
				continue;
			}
			timeout.tv_sec = waitleft;
			timeout.tv_usec = 0;
			n = WaitSelect(libPtr, res_sock+1, &dsmask, NULL,
				NULL, &timeout, NULL);
			if (n < 0) {
#ifdef RES_DEBUG
					perror("select");
#endif /* RES_DEBUG */
				continue;
			}
			if (n == 0) {
				/*
				 * timeout
				 */
#ifdef RES_DEBUG
					printf("timeout\n");
#endif /* RES_DEBUG */
#if 1 || BSD >= 43
				gotsomewhere = 1;
#endif
				continue;
			}
			{
				/*
				 * PORT (AmiTCP_NG) hardening: receive with
				 * recvfrom() and verify the reply's source address
				 * and port. The original used recv() with no source
				 * check at all -- but on the common multi-nameserver
				 * or retry path the socket is UNCONNECTED (see the
				 * connect/disconnect dance above), so the kernel does
				 * no 4-tuple filtering and ANY host that guesses the
				 * 16-bit query id could inject a forged answer
				 * (off-path DNS cache poisoning). host{} holds the
				 * address/port of the server we queried this round;
				 * reject anything that did not come from it. On the
				 * connected single-server path the kernel already
				 * filters and this check simply concurs.
				 */
				struct sockaddr_in from;
				LONG fromlen = sizeof(from);

				resplen = recvfrom(libPtr, res_sock, answer, anslen,
						   0, (struct sockaddr *)&from,
						   &fromlen);
				if (resplen <= 0) {
#ifdef RES_DEBUG
					perror("recvfrom");
#endif /* RES_DEBUG */
					continue;
				}
				if (fromlen < (LONG)sizeof(from) ||
				    from.sin_addr.s_addr != host.sin_addr.s_addr ||
				    from.sin_port != host.sin_port) {
#ifdef RES_DEBUG
					printf("answer from unexpected source, ignored\n");
#endif /* RES_DEBUG */
					wrejects--;
					goto wait;
				}
			}
			gotsomewhere = 1;
			if (id != anhp->id) {
				/*
				 * response from old query, ignore it
				 */
#ifdef RES_DEBUG
					printf("old answer:\n");
					__p_query(answer);
#endif /* RES_DEBUG */
				wrejects--;
				goto wait;
			}
			if (!(_res.options & RES_IGNTC) && anhp->tc) {
				/*
				 * get rest of answer;
				 * use TCP with same server.
				 */
#ifdef RES_DEBUG
					printf("truncated answer\n");
#endif /* RES_DEBUG */
				(void)CloseSocket(libPtr, res_sock);
				res_sock = -1;
				v_circuit = 1;
				goto usevc;
			}
		}
#ifdef RES_DEBUG
			printf("got answer:\n");
			__p_query(answer);
#endif /* RES_DEBUG */
		/*
		 * If using virtual circuits, we assume that the first server
		 * is preferred * over the rest (i.e. it is on the local
		 * machine) and only keep that one open.
		 * If we have temporarily opened a virtual circuit,
		 * or if we haven't been asked to keep a socket open,
		 * close the socket.
		 */
		if ((v_circuit &&
		    ((_res.options & RES_USEVC) == 0 || 1 /* was ns!=0, always true */)) ||
		    (_res.options & RES_STAYOPEN) == 0) {
			(void) CloseSocket(libPtr, res_sock);
			res_sock = -1;
		}
		return (resplen);
	   }
	}
	if (res_sock >= 0) {
		(void) CloseSocket(libPtr, res_sock);
		res_sock = -1;
	}
	if (v_circuit == 0)
	  if (gotsomewhere == 0)
	    writeErrnoValue(libPtr, ECONNREFUSED); /* no nameservers found */
	  else
	    writeErrnoValue(libPtr, ETIMEDOUT);	   /* no answer obtained */
	else
	  writeErrnoValue(libPtr, terrno);	
	return (-1);
}

