/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ng_stageprof.c -- where does the time in a packet actually go?
 *
 * TEMPORARY INSTRUMENTATION, off unless NG_STAGEPROF is set. It exists because
 * arithmetic disagreed with measurement and the measurement won.
 *
 * docker/run-ipprofile.sh times a UDP loopback round trip and, on the emulated
 * 7 MHz 68000, gives ~8.9 us per payload byte. The four passes over the payload
 * that path is known to make -- copy in, checksum out, checksum in, copy out --
 * were measured independently by cksumbench on the SAME machine and account for
 * only ~3.4 us/byte. Something is spending the other ~5.5 us/byte, which is 60%
 * of the per-byte cost, and no amount of reading the source has found it.
 *
 * So: time the four known passes in situ, and see what is left. If they come out
 * near 3.4 the remainder is elsewhere and we go looking with a clear conscience.
 * If they come out near 8.9 then one of them is far more expensive in the real
 * path than it is in a microbenchmark, and THAT is the finding.
 *
 * WHY THE CLOCK IS AFFORDABLE HERE. A round trip on that machine takes ~6.4 ms.
 * ReadEClock is a library call costing a few microseconds, and there are eight of
 * them per round trip, so the instrument perturbs the thing it measures by well
 * under one percent. That would NOT be true on real hardware or in the driver's
 * interrupt, which is why this is gated and temporary.
 *
 * NOT interrupt-safe and not task-safe: one global set of accumulators, no
 * locking. It is built for one benchmark, single-threaded, doing one round trip
 * at a time. Do not leave it enabled.
 */

#include <conf.h>

#if NG_STAGEPROF

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <kern/amiga_includes.h>

/* Accumulated E-clock ticks per stage, and how many samples went into each. */
static u_long ng_prof_acc[NG_PROF_NSTAGE];
static u_long ng_prof_cnt[NG_PROF_NSTAGE];
static u_long ng_prof_byt[NG_PROF_NSTAGE];
static u_long ng_prof_open[NG_PROF_NSTAGE];	/* entry stamp, 0 = not open */
static u_long ng_prof_pkts;

static const char *ng_prof_name[NG_PROF_NSTAGE] = {
	"copyin ",	/* user -> mbuf, in sosend            */
	"cksout ",	/* UDP checksum on the way out        */
	"cksin  ",	/* UDP checksum on the way in         */
	"copyout",	/* mbuf -> user, in soreceive         */
	"NULLcal"	/* the cost of measuring, nothing else */
};

static u_long
ng_prof_now(void)
{
	struct EClockVal e;

	if (TimerBase == NULL)
		return (0);
	ReadEClock(&e);
	return ((u_long)e.ev_lo);		/* 32 bits is ~6000 s at 709 kHz */
}

void
ng_prof_enter(stage)
	int stage;
{
	if (stage >= 0 && stage < NG_PROF_NSTAGE)
		ng_prof_open[stage] = ng_prof_now();
}

void
ng_prof_leave(stage, nbytes)
	int stage;
	long nbytes;
{
	u_long t;

	if (stage < 0 || stage >= NG_PROF_NSTAGE || ng_prof_open[stage] == 0)
		return;
	t = ng_prof_now();
	ng_prof_acc[stage] += t - ng_prof_open[stage];	/* unsigned: wrap is fine */
	ng_prof_cnt[stage]++;
	if (nbytes > 0)
		ng_prof_byt[stage] += (u_long)nbytes;
	ng_prof_open[stage] = 0;
}

/*
 * Called once per completed round trip. Reports periodically rather than every
 * packet, because the log itself is not free.
 */
void
ng_prof_packet()
{
	int i;

	/*
	 * Calibrate against the instrument itself. Two ReadEClock calls bracket every
	 * measured region, and on an emulated 7 MHz 68000 that is NOT free -- the CIA
	 * reads it does are cycle-exact and slow. Timing an empty region once per
	 * packet gives the per-measurement overhead, so it can be subtracted instead
	 * of being silently charged to whichever stage is being blamed.
	 */
	ng_prof_enter(NG_PROF_NULL);
	ng_prof_leave(NG_PROF_NULL, 0L);

	if ((++ng_prof_pkts % 200UL) != 0)
		return;

	log(LOG_DEBUG, "stageprof after %lu packets (E-clock ticks, ~1.41us each):",
	    ng_prof_pkts);
	{
		u_long ovh = ng_prof_cnt[NG_PROF_NULL]
		    ? ng_prof_acc[NG_PROF_NULL] / ng_prof_cnt[NG_PROF_NULL] : 0UL;

		for (i = 0; i < NG_PROF_NSTAGE; i++) {
			u_long raw = ng_prof_acc[i];
			u_long sub = ovh * ng_prof_cnt[i];
			u_long net = (raw > sub) ? (raw - sub) : 0UL;

			log(LOG_DEBUG, "  %s n=%lu bytes=%lu raw=%lu net=%lu per1k=%lu",
			    (ULONG)ng_prof_name[i], ng_prof_cnt[i], ng_prof_byt[i],
			    raw, net,
			    ng_prof_byt[i] ? (net * 1000UL) / ng_prof_byt[i] : 0UL);
		}
	}
}


/*
 * One-shot address report. cksumbench measures a raw CopyMem at 0.87 us/byte on this
 * machine; the socket-path copies measure ~2.5x that. The prime suspect is the same
 * one the receive path had: if source and destination are not congruent, CopyMem
 * cannot move longwords and drops to a slower path. Print the first few pairs and
 * settle it.
 */
void
ng_prof_addr(tag, src, dst, len)
	const char *tag;
	void *src;
	void *dst;
	long len;
{
	static int said[NG_PROF_NSTAGE + 2];
	static int slot;

	if (slot >= 6)
		return;
	slot++;
	log(LOG_DEBUG, "copyaddr %s src=%08lx dst=%08lx len=%ld  src&3=%ld dst&3=%ld",
	    (ULONG)tag, (ULONG)src, (ULONG)dst, len,
	    (ULONG)((ULONG)src & 3UL), (ULONG)((ULONG)dst & 3UL));
	(void)said;
}

#endif /* NG_STAGEPROF */
