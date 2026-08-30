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
#include <kern/amiga_includes.h>	/* CopyMemQuick (inline/exec.h) */

/*
 * MOVE16 IS ON for 68040/68060, and the record of why it briefly was not is worth
 * keeping.
 *
 * MOVE16 is opcode $F620, an F-line instruction, so a processor that does not
 * implement it takes the Line-F exception -- "software failure 8000000B". A boot
 * Guru with exactly that code on a PiStorm/Emu68 A1200 was blamed on this code and
 * it was gated off. That was wrong: v4.1.5's 68040 build contains MOVE16 and boots
 * correctly on that same machine, and the fault was later isolated to a different
 * component entirely. Emu68 implements MOVE16 fine.
 *
 * Left in, therefore, on the evidence rather than removed on a hunch. If a machine
 * is ever shown to claim 68040 without implementing MOVE16, gate it there and say
 * which machine -- do not re-remove it on a symptom alone.
 */
/*
 * ng_bcopy_dev() -- the aligned fast copy, WITHOUT MOVE16.
 *
 * CopyMem is general: it inspects alignment and handles every awkward case, and on a
 * 68000 with no cache that analysis costs more than the copy. CopyMemQuick demands
 * longword-aligned ends and a longword-multiple size and is then a tight move.l loop.
 * Measured on the emulated 7 MHz 68000 with the UDP loopback bench: per-byte cost of a
 * round trip fell from 8.88 to 3.65 us/byte, and a full-MTU round trip from 18.9 ms to
 * 11.4 ms. The tail and any misaligned case still go to CopyMem.
 *
 * SAFE FOR DRIVER-OWNED MEMORY, which is why this is separate from ng_bcopy(). The
 * reason the SANA hooks were kept on CopyMem is MOVE16 specifically -- burst cache-line
 * moves against buffers a driver may have mapped cache-inhibited. CopyMemQuick issues
 * ordinary move.l, no bursts and no cache-line operations, so that objection does not
 * apply to it.
 */
void
ng_bcopy_dev(const void *src, void *dst, long len)
{
#if NG_COPYQUICK
	/*
	 * WHICH ROUTINE WINS DEPENDS ON THE CPU, and it reverses. Measured, both arms
	 * interleaved on the same rig:
	 *
	 *   68000 (UDP loopback bench, A600):  CopyMemQuick 40% FASTER at full MTU
	 *                                      (per-byte 8.88 -> 3.65 us/byte)
	 *   68040 (RX throughput bench, A4000): CopyMemQuick 3.3% SLOWER, repeatably
	 *
	 * The reason is that Kickstart's CopyMem moves many longwords per iteration with
	 * movem.l, which a 68020+ executes far better than CopyMemQuick's plain move.l
	 * loop; on a 68000 that advantage is small and CopyMem's per-call alignment
	 * analysis costs more than the copy.
	 *
	 * So this is a RUNTIME test, not a build-time one: the plain 68000 archive is
	 * the build people run on any machine, and gating at compile time would hand an
	 * accelerated Amiga running that archive a measured 3% regression.
	 */
	static int quick = -1;			/* -1 = not yet determined */

	if (quick < 0)
		quick = (SysBase->AttnFlags & AFF_68020) ? 0 : 1;

	if (quick && len >= 16 &&
	    ((((unsigned long)src | (unsigned long)dst) & 3UL) == 0)) {
		long done = len & ~3L;

		CopyMemQuick((APTR)src, dst, (ULONG)done);
		if (len - done)
			bcopy((const char *)src + done, (char *)dst + done, len - done);
		return;
	}
#endif
	bcopy(src, dst, len);			/* CopyMem: 68020+, short, or misaligned */
}

/*
 * Config: USEMOVE16= in AmiTCP.config (alias MV16), also settable at run time.
 *
 * DEFAULT OFF. MOVE16's ~20-30% claim comes from an earlier copybench run and has
 * never been confirmed to help the packet path on real hardware -- the only rig here
 * that can execute it is an emulated 68040, which is too fast for the benchmark clock
 * to resolve. Until someone measures a benefit on a real 040 or 060, the burst copy
 * stays dormant and the machine uses the same CopyMem path every earlier release did.
 * Turn it on with USEMOVE16=yes to try it.
 */
LONG ng_use_move16 = 0;

void
ng_bcopy(const void *src, void *dst, long len)
{
	/*
	 * The CPU never changes, so cache that; the config CAN change at run time, so
	 * read it every call. Together they mean ng_move16.S -- which is assembled for
	 * 68040 in EVERY library variant -- is reached only on a machine that has the
	 * instruction AND has been told to use it. An instruction that is never
	 * executed cannot fault, which is what lets one binary carry it safely.
	 */
	static int cpu040 = -1;

	if (cpu040 < 0)
		cpu040 = (SysBase->AttnFlags & AFF_68040) ? 1 : 0;

	if (ng_use_move16 && cpu040 && len >= 64 &&
	    ((((unsigned long)src | (unsigned long)dst) & 15UL) == 0)) {
		extern void ng_move16(const void *src, void *dst, long nblocks);
		long blocks = (long)((unsigned long)len >> 4);
		long done   = blocks << 4;
		long tail   = len - done;

		ng_move16(src, dst, blocks);
		if (tail)
			bcopy((const char *)src + done, (char *)dst + done, tail);
		return;
	}
	ng_bcopy_dev(src, dst, len);
}
