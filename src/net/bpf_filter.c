/*-
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bpf.c	7.5 (Berkeley) 7/15/91
 */
/*
 * PORT (AmiTCP_NG): the Berkeley/LBL BPF filter interpreter -- the bpf_filter()
 * VM and bpf_validate() from 4.4BSD / libpcap-0.8.1 -- adapted to the AmiTCP_NG
 * kernel build. Copyright 2026 Andy Taylor (MW0MWZ), added alongside the
 * Berkeley notice above; the project is GPL v2 (see COPYING).
 *
 * Environment adaptation (this preamble):
 *   - our headers instead of the libpcap/pcap-bpf.h / config.h ones;
 *   - packet fields are ALWAYS extracted byte-wise (upstream's "LBL_ALIGN"
 *     path): the no-MMU 68000 address-errors on an unaligned 16/32-bit access,
 *     and a BPF program loads fields at arbitrary packet offsets, so the aligned
 *     pointer-cast path would crash;
 *   - bpf_int32 / bpf_u_int32 map to long / unsigned long (32-bit on m68k);
 *   - MLEN(m) is redefined below the <sys/mbuf.h> include, because our
 *     sys/mbuf.h already uses MLEN as the mbuf data-size constant.
 *
 * Security hardening (the interpreter/validator bodies are otherwise the
 * upstream algorithm). libpcap-0.8.1's bpf_validate()/bpf_filter() leave gaps
 * that are only a nuisance behind an MMU but are exploitable in this single
 * address space, so the validator -- whose whole job is to make an untrusted
 * filter safe to run in the kernel -- is tightened and the interpreter's
 * runtime bounds checks are made signed-safe:
 *   - bpf_validate() now bounds-checks the scratch-memory index for BPF_STX and
 *     BPF_LDX|BPF_MEM too (upstream checked only BPF_ST and BPF_LD|BPF_MEM,
 *     leaving mem[k] read/write unchecked -> arbitrary stack access);
 *   - BPF_JA targets must be strictly forward and in range (upstream accepted a
 *     backward/self jump -> unbounded interpreter loop);
 *   - a null, empty, or over-long (> BPF_MAXINSNS) program is rejected up front
 *     (upstream read f[len-1] with no lower bound on len);
 *   - negative load offsets are rejected in the interpreter's bounds checks and
 *     in MINDEX / m_xword / m_xhalf, closing the unsigned-comparison holes that
 *     let a negative k read before the packet buffer / mbuf data.
 *   - out-of-range shift counts are rejected: a constant count (BPF_LSH/RSH|K)
 *     of < 0 or >= 32 in bpf_validate(), and a register count (|X) of >= 32 at
 *     run time -- shifting a 32-bit value by >= 32 is undefined in C and the
 *     68000 masks the count mod 64, so an unguarded X=64 would silently no-op.
 */

#include <conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <net/bpf.h>

typedef long          bpf_int32;
typedef unsigned long bpf_u_int32;
#define int32   bpf_int32
#define u_int32 bpf_u_int32

/*
 * Byte-wise field extraction -- mandatory on the 68000, which takes an address
 * error on an unaligned word/long access. A BPF program can load a half/word at
 * any packet offset, so we never dereference a cast pointer.
 */
#define EXTRACT_SHORT(p) \
	((u_short)((u_short)*((u_char *)(p)+0)<<8 | \
	           (u_short)*((u_char *)(p)+1)))
#define EXTRACT_LONG(p) \
	((bpf_u_int32)*((u_char *)(p)+0)<<24 | \
	 (bpf_u_int32)*((u_char *)(p)+1)<<16 | \
	 (bpf_u_int32)*((u_char *)(p)+2)<<8  | \
	 (bpf_u_int32)*((u_char *)(p)+3))

#if defined(KERNEL) || defined(_KERNEL)
#include <sys/mbuf.h>
#undef MLEN
#define MLEN(m) ((m)->m_len)

/*
 * MINDEX walks the mbuf chain to the mbuf holding byte offset _k, rebasing _k
 * onto that mbuf. A negative _k is out of bounds -- a filter offset must be
 * non-negative -- so reject it here rather than dereferencing before the data;
 * the 68000 has no MMU to trap the wild read. The load cases and m_xword /
 * m_xhalf enforce the same non-negative contract before reaching packet bytes.
 */
