/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * copybench -- does a hand-tuned per-CPU block copy actually beat exec CopyMem() on the
 * payload hot path? (task #49). CopyMem is already movem-based on Kickstart 3.x, so the
 * realistic win is mostly 68040 move16. This times CopyMem vs a movem copy vs (on 040) a
 * move16 copy over 16-byte-aligned buffers (best case), and correctness-checks each.
 * CPU-bound -- runs headless in fs-uae. Output -> SYS:copybench.log.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdarg.h>

typedef unsigned char u_char;
typedef unsigned long u_long;

extern void copy_movem(const void *src, void *dst, long len);
#if defined(__mc68040__) || defined(__mc68060__)
extern void copy_move16(const void *src, void *dst, long len);
#endif

struct Device *TimerBase;
static BPTR g_log;
static u_long eclk_freq;

static void bprintf(const char *fmt, ...)
{
	va_list ap;
	if (!g_log) return;
	va_start(ap, fmt);
	VFPrintf(g_log, (STRPTR)fmt, (APTR)ap);
	va_end(ap);
	Flush(g_log);
}

static unsigned long long eclk_delta(struct EClockVal *a, struct EClockVal *b)
{
	unsigned long long ta = ((unsigned long long)a->ev_hi << 32) | a->ev_lo;
	unsigned long long tb = ((unsigned long long)b->ev_hi << 32) | b->ev_lo;
	return tb - ta;
}

/* kind: 0=CopyMem 1=movem 2=move16. Returns MB/s*100; ~1s per measurement. */
static u_long bench(int kind, const void *src, void *dst, int len)
{
	struct EClockVal t0, t1;
	unsigned long long ticks, us, bytes;
	u_long iters = 0;

	ReadEClock(&t0);
	do {
		int k;
		for (k = 0; k < 500; k++) {
			if (kind == 0)      CopyMem((APTR)src, dst, len);
			else if (kind == 1) copy_movem(src, dst, len);
#if defined(__mc68040__) || defined(__mc68060__)
			else                copy_move16(src, dst, len);
#endif
		}
		iters += 500;
		ReadEClock(&t1);
		ticks = eclk_delta(&t0, &t1);
	} while (ticks < (unsigned long long)eclk_freq);

	us = ticks * 1000000ULL / (unsigned long long)eclk_freq;
	bytes = (unsigned long long)len * iters;
	if (us == 0) us = 1;
	return (u_long)(bytes * 100ULL / us);		/* MB/s * 100 */
}

/* Verify a candidate produces a byte-identical copy. Returns 1 on OK. */
static int verify(int kind, const u_char *src, u_char *dst, int len)
{
	int i;
	for (i = 0; i < len; i++) dst[i] = 0;
	if (kind == 0)      CopyMem((APTR)src, dst, len);
	else if (kind == 1) copy_movem(src, dst, len);
#if defined(__mc68040__) || defined(__mc68060__)
	else                copy_move16(src, dst, len);
#endif
	for (i = 0; i < len; i++) if (dst[i] != src[i]) return 0;
	return 1;
}

/* Mirror of the library ng_bcopy() decision, for correctness fuzzing. */
static void test_ngbcopy(const void *src, void *dst, long len)
{
#if defined(__mc68040__) || defined(__mc68060__)
	if (len >= 64 && ((((u_long)src | (u_long)dst) & 15UL) == 0)) {
		long done = (len >> 4) << 4;		/* 16-aligned bulk bytes */
		long tail = len - done;
		copy_move16(src, dst, done);		/* candidate copies `done` bytes */
		if (tail) CopyMem((APTR)((const char *)src + done), (char *)dst + done, tail);
		return;
	}
#endif
	CopyMem((APTR)src, dst, len);
}

static u_long g_seed = 0x9e3779b9UL;
static u_long rnd(void) { g_seed = g_seed * 1103515245UL + 12345UL; return g_seed; }

/* Fuzz ng_bcopy over every length 0..255 and every start alignment 0..15 (which
 * exercises the move16+tail path at align 0, len>=64, and the CopyMem fallback for
 * small/misaligned). Compares byte-for-byte against the source. */
static void fuzz_ngbcopy(u_char *base)
{
	int len, off, i, cases = 0, fails = 0;
	for (len = 0; len <= 255; len++) {
		for (off = 0; off < 16; off++) {
			u_char *src = base + off;
			u_char *dst = base + 2048 + off;
			for (i = 0; i < len; i++) src[i] = (u_char)(rnd() >> 9);
			for (i = 0; i < len + 4; i++) dst[i] = 0xEE;	/* guard tail */
			test_ngbcopy(src, dst, len);
			cases++;
			for (i = 0; i < len; i++) if (dst[i] != src[i]) { fails++; break; }
			if (dst[len] != 0xEE) { fails++; }		/* overran? */
		}
	}
	bprintf("ng_bcopy fuzz: %ld cases, %ld failures\n\n", (LONG)cases, (LONG)fails);
}

static void run(u_char *src, u_char *dst, int len)
{
	u_long cm, mv, m16;
	int i, ok = 1;
	for (i = 0; i < len; i++) src[i] = (u_char)(i * 7 + 1);

	ok &= verify(0, src, dst, len);
	ok &= verify(1, src, dst, len);
#if defined(__mc68040__) || defined(__mc68060__)
	ok &= verify(2, src, dst, len);
#endif
	if (!ok) { bprintf("  len %4ld: VERIFY FAILED\n", (LONG)len); return; }

	cm  = bench(0, src, dst, len);
	mv  = bench(1, src, dst, len);
	bprintf("  len %4ld :  CopyMem %3ld.%02ld  movem %3ld.%02ld (%ld.%02ldx)",
		(LONG)len, (LONG)(cm/100),(LONG)(cm%100),
		(LONG)(mv/100),(LONG)(mv%100),
		(LONG)(cm?(mv*100/cm)/100:0),(LONG)(cm?(mv*100/cm)%100:0));
#if defined(__mc68040__) || defined(__mc68060__)
	m16 = bench(2, src, dst, len);
	bprintf("  move16 %3ld.%02ld (%ld.%02ldx)",
		(LONG)(m16/100),(LONG)(m16%100),
		(LONG)(cm?(m16*100/cm)/100:0),(LONG)(cm?(m16*100/cm)%100:0));
#endif
	bprintf("   MB/s\n");
}

int main(void)
{
	struct MsgPort *mp;
	struct timerequest tr;
	APTR base;
	u_char *src, *dst;

	g_log = Open((STRPTR)"SYS:copybench.log", MODE_NEWFILE);
	bprintf("copybench starting...\n");
	base = AllocMem(16384 * 2 + 64, MEMF_ANY);
	if (!base) { bprintf("no memory\n"); goto out; }
	/* 16-byte align both buffers (best case for movem/move16). */
	src = (u_char *)(((u_long)base + 15) & ~15UL);
	dst = (u_char *)(((u_long)(src + 16384) + 15) & ~15UL);

	mp = CreateMsgPort();
	if (!mp || OpenDevice((STRPTR)"timer.device", UNIT_ECLOCK, (struct IORequest *)&tr, 0)) {
		bprintf("timer open failed\n"); goto out;
	}
	TimerBase = tr.tr_node.io_Device;
	{ struct EClockVal e; eclk_freq = ReadEClock(&e); }

	bprintf("AmiTCP_NG copy bench -- EClock %ld Hz  (16-byte-aligned, matched)\n\n", (LONG)eclk_freq);
	fuzz_ngbcopy(src);
	run(src, dst, 256);
	run(src, dst, 1472);	/* ~one MSS of payload */
	run(src, dst, 4096);
	run(src, dst, 16384);
	bprintf("\ndone\n");
	CloseDevice((struct IORequest *)&tr);
out:
	if (g_log) Close(g_log);
	return 0;
}
