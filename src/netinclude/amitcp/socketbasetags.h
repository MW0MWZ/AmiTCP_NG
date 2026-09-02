/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

#ifndef AMITCP_SOCKETBASETAGS_H
#define AMITCP_SOCKETBASETAGS_H
/*
 * $Id: socketbasetags.h,v 3.3 1994/04/07 20:33:07 jraja Exp $
 *
 * Copyright (c) 1994 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * Created: Sun Jan 9 14:33:12 1994 jraja
 * Last modified: Thu Apr  7 23:32:25 1994 jraja
 *
 * HISTORY
 * $Log: socketbasetags.h,v $
 * Revision 3.3  1994/04/07  20:33:07  jraja
 * Put SBTC_COMPAT43 inside #ifdef notyet.
 *
 * Revision 3.2  1994/04/02  10:22:38  jraja
 * Added tag code SBTC_HERRNOLONGPTR for h_errno pointer.
 *
 * Revision 3.1  1994/03/29  12:56:35  ppessi
 * Added SBTC_COMPAT43 tag
 *
 * Revision 1.5  1994/03/22  07:17:13  jraja
 * Added SBTC_FDCALLBACK & definitions for its actions.
 *
 * Revision 1.4  1994/02/26  18:03:28  jraja
 * Moved from netinclude to netinclude/amitcp.
 *
 * Revision 1.3  1994/02/15  21:13:47  jraja
 * fixed the SBTC_ERRNOPTR(size) macro.
 *
 * Revision 1.2  1994/01/20  02:38:00  jraja
 * Reorganized the tags, added rest of the error string table tags and
 * changed the errnoPtr setting tags.
 *
 * Revision 1.1  1994/01/12  06:59:54  jraja
 * Initial revision
 *
 */

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif
/*
 * PORT (AmiTCP_NG): struct Hook for the log/error hooks, struct DateStamp which
 * LogHookMessage embeds BY VALUE. Deliberately OUTSIDE the tagitem.h guard: that
 * guard only means "tagitem.h is already in", and a client that had included
 * tagitem.h first would otherwise skip these two as well and fail to compile
 * LogHookMessage with an incomplete DateStamp. Both headers guard themselves.
 */
#include <utility/hooks.h>
#include <dos/dos.h>

/*
 * utility/tagitem.h specifies that bits 16-30 in tags are reserved. So we 
 * don't use them for maximum compatability.
 */

/*
 * Argument passing convention (bit 15)
 */
#define SBTF_REF 0x8000		/* 0x0000 == VAL */

/*
 * Code (bits 1-14)
 */
#define SBTB_CODE 1
#define SBTS_CODE 0x3FFF
#define SBTM_CODE(tag) (((UWORD)(tag) >> SBTB_CODE) & SBTS_CODE)

/* 
 * Direction (bit 0)
 */
#define SBTF_SET  0x1		/* 0 == GET */

/*
 * Macros to set things up
 * We keep the TAG_USER (bit 31) set to be compatible with tagitem.h
 * conventions.
 */
#define SBTM_GETREF(code) \
  (TAG_USER | SBTF_REF | (((code) & SBTS_CODE) << SBTB_CODE))
#define SBTM_GETVAL(code) \
  (TAG_USER | (((code) & SBTS_CODE) << SBTB_CODE))
#define SBTM_SETREF(code) \
  (TAG_USER | SBTF_REF | (((code) & SBTS_CODE) << SBTB_CODE) | SBTF_SET)
#define SBTM_SETVAL(code) \
  (TAG_USER | (((code) & SBTS_CODE) << SBTB_CODE) | SBTF_SET)

/*
 * Tag code definitions. These codes are used with one of the above macros.
 *
 * All arguments are ULONG's or pointers (PTR suffix).
 *
 * NOTE: Tag code 0 is not used (see utility/tagitem.h).
 */

