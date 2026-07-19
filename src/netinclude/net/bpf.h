/*
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
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
 * ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.  (See the full 4.4BSD
 * disclaimer.)
 *
 *      @(#)bpf.h	8.2 (Berkeley) 1/9/95
 */
/*
 * PORT (AmiTCP_NG): the classic 4.4BSD net/bpf.h, adapted to this tree.
 * Copyright 2026 Andy Taylor (MW0MWZ), added alongside the Berkeley notice
 * above; the project is GPL v2 (see COPYING). Changes vs upstream: our BSD
 * types (u_long/u_short/u_char/long -- byte-for-byte the same layout Roadshow's
 * __ULONG/__UWORD/__UBYTE/__LONG produce, so the on-wire/ABI structs match) and
 * <sys/ioccom.h>/<sys/time.h> instead of the SAS/C-specific includes; the SAS/C
 * alignment pragmas and __timeval indirection are dropped.
 */

#ifndef _NET_BPF_H
#define _NET_BPF_H

#include <sys/ioccom.h>		/* _IOR/_IOW/_IOWR/_IO for the BIOC* ioctls */
#include <sys/time.h>		/* struct timeval, for bpf_hdr.bh_tstamp */

/*
 * Alignment macros. BPF_WORDALIGN rounds up to the next even multiple of
 * BPF_ALIGNMENT.
 */
#define BPF_ALIGNMENT sizeof(long)
#define BPF_WORDALIGN(x) (((x)+(BPF_ALIGNMENT-1))&~(BPF_ALIGNMENT-1))

#define BPF_MAXINSNS 512
#define BPF_MAXBUFSIZE 0x8000
#define BPF_MINBUFSIZE 32

/* Structure for BIOCSETF. */
struct bpf_program {
	u_long bf_len;
	struct bpf_insn *bf_insns;
};

/* Struct returned by BIOCGSTATS. */
struct bpf_stat {
	u_long bs_recv;		/* number of packets received */
	u_long bs_drop;		/* number of packets dropped */
};

/* Struct returned by BIOCVERSION. */
struct bpf_version {
	u_short bv_major;
	u_short bv_minor;
};
#define BPF_MAJOR_VERSION 1
#define BPF_MINOR_VERSION 1

/* BPF ioctls (the classic gcc/'B'-group encodings). */
#define	BIOCGBLEN	_IOR('B',102, u_long)
#define	BIOCSBLEN	_IOWR('B',102, u_long)
#define	BIOCSETF	_IOW('B',103, struct bpf_program)
#define	BIOCFLUSH	_IO('B',104)
#define BIOCPROMISC	_IO('B',105)
#define	BIOCGDLT	_IOR('B',106, u_long)
#define BIOCGETIF	_IOR('B',107, struct ifreq)
#define BIOCSETIF	_IOW('B',108, struct ifreq)
#define BIOCSRTIMEOUT	_IOW('B',109, struct timeval)
#define BIOCGRTIMEOUT	_IOR('B',110, struct timeval)
#define BIOCGSTATS	_IOR('B',111, struct bpf_stat)
#define BIOCIMMEDIATE	_IOW('B',112, u_long)
#define BIOCVERSION	_IOR('B',113, struct bpf_version)

/* Structure prepended to each packet. */
struct bpf_hdr {
	struct timeval	bh_tstamp;	/* time stamp */
	u_long		bh_caplen;	/* length of captured portion */
	u_long		bh_datalen;	/* original length of packet */
	u_short		bh_hdrlen;	/* length of bpf header (incl. alignment padding) */
};

/*
 * Data-link level type codes.
 */
#define DLT_NULL	0	/* no link-layer encapsulation */
#define DLT_EN10MB	1	/* Ethernet (10Mb) */
#define DLT_EN3MB	2	/* Experimental Ethernet (3Mb) */
#define DLT_AX25	3	/* Amateur Radio AX.25 */
#define DLT_PRONET	4	/* Proteon ProNET Token Ring */
#define DLT_CHAOS	5	/* Chaos */
#define DLT_IEEE802	6	/* IEEE 802 Networks */
#define DLT_ARCNET	7	/* ARCNET */
#define DLT_SLIP	8	/* Serial Line IP */
#define DLT_PPP		9	/* Point-to-point Protocol */
#define DLT_FDDI	10	/* FDDI */

/* The instruction encodings. */
/* instruction classes */
#define BPF_CLASS(code) ((code) & 0x07)
#define		BPF_LD		0x00
#define		BPF_LDX		0x01
#define		BPF_ST		0x02
#define		BPF_STX		0x03
#define		BPF_ALU		0x04
#define		BPF_JMP		0x05
#define		BPF_RET		0x06
#define		BPF_MISC	0x07

