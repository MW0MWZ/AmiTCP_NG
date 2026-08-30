/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * cksumbench -- correctness + speed harness for the 68k assembly Internet checksum
 * (src/netinet/in_cksum_asm.S), run in the emulator. It is CPU-bound (no network), so
 * it gives a real before/after even though throughput can't be measured under SLIRP.
 *
 *   CORRECTNESS: for a fuzz of lengths, start alignments, and mbuf-chain split points
 *   (including odd splits that exercise the word-spanning path), it compares the ASM
 *   in_cksum() and a faithful copy of the portable C in_cksum() against an independent,
 *   obviously-correct reference. Any single mismatch fails the run.
 *
 *   SPEED: it times the ASM vs the C version over a representative buffer and reports
 *   MB/s for each plus the speed-up.
 *
 * Output goes to SYS:cksumbench.log (the headless emulator captures files, not stdout).
 * Link against the SAME in_cksum_asm.o the library ships.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdarg.h>

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned long  u_long;

/* Minimal mbuf mirroring the field offsets the asm reads: m_next@0, m_len@8,
 * m_data@12 (see src/sys/mbuf.h struct m_hdr). */
struct tmbuf {
	struct tmbuf *m_next;	/* 0  */
	void         *m_nextpkt;	/* 4  */
	int           m_len;	/* 8  */
	char         *m_data;	/* 12 */
	short         m_type;	/* 16 */
	short         m_flags;	/* 18 */
};

/* The routine under test (assembly), and a faithful copy of the portable C version. */
extern int in_cksum(struct tmbuf *m, int len);
static int cksum_c(struct tmbuf *m, int len);

/*
 * The fused copy+checksum primitive, linked from the REAL src/netinet/in_cksum_copy.c
 * object -- not a transcription of it. That distinction is the point: this closes
 * blocker 5 in that file's own header, which says it "has been proven equal to an
 * independent RFC 1071 reference ... but on the HOST, not on m68k", and asks for
 * exactly this harness to be extended before anything relies on it.
 *
 * What m68k can break that the host cannot: the C is written byte-at-a-time precisely
 * because an odd-address word access on a 68000 is an Address Error, and the shifts
 * and the wide accumulator behave differently under a 16-bit-bus codegen than under
 * the host compiler. None of that is exercised by a host run.
 */
extern u_long in_cksum_copy(const void *src, void *dst, u_long len, u_long sum, int *odd);
extern u_short in_cksum_fold(u_long sum);

/* The assembly under test, same contract, linked from src/netinet/in_cksum_copy_asm.S. */
extern u_long in_cksum_copy_asm(const void *src, void *dst, u_long len, u_long sum, int *odd);

struct Device *TimerBase;

/* ---- logging ------------------------------------------------------------------ */
static BPTR g_log;
static void bprintf(const char *fmt, ...)
{
	va_list ap;
	if (!g_log) return;
	va_start(ap, fmt);
	VFPrintf(g_log, (STRPTR)fmt, (APTR)ap);	/* va_list -> RawDoFmt-style LONG stream */
	va_end(ap);
	Flush(g_log);
}

/* ---- a simple, independent reference checksum over a flat buffer --------------- */
static int ref_cksum(const u_char *p, int len)
{
	u_long sum = 0;
	int i;
	for (i = 0; i + 1 < len; i += 2)
		sum += ((u_long)p[i] << 8) | p[i + 1];	/* network byte order */
	if (i < len)
		sum += (u_long)p[i] << 8;		/* odd trailing byte */
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (int)((~sum) & 0xffff);
}

