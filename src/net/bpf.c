/*
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met (the standard 4-clause 4.4BSD terms; see net/bpf.h for the
 * full text retained in this tree).
 *
 *	@(#)bpf.c	8.2 (Berkeley) 3/28/94
 */
/*
 * AmiTCP_NG: the BPF channel core -- the per-channel capture machinery behind
 * Roadshow's bpf_* library vectors. Copyright 2026 Andy Taylor (MW0MWZ), added
 * alongside the Berkeley notice above; the project is GPL v2 (see COPYING).
 *
 * This is a clean re-implementation of the classic 4.4BSD net/bpf.c device
 * model (double-buffered store/hold capture ring, bpf_tap, bpf_catchpacket,
 * BIOC* ioctls) adapted to:
 *   - Roadshow's channel-numbered API: a channel is a small integer 0..N-1
 *     opened by number (bpf_open(channel)), not a cloned /dev/bpf minor;
 *   - this stack's concurrency model instead of spl/select: the capture tap
 *     runs in the single AmiTCP net task, and a reader blocks in an API task
 *     via tsleep()/wakeup() under splimp() -- NOT a semaphore, since the tap
 *     cannot block. Async delivery is by Exec Signal() to the owning task.
 *
 * PHASE 2 (this file) is the core + its internal API (ng_bpf_*). It is inert:
 * nothing calls ng_bpf_tap() until the SANA-II taps land (Phase 3), and the
 * bpf_* library vectors that drive ng_bpf_open()/read()/ioctl()/... are wired
 * in Phase 4. bpf_write() packet injection is Phase 5 (stubbed EOPNOTSUPP here).
 *
 * Error convention for the ng_bpf_* entry points: >= 0 is success (a byte count
 * for read/write, else 0); < 0 is the negation of a BSD errno. The Phase 4
 * vector wrappers translate that into Roadshow's (errno + return -1).
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/cdefs.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/syslog.h>

#include <kern/amiga_includes.h>		/* Signal(), FindTask(), struct Task */
#include <kern/amiga_time.h>		/* extern TimerBase, for get_time() */

#include <sys/synch.h>			/* splimp()/splx()/tsleep()/wakeup() */
#include <sys/time.h>			/* get_time() */

#include <netinet/in.h>			/* struct in_addr, needed by net/if_sana.h */

#include <net/if.h>
#include <net/if_types.h>		/* IFT_ETHER, IFT_LOOP, ... */
#include <net/if_sana.h>		/* struct sockaddr_sana2 (raw bpf_write inject) */
#include <net/bpf.h>

/*
 * Number of capture channels (NG_BPF_MAXCHAN) is defined in net/bpf.h so the
 * SBTC_NUM_PACKET_FILTER_CHANNELS capability query (api/amiga_generic2.c) can
 * report it too. The table is a small fixed array; per-channel capture buffers
 * are allocated lazily at open, so an unused channel costs almost nothing.
 */
#define NG_BPF_DFLT_BUFSIZE	4096		/* default per-buffer ring size */
/* clamp for BIOCSBLEN; BPF_MINBUFSIZE/BPF_MAXBUFSIZE come from net/bpf.h */

/*
 * One capture channel (the classic BSD "struct bpf_d"). Two equal-sized
 * buffers rotate: the tap fills bd_sbuf (store); when it fills, or in immediate
 * mode, or on read, store becomes bd_hbuf (hold) and the reader drains it while
 * the tap continues into what was the free buffer.
 */
struct bpf_channel {
	struct ifnet	*bd_bif;	/* interface this channel is attached to */
	struct bpf_insn	*bd_filter;	/* installed filter program (or NULL) */
	int		bd_filterlen;	/* number of instructions in bd_filter */

	caddr_t		bd_sbuf;	/* store buffer (tap writes here) */
	caddr_t		bd_hbuf;	/* hold buffer (reader drains here) */
	caddr_t		bd_fbuf;	/* free buffer */
	int		bd_slen;	/* bytes used in bd_sbuf */
	int		bd_hlen;	/* bytes used in bd_hbuf */
	int		bd_bufsize;	/* size of each buffer */

	int		bd_dlt;		/* data-link type of bd_bif (DLT_*) */
	struct timeval	bd_rtout;	/* read timeout (0 = block forever) */

	struct Task	*bd_notify_task;/* task to Signal on activity */
	ULONG		bd_notify_mask;	/* signals the stack SETS when data arrives */
	ULONG		bd_intr_mask;	/* caller's signals that BREAK a blocking read */

	ULONG		bd_rcount;	/* packets received by the filter */
	ULONG		bd_dcount;	/* packets dropped (buffer full) */
	ULONG		bd_ccount;	/* packets captured into a buffer */

	UBYTE		bd_inuse;	/* channel is open */
	UBYTE		bd_immediate;	/* BIOCIMMEDIATE: deliver each packet at once */
	UBYTE		bd_promisc;	/* promiscuous mode requested */
	UBYTE		bd_waiting;	/* a reader is asleep in tsleep() on us */
};

