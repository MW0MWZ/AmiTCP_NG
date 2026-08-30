/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * GPL v2 (see COPYING).
 *
 * in_cksum_copy_asm.S -- copy bytes and accumulate their Internet checksum in ONE
 * pass, in 68k assembly.
 *
 * WHY. Every received byte currently crosses memory three times: the SANA copy into
 * mbufs, a full in_cksum() pass whose only product is a 16-bit number, and the copy
 * out to the user buffer. This folds the second pass into the first, so the
 * copy+checksum work goes from three memory passes to two.
 *
 * THE CONTRACT IS in_cksum_copy()'s, EXACTLY:
 *
 *   u_long in_cksum_copy_asm(const void *src, void *dst, u_long len,
 *                            u_long sum, int *odd)
 *
 * Standard C stack arguments. Returns the updated running sum, folded to 16 bits but
 * NOT complemented -- callers finish with in_cksum_fold(). `*odd` carries "the total
 * already holds an unpaired byte in the high half" across calls, which is how a
 * caller walks an mbuf chain one segment at a time. The portable C in
 * netinet/in_cksum_copy.c is the ORACLE this is fuzzed against on target; if the two
 * ever disagree, the C is right and this is wrong.
 *
 * SRC AND DST MUST NOT OVERLAP -- memcpy semantics, not memmove, same as the C.
 *
 * WHAT MAKES THE FAST PATH LEGAL, and it is not what a first reading suggests.
 * The 68000 raises an Address Error on an ODD address only; a long access at address
 * 2 -- even, not a multiple of 4 -- is perfectly legal. So the fast path needs both
 * pointers EVEN, and nothing more. Congruence mod 4 is NOT a correctness condition:
 * it is a 68020+ bus-efficiency question (the 68000's 16-bit bus makes every long
 * access two word transfers regardless), and misaligned access is legal on 020+
 * anyway. Do NOT "simplify" the guard below to ((src ^ dst) & 1) == 0 -- that is also
 * true when both pointers are ODD, which faults on every access on both sides.
 *
 * Measured on real hardware before this was written: 49,152 frames, 42,350 of them
 * spanning two mbufs, every pointer both even AND 4-aligned -- 100% on both bars,
 * with the fallback never taken. (Amiberry's a2065 disagrees, reporting residue 2,
 * because it hands over the datagram at frame+14. The emulator is not representative
 * for alignment.)
 *
 * CALLED FROM net/sana2copybuff.c (ng_rx_csum_chunk), on 68000/020/030 only -- see
 * NG_RX_CSUM in conf/conf.h for why the fast tier is excluded. It runs at DEVICE
 * INTERRUPT time, at the same call site that once produced a receive-ring re-arm
 * livelock, so lowering total cycles while raising time-in-interrupt remains a real
 * risk that the throughput figures below do NOT measure.
 *
 * Measured, fused vs today's CopyMem + in_cksum (emulated, len 40/576/1460):
 *   68000  1.29x / 1.26x / 1.24x      68020  1.44x / 1.33x / 1.29x
 *   68040  1.57x / 1.03x / 0.92x   <- a LOSS at full MSS
 * The 68040 has 4K of data cache, so today's second pass is largely cache-resident
 * and a bulk copy beats move.l per longword. MOVE16 cannot rescue a FUSED loop: it
 * is memory-to-memory and routes nothing through a data register to addx from, so a
 * one-pass 68040 variant is architecturally impossible. Gate this to 68000/020/030.
 *
 * THE TRICK, same as in_cksum_asm.S: one long `addx.l` dependency chain accumulates
 * 32-bit words with the carry living in the X flag between iterations. Summing whole
 * longwords and folding at the end is equal to summing 16-bit big-endian words, which
 * is what makes it legal to read four bytes at a time. Two instructions here are
 * load-bearing for that chain and must not be replaced casually:
 *
 *   - MOVE sets N/Z/V/C but leaves X ALONE. That is what lets the store sit inside
 *     the chain, between the addx and the next load, for free.
 *   - `dbf` does not touch X either, so the loop counter does not break the chain.
 *     `subq`/`sub` DO write X and would silently corrupt the running carry.
 */

	.text
	.align	2