/* ---- faithful copy of the portable C in_cksum() (the speed baseline) ----------- */
#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; ADDCARRY(sum);}
static int cksum_c(struct tmbuf *m, int len)
{
	register u_short *w;
	register int sum = 0;
	register int mlen = 0;
	int byte_swapped = 0;
	union { char c[2]; u_short s; } s_util;
	union { u_short s[2]; long l; } l_util;

	for (; m && len; m = m->m_next) {
		if (m->m_len == 0) continue;
		w = (u_short *)m->m_data;
		if (mlen == -1) {
			s_util.c[1] = *(char *)w;
			sum += s_util.s;
			w = (u_short *)((char *)w + 1);
			mlen = m->m_len - 1; len--;
		} else
			mlen = m->m_len;
		if (len < mlen) mlen = len;
		len -= mlen;
		if ((1 & (int)w) && (mlen > 0)) {
			REDUCE; sum <<= 8;
			s_util.c[0] = *(u_char *)w;
			w = (u_short *)((char *)w + 1);
			mlen--; byte_swapped = 1;
		}
		while ((mlen -= 32) >= 0) {
			sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
			sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
			sum += w[8]; sum += w[9]; sum += w[10]; sum += w[11];
			sum += w[12]; sum += w[13]; sum += w[14]; sum += w[15];
			w += 16;
		}
		mlen += 32;
		while ((mlen -= 8) >= 0) {
			sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
			w += 4;
		}
		mlen += 8;
		if (mlen == 0 && byte_swapped == 0) continue;
		REDUCE;
		while ((mlen -= 2) >= 0) sum += *w++;
		if (byte_swapped) {
			REDUCE; sum <<= 8; byte_swapped = 0;
			if (mlen == -1) { s_util.c[1] = *(char *)w; sum += s_util.s; mlen = 0; }
			else mlen = -1;
		} else if (mlen == -1)
			s_util.c[0] = *(char *)w;
	}
	if (mlen == -1) {		/* trailing odd byte of the last mbuf */
		s_util.c[1] = 0;
		sum += s_util.s;
	}
	REDUCE;
	return (~sum & 0xffff);
}

/* ---- deterministic PRNG (reproducible fuzz) ----------------------------------- */
static u_long g_seed = 0x1234abcdUL;
static u_long rnd(void) { g_seed = g_seed * 1103515245UL + 12345UL; return g_seed; }

/* ---- correctness fuzz ---------------------------------------------------------- */
/* Big enough for the boundary cases in fuzz_copy_large(): the asm hands anything
 * >= 0x10000 to its byte loop, and nothing else in this harness reaches that. */
#define BUFSZ 65560
static u_char *g_buf;		/* payload area, over-allocated for alignment shifts */
static struct tmbuf g_mb[64];

/* Build a chain over g_buf+off for `len` bytes, split into up to `nsplit` pieces at
 * the given cut points; return the head. Cuts may be odd (spanning path). */
static struct tmbuf *build_chain(int off, int len, const int *cuts, int ncuts)
{
	int i, pos = 0, seg = 0;
	for (i = 0; i <= ncuts; i++) {
		int end = (i < ncuts) ? cuts[i] : len;
		if (end < pos) end = pos;
		if (end > len) end = len;
		g_mb[seg].m_next = 0;
		g_mb[seg].m_len  = end - pos;
		g_mb[seg].m_data = (char *)(g_buf + off + pos);
		if (seg > 0) g_mb[seg - 1].m_next = &g_mb[seg];
		pos = end; seg++;
		if (seg >= 63) break;
	}
	return &g_mb[0];
}

static int fuzz(void)
{
	int fails = 0, cases = 0;
	int len, trial, off;

	/* Exhaustive up to 128 then sampled to 512 -- enough to exercise every path
	 * (setup, all 4 alignments, word-spanning, several 32-byte blocks, and every
	 * 32/8/4/2/1-byte remainder). The main loop is length-independent beyond this. */
	for (len = 0; len <= 512; len++) {
		if (len > 128 && (len & 15) != 0 && (rnd() & 3) != 0) continue;
		for (off = 0; off <= 3; off++) {		/* start alignment */
			int i, want, gotA, gotC;
			int cuts[8], ncuts;

			for (i = 0; i < len; i++)
				g_buf[off + i] = (u_char)(rnd() >> 13);
			want = ref_cksum(g_buf + off, len);

			for (trial = 0; trial < 4; trial++) {
				struct tmbuf *m;
				ncuts = 0;
				if (trial == 1 && len > 1)		/* single odd split */
					cuts[ncuts++] = 1 + (int)(rnd() % (u_long)len);
				else if (trial == 2 && len > 3) {	/* several splits, some odd */
					int c = (int)(rnd() % (u_long)len);
					cuts[ncuts++] = c;
					cuts[ncuts++] = c + 1 <= len ? c + 1 : len;	/* force an odd/1-byte seg */
					if (len > 8) cuts[ncuts++] = len - 3;
				} else if (trial == 3 && len > 5) {	/* byte-per-mbuf near the end */
					cuts[ncuts++] = len - 3;
					cuts[ncuts++] = len - 2;
					cuts[ncuts++] = len - 1;
				}
				m = build_chain(off, len, cuts, ncuts);
				gotA = in_cksum(m, len);
				gotC = cksum_c(m, len);
				cases++;
				if (gotA != want || gotC != want) {
					if (fails < 12)
						bprintf("  MISMATCH len=%ld off=%ld trial=%ld  ref=%04lx asm=%04lx c=%04lx\n",
						     (LONG)len, (LONG)off, (LONG)trial,
						     (LONG)(want & 0xffff), (LONG)(gotA & 0xffff), (LONG)(gotC & 0xffff));
					fails++;
				}
			}
		}
	}
	bprintf("  fuzz: %ld cases, %ld mismatches\n", (LONG)cases, (LONG)fails);
	return fails;
}