/* signal masks */
#define SBTC_BREAKMASK		1
#define SBTC_SIGIOMASK		2
#define SBTC_SIGURGMASK		3
/*
 * PORT (AmiTCP_NG): tag 4 was absent from this header -- AmiTCP 3.0b2, which
 * this fork descends from, predates the asynchronous socket-event mechanism, so
 * the numbering jumped 3 -> 6. The tag code and the FD_* set below are defined
 * by the AmiTCP V4 / Roadshow API (Roadshow SDK netinclude/libraries/bsdsocket.h),
 * reproduced for compatibility with thanks to Olaf Barthel.
 *
 * SBTC_SIGEVENTMASK is the Exec signal sent to the socket owner when an event
 * selected by SO_EVENTMASK (sys/socket.h) occurs. The application then calls
 * GetSocketEvents() to find out which socket, and which events.
 */
#define SBTC_SIGEVENTMASK	4

/*
 * The FD_* event bits that go with this tag are defined in <sys/socket.h>,
 * beside SO_EVENTMASK -- they are that option's value space, and the stack's
 * own socket code needs them without pulling <utility/tagitem.h> into the
 * kernel. Roadshow keeps them in <libraries/bsdsocket.h> instead; either way
 * the values are identical and must not be renumbered, since they are baked
 * into every already-compiled client binary.
 */

/* error code handling */
#define SBTC_ERRNO		6
#define SBTC_HERRNO		7

/* socket descriptor table related tags */
#define SBTC_DTABLESIZE         8

/* link library fd allocation callback
 * 
 * Argument is a callback function with following prototype
 *
 * int fd = fdCallback(int fd, int action);
 *     D0                  D0      D1
 *
 * see net.lib sources for an example
 */
#define SBTC_FDCALLBACK         9
/*
 * "action" values:
 */
#define FDCB_FREE  0
#define FDCB_ALLOC 1
#define FDCB_CHECK 2

/* syslog variables (see netinclude:sys/syslog.h for values) */
#define SBTC_LOGSTAT		10
#define SBTC_LOGTAGPTR		11
#define SBTC_LOGFACILITY	12
#define SBTC_LOGMASK		13

/*
 * The argument of following error string tags is a ULONG,
 * where the error number is stored. On return the string pointer is 
 * returned on this same ULONG. (GET ONLY)
 *
 * NOTE: error numbers defined in <exec/errors.h> are negative and must be
 * negated (turned to positive) before passing to the SocketBaseTagList().
 */
#define SBTC_ERRNOSTRPTR	14 /* <sys/errno.h> */
#define SBTC_HERRNOSTRPTR	15 /* <netdb.h> */
#define SBTC_IOERRNOSTRPTR	16 /* <exec/errors.h> SEE NOTE ABOVE */
#define SBTC_S2ERRNOSTRPTR	17 /* <devices/sana2.h> */
#define SBTC_S2WERRNOSTRPTR	18 /* <devices/sana2.h> */


/* errno pointer & size SETTING (only) */
#define SBTC_ERRNOBYTEPTR	21
#define SBTC_ERRNOWORDPTR	22
#define SBTC_ERRNOLONGPTR	24
/*
 * Macro for generating the errnoptr tag code from a (constant) size.
 * only 1,2 & 4 are legal 'size' values. If the 'size' value is illegal,
 * the tag is set to 0, which causes SocketBaseTagList() to fail.
 */
#define SBTC_ERRNOPTR(size)    ((size == sizeof(long)) ? SBTC_ERRNOLONGPTR   :\
				((size == sizeof(short)) ? SBTC_ERRNOWORDPTR :\
				 ((size == sizeof(char)) ? SBTC_ERRNOBYTEPTR :\
				  0)))

/* h_errno pointer */
#define SBTC_HERRNOLONGPTR	25
/*
 * PORT (AmiTCP_NG): pointer to the stack's name and version string (GET only).
 * Roadshow code 29; absent here, so a client asking who it was talking to got
 * nothing back. We answer with VSTRING -- the same text the Version command
 * reports.
 */
#define SBTC_RELEASESTRPTR	29

#ifdef notyet
/*
 * Different boolean variables
 */
/* use 4.3BSD compatible sockaddr structures */
#define SBTC_COMPAT43           29
#endif