static struct bpf_channel bpf_chan[NG_BPF_MAXCHAN];

/*
 * Count of channels currently attached to an interface. The SANA-II tap points
 * (Phase 3) test this before calling ng_bpf_tap(), so an idle stack with no
 * capture open pays nothing per packet.
 */
static int bpf_nlisteners;

/*
 * Set once ng_bpf_shutdown() has run, to refuse further ng_bpf_open()s. The
 * stack's api_hide() only blocks NEW OpenLibrary() calls, not vector calls from
 * a client that already holds the library open, so without this latch such a
 * client could bpf_open() a fresh channel after shutdown swept the table --
 * leaking it. ng_bpf_init() clears it, so a stack restart opens for business.
 */
static int bpf_down;

static const char bpf_wmesg[] = "bpfread";

/* ---- forward declarations ---------------------------------------------- */

static int  bpf_chan_ok(int);
static int  bpf_allocbufs(struct bpf_channel *);
static void bpf_freebufs(struct bpf_channel *);
static void bpf_freed(struct bpf_channel *);
static void bpf_reset(struct bpf_channel *);
static int  bpf_attachd(struct bpf_channel *, struct ifnet *);
static void bpf_detachd(struct bpf_channel *);
static int  bpf_dlt_of(struct ifnet *);
static u_int bpf_mchainlen(struct mbuf *);
static void bpf_wakeup(struct bpf_channel *);
static void bpf_catchpacket(struct bpf_channel *, struct mbuf *,
			    u_int, u_int);
static int  bpf_setif(struct bpf_channel *, struct ifreq *);
static int  bpf_getif(struct bpf_channel *, struct ifreq *);
static int  bpf_setf(struct bpf_channel *, struct bpf_program *);

/* ------------------------------------------------------------------------ */

/*
 * Validate a channel number and return the channel, or NULL. Used by every
 * entry point; keeps the vector layer from ever indexing out of the table.
 */
static int
bpf_chan_ok(chan)
	int chan;
{
	return (chan >= 0 && chan < NG_BPF_MAXCHAN);
}

/*
 * Map an interface's type to a BPF data-link type. Phase 3 will refine this
 * from the SANA-II hardware type (ss_hwtype) for exotic media; the common
 * Ethernet and loopback cases are exact here.
 */
static int
bpf_dlt_of(ifp)
	struct ifnet *ifp;
{
	if (ifp == NULL || ifp->if_type == IFT_LOOP)
		return (DLT_NULL);
	/*
	 * Everything else in this tree is a SANA-II interface (IFT_SANA). SANA
	 * devices are overwhelmingly Ethernet-framed, so DLT_EN10MB is the right
	 * default; Phase 3 can refine it from the driver's SANA hardware type
	 * (ss_hwtype) for the rare non-Ethernet medium.
	 */
	return (DLT_EN10MB);
}

/* Total length of an mbuf chain (the "on the wire" packet length). */
static u_int
bpf_mchainlen(m)
	struct mbuf *m;
{
	u_int len;

	len = 0;
	for (; m != NULL; m = m->m_next)
		len += m->m_len;
	return (len);
}

/*
 * Allocate the two capture buffers for a channel. Called at open, with the
 * channel not yet attached (so the tap can't be running against it).
 */
static int
bpf_allocbufs(d)
	struct bpf_channel *d;
{
	MALLOC(d->bd_sbuf, caddr_t, d->bd_bufsize, M_TEMP, M_WAITOK);
	if (d->bd_sbuf == NULL)
		return (ENOBUFS);
	MALLOC(d->bd_fbuf, caddr_t, d->bd_bufsize, M_TEMP, M_WAITOK);
	if (d->bd_fbuf == NULL) {
		FREE(d->bd_sbuf, M_TEMP);
		d->bd_sbuf = NULL;
		return (ENOBUFS);
	}
	d->bd_hbuf = NULL;
	d->bd_slen = 0;
	d->bd_hlen = 0;
	return (0);
}

/*
 * Release just a channel's three capture buffers (not its filter), and zero the
 * associated lengths so a concurrently-woken reader can never act on a stale
 * bd_slen/bd_hlen against a freed buffer. Caller holds splimp().
 */
static void
bpf_freebufs(d)
	struct bpf_channel *d;
{
	if (d->bd_sbuf != NULL) {
		FREE(d->bd_sbuf, M_TEMP);
		d->bd_sbuf = NULL;
	}
	if (d->bd_hbuf != NULL) {
		FREE(d->bd_hbuf, M_TEMP);
		d->bd_hbuf = NULL;
	}
	if (d->bd_fbuf != NULL) {
		FREE(d->bd_fbuf, M_TEMP);
		d->bd_fbuf = NULL;
	}
	d->bd_slen = 0;
	d->bd_hlen = 0;
}