/* ld/ldx fields */
#define BPF_SIZE(code)	((code) & 0x18)
#define		BPF_W		0x00
#define		BPF_H		0x08
#define		BPF_B		0x10
#define BPF_MODE(code)	((code) & 0xe0)
#define		BPF_IMM 	0x00
#define		BPF_ABS		0x20
#define		BPF_IND		0x40
#define		BPF_MEM		0x60
#define		BPF_LEN		0x80
#define		BPF_MSH		0xa0

/* alu/jmp fields */
#define BPF_OP(code)	((code) & 0xf0)
#define		BPF_ADD		0x00
#define		BPF_SUB		0x10
#define		BPF_MUL		0x20
#define		BPF_DIV		0x30
#define		BPF_OR		0x40
#define		BPF_AND		0x50
#define		BPF_LSH		0x60
#define		BPF_RSH		0x70
#define		BPF_NEG		0x80
#define		BPF_JA		0x00
#define		BPF_JEQ		0x10
#define		BPF_JGT		0x20
#define		BPF_JGE		0x30
#define		BPF_JSET	0x40
#define BPF_SRC(code)	((code) & 0x08)
#define		BPF_K		0x00
#define		BPF_X		0x08

/* ret - BPF_K and BPF_X also apply */
#define BPF_RVAL(code)	((code) & 0x18)
#define		BPF_A		0x10

/* misc */
#define BPF_MISCOP(code) ((code) & 0xf8)
#define		BPF_TAX		0x00
#define		BPF_TXA		0x80

/* The instruction data structure. */
struct bpf_insn {
	u_short	code;
	u_char	jt;
	u_char	jf;
	long	k;
};

/* Macros for insn array initializers. */
#define BPF_STMT(code, k) { (u_short)(code), 0, 0, k }
#define BPF_JUMP(code, k, jt, jf) { (u_short)(code), jt, jf, k }

/* Number of scratch memory words (for BPF_LD|BPF_MEM and BPF_ST). */
#define BPF_MEMWORDS 16

/*
 * AmiTCP_NG: number of BPF capture channels, reported to clients as Roadshow's
 * SBTC_NUM_PACKET_FILTER_CHANNELS. The per-channel capture buffers are allocated
 * lazily at open, so unopened channels cost almost nothing.
 */
#define NG_BPF_MAXCHAN 40

#if defined(KERNEL) || defined(_KERNEL)
struct ifnet;
struct mbuf;

u_int bpf_filter(struct bpf_insn *pc, u_char *p, u_int wirelen, u_int buflen);
int   bpf_validate(struct bpf_insn *f, int len);

/*
 * AmiTCP_NG BPF channel core (net/bpf.c). ng_bpf_tap() feeds a complete
 * link-layer frame (as an mbuf chain) to every listening channel;
 * ng_bpf_tap_ether() is the SANA-II "cooked" helper that reconstructs the
 * Ethernet header from the separately-delivered addresses + type first. Both
 * are cheap no-ops when no channel is listening.
 */
void ng_bpf_tap(struct ifnet *ifp, struct mbuf *m);
void ng_bpf_tap_ether(struct ifnet *ifp, const u_char *dst, const u_char *src,
		      u_short ethertype, struct mbuf *m);

/*
 * Channel API driven by the bpf_* library vectors (net/bpf.c, wired in Phase 4).
 * Each returns >= 0 on success -- a channel number for open, a byte/record count
 * for read/write/data_waiting, else 0 -- or a negative errno. ng_bpf_read()
 * blocks the calling task via tsleep(), so it takes that task's SocketBase.
 */
struct SocketBase;
void ng_bpf_init(void);
void ng_bpf_shutdown(void);
void ng_bpf_ifdetach(struct ifnet *ifp);
int  ng_bpf_open(int chan);
int  ng_bpf_close(int chan);
int  ng_bpf_read(int chan, caddr_t buf, int len, struct SocketBase *sb);
int  ng_bpf_write(int chan, caddr_t buf, int len, struct SocketBase *sb);
int  ng_bpf_ioctl(int chan, u_long cmd, caddr_t addr);
int  ng_bpf_set_notify_mask(int chan, u_long mask);
int  ng_bpf_set_interrupt_mask(int chan, u_long mask);
int  ng_bpf_data_waiting(int chan);
#endif

#endif /* _NET_BPF_H */