/*
 * PORT (AmiTCP_NG): Roadshow extension capability codes (from the Roadshow
 * <libraries/bsdsocket.h>). A Roadshow-aware client queries these through
 * SocketBaseTagList BEFORE calling an extension family, so it can gracefully
 * skip families the library does not provide. AmiTCP 3.0b2 knew nothing of them
 * -- an unknown code failed the whole SocketBaseTagList call, which some tools
 * read as a hard error. We now answer every one honestly (see amiga_generic2.c):
 * 1 for families we implement, 0 for those still stubbed. As tranches land, flip
 * the corresponding answer from 0 to 1. Codes 40-69 do not collide with AmiTCP's
 * own 1-29.
 */
#define SBTC_NUM_PACKET_FILTER_CHANNELS		40 /* BPF: # of capture channels */
#define SBTC_HAVE_ROUTING_API			41
/*
 * PORT (AmiTCP_NG): stack-wide protocol behaviour controls, all absent from
 * AmiTCP 3.0b2. Codes and meanings are the AmiTCP V4 / Roadshow ones (Roadshow
 * SDK netinclude/libraries/bsdsocket.h), reproduced for compatibility with
 * thanks to Olaf Barthel. Each affects the WHOLE stack, not the base it is set
 * through. See api/amiga_generic2.c for the implementations.
 */
#define SBTC_UDP_CHECKSUM			42 /* generate/check UDP checksums */
#define SBTC_IP_FORWARDING			43 /* forward IP datagrams */
#define SBTC_IP_DEFAULT_TTL			44 /* default outgoing TTL, 1..255 */
#define SBTC_ICMP_MASK_REPLY			45 /* answer address-mask requests
						    * (OFF by default) */
#define SBTC_ICMP_SEND_REDIRECTS		46 /* send redirects when forwarding */
#define SBTC_HAVE_INTERFACE_API			47
#define SBTC_ICMP_PROCESS_ECHO			48 /* IR_* -- see netinet/ip_icmp.h */
#define SBTC_ICMP_PROCESS_TSTAMP		49 /* IR_* -- see netinet/ip_icmp.h */
#define SBTC_HAVE_MONITORING_API		50
/*
 * PORT (AmiTCP_NG): Roadshow's opt-in to sharing one library base between
 * tasks (code 51, matching <libraries/bsdsocket.h> in the Roadshow SDK).
 * Roadshow refuses non-opener callers by default and documents reopening the
 * library per user as the recommended approach; this tag is the escape hatch
 * for an application that accepts the restrictions. See CHECK_TASK() in
 * api/amiga_libcallentry.h for what we do and do not permit once it is set.
 */
#define SBTC_CAN_SHARE_LIBRARY_BASES		51
#define SBTC_HAVE_STATUS_API			53
#define SBTC_HAVE_DNS_API			54
/*
 * PORT (AmiTCP_NG): logging and error-delivery hooks, and the address-change
 * signal. Roadshow codes; absent from AmiTCP 3.0b2. See the structures below.
 */
#define SBTC_LOG_FILE_NAME			52 /* get/set the log destination */
#define SBTC_LOG_HOOK				55 /* struct Hook *, or NULL */
#define SBTC_SIG_ADDRESS_CHANGE_MASK		57 /* signal on address change */
#define SBTC_IDN_DEFAULT_CHARACTER_SET		66 /* IDNCS_* below */
#define SBTC_ERROR_HOOK				68 /* struct Hook *, or NULL */

/*
 * International Domain Name character sets (SBTC_IDN_DEFAULT_CHARACTER_SET).
 * This stack does NOT implement IDN translation, so IDNCS_ASCII -- the value
 * that explicitly DISABLES translation -- is the only one it can honestly
 * accept; asking for Latin-1 fails rather than silently not translating.
 */
#define IDNCS_ASCII		0	/* no translation (all we support) */
#define IDNCS_ISO_8859_LATIN_1	1	/* the native Amiga character set */

/*
 * SBTC_LOG_HOOK message. The hook is called as
 *   VOID hookfunc(struct Hook *hook /-A0-/, APTR reserved /-A2-/,
 *                 struct LogHookMessage *lhm /-A1-/)
 * with `reserved` NULL. It runs on the context of whoever called into the
 * logging code -- which is very often the stack itself -- so it must return
 * promptly. The message is READ-ONLY.
 */