/* Release a channel's buffers AND its filter. Caller holds splimp(). */
static void
bpf_freed(d)
	struct bpf_channel *d;
{
	bpf_freebufs(d);
	if (d->bd_filter != NULL) {
		FREE(d->bd_filter, M_TEMP);
		d->bd_filter = NULL;
		d->bd_filterlen = 0;
	}
}

/* Discard any buffered data and zero the counters (BIOCFLUSH). */
static void
bpf_reset(d)
	struct bpf_channel *d;
{
	spl_t s;

	s = splimp();
	if (d->bd_hbuf != NULL) {
		/* Return the hold buffer to the free slot. */
		d->bd_fbuf = d->bd_hbuf;
		d->bd_hbuf = NULL;
	}
	d->bd_slen = 0;
	d->bd_hlen = 0;
	d->bd_rcount = 0;
	d->bd_dcount = 0;
	d->bd_ccount = 0;
	splx(s);
}

/*
 * Attach a channel to an interface. Records the link type and bumps the
 * listener count so the tap points know to feed us. Caller holds splimp().
 */
static int
bpf_attachd(d, ifp)
	struct bpf_channel *d;
	struct ifnet *ifp;
{
	if (d->bd_bif == ifp)
		return (0);
	if (d->bd_bif != NULL)
		bpf_detachd(d);
	d->bd_bif = ifp;
	d->bd_dlt = bpf_dlt_of(ifp);
	bpf_nlisteners++;
	return (0);
}

/* Detach a channel from its interface. Caller holds splimp(). */
static void
bpf_detachd(d)
	struct bpf_channel *d;
{
	if (d->bd_bif == NULL)
		return;
	d->bd_bif = NULL;
	if (bpf_nlisteners > 0)
		bpf_nlisteners--;
}

/*
 * Wake a reader blocked on this channel and post the async signals. Called
 * from the tap (net task) and from ioctl paths; never blocks.
 */
static void
bpf_wakeup(d)
	struct bpf_channel *d;
{
	if (d->bd_waiting) {
		d->bd_waiting = 0;
		wakeup((caddr_t)d);
	}
	if (d->bd_notify_task != NULL && d->bd_notify_mask != 0)
		Signal(d->bd_notify_task, d->bd_notify_mask);
}

/* ---- public: init / open / close --------------------------------------- */

/*
 * One-time initialisation. Called from stack start-up. Zeroes the table so
 * every channel starts closed and unattached.
 */
void
ng_bpf_init()
{
	bzero((caddr_t)bpf_chan, sizeof bpf_chan);
	bpf_nlisteners = 0;
	bpf_down = 0;
}

/*
 * Close every open channel and free its resources. Called from the stack's
 * deinit_all() at shutdown, BEFORE the interfaces channels are attached to are
 * torn down (so bd_bif never dangles). First latch the subsystem down so a
 * client still holding the library open can't bpf_open() a fresh channel behind
 * us (api_hide() blocks new library opens but not vector calls from existing
 * users). ng_bpf_close() also wakes any reader blocked in bpf_read.
 */
void
ng_bpf_shutdown()
{
	spl_t s;
	int i;

	s = splimp();
	bpf_down = 1;
	splx(s);

	for (i = 0; i < NG_BPF_MAXCHAN; i++)
		if (bpf_chan[i].bd_inuse)
			(void)ng_bpf_close(i);
	bpf_nlisteners = 0;
}

/*
 * Detach every open channel bound to `ifp` from it. Called when a single
 * interface is removed (sana_remove_interface) before its struct ifnet is
 * freed, so no channel is left with a dangling bd_bif pointing at freed memory
 * -- a later bpf_write() would otherwise call through (*ifp->if_output). The
 * channels stay open but unattached; a bpf_write/BIOCGETIF on one returns EINVAL
 * until it is BIOCSETIF'd to another interface.
 */
void
ng_bpf_ifdetach(ifp)
	struct ifnet *ifp;
{
	int i;

	/*
	 * The CALLER holds splimp(): this runs inside sana_remove_interface's
	 * existing critical section (before it closes the interface's device),
	 * which excludes the net-task tap while we rewrite bd_bif, and keeps the
	 * sweep out of the post-CloseDevice window where taking splimp() was
	 * observed to hang.
	 */
	for (i = 0; i < NG_BPF_MAXCHAN; i++)
		if (bpf_chan[i].bd_inuse && bpf_chan[i].bd_bif == ifp)
			bpf_detachd(&bpf_chan[i]);
}

/*
 * Open a capture channel and return its number. `chan` >= 0 opens that specific
 * channel; `chan` < 0 auto-allocates the first free one (Roadshow's
 * bpf_open(-1)). Reserves the slot and allocates its buffers; the channel is not
 * attached to an interface until BIOCSETIF. Returns the channel number (the
 * caller's "fd") on success, or a negative errno.
 */
