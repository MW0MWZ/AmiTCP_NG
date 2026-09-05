/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * This file (C) 2026 Timm S. Mueller (TSM).
 * Licence: GPL v2 (see COPYING).
 *
 * void ng_bcopy(void *src (a0), void *dst (a1), ULONG len (d0)); 
 * void ng_bzero(void *src (a0), ULONG len (d0)); 
 */

BLKSIZE = 32
 
	.text
	.globl	_ng_bcopy
	.globl	_ng_bzero
	.align	8
	
_ng_bcopy:
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
	.align	8
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
	.align	8
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
		moveq	#BLKSIZE,d1
		
		cmp.l	a1,a0
		blt.s	backw
		
		sub.l	d1,d0
		bmi.b	forw_bigdone
		
		movem.l	d3-d6,-(a7)
forw_big:		
		movem.l	(a0)+,d3-d6
		movem.l	d3-d6,(a1)
		add.l	d1,a1
		movem.l	(a0)+,d3-d6
		sub.l	d1,d0
		movem.l	d3-d6,16-BLKSIZE(a1)
		bpl.b	forw_big
		
		movem.l	(a7)+,d3-d6
forw_bigdone:
		add.l	d1,d0
		subq.w	#2,d0
		bmi.b	forw_rest2done
forw_rest2:
		move.w	(a0)+,(a1)+
		subq.w	#2,d0
		bpl.b	forw_rest2
forw_rest2done:
		addq.w	#2,d0
		beq.b	forw_done

		move.b	(a0),(a1)
forw_done:
		rts

backw:
		add.l	d0,a0
		add.l	d0,a1
		bclr	#0,d0
		beq.b	backw_even
		
		move.b	-(a0),-(a1)
backw_even:
		sub.l	d1,d0
		bmi.b	backw_bigdone
		
		movem.l	d3-d6,-(a7)
backw_big:		
		movem.l	16-BLKSIZE(a0),d3-d6
		sub.l	d1,a0
		movem.l	d3-d6,-(a1)
		movem.l	(a0),d3-d6
		sub.l	d1,d0
		movem.l	d3-d6,-(a1)
		bpl.b	backw_big
		
		movem.l	(a7)+,d3-d6
backw_bigdone:
		add.l	d1,d0
		subq.w	#2,d0
		bmi.b	backw_done
backw_rest:
		move.w	-(a0),-(a1)
		subq.w	#2,d0
		bpl.b	backw_rest
		
backw_done:
		rts
		
	
_ng_bzero:
		move.l	a0,d1
		btst	#0,d1
		bne.b	bzerob
		
		moveq	#16,d1
		sub.l	d1,d0
		bmi.b	bzero_bigdone
bzero_big:
		clr.l	(a0)+
		clr.l	(a0)+
		clr.l	(a0)+
		clr.l	(a0)+
		sub.l	d1,d0
		bpl.b	bzero_big
bzero_bigdone:
		add.l	d1,d0
		
bzerob:
		moveq	#7,d1
		and.w	d0,d1
		neg.w	d1
		add.w	d1,d1
		jmp	bzlop1(pc,d1.w)
	.align	8
bzlop:
		clr.b	(a0)+
		clr.b	(a0)+
		clr.b	(a0)+
		clr.b	(a0)+
		clr.b	(a0)+
		clr.b	(a0)+
		clr.b	(a0)+
		clr.b	(a0)+
bzlop1:
		subq.l	#8,d0
		bpl.b	bzlop
		
		rts
	