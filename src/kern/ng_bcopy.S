/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * This file (C) 2026 Timm S. Mueller (TSM)
 * GPL v2 (see COPYING).
 *
 * general memcopy routine - any length, alignment, overlap
 * void ng_bcopy(void *src (a0), void *dst (a1), unsigned long len (d0)); 
 */

BLKSIZE = 32
 
	.text
	.globl	_ng_memcopy
	.align	2
	
_ng_memcopy:
		move.l	a0,d1
		btst	#0,d1
		bne.b	bytecopy
		
		move.l	a1,d1
		btst	#0,d1
		beq.b	wordcopy
		
bytecopy:
		moveq	#7,d1
		and.w	d0,d1
		neg.w	d1
		add.w	d1,d1
		
		cmp.l	a1,a0
		blt.b	bcbackw
		
		jmp	bclop1(pc,d1.w)
bclop:
		move.b	(a0)+,(a1)+
		move.b	(a0)+,(a1)+
		move.b	(a0)+,(a1)+
		move.b	(a0)+,(a1)+
		move.b	(a0)+,(a1)+
		move.b	(a0)+,(a1)+
		move.b	(a0)+,(a1)+
		move.b	(a0)+,(a1)+
bclop1:
		subq.l	#8,d0
		bpl.b	bclop
		
		rts
		
bcbackw:
		add.l	d0,a0
		add.l	d0,a1
		jmp	bcrlop1(pc,d1.w)
bcrlop:
		move.b	-(a0),-(a1)
		move.b	-(a0),-(a1)
		move.b	-(a0),-(a1)
		move.b	-(a0),-(a1)
		move.b	-(a0),-(a1)
		move.b	-(a0),-(a1)
		move.b	-(a0),-(a1)
		move.b	-(a0),-(a1)
bcrlop1:
		subq.l	#8,d0
		bpl.b	bcrlop
		
		rts
	
wordcopy:
		movem.l	d2-d7,-(a7)
		
		bclr	#0,d0
		sne	d7
		move.l	d0,d2
		moveq	#BLKSIZE,d1
		moveq	#~(BLKSIZE-1),d6

		cmp.l	a1,a0
		blt.s	backw

		and.l	d6,d0
		sub.l	d0,d2
		sub.l	d1,d0
		bmi.b	forw_bigdone
forw_big:
		movem.l	(a0)+,d3-d6
		movem.l	d3-d6,(a1)
		movem.l	(a0)+,d3-d6
		movem.l	d3-d6,16(a1)
		lea	BLKSIZE(a1),a1
		sub.l	d1,d0
		bpl.b	forw_big
forw_bigdone:
		subq.w	#2,d2
		bmi.b	forw_almostdone
forw_smlop:
		move.w	(a0)+,(a1)+
		subq.w	#2,d2
		bpl.b	forw_smlop
		
forw_almostdone:
		tst.b	d7
		beq.b	cmq_done
		
		move.b	(a0),(a1)
		bra.b	cmq_done
		
backw:
		add.l	d0,a0
		add.l	d0,a1
		tst.b	d7
		beq.b	backw_noextra
		
		move.b	(a0),(a1)
		
backw_noextra:
		and.l	d6,d0
		sub.l	d0,d2
		sub.l	d1,d0
		bmi.b	backw_bigdone
backw_big:
		lea	-BLKSIZE(a0),a0
		movem.l	16(a0),d3-d6
		movem.l	d3-d6,-(a1)
		movem.l	(a0),d3-d6
		movem.l	d3-d6,-(a1)
		sub.l	d1,d0
		bpl.b	backw_big
backw_bigdone:
		subq.w	#2,d2
		bmi.b	cmq_done
backw_smlop:
		move.w	-(a0),-(a1)
		subq.w	#2,d2
		bpl.b	backw_smlop
		
cmq_done:
		movem.l	(a7)+,d2-d7
		rts