/* Register aliases. d5-d7/a2 are callee-saved and restored on every exit path. */
#define SRC	a0		/* source walk pointer                              */
#define DST	a1		/* destination walk pointer                         */
#define ODD	a2		/* int * -- cross-call parity state                  */
#define ZERO	d5		/* a held zero (addx needs a register source)        */
#define SUM	d6		/* 32-bit running one's-complement accumulator      */
#define LEN	d7		/* bytes of this call still to process              */

/* Arguments, after movem.l pushes four longs and above the return address. */
#define A_SRC	20(sp)
#define A_DST	24(sp)
#define A_LEN	28(sp)
#define A_SUM	32(sp)
#define A_ODD	36(sp)

	.globl	_in_cksum_copy_asm
_in_cksum_copy_asm:
	movem.l	d5-d7/a2,-(sp)		/* 4 longs = 16 bytes                      */
	movea.l	A_SRC,SRC
	movea.l	A_DST,DST
	move.l	A_LEN,LEN
	move.l	A_SUM,SUM
	movea.l	A_ODD,ODD
	moveq	#0,ZERO

	tst.l	LEN
	beq	fold			/* n == 0: return sum untouched, as the C does,
					 * WITHOUT disturbing *odd -- an empty segment
					 * must neither complete nor clear a pending
					 * unpaired byte.                          */

	/*
	 * An unpaired byte is outstanding from a previous call: this byte is the LOW
	 * half of that word. Take it first, which also restores even parity for
	 * everything after it.
	 */
	tst.l	(ODD)
	beq	no_pending
	moveq	#0,d0
	move.b	(SRC)+,d0
	move.b	d0,(DST)+
	add.l	d0,SUM
	subq.l	#1,LEN
	clr.l	(ODD)
	tst.l	LEN
	beq	fold
no_pending:

	/*
	 * FAST PATH GUARD. Both pointers must be even (see the header). Anything else
	 * takes the byte loop, which is a transcription of the C oracle and is correct
	 * for every alignment.
	 */
	move.l	SRC,d0
	move.l	DST,d1			/* OR takes no An source -- via a data reg */
	or.l	d1,d0
	btst	#0,d0
	bne	byteloop

	/*
	 * The 16-bit `dbf` counter caps a block loop at 65535 iterations. Real callers
	 * pass at most one mbuf's worth (mclbytes, 2048 by default), so this never
	 * fires -- but the contract is total, so hand anything larger to the byte loop
	 * rather than leave a silent wrap.
	 *
	 * ⚠️ THIS BOUND HAS A SECOND, LESS OBVIOUS DUTY. Only loop16/loop4 chain the
	 * carry through X. The trailing word, the trailing byte and the whole byte loop
	 * use plain `add.l`, which SILENTLY DROPS a carry out of bit 31. Capping LEN at
	 * 65535 caps the true accumulated value at roughly
	 *   incoming sum (<= 0xffff) + 32768 words * 0xffff  ~=  0x7fff0000
	 * which is comfortably under 2^32, so those un-chained adds can never overflow.
	 * `dbf` alone would tolerate a far larger LEN (~1 MB for loop16), so anyone
	 * reasoning only about the counter could raise this constant and silently
	 * corrupt checksums at large lengths -- no crash, no trap, just a wrong answer.
	 * DO NOT RAISE IT without also carry-catching the trailing adds.
	 */
	cmpi.l	#0x10000,LEN
	bhs	byteloop

	/*
	 * Reach a 4-byte boundary if we are not on one. Pure optimisation for 68020+
	 * (a 4-aligned long is one bus cycle there, a 2-mod-4 long is two); on a 68000
	 * it changes nothing. Both pointers are even here, so SRC&3 is 0 or 2, and
	 * consuming one word keeps the checksum's word parity intact.
	 *
	 * NOTE this aligns SRC only. DST's mod-4 residue is never checked separately,
	 * so when the two differ, DST stays 2-mod-4 for the whole run and only half the
	 * 68020+ bus benefit is realised. That is a performance detail, not a
	 * correctness one -- evenness is the only requirement, and both are even here.
	 * Measured RX traffic has src and dst congruent, so this costs nothing in
	 * practice; it would matter on a driver whose buffer sits at a different
	 * residue (Amiberry's a2065 does exactly that).
	 */
	move.l	SRC,d0
	andi.l	#3,d0
	beq	on_long
	cmpi.l	#2,LEN
	blo	byteloop
	moveq	#0,d0
	move.w	(SRC)+,d0
	move.w	d0,(DST)+
	add.l	d0,SUM
	subq.l	#2,LEN
