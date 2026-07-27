/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * GPL v2 (see COPYING).
 *
 * ng_copy.c -- ng_bcopy(): the payload-copy routine for the socket data path.
 *
 * On a 68040/68060 a 16-byte-aligned bulk copy is ~20-30% faster with MOVE16 (burst-mode
 * cache-line move) than CopyMem() -- so when both ends are 16-aligned and the copy is
 * worth it, use ng_move16(); otherwise fall back to CopyMem(), which is already
 * movem-optimised for the small/misaligned case (and is the only path on 68000/020/030,
 * where a hand copy is no better -- measured with copybench).
 *
 * SCOPE: used ONLY on the user<->mbuf copies (uiomove, uipc_socket.c), which are always
 * plain system RAM. The mbuf<->device SANA copy hooks (net/sana2copybuff.c) deliberately
 * still use CopyMem: their buffer pointer is DRIVER-OWNED memory whose class is unknown
 * -- SANA-II drivers may map onboard packet buffers cache-inhibited or on a bus that does
 * not honour 68040 burst cycles, where MOVE16 can misbehave, and an emulated 68040 does
 * not model that faithfully. Extending MOVE16 to the SANA path needs validation on real
 * 68040/68060 hardware with a real NIC first (a deferred follow-up).
 *
 * Non-overlapping forward copy only, exactly like the CopyMem() it replaces (overlapping
 * copies use ovbcopy()/memmove()).
 */
#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <kern/amiga_subr.h>		/* bcopy() -> CopyMem(), APTR, ng_bcopy proto */

void
ng_bcopy(const void *src, void *dst, long len)
{
#if defined(__mc68040__) || defined(__mc68060__)
	/*
	 * MOVE16 needs both operands 16-byte aligned; require a worthwhile bulk (>= 64 B)
	 * so the setup never loses to CopyMem on small copies. Copy the 16-byte-aligned
	 * blocks with MOVE16, then the sub-16 tail with CopyMem.
	 */
	if (len >= 64 && ((((unsigned long)src | (unsigned long)dst) & 15UL) == 0)) {
		extern void ng_move16(const void *src, void *dst, long nblocks);
		long blocks = (long)((unsigned long)len >> 4);
		long done   = blocks << 4;
		long tail   = len - done;

		ng_move16(src, dst, blocks);
		if (tail)
			bcopy((const char *)src + done, (char *)dst + done, tail);
		return;
	}
#endif
	bcopy(src, dst, len);			/* CopyMem: small / misaligned / non-040 */
}