int
ng_bpf_open(chan)
	int chan;
{
	struct bpf_channel *d;
	int error;
	spl_t s;

	/*
	 * Claim the channel under splimp(): the scan-then-set of bd_inuse must be
	 * atomic against a concurrent bpf_open() (two tasks could otherwise pass
	 * the !bd_inuse check on the same free slot and both claim it). Buffer
	 * allocation stays inside the section too, matching BIOCSBLEN's existing
	 * allocbufs-under-spl; open is rare, so the longer critical section is fine.
	 */
	s = splimp();
	if (bpf_down) {
		splx(s);
		return (-ENXIO);	/* stack is shutting down */
	}
	if (chan < 0) {
		for (chan = 0; chan < NG_BPF_MAXCHAN; chan++)
			if (!bpf_chan[chan].bd_inuse)
				break;
		if (chan >= NG_BPF_MAXCHAN) {
			splx(s);
			return (-EMFILE);	/* every channel is in use */
		}
	} else if (!bpf_chan_ok(chan)) {
		splx(s);
		return (-EINVAL);
	}
	d = &bpf_chan[chan];
	if (d->bd_inuse) {
		splx(s);
		return (-EBUSY);
	}
	bzero((caddr_t)d, sizeof *d);
	d->bd_bufsize = NG_BPF_DFLT_BUFSIZE;
	error = bpf_allocbufs(d);
	if (error != 0) {
		splx(s);
		return (-error);
	}
	d->bd_inuse = 1;
	splx(s);
	return (chan);			/* the opened channel number */
}

/*
 * Close capture channel `chan`: detach from its interface (if any) and free
 * everything. Safe to call on an already-closed channel.
 */
int
ng_bpf_close(chan)
	int chan;
{
	struct bpf_channel *d;
	spl_t s;

	if (!bpf_chan_ok(chan))
		return (-EINVAL);
	d = &bpf_chan[chan];
	if (!d->bd_inuse)
		return (0);

	s = splimp();
	/*
	 * Mark the channel closed and wake any reader blocked in tsleep() on it
	 * BEFORE freeing its buffers. The woken reader re-checks bd_inuse (see
	 * ng_bpf_read) and returns ENXIO rather than touching freed memory; it
	 * cannot actually run until we splx() and drop the syscall semaphore.
	 */
	d->bd_inuse = 0;
	d->bd_waiting = 0;
	wakeup((caddr_t)d);
	bpf_detachd(d);
	bpf_freed(d);
	d->bd_notify_task = NULL;
	d->bd_notify_mask = 0;
	d->bd_intr_mask = 0;
	splx(s);
	return (0);
}

/* ---- public: read ------------------------------------------------------ */

/*
 * Read captured packets from `chan` into `buf` (up to `len` bytes). Returns the
 * number of bytes delivered (a sequence of BPF_WORDALIGN'd bpf_hdr + data
 * records), or a negative errno. `sb` is the SocketBase of the calling task,
 * needed to block in tsleep(); it must be a real API task, never the net task.
 *
 * Semantics follow 4.4BSD bpfread: normally the whole hold buffer is delivered
 * at once; with nothing buffered the reader blocks (honouring bd_rtout) unless
 * BIOCIMMEDIATE has partial data ready.
 */
int
ng_bpf_read(chan, buf, len, sb)
	int chan;
	caddr_t buf;
	int len;
	struct SocketBase *sb;
{
	struct bpf_channel *d;
	const struct timeval *tmo;
	spl_t s;
	int error, count;

	if (!bpf_chan_ok(chan))
		return (-EINVAL);
	d = &bpf_chan[chan];
	if (!d->bd_inuse)
		return (-EINVAL);
	if (buf == NULL || len < 0)
		return (-EINVAL);

	s = splimp();
	/*
	 * If nothing is in the hold buffer, wait for the tap to fill and rotate
	 * a buffer. In immediate mode a partially-filled store buffer is enough.
	 */
	while (d->bd_hbuf == NULL) {
		if (d->bd_immediate && d->bd_slen != 0)
			break;
		/* Nothing yet: block until the tap wakes us (or the timeout). */
		tmo = (d->bd_rtout.tv_sec != 0 || d->bd_rtout.tv_usec != 0)
		    ? &d->bd_rtout : NULL;
		d->bd_waiting = 1;
		error = tsleep(sb, (caddr_t)d, bpf_wmesg, tmo);
		if (!d->bd_inuse) {
			/* ng_bpf_close() woke us; our buffers are gone. */
			splx(s);
			return (-ENXIO);
		}
		if (error != 0) {
			d->bd_waiting = 0;
			splx(s);
			/* EWOULDBLOCK from a timeout means "no data", not a
			 * failure -- report an empty read. */
			return (error == EWOULDBLOCK) ? 0 : -error;
		}
	}

	/*
	 * Rotate the store buffer into the hold buffer if the reader beat the
	 * tap to it (immediate mode, or a race where store filled but wasn't
	 * rotated yet).
	 */
	if (d->bd_hbuf == NULL) {
		d->bd_hbuf = d->bd_sbuf;
		d->bd_hlen = d->bd_slen;
		d->bd_sbuf = d->bd_fbuf;
		d->bd_fbuf = NULL;
		d->bd_slen = 0;
	}