/* ---- correctness fuzz for in_cksum_copy() -------------------------------------- */
/*
 * Two things must hold, and the second is the one that is easy to forget: the SUM must
 * match the independent reference, AND the DESTINATION BYTES must match the source. A
 * routine that returns the right checksum while copying wrong is the worse failure of
 * the two -- it corrupts data and passes every checksum test.
 *
 * The chain is walked one segment at a time with `sum` and `odd` threaded across the
 * calls, which is exactly how the receive path would drive it (one call per destination
 * mbuf). That threading, and the parity state behind it, is the part most likely to be
 * wrong: a segment that starts at an odd offset contributes to the other half of its
 * 16-bit word than the same bytes would at an even offset.
 *
 * Destination offsets are swept independently of source offsets, so src and dst are
 * deliberately given every combination of residues -- including the ones where they
 * differ in parity. src and dst never overlap (separate buffers): in_cksum_copy() is
 * documented memcpy, NOT memmove, and testing it as if overlap were allowed would be
 * testing a contract it does not offer.
 */
static u_char *g_dst;			/* destination for the C oracle, never aliased  */
static u_char *g_dst2;			/* destination for the assembly, never aliased  */

static int fuzz_copy(void)
{
	int fails = 0, cases = 0;
	int len, soff, doff;

	for (len = 0; len <= 512; len++) {
		if (len > 128 && (len & 15) != 0 && (rnd() & 3) != 0) continue;
		for (soff = 0; soff <= 3; soff++) {
			for (doff = 0; doff <= 3; doff++) {
				int i, want, ncuts, cuts[8], pos, seg, bad;
				u_long sum, asum;
				int odd, aodd;

				for (i = 0; i < len; i++)
					g_buf[soff + i] = (u_char)(rnd() >> 13);
				for (i = 0; i < len + 8; i++)
					g_dst[i] = g_dst2[i] = 0xA5;	/* poison: catches short copies */
				want = ref_cksum(g_buf + soff, len);

				/* Split into segments, deliberately including odd-length ones so the
				 * cross-call parity state is exercised rather than assumed.
				 *
				 * The cuts[0] / cuts[0]+1 pair forces a ONE-BYTE segment at a
				 * randomised position for every len > 3 -- the RNG picks where it
				 * lands, never whether it exists, so the odd-length path is covered
				 * by construction rather than by luck.
				 *
				 * The repeated cut then forces a ZERO-LENGTH segment immediately
				 * after it. That is the case that actually bites: in_cksum_copy()
				 * returns early on n == 0, BEFORE touching *odd, so an empty segment
				 * must neither complete nor clear a pending unpaired byte. Whether a
				 * byte is pending there depends on cuts[0]'s parity, which varies
				 * across the sweep, so both sub-cases get hit. Without this the case
				 * only occurred incidentally, when the RNG happened to place cuts[0]
				 * after the fixed len-3 cut. */
				ncuts = 0;
				if (len > 3) {
					cuts[ncuts++] = (int)(rnd() % (u_long)len);
					if (cuts[0] + 1 <= len) {
						cuts[ncuts++] = cuts[0] + 1;
						cuts[ncuts++] = cuts[0] + 1;	/* zero-length segment */
					}
				}
				if (len > 8) cuts[ncuts++] = len - 3;

				/* Drive BOTH implementations over the identical segment walk. */
				sum = 0; odd = 0; pos = 0;
				for (seg = 0; seg <= ncuts; seg++) {
					int end = (seg < ncuts) ? cuts[seg] : len;
					if (end < pos) end = pos;
					if (end > len) end = len;
					sum = in_cksum_copy(g_buf + soff + pos, g_dst + doff + pos,
							    (u_long)(end - pos), sum, &odd);
					pos = end;
				}
				asum = 0; aodd = 0; pos = 0;
				for (seg = 0; seg <= ncuts; seg++) {
					int end = (seg < ncuts) ? cuts[seg] : len;
					if (end < pos) end = pos;
					if (end > len) end = len;
					asum = in_cksum_copy_asm(g_buf + soff + pos, g_dst2 + doff + pos,
								 (u_long)(end - pos), asum, &aodd);
					pos = end;
				}

				cases++;
				bad = 0;
				/* --- the C oracle against the independent reference --- */
				if ((int)in_cksum_fold(sum) != want) bad = 1;
				for (i = 0; i < len; i++)
					if (g_dst[doff + i] != g_buf[soff + i]) { bad = 2; break; }
				/* The byte immediately past the copy must still be poison. */
				if (!bad && g_dst[doff + len] != 0xA5) bad = 3;
				/* --- the assembly against the same reference --- */
				if (!bad && (int)in_cksum_fold(asum) != want) bad = 4;
				if (!bad)
					for (i = 0; i < len; i++)
						if (g_dst2[doff + i] != g_buf[soff + i]) { bad = 5; break; }
				if (!bad && g_dst2[doff + len] != 0xA5) bad = 6;
				/* --- and against each other, including the parity state ---
				 * Stronger than "both match the reference": the running sum is
				 * chained across calls, so an implementation could agree on the
				 * final folded value while carrying different intermediate state.
				 * Requiring the raw sum AND *odd to agree catches that. */
				if (!bad && asum != sum) bad = 7;
				if (!bad && aodd != odd)  bad = 8;

				if (bad) {
					if (fails < 12)
						bprintf("  COPY MISMATCH len=%ld soff=%ld doff=%ld kind=%ld "
							"ref=%04lx c=%04lx asm=%04lx\n",
							(LONG)len, (LONG)soff, (LONG)doff, (LONG)bad,
							(LONG)(want & 0xffff),
							(LONG)(in_cksum_fold(sum) & 0xffff),
							(LONG)(in_cksum_fold(asum) & 0xffff));
					fails++;
				}
			}
		}
	}
	bprintf("  copy fuzz: %ld cases, %ld failures  (C:1=sum 2=bytes 3=ovr  ASM:4=sum 5=bytes 6=ovr  7=sum!=C 8=odd!=C)\n",
		(LONG)cases, (LONG)fails);
	return fails;
}