struct LogHookMessage
{
  LONG		lhm_Size;	/* size of this structure, in bytes */
  LONG		lhm_Priority;	/* LOG_EMERG .. LOG_DEBUG */
  struct DateStamp lhm_Date;	/* when the entry was made */
  STRPTR	lhm_Tag;	/* facility name, may be NULL */
  ULONG		lhm_ID;		/* facility ID, may be zero */
  STRPTR	lhm_Message;	/* NUL-terminated text */
};

/*
 * SBTC_ERROR_HOOK message. The hook is called as
 *   LONG hookfunc(struct Hook *hook /-A0-/, APTR reserved /-A2-/,
 *                 struct ErrorHookMsg *ehm /-A1-/)
 * on the caller's own context, and is asked to PERFORM the assignment -- which
 * is the point of it: with shared library bases the stack cannot know which
 * task's errno to write, and the hook can.
 */
struct ErrorHookMsg
{
  ULONG		ehm_Size;	/* size of this structure; >= 12 */
  ULONG		ehm_Action;	/* EHMA_* below; ignore anything else */
  LONG		ehm_Code;	/* the error code to set */
};

#define EHMA_Set_errno		1	/* set the local errno to ehm_Code */
#define EHMA_Set_h_errno	2	/* set the local h_errno to ehm_Code */

#define SBTC_HAVE_LOCAL_DATABASE_API		59
/*
 * PORT (AmiTCP_NG): stack-wide octet totals. The value is an SBQUAD_T
 * ({ULONG sbq_High; ULONG sbq_Low;}) and MUST be passed by reference
 * (SBTM_GETREF); a by-value request is refused rather than writing eight bytes
 * through a four-byte ti_Data. Roadshow codes 64/65.
 */
#define SBTC_GET_BYTES_RECEIVED			64
#define SBTC_GET_BYTES_SENT			65
#define SBTC_HAVE_ADDRESS_CONVERSION_API	60
#define SBTC_HAVE_KERNEL_MEMORY_API		61
#define SBTC_HAVE_SERVER_API			63
#define SBTC_HAVE_ROADSHOWDATA_API		67
#define SBTC_HAVE_GETHOSTADDR_R_API		69

/*
 * SBTC_SYSTEM_STATUS (GET-only) reports an SBSYSSTAT_* bitmask describing what the
 * stack currently has configured. Roadshow's GetNetStatus tool reads this to decide
 * whether the machine is "online" and which facilities are up. (Roadshow value 56.)
 */
#define SBTC_SYSTEM_STATUS			56

#define SBSYSSTAT_Interfaces		(1L<<0)	/* >=1 non-loopback interface up */
#define SBSYSSTAT_PTP_Interfaces	(1L<<1)	/* >=1 point-to-point interface up */
#define SBSYSSTAT_BCast_Interfaces	(1L<<2)	/* >=1 broadcast interface up */
#define SBSYSSTAT_Resolver		(1L<<3)	/* >=1 domain name server set */
#define SBSYSSTAT_Routes		(1L<<4)	/* >=1 route configured */
#define SBSYSSTAT_DefaultRoute		(1L<<5)	/* a default route is present */

/*
 * AmiTCP_NG-private query codes (GET-only). Not part of the Roadshow ABI -- placed high
 * in the code space (0x2000+) to stay clear of Roadshow's low, sequential codes. They
 * let GetNetStatus DEBUG read back the stack's LIVE tuning: the RAM it detected at
 * startup and the effective global tcp_sendspace/tcp_recvspace/sb_max, so a real machine
 * can be diagnosed without a separate binary. An older library returns 0 (the dispatch's
 * unknown-code default), which the tool reads as "not supported". Keep in step with the
 * NG_SBTC_* mirror in src/tools/ng_lvo.h.
 */
#define SBTC_NG_DETECTED_RAM		0x2000	/* ULONG: installed RAM the stack detected */
#define SBTC_NG_TCP_SENDSPACE		0x2001	/* ULONG: effective global tcp_sendspace  */
#define SBTC_NG_TCP_RECVSPACE		0x2002	/* ULONG: effective global tcp_recvspace  */
#define SBTC_NG_SB_MAX			0x2003	/* ULONG: effective global sb_max          */
#define SBTC_NG_LINK_SPEED		0x2004	/* ULONG: last interface's if_baudrate (bps) */

