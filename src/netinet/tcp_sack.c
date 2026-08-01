/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * RFC 2018 selective acknowledgement -- RECEIVER scoreboard.
 *
 * Stock 4.4BSD acknowledges only a cumulative sequence number, so when a peer's
 * stream loses more than one segment per window (routine on a lossy WiFi link)
 * it either stalls to a retransmit timeout or resends data we already hold. SACK
 * lets us tell the peer EXACTLY which out-of-order blocks we have above rcv_nxt,
 * so it retransmits only the holes.
 *
 * This file maintains that receiver-side block list. It mirrors the out-of-order
 * reassembly queue (netinet/tcp_input.c tcp_reass): tcp_sack_addblock() folds a
 * newly-queued range into the list, most-recent-first (RFC 2018 sec.4 requires the
 * first reported block to contain the segment that triggered the ACK), merging any
 * blocks it now touches; tcp_sack_purge() drops (and left-trims) blocks that the
 * advancing cumulative ack has since covered. tcp_output() reads sackblks[] to
 * emit the SACK option. All blocks are kept strictly above rcv_nxt.
 *
 * Only data actually queued is ever added (the hooks sit after the reassembly
 * insque and after rcv_nxt advances), so we never SACK a segment the queue-bound
 * hardening dropped -- that would wrongly suppress the peer's retransmit.
 *
 * This is base RFC 2018 (report data held above rcv_nxt). It does NOT implement
 * RFC 2883 D-SACK (reporting a duplicate segment BELOW rcv_nxt as the first
 * block); a future addition, not assumed present here.
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
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>	/* TCPT_NTIMERS -- used by struct tcpcb in tcp_var.h */
#include <netinet/tcp_var.h>

/*
 * Fold the freshly-queued range [start,end) into the SACK block list as the new
 * head block (most recent), merging any existing block it overlaps or touches and
 * discarding any that the cumulative ack has already covered. Caller guarantees
 * this range was actually queued in the reassembly buffer.
 */
void
tcp_sack_addblock(tp, start, end)
	struct tcpcb *tp;
	tcp_seq start, end;
{
	struct sackblk head, saved[TCP_MAX_SACK];
	int i, nsaved = 0;

	/* Nothing to report at/below the cumulative ack. */
	if (SEQ_LEQ(end, tp->rcv_nxt))
		return;
	if (SEQ_LT(start, tp->rcv_nxt))
		start = tp->rcv_nxt;
	if (SEQ_GEQ(start, end))
		return;

	head.start = start;
	head.end = end;

	/*
	 * Walk the current blocks: drop any already covered by rcv_nxt, merge any
	 * that overlap or are contiguous with the new head into it, and keep the
	 * rest (preserving their relative recency order in saved[]).
	 */
	for (i = 0; i < tp->rcv_numsacks; i++) {
		tcp_seq bs = tp->sackblks[i].start;
		tcp_seq be = tp->sackblks[i].end;

		if (SEQ_LEQ(be, tp->rcv_nxt))
			continue;			/* delivered -- drop */
		if (SEQ_LEQ(head.start, be) && SEQ_LEQ(bs, head.end)) {
			/* overlaps or touches the head block -- absorb it */
			if (SEQ_LT(bs, head.start))
				head.start = bs;
			if (SEQ_GT(be, head.end))
				head.end = be;
		} else {
			saved[nsaved++] = tp->sackblks[i];	/* separate block */
		}
	}

	/* Rebuild: merged head first, then the survivors, capped at TCP_MAX_SACK. */
	tp->sackblks[0] = head;
	for (i = 0; i < nsaved && (i + 1) < TCP_MAX_SACK; i++)
		tp->sackblks[i + 1] = saved[i];
	tp->rcv_numsacks = min(nsaved + 1, TCP_MAX_SACK);
}

/*
 * The cumulative ack (rcv_nxt) has advanced -- drop blocks it now fully covers
 * and left-trim any it partially covers, compacting the list in place.
 */
void
tcp_sack_purge(tp)
	struct tcpcb *tp;
{
	int i, j = 0;

	for (i = 0; i < tp->rcv_numsacks; i++) {
		if (SEQ_LEQ(tp->sackblks[i].end, tp->rcv_nxt))
			continue;			/* fully delivered -- drop */
		if (SEQ_LT(tp->sackblks[i].start, tp->rcv_nxt))
			tp->sackblks[i].start = tp->rcv_nxt;	/* partial -- left-trim */
		tp->sackblks[j++] = tp->sackblks[i];
	}
	tp->rcv_numsacks = j;
}