	/*
	 * The caller must take the whole hold buffer in one read; copying only
	 * part of it would split a bpf_hdr record and corrupt the capture stream.
	 * Refuse without consuming, so a retry with a large enough buffer (the
	 * size BIOCGBLEN reports) still gets the data. An empty immediate-mode
	 * read (bd_hlen == 0) falls through and returns 0.
	 */
	if (d->bd_hlen > len) {
		splx(s);
		return (-EINVAL);
	}
	count = d->bd_hlen;
	if (count > 0)
		bcopy(d->bd_hbuf, buf, (unsigned)count);

	/* Recycle the hold buffer as the new free buffer. */
	d->bd_fbuf = d->bd_hbuf;
	d->bd_hbuf = NULL;
	d->bd_hlen = 0;
	splx(s);
	return (count);
}

/* ---- public: write (packet injection) ---------------------------------- */

/*
 * Inject a packet: transmit the caller's complete link-layer frame out the
 * channel's attached interface. Returns the number of bytes written, or a
 * negative errno. The channel must be attached (BIOCSETIF) first.
 */
int
ng_bpf_write(chan, buf, len, sb)
	int chan;
	caddr_t buf;
	int len;
	struct SocketBase *sb;
{
	struct bpf_channel *d;
	struct ifnet *ifp;
	struct mbuf *m;
	struct sockaddr_sana2 ss2;
	spl_t s;
	int error;

	(void)sb;
	if (!bpf_chan_ok(chan))
		return (-EINVAL);
	d = &bpf_chan[chan];
	if (!d->bd_inuse)
		return (-EINVAL);
	if (buf == NULL || len <= 0)
		return (-EINVAL);
	if ((unsigned long)len > mbconf.mclbytes)  /* one cluster is the ceiling */
		return (-EMSGSIZE);

	/* Snapshot the attached interface under splimp() (a concurrent close/
	 * BIOCSETIF could clear it). No interface -> nothing to transmit on. */
	s = splimp();
	ifp = d->bd_bif;
	splx(s);
	if (ifp == NULL)
		return (-EINVAL);

	/* Copy the caller's frame into an mbuf (cluster it if it won't fit). */
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (-ENOBUFS);
	if (len > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			return (-ENOBUFS);
		}
	}
	bcopy(buf, mtod(m, caddr_t), (unsigned)len);
	m->m_len = len;
	m->m_pkthdr.len = len;
	m->m_pkthdr.rcvif = (struct ifnet *)0;

	/*
	 * Transmit raw: a SANA-II AF_UNSPEC send with type 0 sets SANA2IOF_RAW,
	 * so the driver puts the frame on the wire exactly as the caller built it
	 * (link-layer header and all) -- which is what a BPF write is. if_output
	 * consumes the mbuf in every case (frees it on error), so we never touch
	 * m again.
	 */
	bzero((caddr_t)&ss2, sizeof ss2);
	ss2.ss2_len = sizeof ss2;
	ss2.ss2_family = AF_UNSPEC;
	ss2.ss2_type = 0;
	error = (*ifp->if_output)(ifp, m, (struct sockaddr *)&ss2, 0);
	if (error != 0)
		return (-error);
	return (len);
}

/* ---- public: ioctl ----------------------------------------------------- */

/*
 * Install a filter program (BIOCSETF). Copies the caller's instructions into
 * kernel memory, validates them, and swaps the new program in under splimp().
 * A zero-length program clears the filter (accept everything).
 */
static int
bpf_setf(d, fp)
	struct bpf_channel *d;
	struct bpf_program *fp;
{
	struct bpf_insn *fcode, *old;
	u_int flen, size;
	int oldlen;
	spl_t s;

	if (fp == NULL)
		return (EINVAL);
	flen = fp->bf_len;

	/* Clear the filter. */
	if (fp->bf_insns == NULL) {
		if (flen != 0)
			return (EINVAL);
		s = splimp();
		old = d->bd_filter;
		d->bd_filter = NULL;
		d->bd_filterlen = 0;
		bpf_reset(d);
		splx(s);
		if (old != NULL)
			FREE(old, M_TEMP);
		return (0);
	}

	if (flen == 0 || flen > BPF_MAXINSNS)
		return (EINVAL);
	size = flen * sizeof(struct bpf_insn);
	MALLOC(fcode, struct bpf_insn *, size, M_TEMP, M_WAITOK);
	if (fcode == NULL)
		return (ENOBUFS);
	bcopy((caddr_t)fp->bf_insns, (caddr_t)fcode, size);

	if (!bpf_validate(fcode, (int)flen)) {
		FREE(fcode, M_TEMP);
		return (EINVAL);
	}

	s = splimp();
	old = d->bd_filter;
	oldlen = d->bd_filterlen;
	d->bd_filter = fcode;
	d->bd_filterlen = (int)flen;
	bpf_reset(d);
	splx(s);
	if (old != NULL)
		FREE(old, M_TEMP);
	(void)oldlen;
	return (0);
}