/*
 * TCP header-prediction ("fast path") accounting, for rxprofile. Every inbound TCP
 * segment lands in exactly one of three buckets: a predicted pure ACK, a predicted
 * in-sequence data segment, or the full slow path. RCVTOTAL is the denominator
 * (tcpstat.tcps_rcvtotal), so a tool can report a hit RATE rather than a bare count
 * that says nothing without knowing how much traffic produced it.
 */
#define SBTC_NG_TCP_PREDACK		0x2005	/* ULONG: fast-path hits, pure ACKs      */
#define SBTC_NG_TCP_PREDDAT		0x2006	/* ULONG: fast-path hits, in-seq data    */
#define SBTC_NG_TCP_RCVTOTAL		0x2007	/* ULONG: all TCP segments received      */
#define SBTC_NG_TCP_PCBMISS		0x2008	/* ULONG: one-entry PCB cache misses     */
#define SBTC_NG_TCP_PREDWIN		0x2009	/* ULONG: fast-path hits, window updates */
#define SBTC_NG_TCP_REASSFULL		0x200A	/* ULONG: segs dropped, reass queue full */

/* Socket wakeup accounting -- is the per-segment sorwakeup() doing real work? */
#define SBTC_NG_SOWK_CALLS		0x2020	/* ULONG: sowakeup() calls, all sockets  */
#define SBTC_NG_SOWK_RCV		0x2021	/* ULONG: of those, on a receive buffer  */
#define SBTC_NG_SOWK_WAIT		0x2022	/* ULONG: found a blocked waiter (costly)*/
#define SBTC_NG_SOWK_SEL		0x2023	/* ULONG: had to service a select()      */
#define SBTC_NG_SOWK_ASYNC		0x2024	/* ULONG: delivered an async signal      */

/*
 * Why header prediction rejected a segment. One bucket per segment (first
 * failing test wins); order matches the prediction test in netinet/tcp_input.c.
 * They do NOT sum to the slow-path total -- segments dropped before prediction
 * is consulted are counted in the total but never attributed, and the rare
 * TIME_WAIT-reopen path is attributed twice. See tcp_predict_miss() for both.
 */
#define SBTC_NG_TPM_STATE		0x2010	/* not ESTABLISHED (handshake/teardown) */
#define SBTC_NG_TPM_FLAGS		0x2011	/* SYN/FIN/RST/URG present, or no ACK   */
#define SBTC_NG_TPM_TSTAMP		0x2012	/* RFC 1323 timestamp went backwards    */
#define SBTC_NG_TPM_SEQ			0x2013	/* out of sequence -- a real hole       */
#define SBTC_NG_TPM_WIN			0x2014	/* peer's advertised window moved       */
#define SBTC_NG_TPM_REXMIT		0x2015	/* a retransmit is outstanding          */
#define SBTC_NG_TPM_DUPACK		0x2016	/* in dup-ack territory                 */
#define SBTC_NG_TPM_SACK		0x2017	/* SACK recovery in progress            */
#define SBTC_NG_TPM_ACK			0x2018	/* dup/out-of-range ack                 */
#define SBTC_NG_TPM_CWND		0x2019	/* snd_cwnd had not reached snd_wnd     */
#define SBTC_NG_TPM_ACKDATA		0x201A	/* data segment that also acked         */
#define SBTC_NG_TPM_REASS		0x201B	/* reassembly queue not empty           */
#define SBTC_NG_TPM_SPACE		0x201C	/* no room in the socket receive buffer */
#define SBTC_NG_TPM_ZEROWIN		0x201D	/* peer advertised a zero window       */
#define SBTC_NG_TPM_ACKDUP		0x201E	/* true duplicate ACK (loss signal)    */
#define SBTC_NG_TPM_WINONLY		0x201F	/* window update carrying no new ack   */

#endif /* !AMITCP_SOCKETBASETAGS_H */