/* ------------------------------------------------------------------------- *
 * RFC 6675 SACK SENDER scoreboard (net/tcp_sack.c).
 *
 * snd_sackblks[] holds the ranges of OUR sent-but-unacked data the peer has
 * reported SACKed, sorted ascending and merged. It is the mirror of the
 * receiver list above but for the send side; the un-SACKed gaps between the
 * blocks (and between snd_una and the first block) are the holes B2's
 * NextSeg()/SetPipe() retransmit. Everything is kept strictly above snd_una.
 * ------------------------------------------------------------------------- */

/* Drop/left-trim any stored SACK block the cumulative ack (snd_una) now covers. */
static void
tcp_sack_snd_trim(tp)
	struct tcpcb *tp;
{
	int i, j = 0;

	for (i = 0; i < tp->snd_numsackblks; i++) {
		if (SEQ_LEQ(tp->snd_sackblks[i].end, tp->snd_una))
			continue;			/* fully acked -- drop */
		if (SEQ_LT(tp->snd_sackblks[i].start, tp->snd_una))
			tp->snd_sackblks[i].start = tp->snd_una;  /* partial -- left-trim */
		tp->snd_sackblks[j++] = tp->snd_sackblks[i];
	}
	tp->snd_numsackblks = j;
}

/* Insert [start,end) into the sorted, merged snd_sackblks list, coalescing any
 * blocks it overlaps or touches. Bounded: if the list is full the extra island
 * is dropped (graceful -- worse case we retransmit a range we needn't, never
 * corrupt). Caller guarantees snd_una <= start < end <= snd_max. */
static void
tcp_sack_snd_insert(tp, start, end)
	struct tcpcb *tp;
	tcp_seq start, end;
{
	struct sackblk merged[TCP_MAX_SACK_SND];
	int i, n = 0, placed = 0;

	for (i = 0; i < tp->snd_numsackblks; i++) {
		tcp_seq bs = tp->snd_sackblks[i].start;
		tcp_seq be = tp->snd_sackblks[i].end;

		if (SEQ_LT(be, start)) {		/* wholly before (gap) -- keep */
			if (n < TCP_MAX_SACK_SND)
				merged[n++] = tp->snd_sackblks[i];
		} else if (SEQ_GT(bs, end)) {		/* wholly after (gap) -- emit new first */
			if (!placed) {
				if (n < TCP_MAX_SACK_SND) {
					merged[n].start = start;
					merged[n].end = end;
					n++;
				}
				placed = 1;
			}
			if (n < TCP_MAX_SACK_SND)
				merged[n++] = tp->snd_sackblks[i];
		} else {				/* overlaps or touches -- absorb */
			if (SEQ_LT(bs, start))
				start = bs;
			if (SEQ_GT(be, end))
				end = be;
		}
	}
	if (!placed && n < TCP_MAX_SACK_SND) {
		merged[n].start = start;
		merged[n].end = end;
		n++;
	}
	for (i = 0; i < n; i++)
		tp->snd_sackblks[i] = merged[i];
	tp->snd_numsackblks = n;
}

/*
 * RFC 6675 "Update": fold this segment's inbound SACK blocks into the sender
 * scoreboard. First drop what the cumulative ack covered, then merge each block
 * (clamped to the valid unacked window [snd_una, snd_max)) into the sorted list.
 */
void
tcp_sack_update(tp, sack, nsack)
	struct tcpcb *tp;
	struct sackblk *sack;
	int nsack;
{
	int i;

	tcp_sack_snd_trim(tp);
	for (i = 0; i < nsack; i++) {
		tcp_seq s = sack[i].start;
		tcp_seq e = sack[i].end;

		if (SEQ_GT(e, tp->snd_max))		/* clamp junk above what we sent */
			e = tp->snd_max;
		if (SEQ_LT(s, tp->snd_una))		/* nothing to learn at/below the ack */
			s = tp->snd_una;
		if (SEQ_GEQ(s, e))
			continue;
		tcp_sack_snd_insert(tp, s, e);
	}
}

/* ------------------------------------------------------------------------- *
 * RFC 6675 loss-recovery decision functions (consumed by tcp_output in B2b).
 * All are read-only over the (sorted, merged) sender scoreboard, so the walks
 * are cheap. HighRxt == snd_highrxt is kept one-past the highest octet already
 * retransmitted this recovery (initialised to snd_una at recovery entry), so a
 * hole "above HighRxt" is simply seq >= snd_highrxt.
 * ------------------------------------------------------------------------- */

/* Does the octet at 'seq' lie inside a SACKed island? (per-segment callers pass
 * a segment start.) */
static int
tcp_sack_issacked(tp, seq)
	struct tcpcb *tp;
	tcp_seq seq;
{
	int i;

	for (i = 0; i < tp->snd_numsackblks; i++)
		if (SEQ_LEQ(tp->snd_sackblks[i].start, seq) &&
		    SEQ_LT(seq, tp->snd_sackblks[i].end))
			return 1;
	return 0;
}