/* Attach to the interface named in fp->ifr_name (BIOCSETIF). */
static int
bpf_setif(d, ifr)
	struct bpf_channel *d;
	struct ifreq *ifr;
{
	struct ifnet *ifp;
	spl_t s;

	if (ifr == NULL)
		return (EINVAL);
	ifr->ifr_name[IFNAMSIZ - 1] = '\0';
	ifp = ifunit(ifr->ifr_name);
	if (ifp == NULL)
		return (ENXIO);

	s = splimp();
	bpf_attachd(d, ifp);
	bpf_reset(d);
	splx(s);
	return (0);
}

/* Report the attached interface's name into ifr->ifr_name (BIOCGETIF). */
static int
bpf_getif(d, ifr)
	struct bpf_channel *d;
	struct ifreq *ifr;
{
	struct ifnet *ifp;
	char *dp;
	const char *sp;
	int unit, i;

	if (ifr == NULL)
		return (EINVAL);
	ifp = d->bd_bif;
	if (ifp == NULL)
		return (EINVAL);

	/* Build "<name><unit>" without depending on sprintf. */
	dp = ifr->ifr_name;
	sp = ifp->if_name;
	i = 0;
	while (sp != NULL && *sp != '\0' && i < IFNAMSIZ - 6)
		dp[i++] = *sp++;
	unit = ifp->if_unit;
	if (unit < 0)
		unit = 0;
	if (unit >= 100 && i < IFNAMSIZ - 1)
		dp[i++] = '0' + (unit / 100) % 10;
	if (unit >= 10 && i < IFNAMSIZ - 1)
		dp[i++] = '0' + (unit / 10) % 10;
	if (i < IFNAMSIZ - 1)
		dp[i++] = '0' + unit % 10;
	dp[i] = '\0';
	return (0);
}

/*
 * Channel control (BIOC* commands). `addr` points at the command's argument
 * (an int, struct, or ifreq) in the caller's memory -- one address space, so
 * we read/write it directly.
 */
int
ng_bpf_ioctl(chan, cmd, addr)
	int chan;
	ULONG cmd;
	caddr_t addr;
{
	struct bpf_channel *d;
	int error;
	spl_t s;

	if (!bpf_chan_ok(chan))
		return (-EINVAL);
	d = &bpf_chan[chan];
	if (!d->bd_inuse)
		return (-EINVAL);

	error = 0;
	switch (cmd) {

	case BIOCGBLEN:			/* get buffer length */
		*(u_long *)addr = (u_long)d->bd_bufsize;
		break;

	case BIOCSBLEN:			/* set buffer length (only before SETIF) */
		if (d->bd_bif != NULL) {
			error = EINVAL;
		} else {
			u_long size = *(u_long *)addr;

			if (size > BPF_MAXBUFSIZE)
				size = BPF_MAXBUFSIZE;
			/*
			 * Round down to a whole BPF word: the capture ring math
			 * (and the per-record BPF_WORDALIGN) assumes a word-
			 * aligned buffer size, otherwise a boundary packet's
			 * bookkeeping can run a few bytes past the allocation.
			 */
			size &= ~(u_long)(BPF_ALIGNMENT - 1);
			if (size < BPF_MINBUFSIZE)
				size = BPF_MINBUFSIZE;
			*(u_long *)addr = size;
			s = splimp();
			bpf_freebufs(d);	/* resize buffers; keep the filter */
			d->bd_bufsize = (int)size;
			error = bpf_allocbufs(d);
			splx(s);
		}
		break;

	case BIOCSETF:			/* set filter program */
		error = bpf_setf(d, (struct bpf_program *)addr);
		break;

	case BIOCFLUSH:			/* drop buffered data + zero stats */
		bpf_reset(d);
		break;

	case BIOCPROMISC:		/* request promiscuous mode */
		d->bd_promisc = 1;	/* honoured against the SANA driver in Phase 3 */
		break;

	case BIOCGDLT:			/* get data-link type */
		*(u_long *)addr = (u_long)d->bd_dlt;
		break;

	case BIOCGETIF:			/* get attached interface */
		error = bpf_getif(d, (struct ifreq *)addr);
		break;

	case BIOCSETIF:			/* attach to an interface */
		error = bpf_setif(d, (struct ifreq *)addr);
		break;

	case BIOCSRTIMEOUT:		/* set read timeout */
		s = splimp();
		d->bd_rtout = *(struct timeval *)addr;
		splx(s);
		break;

	case BIOCGRTIMEOUT:		/* get read timeout */
		*(struct timeval *)addr = d->bd_rtout;
		break;

	case BIOCGSTATS: {		/* get packet statistics */
		struct bpf_stat *bs = (struct bpf_stat *)addr;

		bs->bs_recv = d->bd_rcount;
		bs->bs_drop = d->bd_dcount;
		break;
	}

	case BIOCIMMEDIATE:		/* set immediate-delivery mode */
		d->bd_immediate = (*(u_long *)addr != 0);
		break;

	case BIOCVERSION: {		/* get filter language version */
		struct bpf_version *bv = (struct bpf_version *)addr;

		bv->bv_major = BPF_MAJOR_VERSION;
		bv->bv_minor = BPF_MINOR_VERSION;
		break;
	}

	default:
		error = EINVAL;
		break;
	}

	return (error == 0 ? 0 : -error);
}

