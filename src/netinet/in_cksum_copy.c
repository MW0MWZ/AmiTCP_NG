/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * in_cksum_copy() -- copy bytes and accumulate their Internet checksum in ONE pass.
 *
 * WHY. Every byte of every packet currently crosses memory three times: the SANA
 * copy into mbufs (net/sana2copybuff.c), a full in_cksum() pass whose only product
 * is a 16-bit number (tcp_input.c, udp_usrreq.c), and the copy out to the user
 * buffer (kern/uipc_socket.c). This primitive is what lets the second pass be
 * folded into the first.
 *
 * NOTHING CALLS THIS YET, deliberately, and the receive path must not start using
 * it until four things are settled:
 *
 *   1. ip_stripoptions() (called from tcp_input.c and udp_usrreq.c BEFORE the
 *      checksum check, whenever the packet carries IP options) slides the transport
 *      header down over the option bytes with its own aligned_bcopy and adjusts
 *      m_len directly -- it never calls m_adj(), so any "invalidate on m_adj" rule
 *      silently fails to fire. Gate any fast path on iphlen == sizeof(struct ip).
 *   2. Storing a per-packet sum in struct pkthdr grows it, and MHLEN shrinks to
 *      match. The worst case (max_linkhdr 16 + tcpiphdr 40 + TCP options 40) is
 *      exactly 96, so 4 bytes of pkthdr growth leaves ZERO margin against the
 *      panic("tcphdr too big") in tcp_output.c -- which is live, as DIAGNOSTIC is
 *      defined in shipped builds.
 *   3. A validity flag in m_flags does not survive m_pullup() unless it is added to
 *      M_COPYFLAGS, which also propagates it through m_copym() and therefore the
 *      SO_REUSEPORT fan-out and ip_forward's ICMP quote. Both are content-
 *      preserving, but that must be a decision rather than an accident.
 *   4. This copy runs at INTERRUPT time. Folding checksum work into it lowers total
 *      cycles but raises time-in-interrupt, at the same call site that already
 *      produced a receive-ring re-arm livelock. That cost needs measuring, not
 *      assuming.
 *   5. CLOSED 2026-08-27, on the second attempt. Was: proven only on the HOST, not
 *      on m68k. (The first attempt reported three green CPU tiers while silently
 *      compiling the 68000 object for all three -- run-cksumbench.sh appended
 *      NG_CFLAGS, whose trailing -m68000 default beat the explicit -m$CPU earlier on
 *      the command line. Fixed by exporting NG_ARCH before sourcing ccflags.sh.)
 *      docker/bench/cksumbench.c now fuzzes THIS object (the real one, built with the
 *      library's own flags -- not a transcription pasted into the harness) on target:
 *      3888 cases on each of 68000, 68020 and 68040, sweeping length, source offset
 *      and destination offset independently so every combination of residues is hit,
 *      with the chain walked one segment at a time and (sum, odd) threaded across the
 *      calls exactly as the receive path would drive it. It checks the COPIED BYTES
 *      as well as the sum, plus a poison byte past the end -- a routine that returns
 *      the right checksum while copying wrong is the worse failure of the two. The
 *      byte check was verified non-vacuous by a negative control (flip one destination
 *      byte: the sum still matched, and only the byte check caught it).
 *
 * SRC AND DST MUST NOT OVERLAP. This is a forward copy, memcpy semantics, not
 * memmove: with dst inside [src, src+len) the second byte of a pair is read after
 * the first has already been overwritten. The intended callers copy between a
 * driver buffer and an mbuf, which never overlap -- but nothing here checks, so do
 * not assume memmove-like safety.
 *
 * A wrong checksum does not make the stack slow, it makes it accept corrupt data.
 * So this earns its place by being provably equal to in_cksum() before anything
 * depends on it.
 *
 * THE PART THAT IS EASY TO GET WRONG: PARITY.
 *
 * A one's-complement Internet checksum is position-dependent. The bytes are summed
 * as 16-bit big-endian words counted from the START of the checksummed region, so a
 * byte's contribution depends on whether its offset from that start is even or odd.
 * When the accumulation is split across several calls -- one per mbuf, which is
 * exactly how the receive path walks a chain -- a call that begins at an odd offset
 * contributes to the OTHER half of its word than the same bytes would at an even
 * offset.
 *
 * `*odd` carries that state across calls: it means "the running total already has an
 * unpaired byte in the high half, and the next byte completes that word". Callers
 * must initialise it to 0 alongside sum = 0 and pass the same pointer through the
 * whole chain.
 *
 * (in_cksum_asm.S solves the same problem with its SPAN register. Its BSWAP handling
 * and the trick of keeping the X flag alive across `dbf` are 68000 HARDWARE
 * concerns -- odd-address word reads, and preserving the CPU carry in hand-written
 * assembly. Neither has a counterpart here: this uses a wide accumulator and folds
 * explicitly, so do not go looking for them.)
 *
 * Returns the updated running sum, NOT folded and NOT complemented -- fold once at
 * the end of the chain, exactly as in_cksum() does.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>

#include <netinet/in_cksum_copy_protos.h>

u_long
in_cksum_copy(src, dst, len, sum, odd)
	const void *src;
	void *dst;
	u_long len;
	u_long sum;
	int *odd;
{
	register const u_char *s = (const u_char *)src;
	register u_char *d = (u_char *)dst;
	register u_long n = len;

	if (n == 0)
		return (sum);

	/*
	 * An unpaired byte is outstanding from a previous call: this byte is the
	 * LOW half of that word. Take it first, which also restores even parity
	 * for everything after it.
	 */
	if (*odd) {
		sum += (u_long)*s;
		*d++ = *s++;
		n--;
		*odd = 0;
	}

	/*
	 * Body. Byte-at-a-time on purpose.
	 *
	 * A word-at-a-time loop would be faster, but only where BOTH pointers are
	 * even -- and on a 68000 an odd-address word access is not merely slow, it
	 * is an Address Error exception. The two pointers here are a driver-owned
	 * buffer and an mbuf, whose relative alignment nothing guarantees, so a
	 * word loop would need a run-time test per call and a byte fallback anyway.
	 * Get it CORRECT first; in_cksum_asm.S is where the speed lives, and the
	 * fuzz harness is what will let an assembly version be trusted later.
	 */
	while (n >= 2) {
		sum += ((u_long)s[0] << 8) | (u_long)s[1];
		d[0] = s[0];
		d[1] = s[1];
		s += 2; d += 2; n -= 2;
	}

	/*
	 * A trailing byte is the HIGH half of a word that the NEXT call completes.
	 * Record that so the next call knows its parity.
	 */
	if (n) {
		sum += (u_long)*s << 8;
		*d = *s;
		*odd = 1;
	}

	/* Fold the carries out periodically so a long chain cannot overflow. */
	while (sum >> 16)
		sum = (sum & 0xffffUL) + (sum >> 16);

	return (sum);
}

/*
 * Sum a flat run of bytes into a running total, big-endian 16-bit words, unfolded.
 *
 * Used by the receive consumers for the pseudo-header: the tcpiphdr/ipovly overlay
 * has already been rewritten in place (next/prev/x1 zeroed, length set), so summing
 * its first 20 bytes IS the pseudo-header contribution -- the leading zeros add
 * nothing, leaving protocol, length and the two addresses.
 *
 * Deliberately NOT expressed as in_cksum() over the same bytes: that returns a folded
 * and COMPLEMENTED value, and un-complementing an intermediate is where one's
 * complement's two representations of zero start to matter. Keeping every partial sum
 * raw, and folding exactly once at the end, avoids the question entirely.
 */
u_long
in_cksum_words(buf, len, sum)
	const void *buf;
	u_long len;
	u_long sum;
{
	register const u_char *p = (const u_char *)buf;
	register u_long n = len;

	while (n >= 2) {
		sum += ((u_long)p[0] << 8) | (u_long)p[1];
		p += 2; n -= 2;
	}
	if (n)
		sum += (u_long)*p << 8;		/* odd tail: high half of its word */

	while (sum >> 16)
		sum = (sum & 0xffffUL) + (sum >> 16);
	return (sum);
}

/*
 * Fold a running sum into the 16-bit value that goes on the wire.
 * Separate from the accumulation so a chain is folded exactly once, at the end.
 */
u_short
in_cksum_fold(sum)
	u_long sum;
{
	while (sum >> 16)
		sum = (sum & 0xffffUL) + (sum >> 16);
	return ((u_short)(~sum & 0xffffUL));
}