#define MINDEX(len, _m, _k) \
{ \
	if ((_k) < 0) \
		return 0; \
	len = MLEN(m); \
	while ((_k) >= len) { \
		(_k) -= len; \
		(_m) = (_m)->m_next; \
		if ((_m) == 0) \
			return 0; \
		len = MLEN(m); \
	} \
}

static int
m_xword(m, k, err)
	register struct mbuf *m;
	register int k, *err;
{
	register int len;
	register u_char *cp, *np;
	register struct mbuf *m0;

	if (k < 0) {
		*err = 1;
		return 0;
	}
	MINDEX(len, m, k);
	cp = mtod(m, u_char *) + k;
	if (len - k >= 4) {
		*err = 0;
		return EXTRACT_LONG(cp);
	}
	m0 = m->m_next;
	if (m0 == 0 || MLEN(m0) + len - k < 4)
		goto bad;
	*err = 0;
	np = mtod(m0, u_char *);
	switch (len - k) {

	case 1:
		return (cp[0] << 24) | (np[0] << 16) | (np[1] << 8) | np[2];

	case 2:
		return (cp[0] << 24) | (cp[1] << 16) | (np[0] << 8) | np[1];

	default:
		return (cp[0] << 24) | (cp[1] << 16) | (cp[2] << 8) | np[0];
	}
    bad:
	*err = 1;
	return 0;
}

static int
m_xhalf(m, k, err)
	register struct mbuf *m;
	register int k, *err;
{
	register int len;
	register u_char *cp;
	register struct mbuf *m0;

	if (k < 0) {
		*err = 1;
		return 0;
	}
	MINDEX(len, m, k);
	cp = mtod(m, u_char *) + k;
	if (len - k >= 2) {
		*err = 0;
		return EXTRACT_SHORT(cp);
	}
	m0 = m->m_next;
	if (m0 == 0)
		goto bad;
	*err = 0;
	return (cp[0] << 8) | mtod(m0, u_char *)[0];
 bad:
	*err = 1;
	return 0;
}
#endif

/*
 * Execute the filter program starting at pc on the packet p
 * wirelen is the length of the original packet
 * buflen is the amount of data present
 * For the kernel, p is assumed to be a pointer to an mbuf if buflen is 0,
 * in all other cases, p is a pointer to a buffer and buflen is its size.
 */
