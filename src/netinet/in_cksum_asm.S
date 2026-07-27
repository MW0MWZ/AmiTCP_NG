/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * GPL v2 (see COPYING).
 *
 * in_cksum_asm.S -- the Internet checksum (RFC 1071) in 68k assembly.
 *
 * This is the hand-tuned assembly form of in_cksum(). The C version in in_cksum.c
 * runs over every payload byte of every packet in both directions, so on a 68k it
 * is one of the hottest paths in the whole stack; a 32-bit-word `addx` accumulator
 * loop is several times faster than the portable C. The algorithm is Pekka Pessi's
 * original AmiTCP in_cksum.asm (1993), translated faithfully from SAS/C assembler to
 * GNU `as` syntax for the bebbo m68k-amigaos toolchain and verified byte-for-byte
 * against the C reference over a fuzz of lengths/alignments/chain splits (see the
 * cksumbench harness). The core trick: one long `addx.l` dependency chain accumulates
 * 32-bit words with carry flowing through the X flag for free -- and the X flag is
 * deliberately preserved across the `dbf`/`and`/`ror` counter arithmetic between the
 * unrolled loops (none of those instructions touch X), so the running carry survives.
 *
 * Entry:  int in_cksum(struct mbuf *m, int len)   -- standard C stack args.
 * Clobbers only caller-saved d0/d1/a0/a1; saves/restores d2-d7.
 */

/* struct mbuf field offsets (m_hdr is the first member -- see src/sys/mbuf.h).      */
#define M_NEXT	0		/* struct mbuf *mh_next  */
#define M_LEN	8		/* int          mh_len   */
#define M_DATA	12		/* caddr_t      mh_data  */

/* Register aliases (matching the original, remapped so we save only d2-d7).         */
#define M	a0		/* current mbuf                                        */
#define W	a1		/* walk pointer into the current mbuf's data           */
#define LEN	d7		/* bytes of the request still to checksum              */
#define MLEN	d6		/* bytes left in the current mbuf                       */
#define SUM	d5		/* 32-bit running one's-complement accumulator         */
#define ZERO	d4		/* a held zero (addx needs a register source)          */
#define SPAN	d3		/* nonzero => a word spans into the next mbuf          */
#define BSWAP	d2		/* nonzero => this mbuf's data started odd-aligned     */

	.text
	.align	2
	.globl	_in_cksum
_in_cksum:
	movea.l	4(sp),M			/* arg1: m   (return address at 0(sp))         */
	move.l	8(sp),d0		/* arg2: len                                   */
	movem.l	d2-d7,-(sp)		/* save callee-saved registers                 */
	move.l	d0,LEN
	moveq	#0,SUM
	move.l	SUM,SPAN		/* SPAN = 0 */
	move.l	SUM,ZERO		/* ZERO = 0 */
	bra	loop_test

loop:					/* --- process one mbuf --- */
	cmp.l	MLEN,LEN
	bge	sk01
	move.l	LEN,MLEN		/* MLEN = min(MLEN, LEN) */
sk01:
	sub.l	MLEN,LEN
	movea.l	M_DATA(M),W

	tst.l	SPAN
	beq	no_spanning
	/* First byte here completes a word that spanned from the previous mbuf. */
	move.l	ZERO,d0
	move.b	(W)+,d0
	add.l	d0,SUM
	addx.l	ZERO,SUM
	addx.l	ZERO,SUM
	subq.l	#1,MLEN
	move.l	MLEN,SPAN
	beq	next

no_spanning:
	move.l	W,BSWAP
	andi.l	#1,BSWAP		/* odd address? */
	beq	onword
	/* Odd start: swap the accumulator's bytes and fold in the lone byte. */
	rol.l	#8,SUM
	move.l	ZERO,d0
	move.b	(W)+,d0
	add.l	d0,SUM
	addx.l	ZERO,SUM
	addx.l	ZERO,SUM
	subq.l	#1,MLEN

onword:
	move.l	W,d0
	andi.b	#3,d0			/* at a long boundary yet? */
	beq	onlong
	moveq	#-2,d0
	add.l	MLEN,d0
	blt	loop2out
	move.l	d0,MLEN
	add.w	(W)+,SUM		/* fold one word to reach a long boundary */
	addx.l	ZERO,SUM
	addx.l	ZERO,SUM

onlong:
	move.l	MLEN,d0
	ror.l	#5,d0			/* low word = MLEN / 32 = # of 32-byte blocks */
	add.l	ZERO,ZERO		/* clear X (ZERO+ZERO = 0, X := 0) */
	bra	loop32test

	/* Main loop: eight 32-bit adds per iteration, carry chained via X.        */
loop32:
	move.l	(W)+,d1
	addx.l	d1,SUM
	move.l	(W)+,d1
	addx.l	d1,SUM
	move.l	(W)+,d1
	addx.l	d1,SUM
	move.l	(W)+,d1
	addx.l	d1,SUM
	move.l	(W)+,d1
	addx.l	d1,SUM
	move.l	(W)+,d1
	addx.l	d1,SUM
	move.l	(W)+,d1
	addx.l	d1,SUM
	move.l	(W)+,d1
	addx.l	d1,SUM
loop32test:
	dbf	d0,loop32		/* dbf does not touch X -> carry survives */

	moveq	#31,d0
	and.l	MLEN,d0			/* remainder bytes after the 32-byte blocks */
	ror.l	#2,d0			/* low word = remainder / 4 = # of 4-byte words */
	bra	loop4test
loop4:
	move.l	(W)+,d1
	addx.l	d1,SUM
loop4test:
	dbf	d0,loop4

	moveq	#2,d0
	and.l	MLEN,d0			/* a trailing word? */
	beq	loop2out
	move.w	(W)+,d1
	addx.w	d1,SUM

loop2out:
	addx.l	ZERO,SUM		/* fold the pending carry... */
	addx.l	ZERO,SUM		/* ...and any carry that produced */
	/* SPAN is repurposed here: below it flags a trailing odd byte, and if that byte
	 * is left pending (loop1out with no byte-swap) it carries forward as the "word
	 * spans into the next mbuf" flag that the top of the next iteration tests. */
	moveq	#1,SPAN
	and.l	MLEN,SPAN		/* a trailing odd byte? */
	beq	loop1out
	move.l	ZERO,d0
	move.b	(W)+,d0
	rol.l	#8,d0			/* high byte of the (possibly spanning) word */
	addx.w	d0,SUM
	addx.l	ZERO,SUM
	addx.l	ZERO,SUM

loop1out:
	tst.l	BSWAP
	beq	next
	rol.l	#8,SUM			/* undo the odd-start byte swap */
	subq.l	#1,SPAN			/* invert spanning status */

next:
	movea.l	M_NEXT(M),M
loop_test:
	move.l	M,d0
	beq	return
	tst.l	LEN
	beq	return
	move.l	M_LEN(M),MLEN
	beq	next			/* skip empty mbufs */
	bra	loop

return:
	/*
	 * Fold the 32-bit accumulator down to the 16-bit one's-complement checksum.
	 * (If LEN != 0 here the caller under-supplied data; the C version printed a
	 * diagnostic -- we just fold what we have rather than pull in printf.)
	 */
	move.l	ZERO,d0
	move.w	SUM,d0
	swap	SUM
	add.w	SUM,d0
	addx.w	ZERO,d0
	not.w	d0			/* one's complement (= eori.w #$ffff) */
	movem.l	(sp)+,d2-d7
	rts