/*
 * RFC 6675 IsLost(seq): true if at least DupThresh (tcprexmtthresh) SACKed
 * islands lie strictly above seq, OR more than (DupThresh-1)*SMSS bytes above
 * seq have been SACKed -- either being strong evidence the segment at seq was
 * lost rather than merely reordered.
 */
int
tcp_sack_islost(tp, seq)
	struct tcpcb *tp;
	tcp_seq seq;
{
	int islands = 0;
	long above = 0;
	int i;

	for (i = 0; i < tp->snd_numsackblks; i++) {
		tcp_seq s = tp->snd_sackblks[i].start;
		tcp_seq e = tp->snd_sackblks[i].end;

		if (SEQ_LEQ(e, seq))
			continue;			/* island entirely at/below seq */
		if (SEQ_GT(s, seq))
			islands++;			/* a discontiguous island above seq */
		else
			s = seq;			/* island straddles seq: count only above */
		above += (long)(e - s);
	}
	if (islands >= tcprexmtthresh)
		return 1;
	if (above > (long)(tcprexmtthresh - 1) * (long)tp->t_maxseg)
		return 1;
	return 0;
}

/*
 * RFC 6675 SetPipe(): estimate the octets currently in the network. Walk the
 * unacked window [snd_una, snd_max) a segment at a time; an unSACKed segment
 * counts toward pipe if it is either not (yet) deemed lost (still assumed in
 * flight) OR has been retransmitted (start < HighRxt, so its retransmission is
 * in flight). SACKed segments are not in the network.
 */
u_long
tcp_sack_pipe(tp)
	struct tcpcb *tp;
{
	u_long pipe = 0;
	tcp_seq seq = tp->snd_una;
	u_short mss = tp->t_maxseg ? tp->t_maxseg : 1;

	while (SEQ_LT(seq, tp->snd_max)) {
		tcp_seq end = seq + mss;

		if (SEQ_GT(end, tp->snd_max))
			end = tp->snd_max;
		if (!tcp_sack_issacked(tp, seq) &&
		    (!tcp_sack_islost(tp, seq) || SEQ_LT(seq, tp->snd_highrxt)))
			pipe += (u_long)(end - seq);
		seq = end;
	}
	return pipe;
}

/*
 * RFC 6675 NextSeg(), rules (1) and (2) (rescue rule (3) is tcp_sack_rescue). On success set
 * *startp / *endp to the sequence range to (re)transmit (the caller clamps to
 * one SMSS) and *rxmit to 1 (a hole retransmit) or 0 (new data), returning 1;
 * return 0 only in degenerate cases.
 *
 * Rule (1): the first lost hole -- the gap BEFORE some SACK block (so there is
 * SACKed data above it, satisfying "below the highest SACK") whose start is at
 * or above HighRxt and which IsLost(). Iterating the gaps between the sorted
 * blocks (not stepping by MSS) so a sub-MSS hole is never skipped.
 * Rule (2): no lost hole remains -- offer new data at snd_max.
 */
int
tcp_sack_nextseg(tp, startp, endp, rxmit)
	struct tcpcb *tp;
	tcp_seq *startp, *endp;
	int *rxmit;
{
	tcp_seq holestart = tp->snd_una;
	int i;

	for (i = 0; i < tp->snd_numsackblks; i++) {
		tcp_seq holeend = tp->snd_sackblks[i].start;	/* SACK above -> real hole */
		tcp_seq s = holestart;

		if (SEQ_LT(s, tp->snd_highrxt))
			s = tp->snd_highrxt;			/* resume at/above HighRxt (1.a) */
		if (SEQ_LT(s, holeend)) {			/* a real, not-yet-retransmitted hole */
			if (tcp_sack_islost(tp, s)) {
				*startp = s;
				*endp = holeend;
				*rxmit = 1;
				return 1;
			}
			/*
			 * IsLost() is monotonically non-increasing in seq (higher
			 * holes have fewer SACKed islands/bytes above them), so if
			 * the lowest not-yet-retransmitted hole is not lost, none
			 * above it are either -- stop scanning and fall to rule (2).
			 */
			break;
		}
		holestart = tp->snd_sackblks[i].end;
	}
	/* Rule (2): new data at snd_max (caller checks the send buffer + window). */
	*startp = tp->snd_max;
	*endp = tp->snd_max;
	*rxmit = 0;
	return 1;
}