/* ---- public: async notification masks ---------------------------------- */

/*
 * Set the Exec signal mask posted to the calling task when captured data
 * becomes available (bpf_set_notify_mask). A zero mask disables notification.
 * The calling task becomes the target for both notify and interrupt signals.
 */
int
ng_bpf_set_notify_mask(chan, mask)
	int chan;
	ULONG mask;
{
	struct bpf_channel *d;
	spl_t s;

	if (!bpf_chan_ok(chan))
		return (-EINVAL);
	d = &bpf_chan[chan];
	if (!d->bd_inuse)
		return (-EINVAL);
	/* task+mask is a pair the tap reads under splimp(); update it atomically. */
	s = splimp();
	d->bd_notify_task = FindTask(NULL);
	d->bd_notify_mask = mask;
	splx(s);
	return (0);
}

/*
 * Record the mask of Exec signals that INTERRUPT a blocking read
 * (bpf_set_interrupt_mask) -- e.g. Roadshow's pcap backend passes
 * SIGBREAKF_CTRL_C so a Ctrl-C breaks the app out of bpf_read. These are the
 * caller's OWN signals, waited on (never sent) by the stack. The interruption
 * itself is delivered by tsleep() honouring the SocketBase's sigIntrMask (which
 * defaults to SIGBREAKF_CTRL_C); the bpf_set_interrupt_mask vector
 * (api/amiga_bpf.c) folds this mask into that. We keep the per-channel copy for
 * the record.
 */
int
ng_bpf_set_interrupt_mask(chan, mask)
	int chan;
	ULONG mask;
{
	struct bpf_channel *d;
	spl_t s;

	if (!bpf_chan_ok(chan))
		return (-EINVAL);
	d = &bpf_chan[chan];
	if (!d->bd_inuse)
		return (-EINVAL);
	s = splimp();
	d->bd_intr_mask = mask;
	if (d->bd_notify_task == NULL)
		d->bd_notify_task = FindTask(NULL);
	splx(s);
	return (0);
}

/*
 * Report how many bytes are immediately readable (bpf_data_waiting): the hold
 * buffer, plus the store buffer when in immediate mode.
 */
int
ng_bpf_data_waiting(chan)
	int chan;
{
	struct bpf_channel *d;
	spl_t s;
	int n;

	if (!bpf_chan_ok(chan))
		return (-EINVAL);
	d = &bpf_chan[chan];
	if (!d->bd_inuse)
		return (-EINVAL);

	s = splimp();
	n = d->bd_hlen;
	if (n == 0 && d->bd_immediate)
		n = d->bd_slen;
	splx(s);
	return (n);
}

/* ---- the capture tap --------------------------------------------------- */

/*
 * Store a matched packet into a channel's buffer, prepended with a bpf_hdr.
 * Rotates a full store buffer into the hold buffer and wakes the reader. Caller
 * holds splimp() (we are on the net task's packet path). `pktlen` is the true
 * on-wire length; `snaplen` is how much the filter asked us to capture.
 */
static void
bpf_catchpacket(d, m, pktlen, snaplen)
	struct bpf_channel *d;
	struct mbuf *m;
	u_int pktlen;
	u_int snaplen;
{
	struct bpf_hdr *hp;
	int totlen, curlen;
	int hdrlen, caplen;
	int wake;

	hdrlen = BPF_WORDALIGN(sizeof(struct bpf_hdr));
	caplen = (int)snaplen;
	if (caplen > (int)pktlen)	/* never record more than the packet holds */
		caplen = (int)pktlen;
	wake = 0;

	/*
	 * Figure out how much will fit: header + captured bytes, word-aligned.
	 * If the whole thing can't fit in an empty buffer, clamp the capture.
	 */
	totlen = hdrlen + BPF_WORDALIGN(caplen);
	if (totlen > d->bd_bufsize) {
		caplen = d->bd_bufsize - hdrlen;
		if (caplen < 0)
			return;			/* buffer smaller than a header */
		totlen = hdrlen + BPF_WORDALIGN(caplen);
	}

	/*
	 * If it doesn't fit in the store buffer's remaining space, rotate: the
	 * store buffer becomes the hold buffer for the reader, and we start
	 * afresh in the free buffer. With no free buffer the reader is behind,
	 * so drop the packet.
	 */
	curlen = BPF_WORDALIGN(d->bd_slen);
	if (curlen + totlen > d->bd_bufsize) {
		if (d->bd_fbuf == NULL) {
			/* Reader is behind; drop and count it (BIOCGSTATS
			 * reports bs_drop). We do NOT Signal here -- a drop is
			 * not an app-facing event, and bd_intr_mask is the
			 * caller's OWN break signal, not ours to raise. */
			d->bd_dcount++;
			return;
		}
		d->bd_hbuf = d->bd_sbuf;
		d->bd_hlen = d->bd_slen;
		d->bd_sbuf = d->bd_fbuf;
		d->bd_fbuf = NULL;
		d->bd_slen = 0;
		curlen = 0;
		wake = 1;		/* the hold buffer is now full and readable */
	}

	/* Lay down the header, then copy the captured bytes after it. */
	hp = (struct bpf_hdr *)(void *)(d->bd_sbuf + curlen);
	get_time(&hp->bh_tstamp);
	hp->bh_datalen = pktlen;
	hp->bh_hdrlen = (u_short)hdrlen;
	hp->bh_caplen = (u_long)caplen;
	m_copydata(m, 0, caplen, (caddr_t)hp + hdrlen);
	d->bd_slen = curlen + totlen;
	d->bd_ccount++;

	/* In immediate mode the reader wants each packet without waiting. */
	if (d->bd_immediate)
		wake = 1;

	/*
	 * One wakeup covers both a rotation and immediate mode. Even if both
	 * fired, the tsleep() side is edge-guarded by bd_waiting, and a duplicate
	 * Exec Signal() is idempotent -- it just ORs the bit into the task's mask.
	 */
	if (wake)
		bpf_wakeup(d);
}