on_long:

	/* --- main loop: four longs (16 bytes) per iteration ---------------------- */
	move.l	LEN,d1
	lsr.l	#4,d1			/* number of whole 16-byte blocks          */
	beq	after16
	subq.l	#1,d1			/* dbf runs count+1 times                  */
	add.l	ZERO,ZERO		/* ZERO+ZERO = 0 and clears X              */
loop16:
	move.l	(SRC)+,d0
	addx.l	d0,SUM
	move.l	d0,(DST)+
	move.l	(SRC)+,d0
	addx.l	d0,SUM
	move.l	d0,(DST)+
	move.l	(SRC)+,d0
	addx.l	d0,SUM
	move.l	d0,(DST)+
	move.l	(SRC)+,d0
	addx.l	d0,SUM
	move.l	d0,(DST)+
	dbf	d1,loop16		/* dbf leaves X alone -> carry survives    */
	addx.l	ZERO,SUM		/* absorb the pending carry...             */
	addx.l	ZERO,SUM		/* ...and any carry that produced          */
after16:

	/* --- 4-byte remainder, still carry-chained ------------------------------- */
	move.l	LEN,d1
	andi.l	#15,d1
	lsr.l	#2,d1			/* 0..3 whole longs left                   */
	beq	after4
	subq.l	#1,d1
	add.l	ZERO,ZERO		/* clear X before a fresh chain            */
loop4:
	move.l	(SRC)+,d0
	addx.l	d0,SUM
	move.l	d0,(DST)+
	dbf	d1,loop4
	addx.l	ZERO,SUM
	addx.l	ZERO,SUM
after4:

	/* --- trailing word, then trailing byte ----------------------------------- */
	move.l	LEN,d1
	andi.l	#3,d1
	cmpi.l	#2,d1
	blo	tail_byte
	moveq	#0,d0
	move.w	(SRC)+,d0
	move.w	d0,(DST)+
	add.l	d0,SUM
	subq.l	#2,d1
tail_byte:
	tst.l	d1
	beq	fold
	/*
	 * A trailing byte is the HIGH half of a word the NEXT call completes. Record
	 * that so the next call knows its parity.
	 */
	moveq	#0,d0
	move.b	(SRC)+,d0
	move.b	d0,(DST)+
	lsl.w	#8,d0
	add.l	d0,SUM
	moveq	#1,d0
	move.l	d0,(ODD)
	bra	fold

	/*
	 * --- byte loop: a transcription of the C oracle --------------------------
	 * Taken whenever a pointer is odd (so a word access would fault on a 68000),
	 * or the length is absurd. Correct for every alignment; simply slower.
	 */
byteloop:
	cmpi.l	#2,LEN
	blo	bl_tail
	moveq	#0,d0
	move.b	(SRC)+,d0
	move.b	d0,(DST)+
	lsl.w	#8,d0			/* first byte is the HIGH half             */
	moveq	#0,d1
	move.b	(SRC)+,d1
	move.b	d1,(DST)+
	or.w	d1,d0			/* second byte is the LOW half             */
	add.l	d0,SUM
	subq.l	#2,LEN
	bra	byteloop
bl_tail:
	tst.l	LEN
	beq	fold
	moveq	#0,d0
	move.b	(SRC)+,d0
	move.b	d0,(DST)+
	lsl.w	#8,d0
	add.l	d0,SUM
	moveq	#1,d0
	move.l	d0,(ODD)

	/*
	 * --- fold the 32-bit accumulator down to 16 bits -------------------------
	 * Two rounds are sufficient and necessary for a 32-bit input: the first can
	 * still leave a carry out of the low word, the second cannot. This mirrors the
	 * C oracle's `while (sum >> 16)` loop exactly. NOT complemented -- the caller
	 * finishes with in_cksum_fold().
	 */
fold:
	move.l	SUM,d0
	swap	d0
	andi.l	#0xffff,d0
	andi.l	#0xffff,SUM
	add.l	d0,SUM
	move.l	SUM,d0
	swap	d0
	andi.l	#0xffff,d0
	andi.l	#0xffff,SUM
	add.l	d0,SUM

	move.l	SUM,d0			/* return value in d0                      */
	movem.l	(sp)+,d5-d7/a2
	rts
