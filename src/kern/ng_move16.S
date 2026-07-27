/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * GPL v2 (see COPYING).
 *
 * ng_move16.S -- 68040/68060 burst-mode block copy for the payload hot path.
 *
 *   void ng_move16(const void *src, void *dst, long nblocks);
 *
 * Copies nblocks * 16 bytes. src and dst MUST both be 16-byte aligned (the caller,
 * ng_bcopy(), checks). MOVE16 moves a 16-byte cache line per instruction using the
 * 040/060 burst bus -- ~20-30% faster than CopyMem() for aligned bulk (measured with
 * copybench on an emulated A4000/040). Assembled only for 68040/68060; on other CPUs
 * this file is empty and ng_bcopy() never references it (CopyMem is already
 * movem-based there, so there is nothing to beat).
 *
 * Uses only caller-saved a0/a1/d0 -- no register save/restore needed.
 */
#if defined(__mc68040__) || defined(__mc68060__)
	.text
	.align	2
	.globl	_ng_move16
_ng_move16:
	movea.l	4(sp),a0		/* src (16-byte aligned) */
	movea.l	8(sp),a1		/* dst (16-byte aligned) */
	move.l	12(sp),d0		/* nblocks */
	beq	2f
1:	move16	(a0)+,(a1)+		/* copy one 16-byte line, both post-incremented */
	subq.l	#1,d0			/* 32-bit count: no dbf 64K-block limit */
	bne	1b
2:	rts
#endif