/*
 * The capture tap. Called from the SANA-II receive and transmit paths (Phase 3)
 * once per link-layer frame `m` on interface `ifp`. Runs each attached
 * channel's filter and stores what matches. Runs on the net task under
 * splimp(); it must never block.
 *
 * `m` is the complete link-layer frame as an mbuf chain (Phase 3 prepends the
 * reconstructed L2 header, which the SANA-II model delivers separately).
 */
void
ng_bpf_tap(ifp, m)
	struct ifnet *ifp;
	struct mbuf *m;
{
	struct bpf_channel *d;
	u_int pktlen, slen;
	spl_t s;
	int i;

	if (bpf_nlisteners == 0 || m == NULL)
		return;

	pktlen = bpf_mchainlen(m);

	/*
	 * Guard the capture buffers against a concurrent reader. The Phase-3
	 * SANA rx/tx call sites already run at splimp(), but splimp() nests
	 * safely and cheaply here (an absolute save/restore of TDNestCnt), so
	 * take it rather than depend on the caller's level.
	 */
	s = splimp();
	for (i = 0; i < NG_BPF_MAXCHAN; i++) {
		d = &bpf_chan[i];
		if (!d->bd_inuse || d->bd_bif != ifp)
			continue;
		d->bd_rcount++;
		/*
		 * A NULL filter accepts everything (capture the whole frame);
		 * otherwise the filter returns the number of bytes to capture,
		 * 0 for no match. buflen == 0 selects bpf_filter's mbuf path.
		 */
		if (d->bd_filter == NULL)
			slen = pktlen;
		else
			slen = bpf_filter(d->bd_filter, (u_char *)m, pktlen, 0);
		if (slen != 0)
			bpf_catchpacket(d, m, pktlen,
					slen > pktlen ? pktlen : slen);
	}
	splx(s);
}

/*
 * Cooked Ethernet tap for SANA-II. SANA-II delivers a frame's payload
 * separately from its link-layer header (source/destination hardware address
 * and packet type), so to hand BPF a DLT_EN10MB frame we build the 14-byte
 * Ethernet header (dst[6], src[6], type[2]) in a small temporary mbuf, chain
 * the BORROWED payload behind it, tap, then unchain and free ONLY the header --
 * the caller's payload mbuf is left exactly as it was. `dst`/`src` are 6-byte
 * MAC addresses; `ethertype` is in host order (e.g. the SANA packet type). A
 * cheap no-op when no channel is listening (no mbuf is allocated).
 */
void
ng_bpf_tap_ether(ifp, dst, src, ethertype, m)
	struct ifnet *ifp;
	const u_char *dst;
	const u_char *src;
	u_short ethertype;
	struct mbuf *m;
{
	struct mbuf *hdr;
	u_char *cp;

	if (bpf_nlisteners == 0)
		return;

	hdr = m_get(M_DONTWAIT, MT_DATA);
	if (hdr == NULL)
		return;
	cp = mtod(hdr, u_char *);
	bcopy((caddr_t)dst, (caddr_t)cp, 6);
	bcopy((caddr_t)src, (caddr_t)(cp + 6), 6);
	cp[12] = (u_char)(ethertype >> 8);
	cp[13] = (u_char)(ethertype & 0xff);
	hdr->m_len = 14;			/* sizeof an Ethernet header */

	hdr->m_next = m;			/* borrow the caller's payload chain */
	ng_bpf_tap(ifp, hdr);
	hdr->m_next = NULL;			/* hand it back untouched */
	(void)m_free(hdr);
}