u_int
bpf_filter(pc, p, wirelen, buflen)
	register struct bpf_insn *pc;
	register u_char *p;
	u_int wirelen;
	register u_int buflen;
{
	register u_int32 A, X;
	register int k;
	int32 mem[BPF_MEMWORDS];
#if defined(KERNEL) || defined(_KERNEL)
	struct mbuf *m, *n;
	int merr, len;

	if (buflen == 0) {
		m = (struct mbuf *)p;
		p = mtod(m, u_char *);
		buflen = MLEN(m);
	} else
		m = NULL;
#endif

	if (pc == 0)
		/*
		 * No filter means accept all.
		 */
		return (u_int)-1;
	A = 0;
	X = 0;
	--pc;
	while (1) {
		++pc;
		switch (pc->code) {

		default:
#if defined(KERNEL) || defined(_KERNEL)
			return 0;
#else
			abort();
#endif
		case BPF_RET|BPF_K:
			return (u_int)pc->k;

		case BPF_RET|BPF_A:
			return (u_int)A;

		case BPF_LD|BPF_W|BPF_ABS:
			k = pc->k;
			if (k < 0 || k + sizeof(int32) > buflen) {
#if defined(KERNEL) || defined(_KERNEL)
				if (m == NULL)
					return 0;
				A = m_xword(m, k, &merr);
				if (merr != 0)
					return 0;
				continue;
#else
				return 0;
#endif
			}
			A = EXTRACT_LONG(&p[k]);
			continue;

		case BPF_LD|BPF_H|BPF_ABS:
			k = pc->k;
			if (k < 0 || k + sizeof(short) > buflen) {
#if defined(KERNEL) || defined(_KERNEL)
				if (m == NULL)
					return 0;
				A = m_xhalf(m, k, &merr);
				if (merr != 0)
					return 0;
				continue;
#else
				return 0;
#endif
			}
			A = EXTRACT_SHORT(&p[k]);
			continue;

		case BPF_LD|BPF_B|BPF_ABS:
			k = pc->k;
			if (k >= buflen) {
#if defined(KERNEL) || defined(_KERNEL)
				if (m == NULL)
					return 0;
				n = m;
				MINDEX(len, n, k);
				A = mtod(n, u_char *)[k];
				continue;
#else
				return 0;
#endif
			}
			A = p[k];
			continue;

		case BPF_LD|BPF_W|BPF_LEN:
			A = wirelen;
			continue;

		case BPF_LDX|BPF_W|BPF_LEN:
			X = wirelen;
			continue;

		case BPF_LD|BPF_W|BPF_IND:
			k = X + pc->k;
			if (k < 0 || k + sizeof(int32) > buflen) {
#if defined(KERNEL) || defined(_KERNEL)
				if (m == NULL)
					return 0;
				A = m_xword(m, k, &merr);
				if (merr != 0)
					return 0;
				continue;
#else
				return 0;
#endif
			}
			A = EXTRACT_LONG(&p[k]);
			continue;

		case BPF_LD|BPF_H|BPF_IND:
			k = X + pc->k;
			if (k < 0 || k + sizeof(short) > buflen) {
#if defined(KERNEL) || defined(_KERNEL)
				if (m == NULL)
					return 0;
				A = m_xhalf(m, k, &merr);
				if (merr != 0)
					return 0;
				continue;
#else
				return 0;
#endif
			}
			A = EXTRACT_SHORT(&p[k]);
			continue;

		case BPF_LD|BPF_B|BPF_IND:
			k = X + pc->k;
			if (k >= buflen) {
#if defined(KERNEL) || defined(_KERNEL)
				if (m == NULL)
					return 0;
				n = m;
				MINDEX(len, n, k);
				A = mtod(n, u_char *)[k];
				continue;
#else
				return 0;
#endif
			}
			A = p[k];
			continue;

		case BPF_LDX|BPF_MSH|BPF_B:
			k = pc->k;
			if (k >= buflen) {
#if defined(KERNEL) || defined(_KERNEL)
				if (m == NULL)
					return 0;
				n = m;
				MINDEX(len, n, k);
				X = (mtod(n, char *)[k] & 0xf) << 2;
				continue;
#else
				return 0;
#endif
			}
			X = (p[pc->k] & 0xf) << 2;
			continue;

		case BPF_LD|BPF_IMM:
			A = pc->k;
			continue;

		case BPF_LDX|BPF_IMM:
			X = pc->k;
			continue;

		case BPF_LD|BPF_MEM:
			A = mem[pc->k];
			continue;

		case BPF_LDX|BPF_MEM:
			X = mem[pc->k];
			continue;

		case BPF_ST:
			mem[pc->k] = A;
			continue;

		case BPF_STX:
			mem[pc->k] = X;
			continue;

		case BPF_JMP|BPF_JA:
			pc += pc->k;
			continue;

		case BPF_JMP|BPF_JGT|BPF_K:
			pc += (A > pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JGE|BPF_K:
			pc += (A >= pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JEQ|BPF_K:
			pc += (A == pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JSET|BPF_K:
			pc += (A & pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JGT|BPF_X:
			pc += (A > X) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JGE|BPF_X:
			pc += (A >= X) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JEQ|BPF_X:
			pc += (A == X) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JSET|BPF_X:
			pc += (A & X) ? pc->jt : pc->jf;
			continue;

		case BPF_ALU|BPF_ADD|BPF_X:
			A += X;
			continue;

		case BPF_ALU|BPF_SUB|BPF_X:
			A -= X;
			continue;

		case BPF_ALU|BPF_MUL|BPF_X:
			A *= X;
			continue;

		case BPF_ALU|BPF_DIV|BPF_X:
			if (X == 0)
				return 0;
			A /= X;
			continue;

		case BPF_ALU|BPF_AND|BPF_X:
			A &= X;
			continue;

		case BPF_ALU|BPF_OR|BPF_X:
			A |= X;
			continue;

		case BPF_ALU|BPF_LSH|BPF_X:
			if (X >= 32)		/* >=32 is undefined in C, and the
						 * 68000 masks the count mod 64, so
						 * X=64 would silently no-op; reject
						 * like DIV|X above. */
				return 0;
			A <<= X;
			continue;

		case BPF_ALU|BPF_RSH|BPF_X:
			if (X >= 32)
				return 0;
			A >>= X;
			continue;

		case BPF_ALU|BPF_ADD|BPF_K:
			A += pc->k;
			continue;

		case BPF_ALU|BPF_SUB|BPF_K:
			A -= pc->k;
			continue;

		case BPF_ALU|BPF_MUL|BPF_K:
			A *= pc->k;
			continue;

		case BPF_ALU|BPF_DIV|BPF_K:
			A /= pc->k;
			continue;

		case BPF_ALU|BPF_AND|BPF_K:
			A &= pc->k;
			continue;

		case BPF_ALU|BPF_OR|BPF_K:
			A |= pc->k;
			continue;

		case BPF_ALU|BPF_LSH|BPF_K:
			A <<= pc->k;
			continue;

		case BPF_ALU|BPF_RSH|BPF_K:
			A >>= pc->k;
			continue;

		case BPF_ALU|BPF_NEG:
			A = -A;
			continue;

		case BPF_MISC|BPF_TAX:
			X = A;
			continue;

		case BPF_MISC|BPF_TXA:
			A = X;
			continue;
		}
	}
}


/*
 * Return true if the 'fcode' is a valid filter program.
 * The constraints are that each jump be forward and to a valid
 * code.  The code must terminate with either an accept or reject.
 * 'valid' is an array for use by the routine (it must be at least
 * 'len' bytes long).
 *
 * The kernel needs to be able to verify an application's filter code.
 * Otherwise, a bogus program could easily crash the system.
 */
int
bpf_validate(f, len)
	struct bpf_insn *f;
	int len;
{
	register int i;
	register struct bpf_insn *p;

	/*
	 * Reject a null, empty, or over-long program up front: the terminating
	 * check reads f[len-1], and the interpreter runs in a fixed instruction
	 * space (BPF_MAXINSNS). A bogus length must never reach either.
	 */
	if (f == (struct bpf_insn *)0 || len < 1 || len > BPF_MAXINSNS)
		return 0;

	for (i = 0; i < len; ++i) {
		/*
		 * Check that that jumps are forward, and within
		 * the code block.
		 */
		p = &f[i];
		if (BPF_CLASS(p->code) == BPF_JMP) {
			register int from = i + 1;

			if (BPF_OP(p->code) == BPF_JA) {
				/*
				 * BPF_JA must jump strictly forward and stay in
				 * the program: target = from + k must lie in
				 * [from, len). k is signed, so a negative or
				 * self-referencing k (e.g. -1 -> an infinite
				 * interpreter loop) must be rejected. Test it
				 * without forming from+k, so a huge k can't
				 * overflow the comparison.
				 */
				if (p->k < 0 || p->k >= (long)(len - from))
					return 0;
			}
			else if (from + p->jt >= len || from + p->jf >= len)
				return 0;
		}
		/*
		 * Check that memory operations use valid addresses.
		 */
		if ((BPF_CLASS(p->code) == BPF_ST ||
		     BPF_CLASS(p->code) == BPF_STX ||
		     ((BPF_CLASS(p->code) == BPF_LD ||
		       BPF_CLASS(p->code) == BPF_LDX) &&
		      BPF_MODE(p->code) == BPF_MEM)) &&
		    (p->k >= BPF_MEMWORDS || p->k < 0))
			return 0;
		/*
		 * Check for constant division by 0.
		 */
		if (p->code == (BPF_ALU|BPF_DIV|BPF_K) && p->k == 0)
			return 0;
		/*
		 * Reject a constant shift count that is out of range: shifting
		 * a 32-bit value by >= 32 (or a negative amount) is undefined
		 * in C and, on the 68000, is masked mod 64 in hardware.
		 */
		if ((p->code == (BPF_ALU|BPF_LSH|BPF_K) ||
		     p->code == (BPF_ALU|BPF_RSH|BPF_K)) &&
		    (p->k < 0 || p->k >= 32))
			return 0;
	}
	return BPF_CLASS(f[len - 1].code) == BPF_RET;
}