/*
 * RFC 6675 NextSeg() rule (3): the "rescue" retransmission. When rules (1) and
 * (2) both come up empty -- no hole yet certified lost by IsLost(), and no new
 * data queued to send -- but there is still an un-SACKed hole below the highest
 * SACK block, retransmit that lowest hole once. This is rule (1)'s scan minus the
 * IsLost() test: the hole is real (SACKed data sits above it) but has not yet
 * accrued enough SACK evidence to be declared lost. Without the rescue such a
 * recovery would sit with an empty pipe until the retransmit timeout; resending
 * the hole instead elicits either a cumulative ack (recovered) or a fresh SACK
 * (progress). Returns 1 with [*startp,*endp) set to the hole, else 0. The caller
 * sends at most one SMSS of it and gates the whole thing to once per recovery
 * episode via TF_SACK_RESCUE, so this never devolves into resending merely
 * reordered data.
 */
int
tcp_sack_rescue(tp, startp, endp)
	struct tcpcb *tp;
	tcp_seq *startp, *endp;
{
	tcp_seq holestart = tp->snd_una;
	int i;

	for (i = 0; i < tp->snd_numsackblks; i++) {
		tcp_seq holeend = tp->snd_sackblks[i].start;	/* SACK above -> real hole */
		tcp_seq s = holestart;

		if (SEQ_LT(s, tp->snd_highrxt))
			s = tp->snd_highrxt;			/* only above HighRxt */
		if (SEQ_LT(s, holeend)) {			/* an un-retransmitted hole */
			*startp = s;
			*endp = holeend;
			return 1;
		}
		holestart = tp->snd_sackblks[i].end;
	}
	return 0;
}

/* ------------------------------------------------------------------------- *
 * RFC 6937 Proportional Rate Reduction (PRR) support.
 * ------------------------------------------------------------------------- */

/*
 * Sum of SACKed octets in the sender scoreboard lying at or above `floor`.
 * PRR uses this to measure how much selectively-acked data is still outstanding
 * above the cumulative ack when computing DeliveredData.
 */
u_long
tcp_sack_snd_bytes_above(tp, floor)
	struct tcpcb *tp;
	tcp_seq floor;
{
	u_long bytes = 0;
	int i;

	for (i = 0; i < tp->snd_numsackblks; i++) {
		tcp_seq s = tp->snd_sackblks[i].start;
		tcp_seq e = tp->snd_sackblks[i].end;

		if (SEQ_LEQ(e, floor))
			continue;			/* entirely below the floor */
		if (SEQ_LT(s, floor))
			s = floor;			/* count only the part at/above floor */
		bytes += (u_long)(e - s);
	}
	return bytes;
}

/*
 * RFC 6937 PRR, one ACK's worth. Called for EVERY ack during SACK recovery --
 * both the pure-SACK dup-ack refill path and the cumulative-ack path -- so cwnd
 * is paced on every delivery signal. `acked` is the octets this ack advanced
 * snd_una; `prev_sacked` is tcp_sack_snd_bytes_above(snd_una) sampled BEFORE this
 * ack folded its SACK blocks in, so
 *     DeliveredData = acked + (sacked-now - sacked-before)
 * counts each delivered octet exactly once (a byte moving SACKed -> cumulatively
 * acked nets zero). snd_cwnd is set to pipe + sndcnt so tcp_output emits
 * proportionally, ending recovery near ssthresh. Uses the SSRB (slow-start
 * reduction) bound once pipe has fallen to/below ssthresh.
 */
void
tcp_sack_prr_ack(tp, acked, prev_sacked)
	struct tcpcb *tp;
	long acked;
	u_long prev_sacked;
{
	u_long pipe, cur_sacked, fs;
	long delivered, sndcnt;

	cur_sacked = tcp_sack_snd_bytes_above(tp, tp->snd_una);
	delivered = acked + (long)cur_sacked - (long)prev_sacked;
	if (delivered < 0)
		delivered = 0;			/* defensive: never negative */
	tp->prr_delivered += (u_long)delivered;

	pipe = tcp_sack_pipe(tp);
	fs = tp->prr_recoverfs ? tp->prr_recoverfs : 1;
	if (pipe > tp->snd_ssthresh) {
		/*
		 * Proportional: ceil(prr_delivered * ssthresh / RecoverFS) - prr_out.
		 * The product can exceed 32 bits with RFC 1323 scaled windows, so it
		 * is formed in 64 bits before the divide brings it back into range.
		 */
		sndcnt = (long)(((unsigned long long)tp->prr_delivered *
		    tp->snd_ssthresh + (fs - 1)) / fs) - (long)tp->prr_out;
	} else {
		/* SSRB: cap the increase so a burst of ACKs can't overshoot ssthresh. */
		long lim = (long)tp->prr_delivered - (long)tp->prr_out;
		if (delivered > lim)
			lim = delivered;
		lim += tp->t_maxseg;
		sndcnt = (long)tp->snd_ssthresh - (long)pipe;
		if (lim < sndcnt)
			sndcnt = lim;
	}
	if (sndcnt < 0)
		sndcnt = 0;
	tp->snd_cwnd = pipe + (u_long)sndcnt;
}