/* ---- boundary fuzz: the length at which the asm changes strategy --------------- */
/*
 * in_cksum_copy_asm.S hands any length >= 0x10000 to its byte loop, because `dbf`'s
 * counter is 16-bit -- and, less obviously, because that bound is also what keeps the
 * accumulator small enough that the routine's un-chained trailing `add.l`s cannot
 * overflow 32 bits. The main sweep above stops at 512, so WITHOUT THIS that fallback
 * has never once executed on target and the exact boundary has never been crossed.
 * A hand argument that a branch is safe is not the same as having run it.
 *
 * Single-segment only: this is about the length boundary, not the chaining, which the
 * main sweep already covers thoroughly.
 */
static int fuzz_copy_large(void)
{
	static const long lens[5] = { 65533, 65534, 65535, 65536, 65537 };
	int fails = 0, cases = 0;
	int li, soff, doff;

	for (li = 0; li < 5; li++) {
		int len = (int)lens[li];
		for (soff = 0; soff <= 1; soff++) {
			for (doff = 0; doff <= 1; doff++) {
				u_long sum, asum;
				int odd = 0, aodd = 0, want, i, bad;

				for (i = 0; i < len; i++)
					g_buf[soff + i] = (u_char)(rnd() >> 13);
				g_dst[doff + len] = g_dst2[doff + len] = 0xA5;
				want = ref_cksum(g_buf + soff, len);

				sum  = in_cksum_copy(g_buf + soff, g_dst + doff,
						     (u_long)len, 0UL, &odd);
				asum = in_cksum_copy_asm(g_buf + soff, g_dst2 + doff,
							 (u_long)len, 0UL, &aodd);

				cases++;
				bad = 0;
				if ((int)in_cksum_fold(sum)  != want) bad = 1;
				if (!bad && (int)in_cksum_fold(asum) != want) bad = 4;
				if (!bad)
					for (i = 0; i < len; i++)
						if (g_dst2[doff + i] != g_buf[soff + i]) { bad = 5; break; }
				if (!bad && g_dst2[doff + len] != 0xA5) bad = 6;
				if (!bad && asum != sum) bad = 7;
				if (!bad && aodd != odd)  bad = 8;

				if (bad) {
					bprintf("  LARGE MISMATCH len=%ld soff=%ld doff=%ld kind=%ld "
						"ref=%04lx c=%04lx asm=%04lx\n",
						(LONG)len, (LONG)soff, (LONG)doff, (LONG)bad,
						(LONG)(want & 0xffff),
						(LONG)(in_cksum_fold(sum) & 0xffff),
						(LONG)(in_cksum_fold(asum) & 0xffff));
					fails++;
				}
			}
		}
	}
	bprintf("  large-length fuzz: %ld cases, %ld failures  (crosses the 0x10000 boundary)\n",
		(LONG)cases, (LONG)fails);
	return fails;
}

