/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * copy_candidates.S -- hand-tuned block-copy candidates for the copybench harness, to
 * decide whether a per-CPU asm copy beats exec CopyMem() on the payload hot path
 * (task #49). Best-case forward copies (no overlap): assume the stated alignment and a
 * length that is a multiple of the block size. This measures the CEILING; if it does
 * not clear CopyMem, the production routine is not worth building.
 *
 *   void copy_movem (const void *src, void *dst, long len);  // len % 32 == 0, long-aligned
 *   void copy_move16(const void *src, void *dst, long len);  // len % 16 == 0, 16-byte aligned (040+)
 *
 * Stack args (4/8/12(sp)), C ABI; saves the callee-saved registers it uses.
 */
	.text
	.align	2

/* 8-longs-per-iteration movem copy (32 bytes/iter). Good on 68000 (batches the slow
 * 16-bit-bus instruction fetch) and 68020/030. */
	.globl	_copy_movem
_copy_movem:
	movea.l	4(sp),a0		/* src */
	movea.l	8(sp),a1		/* dst */
	move.l	12(sp),d0		/* len */
	movem.l	d2-d7/a2,-(sp)		/* save the 7 callee-saved regs we use */
	lsr.l	#5,d0			/* len / 32 = number of blocks */
	beq	2f
	subq.l	#1,d0			/* dbf bias */
1:	movem.l	(a0)+,d1-d7/a2		/* load 8 longs (mem->reg allows (an)+) */
	movem.l	d1-d7/a2,(a1)		/* store 8 longs (reg->mem: no (an)+, use (an)) */
	lea	32(a1),a1
	dbf	d0,1b			/* 16-bit count -- fine for the bench sizes */
2:	movem.l	(sp)+,d2-d7/a2
	rts

/* move16 copy (16 bytes/iter, burst-mode) -- 68040/68060 only. */
#if defined(__mc68040__) || defined(__mc68060__)
	.globl	_copy_move16
_copy_move16:
	movea.l	4(sp),a0		/* src (16-byte aligned) */
	movea.l	8(sp),a1		/* dst (16-byte aligned) */
	move.l	12(sp),d0		/* len */
	lsr.l	#4,d0			/* len / 16 = number of blocks */
	beq	2f
	subq.l	#1,d0
1:	move16	(a0)+,(a1)+		/* 16-byte burst copy, both post-incremented */
	dbf	d0,1b
2:	rts
#endif
