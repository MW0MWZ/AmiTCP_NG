/*
 * $Id: conf.h,v 1.3 1993/06/04 11:16:15 jraja Exp $
 * 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * This file contains several definitions which affect the compilation of the 
 * AmiTCP/IP code. Normally they are boolean switches and the comments tell
 * what happens if the value is TRUE (eg. non zero)
 *
 * HISTORY
 * $Log: conf.h,v $
 * Revision 1.3  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.2  1993/05/29  20:48:24  jraja
 * Added default values for IP configurable flags, removed GATEWAY.
 *
 * Revision 1.1  1993/05/17  00:50:39  ppessi
 * General configuration file.
 *
 */

/*
 * Do diagnostic tests which are not necessary in production version
 */
#define DIAGNOSTIC 1

/*
 * NG_COPYQUICK is GONE, along with kern/ng_copy.c itself. It selected Exec's
 * CopyMemQuick inside the old C copy dispatcher; ng_bcopy() (kern/ng_bcopy.S) no
 * longer calls Exec at all, so the switch had nothing left to switch. Do not
 * reintroduce it without a routine that reads it.
 */

/*
 * TEMPORARY: time the four passes a packet's payload makes through the stack, to
 * find where the per-byte cost actually goes. See src/kern/ng_stageprof.c.
 * Costs eight timer reads per round trip; harmless at benchmark scale, wrong to
 * ship. Set to 0 and delete once the question is answered.
 */
#define NG_STAGEPROF	0

#define NG_PROF_COPYIN	0
#define NG_PROF_CKSOUT	1
#define NG_PROF_CKSIN	2
#define NG_PROF_COPYOUT	3
#define NG_PROF_NULL	4	/* calibration: enter+leave with no work between */
#define NG_PROF_NSTAGE	5

#if NG_STAGEPROF
void ng_prof_enter(int stage);
void ng_prof_leave(int stage, long nbytes);
void ng_prof_packet(void);
void ng_prof_addr(const char *tag, void *src, void *dst, long len);
#define NG_PROF_ADDR(t,s,d,n)	ng_prof_addr(t,s,d,n)
#define NG_PROF_IN(s)	ng_prof_enter(s)
#define NG_PROF_OUT(s,n)	ng_prof_leave(s,n)
#define NG_PROF_PKT()	ng_prof_packet()
#else
#define NG_PROF_IN(s)
#define NG_PROF_OUT(s,n)
#define NG_PROF_PKT()
#define NG_PROF_ADDR(t,s,d,n)
#endif

/*
 * Fuse the SANA receive copy with the Internet checksum (net/sana2copybuff.c), so a
 * received frame crosses memory twice instead of three times.
 *
 * GATED BY CPU, on measurement rather than taste. Fused vs today's CopyMem+in_cksum,
 * at 40/576/1460 bytes: 68000 1.29/1.26/1.24x, 68020 1.44/1.33/1.29x, but 68040
 * 1.32/1.00/0.96x -- a LOSS at the sizes that carry the data, because 4K of data cache
 * keeps the frame resident and makes today's second pass nearly free. MOVE16 cannot
 * rescue a fused loop: it is memory-to-memory and routes nothing through a data
 * register to add from, so a one-pass 68040 variant is architecturally impossible.
 *
 * NB the 68040 figures are EMULATED and varied between runs (0.92x then 0.96x at
 * 1460); confirm on real hardware before treating this gate as settled.
 */
#if defined(__mc68040__) || defined(__mc68060__)
#define NG_RX_CSUM	0
#else
#define NG_RX_CSUM	1
#endif

/*
 * A SELF-CHECKING BUILD: recompute every fused receive checksum the slow way and
 * compare, both where it is produced (the SANA copy) and where it is consumed
 * (tcp_input / udp_input). Any disagreement is logged and counted, and the disproved
 * sum is refused rather than used.
 *
 * OFF by default, and it must stay off in anything shipped or measured:
 *
 * ⚠️ A BUILD WITH THIS ON IS SLOWER THAN BASELINE, NOT FASTER. It adds a full extra
 * pass over the payload at DEVICE-INTERRUPT priority, on top of the fused copy -- so
 * total interrupt-time work exceeds even the old plain-copy path, whose checksum
 * happened later at IP/TCP level rather than inside the driver's interrupt. The
 * 1.24-1.29x figures above are fused-vs-baseline and include NONE of this. That
 * matters at this call site in particular: it produced a receive-ring re-arm livelock
 * once before, so time-in-interrupt here is not a free variable.
 *
 * Kept rather than deleted because it earns its place as a diagnostic: if a machine
 * ever shows corruption that might be checksum-related, one flag turns the stack into
 * something that proves or clears itself under real traffic. It has already found two
 * real defects that way. Deliberately NOT gated on DIAGNOSTIC, which ships.
 *
 * Validated with this ON: 145,000+ frames on 68000 hardware under a sustained SMB
 * download, zero disagreements, with both the producer and the TCP consumer confirmed
 * live; UDP consumer confirmed under emulation.
 */
#define NG_RX_CSUM_VERIFY	0

/*
 * Be compatible with BSD 4.2. Affects only checksumming of UDP data. If true
 * the checksum is NOT calculated by default.
 */
#define COMPAT_42 0

/*
 * Make TCP compatible with BSD 4.2
 */
#define TCP_COMPAT_42 0

/*
 * protocol families
 */
#define INET 1
#define CCITT 0
#define NHY 0			/* HYPERchannel */
#define NIMP 0
#define ISO 0
#define NS 0
#define RMP 0

/*
 * optional protocols over IP
 */
#define NSIP 0
#define EON 0
#define TPIP 0

/*
 * default values for IP configurable flags
 */
#define IPFORWARDING    0
#define IPSENDREDIRECTS 1
#define IPPRINTFS       0

/*
 * Network level
 */
#define NETHER 1		/* Call ARP ioctl */