/* ---- timing (ReadEClock, auto-scaled to ~2s per measurement) ------------------- */
static u_long eclk_freq;
static void eclk_read(struct EClockVal *e) { ReadEClock(e); }
static unsigned long long eclk_delta(struct EClockVal *a, struct EClockVal *b)
{
	unsigned long long ta = ((unsigned long long)a->ev_hi << 32) | a->ev_lo;
	unsigned long long tb = ((unsigned long long)b->ev_hi << 32) | b->ev_lo;
	return tb - ta;
}

/* Time fn over one mbuf `m` of `len` bytes; return MB/s * 100. */
static u_long bench_one(int use_asm, struct tmbuf *m, int len)
{
	struct EClockVal t0, t1;
	unsigned long long ticks, us, bytes;
	u_long iters = 0;
	volatile int sink = 0;

	eclk_read(&t0);
	do {
		int k;
		if (use_asm) for (k = 0; k < 2000; k++) sink += in_cksum(m, len);
		else         for (k = 0; k < 2000; k++) sink += cksum_c(m, len);
		iters += 2000;
		eclk_read(&t1);
		ticks = eclk_delta(&t0, &t1);
	} while (ticks < (unsigned long long)eclk_freq);	/* ~1 second */

	us = ticks * 1000000ULL / (unsigned long long)eclk_freq;
	bytes = (unsigned long long)len * iters;
	if (us == 0) us = 1;
	return (u_long)(bytes * 100ULL / us);			/* MB/s * 100 (bytes/us == MB/s) */
}

static void bench_len(int len)
{
	struct tmbuf m;
	u_long a, c;
	int i;
	for (i = 0; i < len; i++) g_buf[i] = (u_char)(rnd() >> 11);
	m.m_next = 0; m.m_len = len; m.m_data = (char *)g_buf;
	c = bench_one(0, &m, len);
	a = bench_one(1, &m, len);
	bprintf("  len %4ld :  C %3ld.%02ld MB/s   ASM %3ld.%02ld MB/s   speed-up %ld.%02ldx\n",
	     (LONG)len,
	     (LONG)(c / 100), (LONG)(c % 100),
	     (LONG)(a / 100), (LONG)(a % 100),
	     (LONG)(c ? (a * 100 / c) / 100 : 0), (LONG)(c ? (a * 100 / c) % 100 : 0));
}

/*
 * ---- the question this whole exercise exists to answer -------------------------
 *
 * TODAY the receive path does TWO passes over the payload: CopyMem() into the mbuf,
 * then a separate in_cksum() read of it. The fused primitive does ONE. Time the two
 * arrangements against each other, doing the same total work, on the same buffers.
 *
 * Both legs are timed with the SAME loop shape and the same auto-scaling, so the
 * comparison is like-for-like rather than one leg carrying different overhead. The
 * `sink` reads keep the optimiser from discarding either result.
 */
static u_long bench_copy_one(int fused, int len)
{
	struct EClockVal t0, t1;
	unsigned long long ticks, us, bytes;
	u_long iters = 0;
	volatile u_long sink = 0;
	struct tmbuf m;
	int odd;

	m.m_next = 0; m.m_len = len; m.m_data = (char *)g_dst;

	eclk_read(&t0);
	do {
		int k;
		if (fused)
			for (k = 0; k < 200; k++) {
				odd = 0;
				sink += in_cksum_copy_asm(g_buf, g_dst, (u_long)len, 0UL, &odd);
			}
		else
			for (k = 0; k < 200; k++) {	/* today: CopyMem, then a second pass */
				CopyMem((APTR)g_buf, (APTR)g_dst, (ULONG)len);
				sink += (u_long)in_cksum(&m, len);
			}
		iters += 200;
		eclk_read(&t1);
		ticks = eclk_delta(&t0, &t1);
	} while (ticks < (unsigned long long)eclk_freq);

	us = ticks * 1000000ULL / (unsigned long long)eclk_freq;
	bytes = (unsigned long long)len * iters;
	if (us == 0) us = 1;
	return (u_long)(bytes * 100ULL / us);
}

