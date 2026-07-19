;;; $Id: in_cksum.asm,v 1.2 1994/02/17 18:35:54 jraja Exp $
;;;
;;; in_cksum.s --- calculate internet checksum
;;;
;;; Author: ppessi <Pekka.Pessi@hut.fi>
;;;
;;; Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
;;;                    Helsinki University of Technology, Finland.
;;;
;;; Created      : Wed Oct 20 08:55:45 1993 ppessi
;;; Last modified: Thu Oct 21 03:18:26 1993 ppessi
;;;
;;; $Log: in_cksum.asm,v $
;; Revision 1.2  1994/02/17  18:35:54  jraja
;; Added __regargs entry point.
;;
;; Revision 1.1  1993/10/21  01:59:45  ppessi
;; Initial revision
;;
;; Revision 1.1  1993/10/21  01:59:45  ppessi
;; Initial revision
;;
;;;

	INCLUDE   "sys/mbuf.i"

	XREF	_printf
	XDEF	_in_cksum
	XDEF	@in_cksum

** stack offsets for __stdargs
m_off		set	$4
len_off		set	$8

** registers for __regargs
m_reg		equr    a0
len_reg         equr    d0

m		equr	a0
w		equr	a1
end		equr	a2

len		equr	d7
mlen		equr	d6
sum		equr	d5
zero		equr	d4
spanning	equr	d3
byte_swapped    equr    d2

		section      text,code
_in_cksum:
		move.l	m_off(sp),m_reg
		move.l  len_off(sp),len_reg 
@in_cksum:
		movem.l d2-d7/a2/a3/a5/a6,-(sp)
* a0 to a0	move.l  m_reg,m
		move.l	len_reg,len
		moveq	#0,sum
		move.l	sum,spanning
		move.l	sum,zero
		bra 	loop_test

loop:		cmp.l	mlen,len
		bge	sk01
		move.l	len,mlen
sk01:		sub.l	mlen,len
		movea.l	M_DATA(m),w
* 
		tst.l	spanning
		beq.s	no_spanning
*
* The first byte of this mbuf is the continuation
* of a word spanning between this mbuf and the
* last mbuf.
*
		move.l	zero,d0
		move.b	(w)+,d0
		add.l	d0,sum
		addx.l	zero,sum
		addx.l	zero,sum
		; End of buffer?
		subq.l	#1,mlen
		move.l	mlen,spanning
		beq.w	next
*
* Force to word boundary
*
no_spanning:	move.l	w,byte_swapped
		andi.l	#1,byte_swapped
		beq.s	onword
*
* Swap sum bytes
*
		rol.l	#8,sum	
		move.l	zero,d0
		move.b	(w)+,d0
		add.l	d0,sum
		addx.l	zero,sum
		addx.l	zero,sum
		subq.l	#1,mlen
*
* Force to long boundary
*
onword:		move.l	w,d0
		andi.b	#3,d0
		beq	onlong
		moveq	#-2,d0
		add.l	mlen,d0
		blt	loop2out
		move.l	d0,mlen
		add.w	(w)+,sum
		addx.l	zero,sum
		addx.l	zero,sum
onlong:		move.l	mlen,d0
		ror.l	#5,d0
		add.l	zero,zero	; clear X
		bra	loop32test
*
* Main loop
*		
loop32:		move.l	(w)+,d1
		addx.l	d1,sum
		move.l	(w)+,d1
		addx.l	d1,sum
		move.l	(w)+,d1
		addx.l	d1,sum
		move.l	(w)+,d1
		addx.l	d1,sum
		move.l	(w)+,d1
		addx.l	d1,sum
		move.l	(w)+,d1
		addx.l	d1,sum
		move.l	(w)+,d1
		addx.l	d1,sum
		move.l	(w)+,d1
		addx.l	d1,sum
loop32test:	dbf.s	d0,loop32
		moveq	#31,d0
		and.l	mlen,d0
		ror.l	#2,d0
		bra.s	loop4test
loop4:		move.l	(w)+,d1
		addx.l	d1,sum	
loop4test:	dbf	d0,loop4
*			
* A single word still in mbuf?
*
		moveq	#2,d0
		and.l	mlen,d0
		beq	loop2out
		move.w	(w)+,d1
		addx.w	d1,sum
*
* A single byte still in mbuf?
*
loop2out:	addx.l	zero,sum
		addx.l	zero,sum
		moveq	#1,spanning
		and.l	mlen,spanning
		beq	loop1out
		move.l	zero,d0
		move.b	(w)+,d0
		rol.l	#8,d0
		addx.w	d0,sum
		addx.l	zero,sum
		addx.l	zero,sum

loop1out:	tst.l	byte_swapped
		beq	next
		; bytes were swapped, swap them back
		rol.l	#8,sum
		; Invert spanning status
		subq.l	#1,spanning		
next:		movea.l	M_NEXT(m),m
loop_test:	move.l	m,d0
		beq.s	return
		tst.l	len
		beq.s	return
*		if (m->m_len == 0)
*			continue;
		move.l	M_LEN(m),mlen
		beq.s	next
		bra.w	loop
return:		tst.l	len
		beq.s	reduce
		pea	out_of_data(pc)
		jsr	_printf(pc)
		addq.l	#4,sp
*
* Reduce
*
reduce:		move.l	zero,d0
		move.w	sum,d0
		swap	sum
		add.w	sum,d0
		addx.w	zero,d0
		eori.w	#$ffff,d0
		movem.l	(a7)+,d2-d7/a2/a3/a5/a6
		rts
		
out_of_data:	dc.b	'cksum: out of data',10,0
		
		end
