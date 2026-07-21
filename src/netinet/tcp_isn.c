/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * Randomised TCP initial sequence numbers (RFC 6528).
 *
 * The stock 4.4BSD scheme derives every connection's ISN from one global,
 * near-linear counter (tcp_iss), which makes ISNs predictable and opens the door
 * to off-path spoofing / blind connection injection. RFC 6528 fixes this:
 *
 *     ISN = M + F(local_ip, local_port, remote_ip, remote_port, secret)
 *
 * M is kept as the existing tcp_iss counter (monotonic and time-driven, so the
 * per-tuple sequence ordering that TIME_WAIT reincarnation relies on is
 * preserved). F is a KEYED hash of the connection 4-tuple with a per-boot secret,
 * so ISNs are unpredictable across connections without knowing that secret.
 *
 * F here is HalfSipHash-1-3: a 32-bit keyed pseudo-random function using only
 * add / rotate / xor -- NO multiplies, so it is cheap on a 68000 -- that, unlike
 * an ad-hoc mix, does not leak its key to an attacker who harvests many outputs.
 * RFC 6528's reference uses MD5; on this small 68k machine a lighter keyed hash is
 * the right trade: some randomisation is far better than a counter starting at 1.
 *
 * The secret is seeded once at tcp_init() from ng_gather_entropy() (in
 * kern/amiga_time.c), a BEST-EFFORT boot seed -- this machine has no hardware RNG
 * and often no real clock, so the entropy is limited. The security benefit is
 * bounded by that entropy, but it is real and varies per boot and per machine.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/route.h>		/* struct route -- needed by netinet/in_pcb.h */
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>		/* struct ip -- needed by netinet/in_pcb.h */
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

/* Per-boot 64-bit secret key for the ISN PRF. All-zero until tcp_isn_init(). */
static u_char tcp_isn_secret[8];

/* Assemble a 32-bit word from 4 bytes, low byte first. Endianness-independent
 * (this is an internal PRF -- only self-consistency within a boot matters). */
#define LD32(p) ((u_long)(p)[0] | ((u_long)(p)[1] << 8) | \
		 ((u_long)(p)[2] << 16) | ((u_long)(p)[3] << 24))

#define ROTL32(x, b) ((u_long)(((x) << (b)) | ((u_long)(x) >> (32 - (b)))))

#define HSIPROUND \
	do { \
		v0 += v1; v1 = ROTL32(v1, 5);  v1 ^= v0; v0 = ROTL32(v0, 16); \
		v2 += v3; v3 = ROTL32(v3, 8);  v3 ^= v2; \
		v0 += v3; v3 = ROTL32(v3, 7);  v3 ^= v0; \
		v2 += v1; v1 = ROTL32(v1, 13); v1 ^= v2; v2 = ROTL32(v2, 16); \
	} while (0)

/*
 * HalfSipHash-1-3 (1 compression round per word, 3 finalisation rounds), 32-bit
 * output. `key` is 8 bytes; returns a keyed 32-bit hash of in[0..inlen).
 */
static u_long
halfsiphash13(const u_char *in, u_long inlen, const u_char *key)
{
	u_long k0 = LD32(key);
	u_long k1 = LD32(key + 4);
	u_long v0 = k0;
	u_long v1 = k1;
	u_long v2 = k0 ^ 0x6c796765UL;
	u_long v3 = k1 ^ 0x74656462UL;
	u_long b = inlen << 24;
	u_long m;
	const u_char *end = in + (inlen - (inlen & 3));
	int left = (int)(inlen & 3);

	for (; in != end; in += 4) {
		m = LD32(in);
		v3 ^= m;
		HSIPROUND;			/* cROUNDS = 1 */
		v0 ^= m;
	}
	switch (left) {				/* trailing 0..3 bytes fold into b */
	case 3: b |= (u_long)in[2] << 16;	/* FALLTHROUGH */
	case 2: b |= (u_long)in[1] << 8;	/* FALLTHROUGH */
	case 1: b |= (u_long)in[0];		break;
	}
	v3 ^= b;
	HSIPROUND;				/* cROUNDS = 1 */
	v0 ^= b;
	v2 ^= 0xff;
	HSIPROUND;				/* dROUNDS = 3 */
	HSIPROUND;
	HSIPROUND;
	return v1 ^ v3;
}

/*
 * Seed the per-boot ISN secret. Called once from tcp_init().
 */
void
tcp_isn_init(void)
{
	extern void ng_gather_entropy(u_char *buf, int len);

	ng_gather_entropy(tcp_isn_secret, (int)sizeof(tcp_isn_secret));
}

/*
 * RFC 6528 randomised ISN for a connection whose 4-tuple is already set on
 * tp->t_inpcb (true at both the passive- and active-open call sites). Returns
 * tcp_iss (the monotonic base M) plus a keyed hash of the 4-tuple, so ISNs still
 * advance over time yet are unpredictable between different connections.
 */
tcp_seq
tcp_new_isn(struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;
	u_char buf[12];
	u_long h;

	bcopy((caddr_t)&inp->inp_laddr.s_addr, (caddr_t)&buf[0], 4);
	bcopy((caddr_t)&inp->inp_lport,        (caddr_t)&buf[4], 2);
	bcopy((caddr_t)&inp->inp_faddr.s_addr, (caddr_t)&buf[6], 4);
	bcopy((caddr_t)&inp->inp_fport,        (caddr_t)&buf[10], 2);

	h = halfsiphash13(buf, (u_long)sizeof(buf), tcp_isn_secret);
	return (tcp_seq)((u_long)tcp_iss + h);
}
