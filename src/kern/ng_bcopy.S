/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * GPL v2 (see COPYING).
 *
 * ng_bcopy() -- the one payload copy routine, contributed by Timm Mueller.
 * With thanks: this replaces a C dispatcher that picked between CopyMem,
 * CopyMemQuick and a MOVE16 burst path per call, all of which are now gone.
 *
 *   void ng_bcopy(const void *src, void *dst, long len);
 *
 * One routine for every case -- any length, any alignment, overlapping or not --
 * so the caller never has to know which it has. Odd ends go byte-at-a-time;
 * everything else copies 64 bytes per iteration with movem.l through eight
 * registers, and runs backwards when the regions overlap the wrong way.
 *
 * NO CPU DISPATCH AND NO MOVE16. It is plain 68000 code and is assembled at the
 * target arch (docker/build.sh), which is what stops a 68020+ encoding reaching a
 * 68000 machine.
 *
 * Two defects in the contributed version are fixed below, each marked FIX: an
 * overlapping forward copy of odd length, and a 16-bit block counter that stopped
 * early at 512 KB. Both were latent -- no current caller hit either.
 */
    
    .text
    
    .globl    _ng_bcopy
    .align    4

_ng_bcopy:
        movea.l    4(sp),a0
        movea.l    8(sp),a1
        move.l    12(sp),d0
        bne.b    cont
        
        rts
cont:
        move.l    a0,d1
        btst    #0,d1
        bne.b    bytecopy
        
        move.l    a1,d1
        btst    #0,d1
        beq.b    wordcopy
    
bytecopy:
        moveq    #7,d1
        and.w    d0,d1
        neg.w    d1
        add.w    d1,d1
        
        cmp.l    a1,a0
        blt.b    bcbackw
        
        jmp    bclop1(pc,d1.w)
        .align    8
bclop:
        move.b    (a0)+,(a1)+
        move.b    (a0)+,(a1)+
        move.b    (a0)+,(a1)+
        move.b    (a0)+,(a1)+
        move.b    (a0)+,(a1)+
        move.b    (a0)+,(a1)+
        move.b    (a0)+,(a1)+
        move.b    (a0)+,(a1)+
bclop1:
        subq.l    #8,d0
        bpl.b    bclop
        bra    cm_done
        
bcbackw:
        add.l    d0,a0
        add.l    d0,a1
        jmp    bcrlop1(pc,d1.w)
        .align    8
bcrlop:
        move.b    -(a0),-(a1)
        move.b    -(a0),-(a1)
        move.b    -(a0),-(a1)
        move.b    -(a0),-(a1)
        move.b    -(a0),-(a1)
        move.b    -(a0),-(a1)
        move.b    -(a0),-(a1)
        move.b    -(a0),-(a1)
bcrlop1:
        subq.l    #8,d0
        bpl.b    bcrlop
        bra    cm_done
    
    
wordcopy:
/* FIX 1 (AmiTCP_NG, on top of Timm's routine): the odd trailing byte must be
 * copied in the same ORDER as the bulk. The original peeled it up front, before
 * the direction had been chosen, so an overlapping FORWARD copy wrote a byte into
 * source it had not read yet: ng_bcopy(src=0x1000, dst=0x0FFE, len=3) stored
 * src[2] at 0x1000 and then read 0x1000 back as src[0], losing it. d1 now carries
 * "one trailing byte still owed" -- the backward path settles it FIRST (its byte
 * is the highest address, which backward order copies first anyway), the forward
 * path settles it LAST, at forw_done. */
        moveq    #0,d1
        btst    #0,d0
        beq.b    wc_dir

        subq.l    #1,d0            /* bulk length = the even part */
        moveq    #1,d1            /* ... and one byte still owed */
wc_dir:
        move.l    d2,-(a7)
        move.l    d0,d2

        cmp.l    a1,a0
        blt    backw

    # move.w    #0x0a0,0xdff180

/* the 64-byte mask is an immediate now, which keeps d1 free to carry the flag */
        and.l    #-64,d0
        beq.s    forw_small

        sub.l    d0,d2
        lsr.l    #6-3,d0

        movem.l    d3-d7/a2-a4,-(a7)
/* .w, not .b: this jumps over whatever padding .align emits, and when the loop
 * already lands aligned the padding is ZERO -- a byte branch of displacement 0 is
 * not encodable and the assembler rejects it. Timm's original carried a comment
 * predicting exactly that; a word branch is simply immune, and it is outside the
 * loop so the extra word costs nothing measurable. */
        bra.w    forw64
        .align    8
forw64:
        movem.l    (a0)+,d3-d7/a2-a4
        movem.l    d3-d7/a2-a4,(a1)
        movem.l    (a0)+,d3-d7/a2-a4
        movem.l    d3-d7/a2-a4,32(a1)
        lea    64(a1),a1
/* FIX 2 (AmiTCP_NG): .l, not .w. d0 counts (bulk bytes / 8), so from 512 KB up a
 * 16-bit decrement stops early: at len=524352 the low word hits zero after ONE
 * iteration and the routine returns having copied 64 of 524352 bytes, silently. */
        subq.l    #8,d0
        bne.b    forw64

        movem.l    (a7)+,d3-d7/a2-a4

forw_small:
        tst.w    d2
        beq.b    forw_done
forw_smlop:
        move.w    (a0)+,(a1)+
        subq.w    #2,d2
        bne.b    forw_smlop

forw_done:
        tst.l    d1
        beq.b    cmq_done
        move.b    (a0),(a1)        /* a0/a1 stopped exactly on the owed byte */
        bra.b    cmq_done

backw:
    # move.w    #0x00e,0xdff180

        tst.l    d1
        beq.b    bw_bulk
        move.b    (a0,d0.l),(a1,d0.l)    /* highest byte first = backward order */
bw_bulk:
        add.l    d0,a0
        add.l    d0,a1

        and.l    #-64,d0
        beq.s    backw_small

        sub.l    d0,d2
        lsr.l    #6-3,d0

        movem.l    d3-d7/a2-a4,-(a7)
backw64:
        lea    -64(a0),a0
        movem.l    32(a0),d3-d7/a2-a4
        movem.l    d3-d7/a2-a4,-(a1)
        movem.l    (a0),d3-d7/a2-a4
        movem.l    d3-d7/a2-a4,-(a1)
        subq.l    #8,d0            /* FIX 2, as forw64 */
        bne.b    backw64
        movem.l    (a7)+,d3-d7/a2-a4

backw_small:
        tst.w    d2
        beq.b    cmq_done
backw_smlop:
        move.w    -(a0),-(a1)
        subq.w    #2,d2
        bne.b    backw_smlop

cmq_done:
        move.l    (a7)+,d2

cm_done:
        # move.w    #0x123,0xdff180
        rts