static void bench_copy(int len)
{
	u_long two, one;
	int i;
	for (i = 0; i < len; i++) g_buf[i] = (u_char)(rnd() >> 11);
	two = bench_copy_one(0, len);
	one = bench_copy_one(1, len);
	bprintf("  len %4ld :  copy+cksum %3ld.%02ld MB/s   fused %3ld.%02ld MB/s   %ld.%02ldx\n",
	     (LONG)len,
	     (LONG)(two / 100), (LONG)(two % 100),
	     (LONG)(one / 100), (LONG)(one % 100),
	     (LONG)(two ? (one * 100 / two) / 100 : 0),
	     (LONG)(two ? (one * 100 / two) % 100 : 0));
}

int main(void)
{
	struct MsgPort *mp;
	struct timerequest tr;
	int rc = 20;

	g_log = Open((STRPTR)"SYS:cksumbench.log", MODE_NEWFILE);
	bprintf("cksumbench starting...\n");
	g_buf = (u_char *)AllocMem(BUFSZ + 8, MEMF_ANY);
	if (!g_buf) { bprintf("no memory\n"); goto out; }
	g_dst = (u_char *)AllocMem(BUFSZ + 8, MEMF_ANY);	/* separate: never aliases g_buf */
	if (!g_dst) { bprintf("no memory\n"); goto out; }
	g_dst2 = (u_char *)AllocMem(BUFSZ + 8, MEMF_ANY);
	if (!g_dst2) { bprintf("no memory\n"); goto out; }

	mp = CreateMsgPort();
	if (!mp) { bprintf("no msgport\n"); goto out; }
	if (OpenDevice((STRPTR)"timer.device", UNIT_ECLOCK, (struct IORequest *)&tr, 0)) {
		bprintf("timer.device open failed\n"); goto out;
	}
	TimerBase = tr.tr_node.io_Device;
	{ struct EClockVal e; eclk_freq = ReadEClock(&e); }	/* E-clock frequency (Hz) */

	bprintf("AmiTCP_NG checksum bench -- EClock %ld Hz\n\n", (LONG)eclk_freq);

	bprintf("Correctness (asm & C vs independent reference):\n");
	if (fuzz() != 0) { bprintf("\nFAILED: checksum mismatch -- asm is NOT safe to ship.\n"); rc = 20; }
	else {
		bprintf("  PASS: asm is byte-for-byte identical to the reference.\n\n");

		bprintf("Correctness (in_cksum_copy C + ASM vs independent reference, ON TARGET):\n");
		if (fuzz_copy() != 0) {
			bprintf("\nFAILED: in_cksum_copy is NOT safe to build on.\n");
			rc = 20;
			goto closedev;
		}
		bprintf("  PASS: sums match and every byte copied correctly.\n");
		if (fuzz_copy_large() != 0) {
			bprintf("\nFAILED: in_cksum_copy disagrees at the 0x10000 boundary.\n");
			rc = 20;
			goto closedev;
		}
		bprintf("  PASS: the >= 0x10000 byte-loop fallback agrees too.\n\n");

		bprintf("Speed (single mbuf, warm buffer):\n");
		bench_len(40);		/* bare TCP/IP header (ACK-sized)  */
		bench_len(576);		/* common path MTU                 */
		bench_len(1460);	/* full-MSS data segment           */

		bprintf("\nRX copy+checksum: today's two passes vs one fused pass:\n");
		bench_copy(40);
		bench_copy(576);
		bench_copy(1460);
		rc = 0;
	}

closedev:

	CloseDevice((struct IORequest *)&tr);
out:
	if (g_buf) FreeMem(g_buf, BUFSZ + 8);
	if (g_dst) FreeMem(g_dst, BUFSZ + 8);
	if (g_dst2) FreeMem(g_dst2, BUFSZ + 8);
	if (g_log) { bprintf("\ndone (rc=%ld)\n", (LONG)rc); Close(g_log); }
	return rc;
}
