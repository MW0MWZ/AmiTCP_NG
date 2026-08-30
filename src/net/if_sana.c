/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: if_sana.c,v 3.2 1994/02/03 19:12:08 ppessi Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * if_sana.c --- Generic Interface Routines for Sana II Drivers
 *
 * Created      : Thu Feb 11 13:41:25 1993 ppessi
 * Last modified: Thu Feb  3 17:47:18 1994 ppessi
 *
 * HISTORY
 * $Log: if_sana.c,v $
 * Revision 3.2  1994/02/03  19:12:08  ppessi
 * Changed ssconfig_make() arguments.
 *
 * Revision 3.1  1994/02/03  03:50:38  ppessi
 * Initially tested interface database
 *
 * Revision 1.31  1993/12/21  22:13:35  jraja
 * Changed sana2 tracking not to be done to an interface if configured so.
 * This is to get around a bug in CBM a2060.device.
 *
 * Revision 1.30  1993/11/14  21:15:19  jraja
 * Changed IPTOS_LOWDELAY check to bitwise and (was == including other fields).
 *
 * Revision 1.29  1993/11/06  23:39:15  ppessi
 * Automatically puts interface up when device returns to online state.
 * The LOWDELAY IP packets are given higher priority IO requests.
 *
 * Revision 1.28  1993/10/11  20:31:46  jraja
 * Added explicit prototype for the CheckIO(), which is prototyped
 * incorrectly in the <clib/exec_protos.h>.
 *
 */

/*
 * if_sana.c --- wire a BSD network interface (ifnet) onto a SANA-II device.
 *
 * This is the most Amiga-specific file in the stack and the best one to study if
 * you want to learn how a Unix network stack is attached to a foreign driver
 * model. docs/ARCHITECTURE.md section 8.
 *
 * THE TWO WORLDS IT JOINS.
 *  - Above: BSD's `struct ifnet` -- a network interface with an if_output()
 *    function that ip_output() hands mbuf chains to, and an input path that feeds
 *    mbuf chains to ip_input(). The stack above knows nothing of Amiga devices.
 *  - Below: a SANA-II device (a2065.device for Ethernet, ppp-*.device, ...). You
 *    drive it with Exec IORequests, exactly like any Amiga device:
 *      CMD_WRITE  transmit a frame, tagged with a packet TYPE (0x0800 = IP,
 *                 0x0806 = ARP) and a destination hardware address.
 *      CMD_READ   receive a frame of a given packet type. You keep SEVERAL read
 *                 requests queued per type so the driver always has an empty
 *                 request to fill the instant a packet arrives.
 *      S2_*       online/offline, get the station (hardware) address, statistics.
 *    The driver has no idea what an mbuf is; when it needs to move payload it
 *    calls back into us via S2_CopyToBuff / S2_CopyFromBuff (net/sana2copybuff.c),
 *    which is exactly why mbufs come from a pre-allocated, interrupt-safe pool
 *    (see kern/uipc_mbuf.c).
 *
 * THE DATA PATH THROUGH THIS FILE.
 *  - Transmit: ip_output() -> sana_output(). It resolves the destination hardware
 *    address (ARP for Ethernet, net/sana2arp.c), turns the mbuf chain into a
 *    CMD_WRITE IORequest, and sends it to the device asynchronously.
 *  - Receive: read requests complete asynchronously and signal the main task,
 *    which calls sana_poll() -> sana_ip_read()/sana_arp_read(). sana_read()
 *    detaches the arrived frame from the completed IORequest into an mbuf chain
 *    and hands it up (IP to the netisr input queue; ARP to the ARP code). The
 *    request is then re-queued so the driver can reuse it.
 *
 * INTERFACE LIFECYCLE. iface_make() builds a `struct sana_softc` (our extended
 * ifnet) for a configured interface; sana_run()/sana_up() open the device and
 * queue the initial reads; sana_online()/sana_down() react to the cable going
 * up/down; sana_unrun()/sana_deinit() tear it all back down.
 *
 * Read first: sana_output() (transmit) and sana_read() (receive) -- the two ends
 * of the bridge.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/cdefs.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/systm.h>
#include <sys/syslog.h>

#include <kern/amiga_includes.h>

#include <sys/synch.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>			/* rtalloc1/rtrequest/rt_key -- offline route teardown */
#include <net/netisr.h>
#include <net/bpf.h>			/* ng_bpf_tap_ether() -- capture tap */

#define NDEBUG
#include <assert.h>

#if INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#if NS
#include <netns/ns.h>
#include <netns/ns_if.h>
#endif

#include <net/if_sana.h>
#include <net/sana2arp.h>

#include <net/sana2config.h>
#include <net/sana2request.h>
#include <net/sana2errno.h>

#if __SASC
#include <proto/dos.h>
#elif __GNUC__
#include <inline/dos.h>
#else
#error Your compiler is not supported in this release.
#endif

/* Correct prototype for the CheckIO.
 * (The one in clib/exec_protos.h has wrong return value type: BOOL (16 bits)
 * instead of a pointer (32 bits)!)
 * PORT (AmiTCP_NG): bebbo's <proto/exec.h> already declares CheckIO as a
 * pointer-returning inline, so this manual redeclaration clashes with it under
 * gcc. Keep it only for SAS/C, whose old prototype was wrong. */
#ifdef __SASC
struct IORequest * CheckIO(struct IORequest *req);
#endif

#define ARP_MTU (sizeof(struct s2_arppkt))

/*
 * sana_submit() --- hand an IOIPReq to the device ASYNCHRONOUSLY.
 *
 * USE THIS INSTEAD OF A BARE BeginIO() FOR EVERY REQUEST THAT WILL BE REPLIED.
 *
 * SendIO() does two stores before BeginIO() and both of them matter. This file
 * used to do neither at six separate submission sites, and the consequences were
 * not subtle:
 *
 *  1. ln_Type = NT_MESSAGE. CreateIORequest() leaves a request at NT_REPLYMSG and
 *     ReplyMsg() puts it back there, so a recycled request stays wrong for ever.
 *     CheckIO() reads that field: on a request that is genuinely in flight it
 *     therefore answered "already complete". Everything downstream believed it.
 *     sana_down()'s `if (!CheckIO(req)) AbortSanaIO(req);` loop could never take
 *     its branch, so THE STACK NEVER ABORTED ANYTHING, on any driver -- which is
 *     also why the A3-passing AbortSanaIO() workaround appeared not to help: the
 *     call it fixes was not being made. sana_unrun()'s abort-and-grace loop was
 *     unreachable for the same reason, leaving it to WaitIO() and then
 *     DeleteIORequest() requests the device still owned. With no MMU that is not
 *     a crash anyone gets to debug.
 *
 *  2. io_Flags &= ~IOF_QUICK. Forces the asynchronous path, so the device replies
 *     through the port instead of possibly satisfying us inside BeginIO() -- which
 *     for a request with a dispatch handler and an attached mbuf is the only
 *     behaviour the completion path knows how to handle. Currently redundant at
 *     every call site (each either assigns io_Flags wholesale from a value that
 *     cannot contain IOF_QUICK -- SANA2IOB_QUICK is bit 0, SANA2IOB_RAW is bit 7
 *     -- or inherits a request that was never submitted with quick I/O). Kept
 *     because "redundant today" is exactly what was true of the ln_Type store
 *     until a driver went asynchronous and it cost a release.
 *
 * This is deliberately a helper and not six open-coded pairs of stores: the bug
 * was fixed once already, in sana_doio_bounded() (07823c4), and the other six
 * sites were missed. One place to get right cannot drift out of step again.
 *
 * NOT for sana_doio_bounded() -- that one wants quick I/O and mirrors DoIO().
 */
static inline void
sana_submit(struct IOIPReq *req)
{
  req->ioip_s2.ios2_Req.io_Message.mn_Node.ln_Type = NT_MESSAGE;
  req->ioip_s2.ios2_Req.io_Flags &= ~IOF_QUICK;
  BeginIO((struct IORequest *)req);
}

int debug_sana = 1;

/* Global port for all SANA-II network interfaces */
struct MsgPort *SanaPort = NULL;

/*
 * Serialises the two consumers of SanaPort's reply messages: the network task
 * draining completions in sana_poll() via GetMsg(), and a client task freeing
 * the request pool during interface teardown in sana_unrun() via WaitIO(). The
 * reply-list node IS io_Message.mn_Node, so if both ran at once one could Remove()
 * a node the other already unlinked (list corruption), or sana_poll could dispatch
 * a request sana_unrun is about to DeleteIORequest(). The interleave is real:
 * splimp()/splnet() are Forbid-based (SysBase->TDNestCnt), and Exec breaks Forbid
 * across WaitIO()'s internal Wait(), so the net task can run mid-teardown. A
 * SignalSemaphore (unlike Forbid) SURVIVES Wait(), so the client task holds this
 * across its whole WaitIO() loop and the net task's GetMsg() is excluded until
 * teardown completes. No deadlock: the SANA device replies the (aborted) requests
 * from its own interrupt/task, not from the blocked net task. Held per-poll, not
 * per-packet, so the RX hot-path cost is negligible.
 */
static struct SignalSemaphore SanaPortSem;

/* queue for sana network interfaces */
struct sana_softc *ssq = NULL;

/* These are wire type dependant parameters of
 * Sana-II Network Interface
 */
/* PORT (AmiTCP_NG): removed `extern struct wiretype_parameters wiretype_table[];`.
 * `struct wiretype_parameters` was never defined anywhere in AmiTCP 3.0b2 and
 * wiretype_table is unused here; gcc 6 rejects an (extern) array of incomplete
 * element type that old gcc tolerated. Dead declaration -> dropped. */

/* 
 * Local prototypes
 */
static struct ifnet *iface_make(struct ssconfig *ifc);
/* Defined below iface_make(), which is its first caller. */
static LONG sana_doio_bounded(struct IOSana2Req *req, ULONG secs,
			      const char *what, const char *ifname,
			      int *abandoned);
static void sana_run(struct sana_softc *ssc, int requests, struct ifaddr *ifa);
static void sana_unrun(struct sana_softc *ssc);
static void sana_up(struct sana_softc *ssc);
static BOOL sana_down(struct sana_softc *ssc);
static struct mbuf *
sana_read(struct sana_softc *ssc, struct IOIPReq *req, 
	  UWORD  flags, UWORD *sent, const char *banner, size_t mtu);
static void sana_ip_read(struct sana_softc *ssc, struct IOIPReq *req);
static void sana_arp_read(struct sana_softc *ssc, struct IOIPReq *req);
static void sana_online(struct sana_softc *ssc, struct IOIPReq *req);
static void sana_notify_if_last_gone(void);

/*
 * The event that means "this device can carry traffic again".
 *
 * S2EVENT_ONLINE, alone, because that is what Roadshow asks for and Roadshow works
 * on the hardware this fails on (ref/roadshow-sdk/source_code/Roadshow/Online.c:
 * `NetEventRequest->ios2_WireError = S2EVENT_ONLINE;`).
 *
 * This used to ask for ONLINE|CONNECT|CONFIGCHANGED, on the theory that a WiFi
 * driver might announce its return by re-associating rather than by going online.
 * That theory was invented here and never had anything behind it: nothing in the
 * SANA-II specification, the headers or any driver on hand describes CONNECT or
 * CONFIGCHANGED as an alternative to ONLINE -- CONNECT is documented as "driver has
 * opened session", a dial-up/PPP-shaped idea, not a link-state signal.
 *
 * Asking for bits a driver does not implement is not free. A driver may reject the
 * combination, and our own retry only recognises S2ERR_NOT_SUPPORTED and IOERR_NOCMD
 * -- a driver rejecting with, say, S2ERR_BAD_ARGUMENT (an entirely reasonable answer
 * to "I don't know that bit") sends us round the transient-failure path instead,
 * re-arming the SAME rejected mask up to NG_S2_EVENT_MAXFAIL times before giving up
 * on events altogether. Worse, a driver may ACCEPT the request and simply never match
 * it, which is silent and indistinguishable from a device that never came back.
 *
 * So: ask for exactly what the implementation that works asks for. A driver that
 * cannot do even this much is caught by the watchdog probe instead.
 */
#define NG_S2_BACK_EVENTS  (S2EVENT_ONLINE)

/* How many times a driver may fail an S2_ONEVENT before we stop asking it. */
#define NG_S2_EVENT_MAXFAIL 8

/*
 * Watchdog probe pacing, in if_slowtimo ticks (~1 s each).
 *
 * NG_PROBE_SECS is how long a probe read is left with the driver before we
 * conclude it is not going to be rejected. It is a compromise: too short and a
 * driver that is merely slow to reject looks like a driver that accepted, too
 * long and a returning device sits unusable. NG_PROBE_ABORT_SECS is the grace
 * given to the abort that follows, which a working driver answers at once.
 */
/* How long a device command (S2_ONLINE / S2_OFFLINE) may take before we stop
 * waiting on it. Generous: bringing a radio up is allowed to take a moment, and
 * aborting a slow-but-working driver would be its own bug. */
/* How long teardown waits for one aborted request before abandoning it (~40 ms a
 * turn). Generous enough for any working driver, bounded for one that is not. */
#define NG_UNRUN_GRACE_TICKS 50

#define NG_DEVCMD_SECS      15

#define NG_PROBE_SECS       5
#define NG_PROBE_ABORT_SECS 2

/* Defined further down (it needs the device's AbortIO vector), but wanted earlier by
 * sana_doio_bounded(). Only the GCC form is a function; the other compilers' branch
 * #defines the name onto AbortIO, which needs no declaration. */
#if defined(__GNUC__) && !defined(__SASC)
static inline void AbortSanaIO(struct IORequest *ioRequest);
#endif

static void sana_probe_step(struct sana_softc *ssc);
static void sana_probe_read(struct sana_softc *ssc, struct IOIPReq *req);
static void sana_back_online(struct sana_softc *ssc, const char *how);
static void free_written_packet(struct sana_softc *ssc, struct IOIPReq *req);
static void sana_start(struct sana_softc *ssc);
static void sana_rearm_reads(struct sana_softc *ssc);
static int  sana_watchdog(struct ifnet *ifp);
static void sana_scrub_inet(struct ifnet *ifp, int notify);
static void sana_flush_iface_routes(struct ifnet *ifp);
static void sana_warn_stale_routes(void);	/* defined with the purge helpers */
static void sana_offline_cleanup(struct sana_softc *ssc);

/*
 * PORT (AmiTCP_NG): send-queue metadata tag.
 *
 * The SANA-II transmit model puts the resolved destination hardware address (and
 * the packet type / raw flag) in the IO REQUEST, not in the packet mbuf the way
 * BSD Ethernet does. When the write-request pool (ss_freereq) is exhausted we must
 * park the packet on the interface send queue (ss_if.if_snd) WITHOUT a request, so
 * this metadata has nowhere to live. We carry it in a small MT_DATA "tag" mbuf
 * prepended to the real packet via m_next (the send-queue linkage uses m_nextpkt, a
 * different field, so there is no collision). At drain time sana_start() copies the
 * tag fields into a freed request and frees only the tag; the real packet keeps its
 * own M_PKTHDR untouched. if_qflush() frees tag + packet together, so teardown needs
 * no special handling for parked tags.
 */
struct sana_sendtag {
	ULONG	st_type;			/* ios2_PacketType                    */
	UBYTE	st_dstaddr[MAXADDRSANA];	/* resolved destination hardware addr */
	UBYTE	st_ioflags;			/* io_Flags: 0 or SANA2IOF_RAW        */
	BYTE	st_pri;				/* transmit node priority (LOWDELAY)  */
};

/*
 * Initialize Sana-II interface
 *
 * This routine creates needed message port for Sana-II IO
 * It returns our signal mask, or 0L on an error.
 */
ULONG 
sana_init(void)
{
  assert(!SanaPort);

  InitSemaphore(&SanaPortSem);	/* guards SanaPort consumption (poll vs teardown) */

  SanaPort = CreateMsgPort();	/* V36 function, creates a PA_SIGNAL port */

  if (SanaPort) {
    SanaPort->mp_Node.ln_Name = (void *)"sana_if.port";
    /*
     * The route table is NOT ours to assume empty. It is bsd_malloc'd, so no
     * teardown frees it and it arrives from the previous stack instance exactly
     * as that instance left it. sana_deinit() sweeps it clean; if anything is
     * still here, that sweep missed something and the leftovers point at freed
     * memory. Say so rather than run on top of it -- a silent stale global is
     * how the loopback address gap hid for a whole session.
     */
    sana_warn_stale_routes();
    loattach();
    return (ULONG) 1 << SanaPort->mp_SigBit;
  }

  return 0L;
}

/*
 * Clean up Sana-II Interfaces
 *
 * Note: main interface queue is SNAFU after deinitializing
 */
void 
sana_deinit(void)
{
  struct sana_softc *ssc; 
  struct IOSana2Req *req;

  assert(SanaPort);

  /*
   * SWEEP THE WHOLE ROUTE TABLE FIRST, while every rt_ifa and rt_ifp in it is
   * still live.
   *
   * Deleting a route is not a passive unlink: rtrequest(RTM_DELETE) reads
   * rt->rt_ifa and calls ifa->ifa_rtrequest through it. So the sweep must happen
   * BEFORE anything it might reference has been freed -- not after, which is
   * where it started out and which is precisely the wrong end. By the time the
   * loop below has torn down even one interface, a route bound to it would be
   * deleted by dereferencing memory that loop just handed back.
   *
   * Doing it here also makes the per-interface purges below near-no-ops, which is
   * fine: they stay because they are what a SINGLE interface removal needs, and
   * a delete of an already-gone route is harmless.
   */
  { spl_t s0 = splimp();
    sana_flush_iface_routes((struct ifnet *)NULL);
    splx(s0); }

  while (ssq) {
    ssq->ss_removing = 1;	/* same teardown guard as sana_remove_interface (defensive:
				 * the service loop has already stopped, so no re-arm fires) */
    sana_down(ssq);
    if (ssq->ss_if.if_flags & IFF_RUNNING) {
      sana_unrun(ssq);
    }
    ssc = ssq;
    ssq = ssc->ss_next;

    /*
     * PURGE THIS INTERFACE'S ROUTES AND ADDRESSES, exactly as
     * sana_remove_interface() does, and in the same order -- routes first,
     * because their rt_ifa points at the addresses scrubbed second.
     *
     * This loop did neither, and that is not a tidiness matter. The radix tree
     * is bsd_malloc'd (AllocVec-backed), so unlike the mbuf pool NOTHING frees
     * it at teardown: it survives intact into the next stack, still holding
     * every route this interface had. Two consequences, both observed:
     *
     *  - The connected-network route for the old address is still RTF_UP, so
     *    re-adding that address on the next stack fails EEXIST. That surfaced
     *    as a DHCP lease being refused with AAMR_AddrChangeFailed: after a
     *    shutdown the interface came back and could never get an address again.
     *    (Measured: SIOCSIFADDR returned errno 17 on the second stack.)
     *
     *  - Worse, those surviving routes keep rt_ifa and rt_ifp pointing at an
     *    ifaddr freed with the mbuf pool and a softc bsd_free'd below. See
     *    sana_flush_iface_routes(): ip_output() derefs those and makes a wild
     *    if_output() call on the next packet to that destination. With no MMU
     *    that is a use-after-free the next stack inherits, silently.
     *
     * splimp() around both, as sana_remove_interface() does, so the table
     * cannot mutate mid-purge.
     */
    { spl_t s = splimp();
      sana_flush_iface_routes((struct ifnet *)ssc);
      sana_scrub_inet((struct ifnet *)ssc, 0);
      splx(s); }

    /*
     * Unlink from the GENERIC interface list too, not just ssq.
     *
     * There are two lists: ssq (ours) and ifnet (net/if.c's, walked by
     * if_slowtimo(), ifinit(), ifunit(), the routing code and more). This loop
     * only ever drained ssq, leaving every freed softc still linked into
     * ifnet -- the stock 1993 comment above this function says as much
     * ("main interface queue is SNAFU after deinitializing"), and it was
     * harmless then because the process exited immediately afterwards.
     *
     * It is not harmless now. This is a restartable library: ifnet is a
     * load-time global that survives a stop/start, so the next self-start
     * walks a list whose entries have been freed. Worse, if_attach() appends
     * at the TAIL, so loattach() re-attaching the static loif finds loif
     * already present with a NULL if_next, stops there, and sets
     * loif.if_next = &loif -- a self-loop that if_slowtimo() then walks
     * forever under splimp(), which is Forbid-equivalent: the whole machine
     * stops, silently, with nothing in the log.
     */
    { struct ifnet **q;
      for (q = &ifnet; *q != NULL; q = &(*q)->if_next)
	if (*q == (struct ifnet *)ssc) { *q = ssc->ss_if.if_next; break; }
    }

    /* Close device */
    req = CreateIOSana2Req(ssc);
    if (req) {
      CloseDevice((struct IORequest*)req);
      DeleteIOSana2Req(req);
    } else {
      log(LOG_ERR, "sana_deinit(): Couldn't close device %s\n",
	  ssc->ss_name);
    }

    /*
     * Free the ARP table and the softc, exactly as sana_remove_interface()
     * does for a single interface. This loop drained the list and closed the
     * devices but released neither, so every configured interface was lost on
     * every shutdown -- fine when the stack exited with the program, a
     * per-restart leak now that the library can be stopped and started again.
     * Last, and in this order: ss_name lives inside the softc, so the log line
     * above must have already run.
     */
    free_arptable(ssc);
    bsd_free(ssc, M_IFNET);
  }

  /*
   * Leave the generic list genuinely empty for the next start.
   *
   * loif is a static ifnet, so it is never freed and is still linked here --
   * and if_attach() does not clear if_next when it appends, so a stale
   * loif.if_next left pointing at a softc we just freed would be followed on
   * the next walk. Clearing both is what makes a restart start from nothing.
   */
  /*
   * AND LOOPBACK. loif is a static ifnet and never on ssq, so the loop above
   * never touched it -- but ng_config_loopback() re-assigns 127.0.0.1 on EVERY
   * stack start, and in_control() appends to loif.if_addrlist, a per-interface
   * field no reset clears. Left alone, the second start walks that list into
   * mbuf memory the pool free already reclaimed (an odd ifa_next there is an
   * Address Error on 68000), and rtinit() then hits the surviving 127.0.0.1 host
   * route and fails EEXIST -- silently, because amiga_main.c discards
   * ng_config_loopback()'s result. That is this same bug wearing lo0's clothes,
   * and it lands on `ping 127.0.0.1`, which this project uses as its revival test.
   *
   * Both callees take a plain struct ifnet and touch no sana_softc field, so loif
   * is a legal argument. No notify: the stack is going away.
   */
  { extern struct ifnet loif;
    spl_t s2 = splimp();
    sana_flush_iface_routes(&loif);
    sana_scrub_inet(&loif, 0);
    splx(s2);
    ifnet = NULL;
    loif.if_next = NULL; }

  if (SanaPort) {
    /* Clear possible pending signals */
    SetSignal(1<<SanaPort->mp_SigBit, 0L);
    DeleteMsgPort(SanaPort);
    SanaPort = NULL;
  }
}

/*
 * sana_poll()
 *  This routine polls SanaPort and processes replied
 *  requests appropriately
 */
#define NG_RECONF_MAX 4

/*
 * PORT (AmiTCP_NG): apply the configuration of any interface whose device came
 * back online. Called from the stack's service loop AFTER sana_poll() returns.
 *
 * It must not live inside sana_poll(): that runs its whole body under splnet(),
 * which is a Forbid() equivalent in this port, and the work here opens sockets,
 * takes the NetDataBase semaphore and may spawn the DHCP client -- all of which
 * block. Doing it there hung the machine outright, with the interface reporting
 * itself online and the boot script never reaching its next command.
 *
 * The interface NAME is copied out under a brief splimp() and everything after
 * works from that copy, never the softc: a racing sana_remove_interface() may
 * free it the moment we drop splimp(). ng_reconfigure_interface() looks the
 * interface up again by name and does nothing if it has gone.
 */
/*
 * Build the name a caller would use for this interface ("smoke" + unit 0 ->
 * "smoke0"), into a buffer of at least IFNAMSIZ + 8. Shared so the two places
 * that need a name out of a softc cannot drift apart, and so neither of them
 * keeps the softc pointer beyond the splimp() they read it under.
 */
static void
sana_ifname_copy(struct sana_softc *p, char *out)
{
  const char *nm = p->ss_if.if_name;
  int n = 0, u;

  while (nm && nm[n] && n < IFNAMSIZ - 1) { out[n] = nm[n]; n++; }
  u = p->ss_if.if_unit;
  if (u < 0) u = 0;
  { char d[8]; int k = 0;
    do { d[k++] = (char)('0' + (u % 10)); u /= 10; } while (u && k < (int)sizeof(d));
    while (k > 0) out[n++] = d[--k]; }
  out[n] = '\0';
}

void
sana_reconfig_poll(void)
{
  extern void ng_reconfigure_interface(const char *ifname);
  char  names[NG_RECONF_MAX][IFNAMSIZ + 8];
  int   count = 0, k;
  struct sana_softc *p;
  spl_t s;

  s = splimp();
  for (p = ssq; p != NULL && count < NG_RECONF_MAX; p = p->ss_next) {
    /* Skip one already being reconfigured: a flapping driver must not start a
     * second DHCP client for the same interface. ss_reconfig stays SET so the
     * request is honoured once the running one finishes. */
    if (p->ss_reconfig && !p->ss_reconfiguring) {
      p->ss_reconfig = 0;		/* cleared only now the name is recorded */
      sana_ifname_copy(p, names[count]);
      count++;
    }
  }
  splx(s);

  for (k = 0; k < count; k++)
    ng_reconfigure_interface(names[k]);
}

BOOL
sana_poll(void)
{
  struct IOIPReq * io;
  spl_t s;

  /*
   * Exclude a concurrent interface teardown (sana_unrun's WaitIO loop) from the
   * shared SanaPort while we drain completions -- see SanaPortSem. Taken before
   * splnet() so that if it blocks (a teardown is in progress) we wait at SPL0
   * with no partial state raised.
   */
  ObtainSemaphore(&SanaPortSem);
  s = splnet();

  while (io = (struct IOIPReq *)GetMsg(SanaPort)) {
    /*
     * A request whose interface has been torn down under it (sana_unrun() gave up
     * waiting for a driver that would not return it) has had ioip_if cleared. It must
     * be dropped BEFORE the dereference below -- that softc has been freed. The
     * request itself is not reclaimed here either: the driver has only just let go of
     * it, and nothing now tracks which pool it came from. */
    if (io->ioip_if == NULL)
      continue;
    /* touch the network interface */
    get_time(&io->ioip_if->ss_if.if_lastchange);
    if (io->ioip_dispatch) {
      (*io->ioip_dispatch)(io->ioip_if, io);
     } else {
       log(LOG_ERR, "No dispatch function in request for %s\n",
	   io->ioip_if->ss_name);
     }
  }
  ReleaseSemaphore(&SanaPortSem);

  /*
   * Re-arm any read requests that sana_read() retired under transient mbuf-pool
   * pressure (it decrements ss_ip.sent/ss_arp.sent and parks the request on
   * ss_freereq without re-posting). Without this the receive ring bleeds down
   * during a sustained transfer and RX eventually stalls. Doing it here, after
   * draining completions (which freed requests) and as mbufs become available,
   * keeps the ring topped up whenever the device is active. The if_slowtimo
   * watchdog (sana_watchdog) is the backstop for when the ring has already
   * bled to zero -- then no completion arrives to wake this poll.
   */
  {
    struct sana_softc *p;
    for (p = ssq; p != NULL; p = p->ss_next)
      sana_rearm_reads(p);
  }

  net_poll();

  splx(s);

  /*
   * Deferred offline teardown: any interface whose SANA-II driver went offline
   * during this poll (S2ERR_OUTOFSERVICE) asked us, via ss_offcleanup, to
   * deconfigure it. Do it here in the network task -- NOT the interrupt completion
   * path -- where touching the routing table and address lists is safe.
   *
   * Hold ONE continuous splimp() across the whole ssq walk and the per-interface
   * cleanup: splimp() is a Forbid()-equivalent here, so besides excluding
   * interrupt-time completion it also stops another task's sana_remove_interface()
   * from unlinking/freeing a softc while we traverse and dereference it. The DNS
   * teardown blocks on the NDB semaphore, so it must run OUTSIDE splimp; dynamic
   * name servers are global (not per-interface), so one flush after any offline is
   * both sufficient and correct.
   */
  {
    struct sana_softc *p;
    spl_t s2;
    extern void ng_flush_dynamic_nameservers_for(const char *ifname);
    /* Names of interfaces that went offline AND claimed their name servers, so
     * only those servers are withdrawn. Flushing the whole dynamic list on any
     * offline took name resolution away from every other interface still up. */
    char dnsflush[NG_RECONF_MAX][IFNAMSIZ + 8];
    int  ndns = 0;

    s2 = splimp();
    for (p = ssq; p != NULL; p = p->ss_next) {
      if (p->ss_offcleanup) {
	p->ss_offcleanup = 0;
	sana_offline_cleanup(p);
	if (p->ss_assoc_dns && ndns < NG_RECONF_MAX)
	  sana_ifname_copy(p, dnsflush[ndns++]);
      }
    }
    splx(s2);

    /* Outside splimp(): the flush takes the NetDataBase semaphore and blocks. */
    { int k;
      for (k = 0; k < ndns; k++)
	ng_flush_dynamic_nameservers_for(dnsflush[k]); }
  }

  return FALSE;
}

#ifdef COMPAT_AMITCP2
/*
 * PORT (AmiTCP_NG): this block does not compile and never has in this fork.
 *
 * aiface_find() below contains `struct  = sana2tag_find_exec(name, unit);` --
 * a declaration with no type -- and sana2tag_find_exec() has no definition
 * anywhere in the tree. It is AmiTCP 2 compatibility that was never finished.
 * Nothing in the build defines COMPAT_AMITCP2 (checked docker/ccflags.sh and
 * src/Smakefile), so it has been inert rather than harmful.
 *
 * Failing loudly here rather than leaving the syntax error to be discovered:
 * anyone enabling this flag deserves to be told it needs finishing, not handed
 * a parse error a hundred lines further down.
 */
#error "COMPAT_AMITCP2 is incomplete in AmiTCP_NG -- aiface_find() is unbuildable (see comment)"
/*
 * Name points to the full device name.
 * Device name is a legal DOS file name,
 * appended with a slash and a decimal unit number
 *
 * Some explanation on the device names:
 * There is a DOS wrapper around Exec OpenDevice() function.
 * The device is first searched from the Exec list, if that fails
 * DOS tries to load the segment file with the device name. 
 * If that fails too, the filename is catenated to string "DEVS:" and
 * DOS tries again. 
 *
 * AmiTCP uses internally only the Exec device name (ie. device name
 * without pathpart)
 */

/*
 * Map exec device name to
 * interface structure pointer.
 */
struct ifnet *aifunit(register char *name)
{
  register char *cp;
  register struct ifnet *ifp;
  long unit;
  unsigned len;
  char *ep, c;

  /* AmigaTCP/IP uses the slash as unit number separator 
   * because Exec device name may contain digits.
   */
  char *up;
  cp = ep = name - 1;
  /* Find pathpart */
  for (up = name; *up; up++) 
    if (*up == '/' || *up == ':') {
      cp = ep;
      ep = up;
    }
  /* Name is too long, or there is no unit number */
  if (up >= cp + IFNAMSIZ || cp == ep)	
    return ((struct ifnet *)0);
  cp++;

  /*
   * cp points first char in device name,
   * ep to unit number separator ('/')
   * and up to NUL ('\0') at the end of string
   */
  len = ep - cp;
  c = *ep;
  *ep = '\0';			/* sentinel */
  for (unit = 0, up--; *up >= '0' && *up <= '9'; up--) 
    unit = unit * 10 + *up - '0';
  if (up != ep) {
    *ep = c;
    return NULL;
  }

  /* Pathpart is not included in search */
  for (ifp = ifnet; ifp; ifp = ifp->if_next) {
    if (bcmp(ifp->if_name, cp, len))
      continue;
    if (unit == ifp->if_unit)
      break;
  }
  {
    extern struct ifnet *aiface_find(char *, long unit);
    *ep = '\0';			/* sentinel */
    if (ifp == 0)
      ifp = aiface_find(name, unit);
    *ep = c;
  }
  return (ifp);
}

struct ifnet *
aiface_find(char *name, long unit)
{
  struct  = sana2tag_find_exec(name, unit);

  /* No alias found, use defaults */
  if (sifp == NULL) {
    static short sana_units = 0;
    struct interface_parameters sifp[1];
    const static long tag_end = TAG_END;

    sifp->ifname = "sana";
    sifp->unit = sana_units++;
    sifp->execname = name;
    sifp->execunit = unit;
    sifp->tags = (struct TagItem *)&tag_end;
    return make_iface(sifp, sifp->unit);
  }
  return make_iface(sifp, sifp->unit);
}
#endif

/*
 * This function strategically plugs into ifunit(), and it is called
 * on a non-existant interface.  We try to look it up, and if successful
 * initialize a descriptor and call if_attach() with it.
 *
 * Name is Unix kernel device name,
 * we convert it to Exec device and unit.
 */
struct ifnet *
iface_find(char *name, short unit)
{
  struct ssconfig *ifc = ssconfig_make(SSC_ALIAS, name, unit);
  
  if (ifc) {
    struct ifnet *ifp = iface_make(ifc);
    ssconfig_free(ifc);
    return ifp;
  }
  return NULL;
}


static struct ifnet *
iface_make(struct ssconfig *ifc)
{
  register struct sana_softc *ssc = NULL;
  register struct IOSana2Req *req;
  struct Sana2DeviceQuery *dq;
  LONG oerr = 0;
  LONG err;
  int  abandoned = 0;

  /*
   * PORT (AmiTCP_NG): the device query buffer MUST NOT live on this function's
   * stack, because the S2_DEVICEQUERY below is now bounded and may be ABANDONED.
   *
   * When sana_doio_bounded() gives up on a driver it neutralises the reply port and
   * walks away, and the driver keeps the request -- and every pointer in it --
   * indefinitely. ios2_StatData points at this buffer. Had it stayed a local, an
   * abandoned query would leave the driver holding a pointer into a stack frame
   * about to be reused, and a late completion would write sizeof(Sana2DeviceQuery)
   * bytes over whatever occupied it. With no MMU that is silent corruption, and it
   * would be strictly worse than the hang being fixed.
   *
   * AllocMem, deliberately, and not the stack's own bsd_malloc pool: a leaked pool
   * block returns to the system when the stack tears the pool down, at which point
   * the driver would be writing into memory somebody else owns. An AllocMem block
   * that is never freed stays ours for ever, which on this path is the point.
   * MEMF_PUBLIC because a driver task writes it; MEMF_CLEAR so a driver that fills
   * fewer fields than it claims leaves zeroes rather than somebody's old heap -- a
   * zero MTU then trips the "driver reported nonsense" fallback below instead of
   * being believed.
   */
  dq = (struct Sana2DeviceQuery *)AllocMem(sizeof(*dq), MEMF_PUBLIC | MEMF_CLEAR);
  if (dq == NULL) {
    log(LOG_ERR, "iface_make: no memory for the device query buffer\n");
    return NULL;
  }

  /* Allocate the request for opening the device */
  if ((req = CreateIOSana2Req(NULL)) == NULL)
    log(LOG_ERR, "iface_find(): CreateIOSana2Req failed\n");
  else {
    req->ios2_BufferManagement = buffermanagement;

    /* PORT (AmiTCP_NG): resolve the SANA-II driver robustly. A config just names the
     * driver (e.g. `device=wifipi.device`). Try it exactly as given FIRST -- a bare
     * name finds a driver already resident in memory (e.g. PiStorm's wifipi.device),
     * which is what Roadshow does and which avoids force-loading a conflicting second
     * copy of a hardware driver. Only if that fails do we try the driver's bare name
     * (when a path was given) and then the conventional DEVS:Networks/ location (a
     * driver file on disk, e.g. a2065.device). This fixes the ENXIO you get when a
     * path-qualified open reloads an already-resident driver. */
    {
      UBYTE *a_dev = ifc->args->a_dev;
      LONG   unit  = *ifc->args->a_unit;
      UBYTE *base  = (UBYTE *)FilePart((STRPTR)a_dev);
      int    dl    = 0; char nm[128]; int k, j;
      int    has_dev;
      while (a_dev[dl]) dl++;
      /* Does the name already end in ".device"? */
      has_dev = (dl >= 7 && a_dev[dl-7]=='.' && a_dev[dl-6]=='d' && a_dev[dl-5]=='e' &&
		 a_dev[dl-4]=='v' && a_dev[dl-3]=='i' && a_dev[dl-2]=='c' && a_dev[dl-1]=='e');

      /* Try, in order, until one opens:
       *   1. the name exactly as given
       *   2. <name>.device      -- Roadshow's convention is a BARE name (`device=wifipi`
       *                            for wifipi.device); OpenDevice needs the full name
       *   3. the bare FilePart  -- a resident driver named by a path
       *   4. DEVS:Networks/<base>          (a driver file on disk, e.g. a2065.device)
       *   5. DEVS:Networks/<base>.device   -- bare name under the conventional location
       * Trying the exact name first finds an already-resident driver without force-
       * loading a second copy. */
      oerr = OpenDevice(a_dev, unit, (struct IORequest *)req, 0L);

      if (oerr && !has_dev) {				/* 2. <a_dev>.device */
	k = 0; for (j = 0; a_dev[j] && k < 118; j++) nm[k++] = a_dev[j];
	{ const char *s = ".device"; for (j = 0; s[j]; j++) nm[k++] = s[j]; } nm[k] = 0;
	oerr = OpenDevice((STRPTR)nm, unit, (struct IORequest *)req, 0L);
      }
      if (oerr && base != a_dev)			/* 3. bare FilePart */
	oerr = OpenDevice(base, unit, (struct IORequest *)req, 0L);
      if (oerr) {					/* 4. DEVS:Networks/<base> */
	const char *p = "DEVS:Networks/";
	k = 0; for (j = 0; p[j]; j++) nm[k++] = p[j];
	for (j = 0; base[j] && k < 110; j++) { nm[k++] = base[j]; } nm[k] = 0;
	oerr = OpenDevice((STRPTR)nm, unit, (struct IORequest *)req, 0L);
	if (oerr && !has_dev) {				/* 5. DEVS:Networks/<base>.device */
	  const char *s = ".device"; for (j = 0; s[j]; j++) nm[k++] = s[j]; nm[k] = 0;
	  oerr = OpenDevice((STRPTR)nm, unit, (struct IORequest *)req, 0L);
	}
      }
    }
    if (oerr) {
      sana2perror("OpenDevice", req);
    } else {
      /* Ask for our type, address length, MTU
       * Obl. bitch: nobody tells, WHO is supplying
       * DevQueryFormat and DeviceLevel
       */
      req->ios2_Req.io_Command   = S2_DEVICEQUERY;
      req->ios2_StatData         = dq;
      dq->SizeAvailable          = sizeof(*dq);
      dq->DevQueryFormat         = 0L;

      /* Bounded, because a driver that swallows this command used to hang the caller
       * for ever -- and the caller holds the library's syscall semaphore, so it took
       * the whole library with it. See sana_doio_bounded(). */
      err = sana_doio_bounded(req, NG_DEVCMD_SECS, "S2_DEVICEQUERY",
			      (const char *)ifc->args->a_dev, &abandoned);
      if (abandoned) {
	/* The driver still owns req -- and dq, and the open device unit. Nothing below
	 * may read io_Error or dq, both of which it can still write at any moment, and
	 * nothing may free any of it. Refuse the interface and leak; see the tail. */
	log(LOG_ERR, "iface_make: %s never answered S2_DEVICEQUERY; refusing the "
	    "interface and leaking its request rather than hanging the stack\n",
	    ifc->args->a_dev);
      }
      else if (err)
	sana2perror("S2_DEVICEQUERY", req);
      else if (((dq->AddrFieldSize + 7) >> 3) > MAXADDRSANA) {
	/*
	 * PORT (AmiTCP_NG) security fix: AddrFieldSize comes from the driver's
	 * S2_DEVICEQUERY and is turned into if_addrlen, which is then used as the
	 * length for bcopy() into fixed MAXADDRSANA-byte buffers -- ss_hwaddr here
	 * and, worse, the on-stack hwaddr[MAXADDRSANA] in sana_arp_read() on every
	 * received frame. A driver that misreports its address length would smash
	 * those buffers frame after frame. Refuse the interface up front (ssc stays
	 * NULL, so the cleanup below closes the device) rather than corrupt memory.
	 */
	log(LOG_ERR, "iface_make: %s reports hardware address length %ld, "
	    "exceeding the %ld-byte SANA-II address buffers; refusing interface",
	    ifc->args->a_dev,
	    (long)((dq->AddrFieldSize + 7) >> 3), (long)MAXADDRSANA);
      }
      else {
	/* Get Our Station address */
	req->ios2_StatData = NULL;
	req->ios2_Req.io_Command = S2_GETSTATIONADDRESS;
	err = sana_doio_bounded(req, NG_DEVCMD_SECS, "S2_GETSTATIONADDRESS",
				(const char *)ifc->args->a_dev, &abandoned);
	if (abandoned) {
	  log(LOG_ERR, "iface_make: %s never answered S2_GETSTATIONADDRESS; refusing "
	      "the interface and leaking its request rather than hanging the stack\n",
	      ifc->args->a_dev);
	}
	else if (err)
	  sana2perror("S2_GETSTATIONADDRESS", req);
	else {
	  req->ios2_Req.io_Command = 0;
	  
	  /* Allocate the interface structure */
	  ssc = (struct sana_softc *)
	    bsd_malloc(sizeof(*ssc) + strlen((char *)ifc->args->a_dev) + 1,
		       M_IFNET, M_WAITOK);
	  if (!ssc)
	    log(LOG_ERR, "iface_find: out of memory\n");
	  else {
	    aligned_bzero_const(ssc, sizeof(*ssc));
	    
	    /* Save request pointers */
	    ssc->ss_dev     = req->ios2_Req.io_Device;
	    ssc->ss_unit    = req->ios2_Req.io_Unit;
	    ssc->ss_bufmgnt = req->ios2_BufferManagement;
	    
	    /* Address must be full bytes */
	    ssc->ss_if.if_addrlen  = (dq->AddrFieldSize + 7) >> 3;
	    bcopy(req->ios2_DstAddr, ssc->ss_hwaddr, ssc->ss_if.if_addrlen);
	    /*
	     * PORT (AmiTCP_NG) security fix: dq->MTU is a ULONG coming
	     * from a third-party SANA-II driver, and it was stored unchecked
	     * into if_mtu (a SIGNED short) and ss_maxmtu (a UWORD). A driver
	     * reporting 32768 or more made if_mtu negative, which then feeds the
	     * MSS and fragmentation arithmetic. Clamp it to something both
	     * fields can represent, and fall back to the Ethernet default if the
	     * driver reports a value too small to carry an IP datagram.
	     */
	    {
	      ULONG mtu = dq->MTU;

	      if (mtu < 68)
		mtu = 1500;		/* driver reported nonsense */
	      else if (mtu > 32767)
		mtu = 32767;		/* if_mtu is a signed short */
	      ssc->ss_if.if_mtu = (short)mtu;
	      ssc->ss_maxmtu    = (UWORD)mtu;
	    }
	    ssc->ss_if.if_baudrate = dq->BPS;
	    ssc->ss_hwtype         = dq->HardwareType;	
	    
	    /* These might be different on different hwtypes */
	    ssc->ss_if.if_output = sana_output;
	    ssc->ss_if.if_ioctl  = sana_ioctl;

	    /* Initialize */ 
	    ssconfig(ssc, ifc);
	    
	    NewList((struct List*)&ssc->ss_freereq);

	    if_attach((struct ifnet*)ssc);
	    ifinit();
	    
	    ssc->ss_next = ssq;
	    ssq = ssc;
	  }
	}
      }
      /* Closing a unit that still has an outstanding request against it is illegal
       * in Exec, and the driver typically faults on it. An abandoned request is by
       * definition still outstanding, so the open reference leaks with the rest. */
      if (!ssc && !abandoned)
	CloseDevice((struct IORequest *)req);
    }
    /* The device may still write into the request and its reply port. Handing that
     * memory back while it can is the no-MMU corruption this whole path exists to
     * avoid; the same reasoning as sana_device_online(). */
    if (!abandoned)
      DeleteIOSana2Req(req);
  }

  /*
   * ONE FreeMem, HERE, and only here. dq is read again long after S2_DEVICEQUERY
   * completes -- if_addrlen, MTU, BPS and HardwareType above all come out of it,
   * after S2_GETSTATIONADDRESS has been and gone. Freeing it at the end of the
   * query, which is the obvious-looking place, would be a use-after-free on the
   * ordinary success path.
   *
   * Skipped entirely once anything has been abandoned: the driver may still write
   * here. That leaks this block on a path that already leaks the request, its port
   * and the device open, and deliberately so.
   */
  if (!abandoned)
    FreeMem(dq, sizeof(*dq));

  return (struct ifnet *)ssc;
}

/*
 * PORT (AmiTCP_NG): switch this interface's DEVICE online or offline.
 *
 * The Roadshow SDK defines SM_Offline as "same as SM_Down, but also sends an
 * S2_OFFLINE command to the underlying SANA-II device first", and SM_Online as
 * "same as SM_Up, but tries to send an S2_ONLINE command first; if the command
 * succeeds, the other necessary configuration operations will take place. If it
 * fails, then this function will return with an error code set and no further
 * configuration will have been done." This is that device command.
 *
 * Doing it from the STACK is what makes the interface lifecycle deterministic:
 * when we issue the command ourselves we know the outcome immediately and need
 * no notification from the driver. That matters because a driver is not obliged
 * to report anything -- see the S2_ONEVENT handling in sana_online().
 *
 * Benign refusals are success. A device already in the requested state answers
 * S2ERR_BAD_STATE, and one that does not distinguish the states at all answers
 * S2ERR_NOT_SUPPORTED or IOERR_NOCMD; in each case the device is as online as it
 * is ever going to be. Returns 0, or an errno for a real failure.
 *
 * Blocking, so this must be called at task level, never from a completion or under
 * splimp() -- but bounded, so it always returns. See sana_doio_bounded() below for
 * why that distinction is the difference between one dead interface and a dead stack.
 */
/*
 * Send a device command and wait for it -- but ALWAYS come back.
 *
 * This replaced a plain DoIO(), and the difference is not academic. The caller runs
 * inside ConfigureInterfaceTagList, which holds the library's syscall semaphore, and
 * that semaphore serialises much of the socket API. A DoIO() against a driver that
 * accepts the command and never completes it therefore does not merely fail to bring
 * one interface back: the calling task never returns, the semaphore is never
 * released, and every other task that needs it stops too. The whole library dies.
 *
 * That is not a theory. Crippling a driver so it swallows S2_ONLINE (MODE=hang in
 * docker/run-offline.sh) wedged the emulated machine exactly so: the
 * ONLINE command never returned, and an unrelated GetNetStatus issued afterwards
 * never returned either. The guest never reached the end of its own boot script.
 *
 * So the wait is bounded, in two stages, and the last stage cannot block at all:
 *
 *   1. Wait up to `secs` for the driver to answer. Generous -- bringing a radio up is
 *      allowed to take a moment, and aborting a slow-but-working driver would be its
 *      own bug.
 *   2. If it does not, AbortSanaIO() it (the A3-passing abort this driver family
 *      needs -- plain AbortIO() is ignored by several SANA-II drivers) and wait a
 *      short grace period for the abort to be honoured.
 *   3. If the request STILL has not come back, do not wait for it, because there is
 *      nothing left to wait on that is guaranteed to happen. Neutralise its reply
 *      port -- PA_IGNORE and no signal task -- so that a reply arriving later is
 *      simply linked onto a list nobody reads, rather than signalling a task that may
 *      by then be gone (with no MMU, signalling a recycled Task corrupts whatever
 *      occupies it). Then abandon the request and the port: they leak, deliberately,
 *      and one leaked request on a driver this broken is a trade worth making
 *      against hanging the machine.
 *
 * Returns the driver's io_Error, or IOERR_ABORTED if we gave up on it.
 */
static LONG
sana_doio_bounded(struct IOSana2Req *req, ULONG secs,
		  const char *what, const char *ifname, int *abandoned)
{
  struct MsgPort     *tport;
  struct timerequest *treq;
  struct MsgPort     *rport = req->ios2_Req.io_Message.mn_ReplyPort;
  ULONG devmask, timmask, got;
  LONG  err;
  int   i;
  int   treq_pending = 0;

  *abandoned = 0;

  tport = CreateMsgPort();
  treq  = tport ? (struct timerequest *)CreateIORequest(tport, sizeof(*treq)) : NULL;
  if (treq != NULL &&
      OpenDevice((STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)treq, 0) != 0) {
    DeleteIORequest((struct IORequest *)treq);
    treq = NULL;
  }
  if (treq == NULL) {
    /* No timer to be had. Fall back to the old behaviour rather than refusing to do
     * the operation -- no worse than before this function existed. */
    if (tport) DeleteMsgPort(tport);
    DoIO((struct IORequest *)req);
    return req->ios2_Req.io_Error;
  }

  devmask = 1UL << rport->mp_SigBit;
  timmask = 1UL << tport->mp_SigBit;

  /*
   * PRESERVE QUICK I/O. This is not an optimisation; getting it wrong kills the
   * machine.
   *
   * DoIO() sets IOF_QUICK before BeginIO(). A device that can satisfy the command
   * immediately then does so inside BeginIO() and never replies via the message
   * port -- there is nothing to wait for and nothing to reap. SendIO() does the
   * opposite: it CLEARS IOF_QUICK, obliging the device to take the asynchronous
   * path and reply.
   *
   * Submitting with SendIO() is what this function used to do, and it wedged
   * a2065.device's open-time S2_DEVICEQUERY solid: the reply never came, and
   * neither did the timeout below -- Wait() never returned at all, for either
   * signal, which is the signature of a dead machine rather than a slow driver.
   * The same command under DoIO() answers instantly. Whatever the driver does with
   * a non-quick DEVICEQUERY during open, it does not survive it.
   *
   * So mirror DoIO exactly: ask for quick completion, and only take the bounded
   * asynchronous path if the device actually cleared the flag and went async.
   * The timer is not started until we know we have something to wait for.
   */
  /*
   * ln_Type = NT_MESSAGE BEFORE BeginIO, exactly as SendIO()/DoIO() do it.
   *
   * This is not bookkeeping that can be skipped. CreateIORequest() leaves a fresh
   * request with ln_Type == NT_REPLYMSG, so CheckIO() on a request that has never
   * been sent returns NON-ZERO -- "already complete". SendIO() and DoIO() both
   * overwrite it with NT_MESSAGE on the way in, which is what makes CheckIO mean
   * anything afterwards.
   *
   * Calling BeginIO() directly without that store cost us the async path entirely:
   * the moment a driver went asynchronous, the wait loop's first CheckIO() said the
   * driver had answered, we broke out, and WaitIO() then blocked for ever on a reply
   * that was never coming -- while holding the library's syscall semaphore, so the
   * whole stack stopped behind it. Every normal driver answers these commands with
   * quick I/O, which is why the gate never saw it; only a driver that actually goes
   * async does, and that is precisely the case this function exists for.
   */
  req->ios2_Req.io_Message.mn_Node.ln_Type = NT_MESSAGE;
  req->ios2_Req.io_Flags |= IOF_QUICK;
  BeginIO((struct IORequest *)req);
  if (req->ios2_Req.io_Flags & IOF_QUICK) {
    /* Satisfied inside BeginIO(). No reply was sent, so there is nothing to reap
     * and nothing to abort -- touching the reply port here would hang. */
    err = req->ios2_Req.io_Error;
    CloseDevice((struct IORequest *)treq);
    DeleteIORequest((struct IORequest *)treq);
    DeleteMsgPort(tport);
    return err;
  }

  treq->tr_node.io_Command = TR_ADDREQUEST;
  treq->tr_time.tv_secs    = secs;
  treq->tr_time.tv_micro   = 0;
  SendIO((struct IORequest *)treq);
  treq_pending = 1;

  for (;;) {
    got = Wait(devmask | timmask);
    if (CheckIO((struct IORequest *)req))
      break;				/* the driver answered: normal case */
    if (got & timmask) {
      log(LOG_ERR, "%s: driver did not answer %s in %ld seconds\n",
	  ifname, what, (long)secs);
      AbortSanaIO((struct IORequest *)req);
      /* Reap the timer's OWN completion before the grace loop below reuses treq.
       * Wait() reports a signal but does not dequeue the reply, and re-submitting an
       * IORequest whose previous reply is still linked on its port corrupts that
       * port's list. */
      WaitIO((struct IORequest *)treq);
      treq_pending = 0;
      break;
    }
  }

  /* Give an honoured abort a moment to come back, without committing to WaitIO -- a
   * driver that never received the request (or ignores AbortIO) would never complete
   * it, and WaitIO would hang here instead of in the DoIO we just removed. */
  for (i = 0; i < 20 && !CheckIO((struct IORequest *)req); i++) {
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs    = 0;
    treq->tr_time.tv_micro   = 100000;		/* 0.1 s */
    DoIO((struct IORequest *)treq);
  }

  if (CheckIO((struct IORequest *)req)) {
    WaitIO((struct IORequest *)req);		/* reaps it; cannot block now */
    err = req->ios2_Req.io_Error;
  } else {
    log(LOG_ERR, "%s: driver kept %s and ignored the abort -- abandoning the request "
	"rather than hanging the stack\n", ifname, what);
    Forbid();
    rport->mp_Flags   = PA_IGNORE;	/* a late reply must not signal anybody */
    rport->mp_SigTask = NULL;
    Permit();
    *abandoned = 1;			/* the device still owns it -- do NOT free it */
    err = IOERR_ABORTED;
  }

  /*
   * Reap the timer ONLY if it is still outstanding, tracked explicitly rather than
   * inferred from CheckIO().
   *
   * The two paths here differ, and the difference is not cosmetic. If the driver
   * answered, treq is still queued from the SendIO above and MUST be aborted and
   * reaped. If we timed out, treq was already reaped in the timeout branch, and the
   * grace loop's DoIO() calls each submit and collect it again -- so by this point
   * it is idle. Calling WaitIO() on an idle request relies on it being harmless,
   * which is a semantic nobody here can cite from the Autodocs on disk; if it
   * instead waits for a signal that will never come again, this function would hang
   * precisely when a driver has hung, which is the one case it exists to survive.
   * A flag costs nothing and does not depend on the answer.
   */
  if (treq_pending) {
    if (!CheckIO((struct IORequest *)treq))
      AbortIO((struct IORequest *)treq);
    WaitIO((struct IORequest *)treq);
  }
  CloseDevice((struct IORequest *)treq);
  DeleteIORequest((struct IORequest *)treq);
  DeleteMsgPort(tport);
  return err;
}

/* NOT static: declared extern by the IFC_State handler in api/amiga_roadshow_compat.c. */
int
sana_device_online(struct ifnet *ifp, int online)
{
  struct sana_softc *ssc = (struct sana_softc *)ifp;
  struct IOSana2Req *req;
  LONG err;
  int  abandoned = 0;

  if (ifp->if_type != IFT_SANA)
    return EINVAL;
  if ((req = CreateIOSana2Req(ssc)) == NULL)
    return ENOMEM;

  req->ios2_Req.io_Command = online ? S2_ONLINE : S2_OFFLINE;
  err = sana_doio_bounded(req, NG_DEVCMD_SECS,
			  online ? "S2_ONLINE" : "S2_OFFLINE", (const char *)ssc->ss_name,
			  &abandoned);
  if (err && err != S2ERR_BAD_STATE && err != S2ERR_NOT_SUPPORTED &&
      err != IOERR_NOCMD)
    sana2perror(online ? "S2_ONLINE" : "S2_OFFLINE", req);
  else
    err = 0;
  /* Freeing a request the device still holds would hand its memory back while the
   * driver can still write to it -- with no MMU, that is silent corruption. */
  if (!abandoned)
    DeleteIOSana2Req(req);
  return err ? EIO : 0;
}

/*
 * PORT (AmiTCP_NG): create a SANA-II interface PROGRAMMATICALLY, without the
 * config file -- the mechanism behind the Roadshow AddInterfaceTagList() API,
 * whose whole point is to bring interfaces up without a sana2.config entry.
 * We build an in-memory ssconfig from the interface name, exec device name and
 * unit, then hand it to iface_make() (static, hence this same-file wrapper).
 *
 * The interface name is split into base + unit exactly the way ifunit() parses
 * it ("eth0" -> if_name "eth", if_unit 0), so the interface can be looked up
 * afterwards. Every wire type/count field is left NULL/0, so ssconfig() fills in
 * the per-wiretype defaults (as it does for a bare config-file definition).
 * Returns the new ifnet, or NULL if the device could not be opened. No phantom
 * interface is ever left behind: if_attach() is not reached on any failure path, so
 * nothing appears in the ifnet list. Resources are a separate question -- iface_make
 * releases everything EXCEPT when a driver abandons a device command, where the
 * request, its port, the device open and the query buffer are leaked on purpose
 * rather than freed while the driver can still write to them.
 */
struct ifnet *
sana_add_interface(char *ifname, char *devname, long devunit,
		   long ipreq, long wreq, long bps)
{
  struct ssconfig ssc;
  LONG unit_val = devunit;
  LONG ipreq_val = ipreq, wreq_val = wreq, bps_val = bps;
  int i;

  aligned_bzero_const((caddr_t)&ssc, sizeof ssc);

  /*
   * The unit is the TRAILING run of digits. Splitting at the FIRST digit assumed
   * an interface name never starts with one -- untrue of the names people use: a
   * 3Com PCMCIA config file named "3c589" gave base "" + unit 3, so the interface
   * was created, and displayed, as "3" (github issue #6).
   *
   * This must stay identical to ifunit() in net/if.c, INCLUDING the clamp: that is
   * how the interface is looked up again after it is created, and a base/unit pair
   * the lookup computes differently is an interface nobody can find.
   *
   *   "eth0"   -> "eth"  + 0        "3c589" -> "3c" + 589
   *   "wifipi" -> "wifipi" + 0      "3"     -> ""   + 3
   */
  {
    int n, b;

    for (n = 0; n < IFNAMSIZ - 1 && ifname[n] != '\0'; n++)
      ;
    for (b = n; b > 0 && ifname[b - 1] >= '0' && ifname[b - 1] <= '9'; b--)
      ;
    for (i = 0; i < b; i++)
      ssc.name[i] = ifname[i];
    ssc.name[b] = '\0';
    /* Clamped to what if_unit (a short) holds -- see ifunit(). */
    for (ssc.unit = 0, i = b; i < n; i++) {
      ssc.unit = ssc.unit * 10 + (ifname[i] - '0');
      if (ssc.unit > 32767) {
	ssc.unit = 32767;
	break;
      }
    }
  }

  ssc.flags = 0;			/* no ReadArgs RDArgs to free */
  ssc.args->a_name = (UBYTE *)ssc.name;
  ssc.args->a_dev  = (UBYTE *)devname;
  ssc.args->a_unit = &unit_val;
  /*
   * PORT (AmiTCP_NG): honour an explicit iprequests=/writerequests= from the
   * interface config (0 = unset -> ssconfig() uses the RAM-tiered default in
   * net/sana2config.c). a_ipno/a_writeno are LONG* into these locals, which stay
   * valid for the synchronous iface_make() call below.
   */
  if (ipreq > 0) ssc.args->a_ipno    = &ipreq_val;
  if (wreq  > 0) ssc.args->a_writeno = &wreq_val;
  /* Likewise bps=: 0 leaves the driver's reported S2_DEVICEQUERY BPS in place. */
  if (bps   > 0) ssc.args->a_bps     = &bps_val;
  /* All other ssc_args fields remain 0/NULL => ssconfig() uses wire defaults. */

  return iface_make(&ssc);
}

/*
 * Scrub every AF_INET address from `ifp`: in_ifscrub() each (removing its
 * connected route), unlink it from the interface's address list and the global
 * in_ifaddr list, and free it. Mirrors in_control()'s SIOCDIFADDR. The CALLER
 * must hold splimp() -- these lists are shared with interrupt-time completion.
 * Factored out of sana_remove_interface() so sana_offline_cleanup() can reuse it.
 */
static void
sana_scrub_inet(struct ifnet *ifp, int notify)
{
  struct ifaddr *ifa;
  struct in_ifaddr *ia, *oia, *p;
  extern struct in_ifaddr *in_ifaddr;
  extern void in_ifscrub(struct ifnet *, struct in_ifaddr *);

  while ((ifa = ifp->if_addrlist) != NULL) {
    if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
      ifp->if_addrlist = ifa->ifa_next;		/* skip (should not occur) */
      continue;
    }
    ia = (struct in_ifaddr *)ifa;
    /*
     * Tell this address's sockets before it is taken away -- once scrubbed there
     * is nothing left to match them on. BSD raises PRC_IFDOWN here and discards
     * it, leaving applications to find out when something eventually fails.
     * Safe from this context: the caller holds splimp() throughout, which is what
     * stops a concurrent soclose() freeing a pcb under the walk.
     */
    /*
     * NOTIFY ONLY WHEN THE STACK IS STAYING UP.
     *
     * Waking a blocked reader is right when ONE interface goes away: the socket
     * survives, the task returns with ENETDOWN, everything it touches is still
     * there. It is wrong during a whole-stack teardown. A client that ignored the
     * shutdown warnings is deliberately left with its base and sockets INTACT
     * (see api_abandon_bases) precisely because it may be asleep inside recv();
     * waking it here makes it runnable moments before mbdeinit() hands the whole
     * mbuf pool -- its socket, its buffers -- back to Exec. It is priority 0 and
     * the stack task is 5, so it cannot run until the stack task next blocks, and
     * when it does it resumes reading memory about to be freed underneath it.
     * Asleep and wedged is survivable; awake and reading freed pool memory, with
     * no MMU, is not.
     */
    if (notify) {
      extern void in_ifdown_notify(struct in_addr laddr, int error, int match_any);
      in_ifdown_notify(IA_SIN(ia)->sin_addr, ENETDOWN, 0);
    }
    in_ifscrub(ifp, ia);
    ifp->if_addrlist = ifa->ifa_next;		/* always the head here */
    oia = ia;
    if (oia == (ia = in_ifaddr)) {
      in_ifaddr = ia->ia_next;
    } else {
      for (p = in_ifaddr; p->ia_next && p->ia_next != oia; p = p->ia_next)
	;
      if (p->ia_next)
	p->ia_next = oia->ia_next;
    }
    (void)m_free(dtom(oia));
  }
}

/* rt_walk() lives in net/rtsock.c; walkarg is opaque (only passed through). */
struct walkarg;
extern int rt_walk(struct radix_node *rn,
		   int (*f)(struct radix_node *, struct walkarg *),
		   struct walkarg *w);

#define SANA_RTPURGE_MAX 32
struct sana_rtpurge {
  struct ifnet   *ifp;
  struct rtentry *rt[SANA_RTPURGE_MAX];
  int             n;
  int             overflow;
};

/*
 * rt_walk() callback: COLLECT every route that exits via this interface. We must
 * not delete here -- rt_walk() navigates the tree through the very nodes we'd
 * free -- so gather them (up to the array; set overflow to re-walk for the rest)
 * and delete after the walk completes.
 */
static int
sana_collect_ifroute(struct radix_node *rn, struct walkarg *wa)
{
  struct sana_rtpurge *w = (struct sana_rtpurge *)wa;

  for (; rn != NULL; rn = rn->rn_dupedkey) {
    struct rtentry *rt = (struct rtentry *)rn;
    if (rn->rn_flags & RNF_ROOT)		/* tree sentinel, not a route */
      continue;
    /* w->ifp == NULL means EVERY route, whatever it exits through -- used by the
     * whole-stack teardown sweep, where nothing in the table may outlive us. */
    if (w->ifp != NULL && rt->rt_ifp != w->ifp)
      continue;
    if (w->n < SANA_RTPURGE_MAX)
      w->rt[w->n++] = rt;
    else
      w->overflow = 1;
  }
  return (0);
}

/*
 * Report a route table that arrived non-empty from a previous stack instance.
 *
 * The table is bsd_malloc'd, so nothing frees it at teardown: it reaches the next
 * stack exactly as the last one left it. sana_deinit() sweeps it, so anything
 * still here means the sweep missed something -- and a missed route keeps rt_ifa
 * and rt_ifp pointing into pool memory that has since been handed back, which
 * ip_output() will follow on the next packet to that destination.
 *
 * Warn rather than delete: deleting would hide the teardown bug, and this runs
 * before any interface exists, so there is nothing to lose by leaving them for
 * the sweep to catch next time. A silent stale global is exactly how the loopback
 * address gap stayed invisible.
 */
static void
sana_warn_stale_routes(void)
{
  struct sana_rtpurge w;
  struct radix_node_head *rnh;
  spl_t s;

  /* Every other walker of this tree holds splimp(); nothing can be mutating it
   * at this point in bring-up, but matching the file's own stated invariant
   * costs two instructions and keeps the helper safe if it is ever called from
   * somewhere less quiet. */
  s = splimp();
  w.ifp = NULL; w.n = 0; w.overflow = 0;
  for (rnh = radix_node_head; rnh != NULL; rnh = rnh->rnh_next)
    if (rnh->rnh_af == AF_INET)
      rt_walk(rnh->rnh_treetop, sana_collect_ifroute, (struct walkarg *)&w);
  splx(s);
  /*
   * PROVEN TO WORK, not assumed. Called either side of the teardown sweep with a
   * temporary probe, this reported n=3 before and n=0 after, and n=0 again at the
   * next stack start -- so its silence in normal running means the table really is
   * empty, rather than meaning the walk never ran. A detector nobody has watched
   * fire is not a detector.
   */
  if (w.n > 0 || w.overflow)
    log(LOG_WARNING, "sana_init: route table was not empty at stack start "
	"(%ld left%s) -- teardown missed them; they point at freed memory\n",
	(long)w.n, w.overflow ? "+" : "");
}

/*
 * Drop EVERY route that exits via this interface before its ifaddr(s)/softc are
 * freed -- a classic BSD if_detach()-style purge. The old code deleted only the
 * default route and the DHCP 255.255.255.255 host route; any OTHER route bound to
 * this interface -- an ICMP-redirect (RTF_DYNAMIC) route, or one a caller added
 * via AddRouteTagList with an on-subnet gateway -- was left RTF_UP in the radix
 * tree with rt_ifa (freed by sana_scrub_inet) and rt_ifp (freed with the softc)
 * dangling, and ip_output() would deref it and make a wild if_output() call on the
 * next packet to that destination (a no-MMU use-after-free). The connected-network
 * route is caught here too, by its OWN key -- which deletes it correctly even if a
 * live SIOCSIFNETMASK desynced in_ifscrub()'s delete key (a later in_ifscrub()
 * delete then simply finds it already gone). Caller holds splimp() -- one
 * continuous critical section covering the whole collect+delete, so the table
 * cannot mutate under us.
 */
static void
sana_flush_iface_routes(struct ifnet *ifp)
{
  struct sana_rtpurge w;
  struct radix_node_head *rnh;
  int i, pass;

  /* Bounded re-walk: each pass deletes up to SANA_RTPURGE_MAX routes, so the cap
   * covers any realistic per-interface route count while ruling out an infinite
   * loop should a delete ever fail to make progress. */
  for (pass = 0; pass < 64; pass++) {
    w.ifp = ifp; w.n = 0; w.overflow = 0;
    for (rnh = radix_node_head; rnh != NULL; rnh = rnh->rnh_next) {
      if (rnh->rnh_af != AF_INET)
	continue;
      rt_walk(rnh->rnh_treetop, sana_collect_ifroute, (struct walkarg *)&w);
    }
    for (i = 0; i < w.n; i++) {
      struct rtentry *rt = w.rt[i];
      /* Delete by the route's OWN key/mask. RTM_DELETE re-finds the node and
       * never dereferences the gateway arg. We hold no reference: it unlinks the
       * route and frees it if rt_refcnt == 0; a still-referenced route is unlinked
       * (RTF_UP cleared, no longer matchable) and freed later by its holder. */
      (void)rtrequest(RTM_DELETE, rt_key(rt), rt->rt_gateway, rt_mask(rt),
		      rt->rt_flags, (struct rtentry **)0);
    }
    if (!w.overflow)
      break;
  }
  if (pass == 64)
    log(LOG_ERR, "sana_flush_iface_routes: route purge for %s hit the "
	"pass cap; some routes may still be bound to the interface\n",
	ifp == NULL ? "(all)" : (ifp->if_name ? ifp->if_name : "?"));
}

/*
 * Deconfigure an interface that has just gone offline: delete the routes bound to
 * its ifaddrs (default + DHCP broadcast host route), then scrub its IP
 * address(es). Called ONLY from sana_poll()'s deferred loop, which MUST already
 * hold splimp() -- that single continuous critical section is what makes this
 * safe: splimp() is a Forbid()-equivalent here (see sys/synch.h), so besides
 * excluding interrupt-time completion it stops another task's
 * sana_remove_interface() from unlinking and freeing this softc while we walk it
 * (no cross-task use-after-free). The blocking DNS teardown is deliberately NOT
 * done here (the caller does it once, outside splimp). The interface itself is
 * left in place for the automatic re-raise (sana_online -> sana_up).
 */
static void
sana_offline_cleanup(struct sana_softc *ssc)
{
  struct ifnet *ifp = (struct ifnet *)ssc;

  /*
   * ROUTES FIRST, AND UNCONDITIONALLY.
   *
   * This used to flush the routes only when the interface had CLAIMED them
   * (Roadshow's IFC_AssociatedRoute), on the reasoning that taking a device
   * offline is not a licence to delete a default route an operator added by
   * hand. Decent intent, impossible in practice: the scrub below frees this
   * interface's ifaddrs, and every route that exits through this interface
   * points at one of them through rt_ifa. Keeping such a route does not preserve
   * it -- it leaves it in the table with rt_ifa dangling.
   *
   * That is not a passive leak. rtrequest(RTM_DELETE) reads rt->rt_ifa and CALLS
   * ifa->ifa_rtrequest through it (net/route.c), so the next thing to delete that
   * route -- an interface removal, a stack teardown, the sweep in sana_deinit() --
   * takes a function pointer out of freed mbuf memory and jumps through it. With
   * no MMU, whatever has since reused that mbuf decides what happens. ip_output()
   * following rt_ifp on the next packet to that destination is the same story.
   *
   * A statically-configured interface with a hand-added route is exactly the case
   * that took this path, because it never sets ss_assoc_route.
   *
   * So the choice was never "keep the operator's route or delete it". It was
   * "delete it, or leave a landmine addressed to whoever deletes it next".
   */
  sana_flush_iface_routes(ifp);
  sana_scrub_inet(ifp, 1);

  sana_notify_if_last_gone();
}

/*
 * Was that the last one? A socket bound to INADDR_ANY is not tied to any single
 * interface, so it is left alone while another still carries an address -- but
 * once none do, there is no network left for it to wait on and it would otherwise
 * block for ever.
 *
 * Loopback does not count: it is always there, so counting it would mean this
 * never fires at all.
 *
 * Called from BOTH paths that strip an interface's addresses -- going offline and
 * being removed outright. Removal matters more, not less: an offlined interface
 * may reconfigure itself when its device returns, while a removed one never will,
 * so a socket left blocked there is stuck for good.
 *
 * Caller must hold splimp(): this walks the interface and address lists.
 */
static void
sana_notify_if_last_gone(void)
{
  extern void in_ifdown_notify(struct in_addr laddr, int error, int match_any);
  extern struct ifnet *ifnet;
  struct ifnet *p;
  struct ifaddr *ifa;
  struct in_addr none;
  int live = 0;

  for (p = ifnet; p != NULL && !live; p = p->if_next) {
    if (p->if_flags & IFF_LOOPBACK)
      continue;
    for (ifa = p->if_addrlist; ifa != NULL; ifa = ifa->ifa_next)
      if (ifa->ifa_addr != NULL && ifa->ifa_addr->sa_family == AF_INET) {
	live = 1;
	break;
      }
  }
  if (!live) {
    none.s_addr = 0;
    in_ifdown_notify(none, ENETDOWN, 1);
    log(LOG_NOTICE, "no network interface is configured any more");
  }
}

/*
 * PORT (AmiTCP_NG): tear down and free a SANA-II interface -- the mechanism
 * behind the Roadshow RemoveInterface() API (the counterpart to
 * sana_add_interface()/AddInterfaceTagList()). It reverses iface_make() for a
 * SINGLE interface, combining the device-teardown of sana_deinit() (which only
 * runs at total shutdown) with the address-scrub of in_control()'s SIOCDIFADDR
 * and the list-unlinking that no if_detach() provides here. Returns 0, or an
 * errno: EINVAL if `ifp` is not a SANA interface (e.g. the loopback, which was
 * not added this way), EBUSY if it is still up and `force` is false. Lives here,
 * beside the static sana_down()/sana_unrun() and the ssq list it must edit.
 */
int
sana_remove_interface(struct ifnet *ifp, int force)
{
  struct sana_softc *ssc = (struct sana_softc *)ifp;
  struct IOSana2Req *req;
  struct ifnet **q;
  struct sana_softc **pp;
  spl_t s;
  extern struct ifnet *ifnet;
  extern void ng_flush_dynamic_nameservers(void);

  if (ifp->if_type != IFT_SANA)
    return EINVAL;			/* not a SANA interface */
  if (!force && (ifp->if_flags & IFF_UP))
    return EBUSY;			/* in use -- bring it down first */

  /*
   * Disarm the read-ring re-arm BEFORE releasing the SANA request buffers below.
   * sana_down()/sana_unrun() free the IOIPReq pool while running OUTSIDE splimp()
   * (sana_unrun() yields in WaitIO() and leaves the freed requests linked on
   * ss_freereq). If the if_slowtimo watchdog (sana_watchdog) or a concurrent
   * sana_poll re-armed reads during that window, sana_send_read() would RemHead a
   * freed request and BeginIO() it -- a use-after-free. Clearing IFF_UP and the
   * watchdog under splimp() is atomic against both re-arm paths (sana_rearm_reads
   * is IFF_UP-gated; if_slowtimo skips a zero if_timer / NULL if_watchdog), so
   * once we drop splimp() here no re-arm can reach this interface again.
   */
  s = splimp();
  ssc->ss_removing       = 1;		/* also neutralise a racing sana_online()->sana_up() */
  ssc->ss_if.if_flags   &= ~IFF_UP;
  ssc->ss_if.if_watchdog = NULL;
  ssc->ss_if.if_timer    = 0;
  splx(s);

  /* Take it offline and release its SANA request buffers (as sana_deinit). */
  sana_down(ssc);
  if (ssc->ss_if.if_flags & IFF_RUNNING)
    sana_unrun(ssc);

  s = splimp();

  /* Delete the interface-bound routes (default + DHCP 255.255.255.255 host route)
   * BEFORE scrubbing the ifaddrs they reference, or their rt_ifa would dangle at
   * freed memory. Then scrub every AF_INET address (mirrors in_control()'s
   * SIOCDIFADDR). */
  sana_flush_iface_routes(ifp);
  sana_scrub_inet(ifp, 1);

  /* Unlink from the interface list (no if_detach() exists). */
  for (q = &ifnet; *q != NULL; q = &(*q)->if_next)
    if (*q == ifp) { *q = ifp->if_next; break; }

  /* Same duty as the offline path, and more pressing here: a removed interface
   * never reconfigures itself, so anything still waiting on it waits for good. */
  sana_notify_if_last_gone();

  /* Unlink from the SANA-II softc list. */
  for (pp = &ssq; *pp != NULL; pp = &(*pp)->ss_next)
    if (*pp == ssc) { *pp = ssc->ss_next; break; }

  /*
   * Detach any BPF capture channels bound to this interface, while we still hold
   * splimp() and before the softc (which embeds the struct ifnet) is freed, so no
   * bd_bif is left dangling. Kept inside this one critical section together with the
   * list edits above -- a clean single-critical-section teardown.
   */
  ng_bpf_ifdetach(ifp);

  splx(s);

  /*
   * Drop this interface's DHCP/runtime-added DNS servers now that it is gone (its
   * routes and addresses were removed above). This blocks on the NDB semaphore, so
   * it must run here at task level, OUTSIDE the splimp() region. Statically
   * configured servers (from the config file) are kept.
   */
  ng_flush_dynamic_nameservers();

  /*
   * Close the SANA-II device.
   *
   * (History: an earlier version hung when the BPF detach ran here, after
   * CloseDevice(), and that was mis-blamed on splimp()/splx() interacting with
   * CloseDevice() through the TDNestCnt-as-spl-level convention in sys/synch.h.
   * Measured and DISPROVEN: CloseDevice() leaves TDNestCnt balanced (-1 both
   * before and after), and an splimp()/splx() pair placed here runs fine with no
   * hang. The real fault was in that old detach code; keeping the detach in the
   * single critical section above is just the tidy way to do it, not a spl
   * workaround.)
   */
  req = CreateIOSana2Req(ssc);
  if (req) {
    CloseDevice((struct IORequest *)req);
    DeleteIOSana2Req(req);
  }

  /* The ARP table BEFORE the softc that points at it -- freeing the softc
   * first would take the only route to the table with it. */
  free_arptable(ssc);

  /* Release the softc (it also carries the exec device-name string). */
  bsd_free(ssc, M_IFNET);
  return 0;
}

/*
 * Allocate Sana-II IORequests for TCP/IP process
 */
static void
sana_run(struct sana_softc *ssc, int requests, struct ifaddr *ifa)
{
  int i;
  spl_t s = splimp();
  struct IOIPReq *req, *next = ssc->ss_reqs;
  
  /*
   * Configure the Sana-II device driver
   * (now with factory address)
   */
  if ((ssc->ss_if.if_flags & IFF_RUNNING) == 0) {
    struct IOSana2Req *req;
    /*
     * Every device command below is BOUNDED. This is interface bring-up -- the path
     * AddNetInterface and the DHCP helper both come through -- and a driver that
     * accepts a command and never completes it would hang whichever of them got
     * here, for ever, with nothing said. That is not hypothetical: a bare DoIO()
     * exactly like these ones wedged a machine in testing, and because the DHCP
     * helper is what calls in, the symptom was AddNetInterface hanging with no
     * output rather than anything pointing at the driver.
     *
     * Once a request has been abandoned it belongs to the driver and must not be
     * reused for the next command, nor freed -- so each step checks and bails out.
     */
    int abandoned = 0;

    if ((req = CreateIOSana2Req(ssc))) {
      req->ios2_Req.io_Command = S2_CONFIGINTERFACE;
      bcopy(ssc->ss_hwaddr, req->ios2_SrcAddr, ssc->ss_if.if_addrlen);

      (void)sana_doio_bounded(req, NG_DEVCMD_SECS, "S2_CONFIGINTERFACE",
			      (const char *)ssc->ss_name, &abandoned);
      if (abandoned) goto req_gone;
      /* An "already configured" reply is success (see the test below), not an error
       * worth logging -- only log a genuine configuration failure. */
      if (req->ios2_Req.io_Error &&
	  req->ios2_WireError != S2WERR_IS_CONFIGURED)
	sana2perror("S2_CONFIGINTERFACE", req);

      if (req->ios2_Req.io_Error == 0 ||
	  req->ios2_WireError == S2WERR_IS_CONFIGURED) {
	/* Mark us as running */
	ssc->ss_if.if_flags |= IFF_RUNNING;

	/* Take the device ONLINE. Some SANA-II drivers -- notably WiFi ones such as
	 * PiStorm's wifipi.device -- do not begin passing traffic on S2_CONFIGINTERFACE
	 * alone and need an explicit S2_ONLINE before they will send or receive; without
	 * it, DHCP fires its DISCOVERs into a still-offline device and simply times out.
	 * (a2065 and most wired drivers online themselves on configure, so this is a
	 * no-op for them.) An already-online driver returns S2ERR_BAD_STATE and one that
	 * does not distinguish the states returns S2ERR_NOT_SUPPORTED -- both are fine. */
	req->ios2_Req.io_Command = S2_ONLINE;
	(void)sana_doio_bounded(req, NG_DEVCMD_SECS, "S2_ONLINE",
				(const char *)ssc->ss_name, &abandoned);
	if (abandoned) goto req_gone;
	if (req->ios2_Req.io_Error &&
	    req->ios2_Req.io_Error != S2ERR_BAD_STATE &&
	    req->ios2_Req.io_Error != S2ERR_NOT_SUPPORTED &&
	    req->ios2_Req.io_Error != IOERR_NOCMD)
	  sana2perror("S2_ONLINE", req);

	if (ssc->ss_cflags & SSF_TRACK) {
#ifdef INET
	  /* Ask for packet type specific statistics. Tracking is OPTIONAL: a driver
	   * that does not implement it returns S2ERR_NOT_SUPPORTED, and one that
	   * already tracks the type returns S2WERR_ALREADY_TRACKED. Neither is an
	   * error -- the interface comes up fine either way -- so do not log them
	   * (that noise is exactly what pops the "AmiTCPIP Log" window on drivers
	   * without tracking). Only a genuine, unexpected failure is logged. */
	  req->ios2_Req.io_Command = S2_TRACKTYPE;
	  req->ios2_PacketType = ssc->ss_ip.type;
	  (void)sana_doio_bounded(req, NG_DEVCMD_SECS, "S2_TRACKTYPE",
				  (const char *)ssc->ss_name, &abandoned);
	  if (abandoned) goto req_gone;
	  /* It is *not* safe to turn tracking off */
	  if (req->ios2_Req.io_Error &&
	      req->ios2_Req.io_Error != S2ERR_NOT_SUPPORTED &&
	      req->ios2_Req.io_Error != IOERR_NOCMD &&
	      req->ios2_WireError != S2WERR_ALREADY_TRACKED)
	    sana2perror("S2_TRACKTYPE for IP", req);
	  if (ssc->ss_arp.reqno) {
	    req->ios2_Req.io_Command = S2_TRACKTYPE;
	    req->ios2_PacketType = ssc->ss_arp.type;
	    (void)sana_doio_bounded(req, NG_DEVCMD_SECS, "S2_TRACKTYPE",
				    (const char *)ssc->ss_name, &abandoned);
	    if (abandoned) goto req_gone;
	    if (req->ios2_Req.io_Error &&
		req->ios2_Req.io_Error != S2ERR_NOT_SUPPORTED &&
		req->ios2_Req.io_Error != IOERR_NOCMD &&
		req->ios2_WireError != S2WERR_ALREADY_TRACKED)
	      sana2perror("S2_TRACKTYPE for ARP", req);
	  }
#endif	
	}
      }
      DeleteIOSana2Req(req);
      goto req_done;
    req_gone:
      /* The driver kept it. It is still holding our memory, so it is not freed --
       * see sana_doio_bounded(): the reply port has been neutralised so a late
       * reply cannot signal anybody. */
      ;
    req_done:
      ;
    }
  }

  if ((ssc->ss_if.if_flags & IFF_RUNNING)) {
    /* Initialize ioRequests, add them into free queue */
    for (i = 0; i < requests ; i++) {
      if (!(req = CreateIORequest(SanaPort, sizeof(*req))))
	break;
      req->ioip_s2.ios2_Req.io_Device    = ssc->ss_dev;    
      req->ioip_s2.ios2_Req.io_Unit      = ssc->ss_unit;   
      req->ioip_s2.ios2_BufferManagement = ssc->ss_bufmgnt;
      aligned_bcopy(ssc->ss_hwaddr, req->ioip_s2.ios2_SrcAddr, ssc->ss_if.if_addrlen);
      req->ioip_s2.ios2_Req.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
      req->ioip_s2.ios2_Data = req;
      req->ioip_if = ssc;
      req->ioip_next = next;
      AddTail((struct List*)&ssc->ss_freereq, (struct Node*)req);
      next = req;
    }
    ssc->ss_reqs = next;
  }

  /*
   * Size the interface send queue to the transmit window. TCP can offer up to
   * ~tcp_sendspace/MTU segments in flight; if_snd must hold the excess over the
   * write-request ring so a burst is parked, not dropped (a drop returns ENOBUFS
   * and collapses cwnd). Scales with the RAM tier via tcp_sendspace (set by
   * ng_ram_tier at init, before any interface); floored at the IFQ_MAXLEN default
   * and capped so worst-case parked memory stays bounded. Small-tier machines
   * never fill it -- their window is small anyway.
   */
  { extern u_long tcp_sendspace;
    long mtu = ssc->ss_if.if_mtu ? ssc->ss_if.if_mtu : 1500;
    long q   = (long)(tcp_sendspace / mtu) + 16;	/* window in segments + slack */
    if (q < IFQ_MAXLEN) q = IFQ_MAXLEN;			/* never below the default 50 */
    if (q > 256)        q = 256;			/* bound parked memory */
    ssc->ss_if.if_snd.ifq_maxlen = (int)q;
  }
  splx(s);
}

/*
 * Free Sana-II IO Requests
 * Note: this is protected by splimp();
 */
static void
sana_unrun(struct sana_softc *ssc)
{
  struct IOIPReq *req, *next;

  /*
   * Hold SanaPortSem across the whole WaitIO()/free loop so the network task's
   * sana_poll() cannot GetMsg() (and dispatch, or Remove) these reply messages
   * while we drain and DeleteIORequest() them. WaitIO() Wait()s -- which breaks
   * the caller's Forbid -- but the semaphore survives that, keeping the net task
   * excluded until every request is reaped. (The device replies the aborted
   * requests from its own context, so WaitIO() still completes -- no deadlock.)
   *
   * Tradeoff: for this loop's duration sana_poll is blocked, pausing completion
   * draining for ALL SANA interfaces, not just the one being removed. Acceptable
   * because removal is a rare admin action and every request here was already
   * AbortSanaIO()'d by sana_down(), so its completion is prompt.
   */
  ObtainSemaphore(&SanaPortSem);

  for ( next = ssc->ss_reqs; (req = next) ;) {
    next = req -> ioip_next;

    /*
     * BOUNDED. This was a bare WaitIO(), on the stated assumption that sana_down()
     * had already aborted every request here so completion would be prompt. That
     * assumption rests entirely on the driver honouring AbortIO -- and this file
     * carries an A3-passing AbortSanaIO() workaround precisely BECAUSE several
     * SANA-II drivers (wifipi.device among them) ignore a plain one. When that
     * assumption fails the wait never ends, and because SanaPortSem is held across
     * this whole loop it does not merely hang the removal: sana_poll() stops for
     * EVERY interface on the machine. One driver refusing to answer took the whole
     * stack down with it.
     *
     * So: wait a bounded time, and if the request still has not come back, leave it
     * with the driver rather than blocking here for ever. It is not freed and its
     * mbufs are not freed -- the driver may still write into both -- which leaks,
     * deliberately, and says so. A bounded leak on a broken driver beats a machine
     * that has to be reset.
     */
    if (!CheckIO((struct IORequest *)req)) {
      int spin;
      AbortSanaIO((struct IORequest *)req);	/* ask again; sana_down already did */
      for (spin = 0; spin < NG_UNRUN_GRACE_TICKS &&
		     !CheckIO((struct IORequest *)req); spin++)
	Delay(2);				/* ~40 ms per turn */
    }

    if (!CheckIO((struct IORequest *)req)) {
      log(LOG_ERR, "%s: driver will not return a request on teardown -- leaving it "
	  "and its buffers allocated rather than waiting for ever\n", ssc->ss_name);
      /*
       * CUT IT LOOSE PROPERLY. Leaving the request allocated is not enough: it still
       * carries ioip_if, a pointer to the softc that sana_remove_interface() is about
       * to bsd_free(), and ioip_dispatch, a function pointer. Its reply port is the
       * SHARED SanaPort, so if the driver ever does answer, sana_poll() picks the
       * message up on behalf of some OTHER live interface and, before it checks
       * anything, does get_time(&io->ioip_if->ss_if.if_lastchange) and then calls
       * through ioip_dispatch -- a write into freed memory followed by a jump through
       * whatever now occupies it. With no MMU that is not a crash you get to debug.
       *
       * sana_doio_bounded() solves the same problem by neutralising its reply port,
       * but it can only do that because its port is private to one call; SanaPort is
       * shared with every interface on the machine and must keep signalling. So the
       * REQUEST is neutralised instead, and sana_poll() skips one that has been.
       */
      req->ioip_if       = NULL;
      req->ioip_dispatch = NULL;
      continue;
    }

    WaitIO((struct IORequest *)req);		/* it is complete; this cannot block */
    /* Free the mbufs this request still holds. The normal completion path
     * (sana_read()/free_written_packet()) frees these, but teardown bypasses it,
     * so a posted read leaks ioip_reserved and an in-flight write leaks ioip_packet.
     * WaitIO() above guarantees the device is finished with them; idle free-list
     * requests have both NULL. */
    if (req->ioip_reserved) m_freem(req->ioip_reserved);
    if (req->ioip_packet)   m_freem(req->ioip_packet);
    DeleteIORequest(req);
  }
  ssc->ss_reqs = next;

  ssc->ss_if.if_flags &= ~IFF_RUNNING;

  /*
   * PORT (AmiTCP_NG): replace any SanaPort wakeup this loop consumed.
   *
   * WaitIO() above Wait()s on SanaPort's signal bit, which is SHARED by every
   * interface. If a still-live interface's read or write completed while we
   * were blocked waiting for one of THIS interface's requests, that Wait()
   * cleared the bit and removed only its own message -- leaving the other
   * interface's completion sitting in the port with its wakeup gone, and
   * sana_poll() (which is gated on that bit) never called again for it. That
   * is the same permanent silent death described in sana_start(), reached by a
   * different route.
   *
   * ONE signal after the loop is sufficient rather than one per WaitIO():
   * Exec signals are a level, not a count, and a single sana_poll() drains the
   * whole port -- so N consumed wakeups are all repaired by one bit. The
   * network task cannot act on it before we get here anyway, since it blocks
   * on SanaPortSem, which we hold across the entire loop.
   *
   * Guarded, unlike the other two signal sites: this one can run during a full
   * stack shutdown. UL_Close()'s early return for a base that was ever SHARED
   * decrements the master open count WITHOUT waiting for sharers (deliberately
   * -- see api/amiga_api.c), so the count can reach the shutdown threshold
   * while a sharer task is still inside this loop. If sana_deinit() then gets
   * as far as DeleteMsgPort(SanaPort)/SanaPort = NULL, this would dereference
   * NULL. Exotic -- it needs shared-base mode, a shutdown and a slow-aborting
   * device at once -- and the same window is already a latent hazard for the
   * WaitIO() above, but the guard is free.
   */
  if (SanaPort)
    Signal(SanaPort->mp_SigTask, 1L << SanaPort->mp_SigBit);

  ReleaseSemaphore(&SanaPortSem);
}

/*
 * Generic SANA-II interface ioctl
 *
 * Interface setup is thru IOCTL.
 */
int 
sana_ioctl(register struct ifnet *ifp, int cmd, caddr_t data)
{
  register struct sana_softc *ssc = (struct sana_softc*)ifp;
  register struct ifaddr *ifa = (struct ifaddr *)data;
  register struct ifreq *ifr = (struct ifreq *)data;
  
  spl_t s = splimp();
  int error = 0;

  /*
   * Reject any ioctl on an interface being torn down (ss_removing set under
   * splimp by sana_remove_interface). SIOCSIFADDR -> sana_run() would otherwise
   * re-create the request pool and re-online the device on a softc about to be
   * freed; SIOCSIFFLAGS -> sana_up() would re-raise it. Removal runs in a client
   * task that is not serialised against this one, so honour the flag here too --
   * this closes the sana_run() resurrection path that the sana_up() guard alone
   * does not cover.
   */
  if (ssc->ss_removing) {
    splx(s);
    return ENXIO;
  }

  switch (cmd) {

  case SIOCSIFFLAGS:
    if ((ifr->ifr_flags & (IFF_UP|IFF_RUNNING)) == (IFF_UP|IFF_RUNNING))
      sana_up(ssc);
    /* Call sana_down() in every case */
    if ((ifr->ifr_flags & IFF_UP) == 0) 
      sana_down(ssc);
    if ((ifr->ifr_flags & IFF_NOARP) == 0)
      alloc_arptable(ssc, 0);
    break;

    /*
     * Set interface address (and mark interface up).
     */
  case SIOCSIFADDR:		/* Set Interface Address */
    if (!(ssc->ss_if.if_flags & IFF_RUNNING)) 
      sana_run(ssc, ssc->ss_reqno, ifa);
    if ((ssc->ss_if.if_flags & IFF_RUNNING) && !(ssc->ss_if.if_flags & IFF_UP))
      sana_up(ssc);
    /*
     * PORT (AmiTCP_NG) fix: read the flags from the interface, not from `ifr`.
     * For SIOCSIFADDR the ioctl `data` argument is a struct in_ifaddr * (see
     * in.c's (*ifp->if_ioctl)(ifp, SIOCSIFADDR, (caddr_t)ia)), NOT a struct
     * ifreq * -- the two aliases of `data` are not interchangeable per-command.
     * ifr_flags sits at the same offset as struct ifaddr's ifa_next, so this
     * was testing the top half of a pointer as if it were IFF_NOARP.
     */
    if ((ssc->ss_if.if_flags & IFF_NOARP) == 0)
      alloc_arptable(ssc, 0);

  case SIOCAIFADDR:		/* Alter Interface Address */
    switch (ifa->ifa_addr->sa_family) {
#if INET
    case AF_INET:
      ssc->ss_ipaddr = IA_SIN(ifa)->sin_addr;
      break;
#endif
    }
    break;

  case SIOCSIFDSTADDR:		/* Sets P-P-link destination address */
    break;

  default:
    error = EINVAL;
    break;
  }
  splx(s);
  return (error);
}

/*
 * sana_send_read(): 
 * send read requests with given types, dispatcher & c  
 * MUST be called at splimp()
 */
static inline WORD
sana_send_read(struct sana_softc *ssc, WORD count, ULONG type, ULONG mtu,
	       void (*dispatch)(struct sana_softc *, struct IOIPReq *),
	       UWORD command, UBYTE flags, UBYTE quiet)
{
  struct IOIPReq *req = NULL;
  WORD i;

  for (i = 0; i < count; i++) {
    if (!(req = (struct IOIPReq*)RemHead((struct List*)&ssc->ss_freereq)))
      return i;
    req->ioip_dispatch = dispatch;
    req->ioip_s2.ios2_PacketType = type;
    req->ioip_Command = command;
    req->ioip_s2.ios2_Req.io_Flags = flags;
    if (!ioip_alloc_mbuf(req, mtu))
      goto no_resources;
    sana_submit(req);
  }
  return i;

 no_resources:
  if (req)
    AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
  /* `quiet` suppresses this on the steady-state re-arm path (sana_rearm_reads),
   * which retries every sana_poll during pool pressure and would otherwise flood
   * the log; sana_up() (config time) still reports it. ss_rxnobuf already counts
   * the runtime failures. */
  if (!quiet)
    log(LOG_ERR, "sana: could not queue enough read requests\n");
  return i;
}

/*
 * sana_up():
 * send read requests
 */
static void
sana_up(struct sana_softc *ssc)
{
  spl_t s = splimp();

  /* If the interface is being torn down (ss_removing set under splimp by
   * sana_remove_interface), do NOT re-raise IFF_UP, post reads, or re-arm the
   * watchdog: a sana_online() dispatched from a racing S2EVENT_ONLINE completion
   * must not resurrect a doomed interface whose request pool is being freed. */
  if (ssc->ss_removing) {
    splx(s);
    return;
  }
  ssc->ss_if.if_flags |= IFF_UP;
  
  /* Send read requests to device driver */
#if	INET
  /* IP */
  ssc->ss_ip.sent += 
    sana_send_read(ssc, ssc->ss_ip.reqno - ssc->ss_ip.sent, ssc->ss_ip.type,
		   ssc->ss_if.if_mtu, sana_ip_read, CMD_READ, 0, 0);

  ssc->ss_arp.sent += 
    sana_send_read(ssc, ssc->ss_arp.reqno - ssc->ss_arp.sent, ssc->ss_arp.type,
		   ARP_MTU, sana_arp_read, CMD_READ, 0, 0);

#endif /* INET */
#if	ISO
#endif /* ISO */
#if	CCITT
#endif /* CCITT */
#if	NS
#endif /* NS */
#if 0
  ssc->ss_rawsent += 
    sana_send_read(ssc, ssc->ss_rawreqno - ssc->ss_rawsent, 0,
		   ssc->ss_if.if_mtu, sana_raw_read,
		   S2_READORPHAN, SANA2_IOF_RAW, 0);
#endif
  /*
   * Arm the read-ring re-arm watchdog. if_slowtimo() (net/if.c, driven by the
   * ~1 s timer in amiga_time.c) calls sana_watchdog once if_timer counts down to
   * 0; the watchdog tops the read rings back up and re-arms if_timer. This is the
   * backstop that recovers a receive ring which has bled to zero under mbuf-pool
   * pressure -- the case where no device completion arrives to drive sana_poll().
   */
  ssc->ss_if.if_watchdog = sana_watchdog;
  ssc->ss_if.if_timer    = 1;
  splx(s);
  return;
}

/*
 * sana_rearm_reads(): top the SANA-II receive rings back up to their configured
 * depth (ss_ip.reqno / ss_arp.reqno). sana_read() retires a read whose re-post
 * failed under transient mbuf-pool pressure -- it decrements *sent and parks the
 * request on ss_freereq WITHOUT re-posting -- so without a re-arm the ring bleeds
 * monotonically down under a sustained transfer until RX stalls with no recovery.
 * Re-post the shortfall as mbufs become available; sana_send_read() posts as many
 * as the pool currently allows (returns the count actually posted) and we retry
 * on the next call. Holds splimp(): the request lists and *sent are shared with
 * the interrupt-time completion path. Safe to nest under a caller's splimp().
 */
static void
sana_rearm_reads(struct sana_softc *ssc)
{
  spl_t s = splimp();

  if ((ssc->ss_if.if_flags & IFF_UP) && !ssc->ss_removing) {
#if	INET
    if (ssc->ss_ip.sent < ssc->ss_ip.reqno)
      ssc->ss_ip.sent +=
	sana_send_read(ssc, ssc->ss_ip.reqno - ssc->ss_ip.sent, ssc->ss_ip.type,
		       ssc->ss_if.if_mtu, sana_ip_read, CMD_READ, 0, 1);

    if (ssc->ss_arp.sent < ssc->ss_arp.reqno)
      ssc->ss_arp.sent +=
	sana_send_read(ssc, ssc->ss_arp.reqno - ssc->ss_arp.sent, ssc->ss_arp.type,
		       ARP_MTU, sana_arp_read, CMD_READ, 0, 1);
#endif	/* INET */
  }
  splx(s);
}

/*
 * sana_watchdog(): the per-interface if_slowtimo watchdog (~1 s tick). Re-arm any
 * retired reads, then re-arm if_timer so it fires again next tick. Called from
 * if_slowtimo() with splimp() already held (sana_rearm_reads nests its own).
 */
static int
sana_watchdog(struct ifnet *ifp)
{
  struct sana_softc *ssc = (struct sana_softc *)ifp;

  sana_rearm_reads(ssc);
  /*
   * PORT (AmiTCP_NG): also wake the network task, unconditionally.
   *
   * The re-arm above is not enough on its own. It is gated on
   * `sent < reqno`, and `sent` is only ever decremented by sana_read(), which
   * runs only from sana_poll() -- so in exactly the state worth recovering
   * from (sana_poll() not running) the counters are stale, the gate is false,
   * and this watchdog is a no-op. Signalling is not gated on anything.
   *
   * This is the backstop of last resort: it bounds ANY lost-wakeup on
   * SanaPort -- including causes not yet identified -- at one if_slowtimo tick
   * (~1s) instead of "until the machine is rebooted". It is exactly what a
   * `ping 127.0.0.1` does by hand, via schednetisr(), which is how this class
   * of failure was found. Signalling our own task is safe and cheap: this runs
   * on the network task itself (if_slowtimo <- timer_poll <- the main loop),
   * and a signal with nothing to do costs one sana_poll() that finds an empty
   * port.
   */
  Signal(SanaPort->mp_SigTask, 1L << SanaPort->mp_SigBit);

  /*
   * And, for an interface whose device went away on its own, one step of the
   * probe that looks for it coming back. This runs while the interface is DOWN,
   * which is the whole reason it lives here: if_slowtimo() calls a watchdog on
   * if_timer alone and does not care about IFF_UP, and nothing but
   * sana_remove_interface() ever stops the timer -- so this tick keeps arriving
   * when every other part of the receive path has gone quiet.
   */
  sana_probe_step(ssc);

  ssc->ss_if.if_timer = 1;		/* keep firing every if_slowtimo tick */
  return 0;
}

#if __SASC
/*
 * "Fix" for numerous sana2 drivers, which expect to get Unit * in the
 * register A3 when their AbortIO function is called.
 * Note that Exec AbortIO() does NOT put it there.
 */
extern VOID _AbortSanaIO(struct IORequest *, struct Unit *);
#pragma libcall DeviceBase _AbortSanaIO 24 B902

static inline __asm VOID
AbortSanaIO(register __a1 struct IORequest *ioRequest)
{
#define DeviceBase ioRequest->io_Device
  _AbortSanaIO(ioRequest, ioRequest->io_Unit);
#undef DeviceBase
}
#elif defined(__GNUC__)
/*
 * The same fix for GCC (bebbo m68k-amigaos-gcc, this project's toolchain). Exec's
 * AbortIO() calls the device's AbortIO vector with the IORequest in A1 but leaves
 * A3 undefined; numerous SANA-II drivers (e.g. PiStorm's wifipi.device) read their
 * Unit from A3 in AbortIO and, without it, silently do nothing -- so the aborted
 * CMD_READ never completes and sana_unrun()'s WaitIO() hangs forever on teardown.
 * Call the AbortIO vector (device offset -36) directly with A1 = IORequest,
 * A3 = its Unit, A6 = its Device, exactly as the SAS/C _AbortSanaIO libcall does.
 */
static inline void
AbortSanaIO(struct IORequest *ioRequest)
{
  register struct IORequest *_a1 __asm("a1") = ioRequest;
  register struct Unit      *_a3 __asm("a3") = ioRequest->io_Unit;
  register struct Device    *_a6 __asm("a6") = ioRequest->io_Device;
  __asm__ __volatile__("jsr a6@(-36)"
		       : "+r"(_a1), "+r"(_a3)
		       : "r"(_a6)
		       : "d0", "d1", "a0", "a2", "cc", "memory");
}
#else /* implement later for other compilers */
#define AbortSanaIO AbortIO
#endif

/*
 * ---------------------------------------------------------------------------
 * The watchdog probe: noticing a device that came back without saying so.
 * ---------------------------------------------------------------------------
 *
 * When a SANA-II device drops out from under a live interface -- a cable pulled,
 * a WiFi radio de-associating -- we find out because the pending CMD_READs
 * complete with S2ERR_OUTOFSERVICE (sana_read). We then ask to be told when it
 * returns, with S2_ONEVENT, and sana_online() puts the interface back together.
 *
 * That is the only mechanism there was, and it fails two ways. A driver may
 * refuse S2_ONEVENT outright (handled: ss_noevents). Worse, a driver may ACCEPT
 * S2_ONEVENT and then never complete it, even though the hardware is back --
 * which is silent, indistinguishable from a device that really is still gone, and
 * is the reported behaviour on real hardware. Neither event handling nor better
 * event masks can fix a driver that never posts the event, so we have to go and
 * look for ourselves.
 *
 * WHAT WE LOOK WITH. The SANA-II specification, S2_OFFLINE, NOTES:
 *
 *     While the interface is offline, all read, writes and any other command
 *     that touches interface hardware will be rejected with ios2_Error set to
 *     S2ERR_OUTOFSERVICE.
 *
 * So a CMD_READ is a hardware-touching command: rejected while the device is
 * offline, accepted while it is online. That makes an ordinary read the probe,
 * with no side effects and nothing special asked of the driver.
 *
 * WHAT WE DELIBERATELY DO NOT DO. We never send S2_ONLINE to find out. S2_ONLINE
 * is a command, not a question -- the spec has it reinitialise the hardware and
 * reset the unit's statistics -- so using it as a probe would both destroy
 * counters and force back up a device somebody else may have offlined on purpose.
 * S2_ONLINE is sent only when an operator asks for it, from the IFC_State
 * SM_Online path. Nor do we probe with S2_GETGLOBALSTATS, S2_DEVICEQUERY or
 * S2_GETSTATIONADDRESS: those can be answered from software or cached state, so
 * they are not required to fail while offline and prove nothing.
 *
 * THE VERDICT IS ONLY EVER DECLARED WITH THE REQUEST IN HAND. The tempting
 * shortcut -- "the read has not been rejected in five seconds, so the device must
 * be back" -- decides while the very request that constitutes the evidence is
 * still outstanding on the driver, and that is what makes it wrong: the same
 * request may simultaneously be being aborted by an operator taking the interface
 * down, so the interface gets resurrected seconds after somebody deliberately
 * downed it. Instead the timeout ABORTS the probe, and the verdict is reached in
 * sana_probe_read() where the request has actually come back:
 *
 *     completed, io_Error == 0            a frame arrived: the device is carrying
 *                                         traffic. Online.
 *     completed IOERR_ABORTED, by US      the driver held our read for a full
 *                                         interval rather than rejecting it, and
 *                                         gave it back when asked. Online.
 *     completed S2ERR_OUTOFSERVICE        still offline. Wait and probe again.
 *     completed IOERR_ABORTED, not by us  something else is taking this interface
 *                                         down. Not our business; do nothing.
 *
 * Only an interface with ss_wantback set is probed at all, and that flag is set
 * in exactly one place: sana_read()'s S2ERR_OUTOFSERVICE branch, which is reached
 * only while IFF_UP is still set -- i.e. the device went away without being asked.
 * An operator-requested offline clears IFF_UP before the device is offlined, so it
 * never sets the flag, and sana_down() clears the flag outright. An interface that
 * was put down on purpose is never brought back up by this code.
 */

/*
 * Post one probe read. Returns 1 if it went to the driver, 0 if there was no free
 * request or no mbuf to receive into -- in which case the caller simply tries
 * again on the next tick. MUST be called at splimp().
 */
static int
sana_post_probe(struct sana_softc *ssc)
{
  struct IOIPReq *req;

  if (!(req = (struct IOIPReq *)RemHead((struct List *)&ssc->ss_freereq)))
    return 0;

  req->ioip_dispatch		 = sana_probe_read;
  req->ioip_s2.ios2_PacketType	 = ssc->ss_ip.type;
  req->ioip_Command		 = CMD_READ;
  req->ioip_s2.ios2_Req.io_Flags = 0;

  if (!ioip_alloc_mbuf(req, ssc->ss_if.if_mtu)) {
    req->ioip_dispatch = NULL;
    AddHead((struct List *)&ssc->ss_freereq, (struct Node *)req);
    return 0;
  }

  ssc->ss_probe_req   = req;
  ssc->ss_probing     = 1;
  ssc->ss_probe_abort = 0;
  sana_submit(req);
  return 1;
}

/*
 * One tick of the probe state machine, driven by sana_watchdog(). Runs at
 * splimp() (if_slowtimo holds it across the whole interface walk).
 */
static void
sana_probe_step(struct sana_softc *ssc)
{
  if (!ssc->ss_wantback || ssc->ss_removing ||
      (ssc->ss_if.if_flags & IFF_UP))
    return;

  if (ssc->ss_probe_wait) {
    ssc->ss_probe_wait--;
    return;
  }

  if (!ssc->ss_probing) {
    if (sana_post_probe(ssc))
      ssc->ss_probe_wait = NG_PROBE_SECS;
    return;				/* no resources: retry on the next tick */
  }

  if (!ssc->ss_probe_abort) {
    /*
     * The read has been with the driver for a full interval without being
     * rejected. Ask for it back; whether and how it returns is the answer.
     */
    ssc->ss_probe_abort = 1;
    ssc->ss_probe_wait  = NG_PROBE_ABORT_SECS;
    AbortSanaIO((struct IORequest *)ssc->ss_probe_req);
    return;
  }

  /*
   * The driver was asked for the request back and has not returned it. There is
   * no further evidence to gather and nothing safe to assume: guessing "online"
   * here would bring the interface up over a device that may be dead, and
   * guessing "offline" would leak the request on every cycle. Say so once -- a
   * driver that ignores AbortIO breaks interface teardown too, so this names a
   * real fault rather than hiding it -- and stop probing this interface.
   */
  if (!ssc->ss_probe_stuck) {
    ssc->ss_probe_stuck = 1;
    log(LOG_ERR, "%s: driver did not return an aborted read, so whether its "
	"device is back cannot be told\n", ssc->ss_name);
  }

  /*
   * Keep asking, rather than giving up for good. A wedged driver may come back to
   * itself -- and returning that request is the only thing that lets the probe
   * resume, so a one-shot complaint would leave the interface permanently stuck
   * AND permanently one request short. Do NOT post a fresh probe instead: the
   * request pool is finite and a driver that swallowed one read will swallow
   * every one it is given. One AbortIO per interval is cheap.
   */
  ssc->ss_probe_wait = NG_PROBE_SECS;
  AbortSanaIO((struct IORequest *)ssc->ss_probe_req);
}

/*
 * A probe read came back. This is the ONLY place the probe declares a verdict,
 * and it has the request in hand when it does. Called from sana_poll().
 */
static void
sana_probe_read(struct sana_softc *ssc, struct IOIPReq *req)
{
  spl_t s = splimp();
  LONG  err	   = req->ioip_Error;
  UBYTE weaborted  = ssc->ss_probe_abort;

  /*
   * Retire the request. Both mbuf fields can be set at once -- the driver copies
   * a frame into ioip_packet and leaves whatever it did not need on
   * ioip_reserved -- and m_freem(NULL) is a no-op, so free both unconditionally.
   */
  m_freem(req->ioip_packet);
  req->ioip_packet = NULL;
  m_freem(req->ioip_reserved);
  req->ioip_reserved = NULL;
  req->ioip_dispatch = NULL;
  AddHead((struct List *)&ssc->ss_freereq, (struct Node *)req);

  if (ssc->ss_probe_req == req) {
    ssc->ss_probe_req	= NULL;
    ssc->ss_probing	= 0;
    ssc->ss_probe_abort = 0;
    ssc->ss_probe_stuck = 0;
    ssc->ss_probe_wait	= NG_PROBE_SECS;	/* pace the next probe */
  }
  splx(s);

  /*
   * IOERR_ABORTED counts only if WE asked for it back. sana_down() aborts every
   * outstanding request when an interface is taken down, and it clears
   * ss_probe_abort in the same breath, so an abort that came from there cannot
   * be read as the device announcing itself.
   */
  if (err == 0)
    sana_back_online(ssc, "watchdog probe: its driver delivered a frame");
  else if (err == IOERR_ABORTED && weaborted)
    sana_back_online(ssc, "watchdog probe: its driver accepted a read");
}

/*
 * The device is usable again: put the interface back. Shared by the S2_ONEVENT
 * path (sana_online) and the watchdog probe, so the two cannot drift apart.
 *
 * Guarded and idempotent, which is the point of it being one function. Both
 * routes can reach here for the same return -- a driver that posts its event
 * just as a probe wins -- and doing this work twice means a second sana_up() and,
 * far worse, a second ss_reconfig: sana_reconfig_poll() leaves ss_reconfig set
 * when a reconfigure is already running, so the duplicate is not dropped, it is
 * merely deferred into a second DHCP client for the same interface. ss_wantback
 * is the interlock: the first caller through clears it and the second finds
 * nothing to do.
 *
 * Does NOT send S2_ONLINE. Whoever got here did so by watching the device accept
 * work, so it is already online; and if it were not, forcing it would override an
 * operator who offlined it on purpose.
 */
static void
sana_back_online(struct sana_softc *ssc, const char *how)
{
  spl_t s = splimp();

  if (!ssc->ss_wantback || ssc->ss_removing) {
    splx(s);
    return;
  }

  /*
   * Reclaim the LOSER. Two watchers can be armed at once -- the S2_ONEVENT armed
   * when the device dropped, and the probe read posted a few seconds later -- and
   * whichever gets here first ends the watch for both. The probe read must be
   * asked back explicitly, because nothing else will ever look at it again:
   * sana_probe_step() returns immediately once ss_wantback is clear, so a probe
   * left outstanding here stays outstanding until the interface is torn down,
   * holding a request out of a finite pool and swallowing the next frame that
   * arrives on it.
   *
   * (When the PROBE is the winner this is a no-op -- sana_probe_read() has already
   * cleared ss_probing and has the request in hand. The reverse case, an
   * S2_ONEVENT left armed after the probe won, needs nothing: any driver that
   * completes events at all will complete it on this very transition, and
   * sana_online() retires it properly. A driver that never completes it was never
   * going to return it whatever we did -- that is the fault being worked around.)
   */
  if (ssc->ss_probing && ssc->ss_probe_req != NULL)
    AbortSanaIO((struct IORequest *)ssc->ss_probe_req);

  ssc->ss_wantback    = 0;
  ssc->ss_probing     = 0;
  ssc->ss_probe_abort = 0;
  ssc->ss_probe_stuck = 0;
  ssc->ss_probe_wait  = 0;
  ssc->ss_eventfails  = 0;		/* it answered: forget earlier refusals */
  splx(s);

  /*
   * Say WHICH mechanism found the device, because on a machine that is misbehaving
   * that is the useful half of the message. "the driver said so" and "we had to go
   * and look, because the driver never said anything" describe two very different
   * systems, and telling them apart from the log is otherwise impossible.
   */
  log(LOG_NOTICE, "%s is online again (%s).\n", ssc->ss_name, how);
  sana_up(ssc);
  /*
   * sana_up() only re-raises the DEVICE. Everything that made this a usable
   * interface -- address, net mask, default route, name servers -- was scrubbed
   * when it went offline, so without this the interface comes back unnumbered and
   * silent. Ask sana_poll() to configure it again; not here, because this may run
   * in the IO completion path and reconfiguration takes semaphores, opens sockets
   * and may spawn the DHCP client.
   */
  ssc->ss_reconfig = 1;
}

/*
 * sana_down(): Mark interface as down, abort all pending requests
 */
static BOOL
sana_down(struct sana_softc *ssc)
{
  extern void if_qflush(struct ifqueue *);
  spl_t s = splimp();
  struct IOIPReq *req = ssc->ss_reqs;

  /* Free any packets still parked on the interface send queue -- they will never
   * be sent once we abort the device. if_qflush() m_freem()s each queued tag,
   * which frees the tag AND its chained real packet. Idempotent: a no-op if the
   * offline path already flushed via if_down(). */
  if_qflush(&ssc->ss_if.if_snd);

  /*
   * Stop watching for the device's return, NOW, synchronously, before a single
   * request is aborted.
   *
   * This is what keeps an operator's decision from being undone. sana_down() is
   * the one choke point every deliberate down goes through, and the abort below
   * is asynchronous -- it returns long before the driver replies. Leaving
   * ss_wantback set until those replies were processed would leave a window in
   * which the watchdog still believes it is chasing a device that vanished on its
   * own, and an interface an operator has just taken down could be brought back
   * up, with a fresh DHCP lease, seconds later. Clearing it here closes the window
   * completely: if_slowtimo() runs the watchdog under the same splimp()/Forbid
   * this holds, so the next tick cannot see a half-updated state.
   */
  ssc->ss_wantback    = 0;
  ssc->ss_probing     = 0;
  ssc->ss_probe_abort = 0;
  ssc->ss_probe_stuck = 0;
  ssc->ss_probe_wait  = 0;
  ssc->ss_probe_req   = NULL;

  /* Completed, Remove()'d requests are not aborted */
  while (req) {
    if (!CheckIO((struct IORequest*)req)) {
      AbortSanaIO((struct IORequest*)req);
    }
    req = req->ioip_next;
  }

  splx(s);

  return(TRUE);
}

/*
 * sana_read --- collect a received packet from a completed read IORequest.
 *
 * The receive side is asynchronous: we keep a pool of CMD_READ IORequests queued
 * on the device, and as each frame arrives the driver fills one request -- copying
 * the payload into an mbuf chain via our S2_CopyToBuff callback -- and completes
 * it, waking the main task. sana_poll() then calls the per-type reader
 * (sana_ip_read/sana_arp_read), which calls THIS to turn the finished request back
 * into an mbuf chain the stack can consume.
 *
 * Here we:
 *  - detach the already-filled mbuf chain (req->ioip_packet) from the request;
 *  - on success, propagate the broadcast/multicast flags onto the mbuf;
 *  - on S2ERR_OUTOFSERVICE (someone took the driver offline), bring the BSD
 *    interface down (if_down), free the packet, and arm an S2_ONEVENT so we get
 *    told when the driver comes back (sana_online re-raises the interface);
 *  - on any other error, drop the packet.
 * The caller re-queues a fresh read so the device always has somewhere to put the
 * next frame. Returns the mbuf chain to hand upstream, or NULL if none.
 *
 * Runs at splimp(): the request lists are shared with interrupt-time completion.
 */
static struct mbuf *
sana_read(struct sana_softc *ssc, struct IOIPReq *req,
	  UWORD  flags, UWORD *sent, const char *banner, size_t mtu)
{
  register struct mbuf *m = req->ioip_packet;
  register spl_t s = splimp();

  req->ioip_packet = NULL;

  switch (req->ioip_Error) {
  case 0:
    /* A driver that completes a read with io_Error == 0 but never ran CopyToBuff
     * (m_copy_to_mbuf returned FALSE -- e.g. a rejected over/undersized or zero-
     * length frame) leaves ioip_packet, and so m, NULL. Writing m->m_flags then
     * would be a wild write near address 0 on this no-MMU 68k. Nothing to hand
     * up; the read is re-armed below. */
    if (m == NULL)
      break;
    if (req->ioip_s2.ios2_Req.io_Flags & SANA2IOF_BCAST)
      m->m_flags |= M_BCAST;
    if (req->ioip_s2.ios2_Req.io_Flags & SANA2IOF_MCAST)
      m->m_flags |= M_MCAST;
    break;
  case S2ERR_OUTOFSERVICE:
    /*
     * Somebody put Sana-II driver offline.
     * We put down also the network interface 
     */
    if (ssc->ss_if.if_flags & IFF_UP) {
      /* Show a log message */
      sana2perror(ssc->ss_if.if_name, (struct IOSana2Req *)req);

      /* tell it to protocols */
      if_down((struct ifnet *)ssc);

      /* Deconfigure this interface (default route / address / dynamic DNS) once
       * the driver has gone offline. The teardown touches the routing table and
       * address lists, so it must not run here at splimp() in the completion path;
       * defer it to sana_poll(), which runs in the network task. */
      ssc->ss_offcleanup = 1;

      /*
       * This branch is reached ONLY with IFF_UP still set, which is precisely
       * what makes it the involuntary case: an operator-requested offline clears
       * IFF_UP first and so never lands here. So this, and only this, is where we
       * take on the job of watching for the device to come back -- see the probe
       * block comment above sana_post_probe(). The watchdog is (re-)armed
       * explicitly because the probe is driven by it and by nothing else.
       */
      ssc->ss_wantback	     = 1;
      ssc->ss_probe_stuck    = 0;
      ssc->ss_probe_wait     = NG_PROBE_SECS;	/* settle first; do not probe instantly */
      ssc->ss_if.if_watchdog = sana_watchdog;
      ssc->ss_if.if_timer    = 1;

      /* Free mbufs allocated for packet */
      m_freem(req->ioip_reserved);
      req->ioip_reserved = NULL;

      /*
       * Ask to be told when the driver comes back -- unless we already learned it
       * cannot tell us. ss_noevents is remembered across cycles on purpose: a
       * driver that refused S2_ONEVENT once will refuse it every time, and a
       * flapping link would otherwise replay the whole wide-then-narrow-then-give-up
       * negotiation, with its log line, on every single drop.
       */
      if (ssc->ss_noevents) {
	req->ioip_dispatch = NULL;	/* nothing will ever complete it */
	AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
      } else {
	ssc->ss_eventsent++;
	req->ioip_s2.ios2_Req.io_Command = S2_ONEVENT;
	req->ioip_s2.ios2_WireError = NG_S2_BACK_EVENTS;
	req->ioip_dispatch = sana_online;
	sana_submit(req);
      }
      req = NULL;
    }
    ssc->ss_if.if_flags &= ~IFF_UP;
    m_freem(m);
    m = NULL;
    break;
  default:
    if (req->ioip_Error != IOERR_ABORTED) {
      ssc->ss_if.if_ierrors++;		/* a genuine receive error (not a teardown abort) */
      if (debug_sana)
	sana2perror(banner, (struct IOSana2Req *)req);
    }
    m_freem(m);
    m = NULL;
  }

  if (ssc->ss_if.if_flags & IFF_UP) {
    /* Return request to the Sana-II driver */
    if (ioip_alloc_mbuf(req, mtu)) {
      req->ioip_s2.ios2_Req.io_Flags = flags;
      sana_submit(req);
      splx(s);
      return m;
    }
    ssc->ss_rxnobuf++;			/* read re-post mbuf alloc failed -- an RX no-buffer drop */
    log(LOG_ERR, "sana_read (%s): not enough mbufs\n", ssc->ss_name);
  }

  /* do not resend, free used resources */
  (*sent)--;
  if (req) {
    m_freem(req->ioip_reserved);
    req->ioip_reserved = NULL;
    req->ioip_dispatch = NULL;
    AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
  }

  if (m) {
    m_freem(m);
  }

  splx(s);
  return NULL;
}

/*
 * All-ones Ethernet broadcast address, used to reconstruct the destination MAC
 * of a captured broadcast frame for BPF (SANA-II gives us the M_BCAST flag, not
 * the literal address). Only used for 6-byte (Ethernet) SANA interfaces.
 */
static const UBYTE bpf_ether_bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/*
 * sana_ip_read(): feed a packet to the IP queue
 * (This routine is called from sana_poll)
 */
static void
sana_ip_read(struct sana_softc *ssc, struct IOIPReq *req)
{
  struct mbuf *m;
  UBYTE srchw[MAXADDRSANA];		/* sender's hw address, captured pre-read */
  spl_t s;

  /* ios2_SrcAddr is overwritten when sana_read re-submits the request, so grab
   * the sender's hardware address now (needed to reconstruct the BPF frame,
   * and to confirm ARP reachability below). */
  bcopy(req->ioip_s2.ios2_SrcAddr, srchw, ssc->ss_if.if_addrlen);

  m = sana_read(ssc, req, 0, &ssc->ss_ip.sent, "sana_ip_read",
		ssc->ss_if.if_mtu);

  if (m) {
    s = splimp();
    /* BPF capture: tap every received frame at the interface, BEFORE the IP
     * input queue may drop it -- a capture must reflect what arrived on the wire
     * (like a classic bpf tap), including frames lost to queue congestion. m is
     * borrowed read-only, so the drop/enqueue below is unaffected. */
    if (ssc->ss_if.if_addrlen == 6)
      ng_bpf_tap_ether((struct ifnet *)ssc,
		       (m->m_flags & M_BCAST) ? bpf_ether_bcast : ssc->ss_hwaddr,
		       srchw, (u_short)ssc->ss_ip.type, m);
    if (IF_QFULL(&ipintrq)) {
      IF_DROP(&ipintrq);
      ssc->ss_if.if_iqdrops++;
      m_freem(m);
      /* m = NULL; */
    } else {
      /* Receive-side statistics (input packet/byte counters) */
      ssc->ss_if.if_ipackets++;
      ssc->ss_if.if_ibytes += m->m_pkthdr.len;
      /* Set interface pointer (needed for broadcasts) */
      m->m_pkthdr.rcvif = (struct ifnet *)ssc;
      IF_ENQUEUE(&ipintrq, m);
      /* A signal might be needed if we use PA_EXCEPTION port */
      schednetisr_nosignal(NETISR_IP);
      /* m = NULL; */
    }
    splx(s);

    /*
     * Reachability confirmation. This takes the ARP table semaphore, so it is
     * placed after splx() to mirror sana_arp_read(), which likewise drops spl
     * before calling arpinput() (which locks the same table).
     *
     * Be precise about what splx() does here, because it is NOT "we are now
     * outside Forbid": spl_t in sys/synch.h stamps SysBase->TDNestCnt with a
     * LEVEL, and `s` was captured as SPLNET -- sana_poll() raised it before
     * dispatching us and only returns to SPL0 after its whole loop. So this
     * drops SPLIMP -> SPLNET and remains Forbid-nested, and ObtainSemaphore()
     * can still Wait() here.
     *
     * That is safe for a different reason than the spl level: sana_poll()
     * holds SanaPortSem across the entire dispatch loop, and teardown
     * (sana_unrun()) takes the same semaphore before touching the port or the
     * request lists -- so a teardown cannot proceed while the network task is
     * blocked on the ARP lock. If SanaPortSem's scope is ever narrowed, THAT
     * is what breaks this, not the spl level.
     *
     * srchw (the frame's SENDER hardware address) is the whole input: it is the
     * next hop that delivered this packet, which is the thing whose ARP entry
     * reachability applies to. The packet's IP source is deliberately NOT used
     * -- see the comment on ng_arp_confirm_inbound().
     */
    ng_arp_confirm_inbound(ssc, srchw);
  }
}

/*
 * sana_arp_read(): process an ARP packet
 * (This routine is called from sana_poll)
 */
static void
sana_arp_read(struct sana_softc *ssc, struct IOIPReq *req)
{
  struct mbuf *m; 
  UBYTE hwaddr[MAXADDRSANA];

  bcopy(req->ioip_s2.ios2_SrcAddr, hwaddr, ssc->ss_if.if_addrlen);

  m = sana_read(ssc, req, 0, &ssc->ss_arp.sent, "sana_arp_read", ARP_MTU);

  if (m) {
    /* Receive-side statistics (input packet/byte counters). Bump under splimp()
     * to match sana_ip_read() and if_loop.c: the increment must be atomic against
     * readers (e.g. a ShowNetStatus query) that are not Forbid()-protected. */
    spl_t s = splimp();
    ssc->ss_if.if_ipackets++;
    ssc->ss_if.if_ibytes += m->m_pkthdr.len;
    /* BPF capture: reconstruct the Ethernet frame and tap it (m is borrowed)
     * before arpinput() consumes it. */
    if (ssc->ss_if.if_addrlen == 6)
      ng_bpf_tap_ether((struct ifnet *)ssc,
		       (m->m_flags & M_BCAST) ? bpf_ether_bcast : ssc->ss_hwaddr,
		       hwaddr, (u_short)ssc->ss_arp.type, m);
    splx(s);
    arpinput(ssc, m, (caddr_t)hwaddr);
  }
}

/*
 * sana_online(): process an ONLINE event
 */
static void
sana_online(struct sana_softc *ssc, struct IOIPReq *req)
{
  LONG events = req->ioip_s2.ios2_WireError;

  if (req->ioip_s2.ios2_Req.io_Error == 0 &&
      (events & NG_S2_BACK_EVENTS)) {
    ssc->ss_eventsent--;
    req->ioip_dispatch = NULL;
    AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
    /*
     * Retiring the request is this path's own business and is done above,
     * unconditionally, so the accounting balances whatever happens next. Putting
     * the INTERFACE back is shared with the watchdog probe, and is guarded: if a
     * probe already won the race for this same return, or an operator has taken
     * the interface down since, sana_back_online() does nothing.
     */
    sana_back_online(ssc, "reported by its driver");
    return;
  }

  /*
   * An error, or an event we did not ask for.
   *
   * Re-arming is RECOVERY, so it must not depend on a debug setting -- with
   * DEBUG_SANA off this used to fall straight through to the abort branch, drop
   * the request and leave the interface with nothing watching for its device
   * ever again. Only the LOGGING is debug-gated now.
   *
   * A driver that will not accept the wider mask (S2ERR_NOT_SUPPORTED, or no
   * S2_ONEVENT at all) is told apart here: retry once with ONLINE alone, and if
   * even that is refused give up on events for this interface and let the
   * watchdog probe find the device instead.
   */
  if (req->ioip_Error != IOERR_ABORTED) {
    if (debug_sana)
      sana2perror("sana_online", (struct IOSana2Req *)req);
    if (req->ioip_s2.ios2_Req.io_Error == S2ERR_NOT_SUPPORTED ||
	req->ioip_s2.ios2_Req.io_Error == IOERR_NOCMD) {
      if (ssc->ss_noevents) {		/* already tried the narrow mask: give up */
	ssc->ss_eventsent--;
	req->ioip_dispatch = NULL;
	AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
	log(LOG_NOTICE, "%s: driver reports no online events; watching for its "
	    "return instead", ssc->ss_name);
	return;
      }
      ssc->ss_noevents = 1;		/* fall back to the narrow mask once */
      req->ioip_s2.ios2_WireError = S2EVENT_ONLINE;
    } else if (++ssc->ss_eventfails > NG_S2_EVENT_MAXFAIL) {
      /*
       * A driver answering S2_ONEVENT with the same error immediately, for ever,
       * would have us re-arm just as fast -- on the shared network task, which
       * every interface depends on. Re-arming used to be gated behind debug_sana
       * (so with logging off the request was silently dropped instead); ungating
       * it fixed the leak but made an unbounded retry the default. Bound it.
       */
      ssc->ss_eventsent--;
      ssc->ss_noevents = 1;		/* stop asking on later cycles too */
      req->ioip_dispatch = NULL;
      AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
      log(LOG_NOTICE, "%s: driver keeps refusing online events; watching for its "
	  "return instead", ssc->ss_name);
      return;
    } else {
      req->ioip_s2.ios2_WireError = NG_S2_BACK_EVENTS;
    }
    req->ioip_s2.ios2_Req.io_Command = S2_ONEVENT;
    sana_submit(req);
  } else {
    /* Aborted -- probably because "ifconfig xxx/0 down" */
    ssc->ss_eventsent--;
    req->ioip_dispatch = NULL;
    AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
  }
}

/*
 * sana_output --- transmit one packet (the interface's if_output).
 *
 * ip_output() calls this with a completed mbuf chain `m0` and the next-hop
 * destination `dst`. Our job is to get the frame onto the wire via the SANA-II
 * device. The steps, and the subtleties worth learning:
 *
 *  1. Refuse if the interface is not UP and RUNNING (ENETDOWN).
 *  2. Grab a spare IORequest from ss_freereq (ENOBUFS if none are free -- the
 *     transmit ring is finite).
 *  3. Resolve the destination HARDWARE address by address family:
 *       AF_INET  -> arpresolve() (net/sana2arp.c). KEY: if the ARP entry is not
 *                  yet known, arpresolve() QUEUES this packet internally, fires an
 *                  ARP request, and returns 0 -- so we return success (0) here
 *                  WITHOUT transmitting; the packet is sent later when the reply
 *                  arrives. This is why a first ping to a new host "loses" packet 0.
 *       AF_UNSPEC-> a raw SANA-II packet: the caller supplied the type+hw address.
 *  4. SIMPLEX handling: an interface that cannot hear its own broadcasts must be
 *     handed a loopback copy of every broadcast it sends (looutput), or local
 *     broadcast traffic would never reach this host.
 *  5. Prioritise low-delay IP (IPTOS_LOWDELAY) with a higher IORequest priority.
 *  6. Attach the mbuf chain to the IORequest and BeginIO() a CMD_WRITE (below the
 *     switch): the write completes asynchronously and is reclaimed later in
 *     free_written_packet().
 *
 * Runs at splimp() (transmit and the interrupt-time receive path share the
 * request lists). Returns 0 on success/queued, or a BSD errno on failure.
 */
int
sana_output(struct ifnet *ifp, struct mbuf *m0,
	    struct sockaddr *dst, struct rtentry *rt)
{
  register struct sana_softc *ssc = (struct sana_softc *)ifp;
  ULONG type;
  int error = 0;
  struct in_addr idst;

  /* If a broadcast, send a copy to ourself too */
  struct mbuf *mcopy = (struct mbuf *)NULL;
  struct mbuf *tag;
  struct sana_sendtag *st;
  /* Zeroed rather than left as stack garbage: the broadcast paths return from
   * arpresolve() without ever writing it, and it is copied into the send tag
   * and on into ios2_DstAddr regardless. Harmless today (those packets always
   * carry M_BCAST, so sana_start() issues S2_BROADCAST, which ignores
   * ios2_DstAddr), but that is an invariant two layers away -- do not put
   * uninitialised stack bytes on the wire if the invariant ever slips. */
  UBYTE dst_hw[MAXADDRSANA] = { 0 };	/* resolved destination, stashed in the tag */
  UBYTE ioflags = 0;
  BYTE  pri = 0;
  register struct mbuf *m = m0;

  spl_t s = splimp();

  ifp->if_opackets++;		/* stats */

  /* Check if we are up and running... */
  if ((ssc->ss_if.if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING)) {
    error = ENETDOWN;
    ifp->if_oerrors++;
    goto bad;
  }

  get_time(&ssc->ss_if.if_lastchange);

  /*
   * Resolve the destination framing into on-stack scratch (dst_hw / type /
   * ioflags / pri). We deliberately do NOT grab a write request here: the packet
   * is parked on the interface send queue and bound to a request only at drain
   * time (sana_start), so a transiently-empty request pool no longer forces an
   * ENOBUFS drop (which would collapse the TCP congestion window).
   */
  switch (dst->sa_family) {
#if INET
  case AF_INET:
    idst = ((struct sockaddr_in *)dst)->sin_addr;

    /* If the address is not resolved, arpresolve
     * stores the packet to its private queue for
     * later transmit and broadcasts the resolve
     * request packet to the (ether)net.
     * (Now ARP works only with IP and ethernet.)
     */
    if ((ssc->ss_if.if_flags & IFF_NOARP) != IFF_NOARP &&
	/* ssc = network interface 
	 * m = Packet to send 
	 * idst = destination IP address 
	 * ios2_DestAddr = destination hw address 
	 * error = error return
	 */
	!arpresolve(ssc, m, &idst, dst_hw, &error)) {
      /*
       * Unresolved: ARP holds the packet on its own queue and re-injects it via
       * if_output once it resolves. Nothing to send now.
       *
       * PORT (AmiTCP_NG): return the error arpresolve set, rather than 0.
       *
       * This used to return 0 -- success -- for EVERY unresolved outcome, so
       * the errno arpresolve carefully produced went nowhere: ip_output() reads
       * if_output's RETURN VALUE and never an out-parameter, so ENETUNREACH,
       * ENOBUFS and (now) EHOSTDOWN were all reported to the stack as "sent".
       * TCP then had no way to learn a destination was unreachable except its
       * own retransmit timeout -- which is precisely the "transmits into
       * silence" behaviour the ARP hold-down exists to end. A zero error still
       * returns 0, so the ordinary "queued, waiting for the reply" case is
       * unchanged.
       */
      splx(s);
      return (error);
    }
    type = ssc->ss_ip.type;

    /* Send to loopback if we do not hear our broadcasts */
    if ((ssc->ss_if.if_flags & IFF_SIMPLEX) && (m->m_flags & M_BCAST)) {
      /* m_copy returns NULL under mbuf exhaustion; looutput() derefs its
       * mbuf unconditionally, so skip the loopback copy if it failed
       * (the real broadcast still goes out below). */
      mcopy = m_copy(m, 0, (int)M_COPYALL);
      if (mcopy != NULL)
	(void) looutput(&ssc->ss_if, mcopy, dst, rt);
    }
    /* Low-delay IP gets a higher IORequest priority. */
    pri = (IPTOS_LOWDELAY & mtod(m, struct ip *)->ip_tos) ? 1 : 0;
    break;
#endif
#if NS
#error NS unimplemented!!!
  case AF_NS:
    type = ssc->ss_nstype;
    /* There is hardware address straight in socket */
    /* Dunno how this works, if we have a P-to-P device */
    bcopy((caddr_t)&(((struct sockaddr_ns *)dst)->sns_addr.x_host),
	  (caddr_t)req->ioip_s2.ios2_DestAddr, ssc->ss_if.if_addrlen);
    /* Local send */
    if (!bcmp((caddr_t)req->ioip_s2.ios2_DestAddr,
	      (caddr_t)&ns_thishost, ssc->ss_if.if_addrlen)) {
      AddHead(&ssc->ss_freereq, req);
      return (looutput(ifp, m, dst, rt));
    }
    req->ioip_s2.ios2_Req.io_Message.mn_Node.ln_Pri = 0;
    break;
#endif
  case AF_UNSPEC:
    /* Raw packets. Sana-II address (a tuple of type and host)
     * specifies the destination 
     */
    if ((type = ((struct sockaddr_sana2*)dst)->ss2_type)) {
      bcopy(((struct sockaddr_sana2*)dst)->ss2_host, dst_hw,
	    ssc->ss_if.if_addrlen);
    } else {
      ioflags = SANA2IOF_RAW;
      type = 0L;
    }
    break;

#if	ISO
#endif /* ISO */
#if RMP
  case AF_RMP:
#endif

  default: 
    log(LOG_ERR, "%s%ld: can't handle af%ld\n",
	ssc->ss_if.if_name, ssc->ss_if.if_unit, dst->sa_family);
    error = EAFNOSUPPORT;
    ifp->if_oerrors++;
    goto bad;
  }

  /*
   * BPF capture: tap the outgoing frame (payload m is borrowed, not consumed).
   * Reached only once committed to transmitting (the ARP-unresolved case
   * returned earlier). IP (AF_INET) and ARP/typed (AF_UNSPEC, type != 0) hand us
   * a payload with NO link header, so ng_bpf_tap_ether() reconstructs the
   * Ethernet header from dst_hw + our address + type. A SANA2IOF_RAW send
   * (type == 0) is different: the caller already built the COMPLETE frame into m
   * (and dst_hw is left unset), so tap it as-is -- reconstructing a second
   * header would corrupt the captured frame and read the uninitialised dst_hw.
   */
  if (ssc->ss_if.if_addrlen == 6) {
    if (type != 0)
      /* dst_hw is left UNSET for a broadcast (arpresolve returns success for
       * M_BCAST without writing it) and for an ARP request (arp_request_out
       * leaves ss2_host unset), so capturing dst_hw verbatim would put
       * uninitialised stack bytes -- not ff:ff:ff:ff:ff:ff -- into the .pcap
       * destination MAC. Substitute the broadcast address for M_BCAST, mirroring
       * the receive-side taps above. */
      ng_bpf_tap_ether(ifp,
		       (m->m_flags & M_BCAST) ? bpf_ether_bcast : dst_hw,
		       ssc->ss_hwaddr, (u_short)type, m);
    else
      ng_bpf_tap(ifp, m);
  }

  /*
   * Park the resolved packet on the interface send queue. A small MT_DATA tag
   * mbuf carries the framing (dst_hw / type / ioflags / pri) that would normally
   * live in the write request; it is prepended to the packet via m_next (the
   * queue linkage uses m_nextpkt, a different field). sana_start() then binds a
   * free request to it and submits. Only if the queue is genuinely full (sustained
   * overload -- ifq_maxlen is far above the write-request pool) do we drop and
   * return ENOBUFS, as correct backpressure -- the rare last resort, not the
   * per-burst drop that used to collapse cwnd.
   */
  if (!(tag = m_get(M_DONTWAIT, MT_DATA))) {
    ssc->ss_txnobuf++;			/* send-tag mbuf alloc failed -- a no-buffer drop */
    error = ENOBUFS;
    goto bad;
  }
  st = mtod(tag, struct sana_sendtag *);
  st->st_type    = type;
  bcopy(dst_hw, st->st_dstaddr, ssc->ss_if.if_addrlen);
  st->st_ioflags = ioflags;
  st->st_pri     = pri;
  tag->m_len  = sizeof(struct sana_sendtag);
  tag->m_next = m;			/* tag -> real packet */

  if (IF_QFULL(&ssc->ss_if.if_snd)) {
    IF_DROP(&ssc->ss_if.if_snd);	/* counted as if_snd.ifq_drops (queue-full drop) */
    m_freem(tag);			/* frees the tag AND the packet (m_next) */
    splx(s);
    return (ENOBUFS);
  }
  IF_ENQUEUE(&ssc->ss_if.if_snd, tag);

  /* Send now if a request is free; otherwise the packet waits and
   * free_written_packet() drains it as write requests complete. */
  sana_start(ssc);

  splx(s);
  return 0;

 bad:
  splx(s);
  if (m)
    m_freem(m);
  return error;
}

/*
 * free_written_packet(): free mbufs of written packet,
 *                        queue IOrequest for reuse
 * (This routine is called from sana_poll)
 */
static void
free_written_packet(struct sana_softc *ssc, struct IOIPReq *req)
{
  spl_t s = splimp();

  if (req->ioip_packet) {
    m_freem(req->ioip_packet);
    req->ioip_packet = NULL;
  }
  req->ioip_dispatch = NULL;
  if (req->ioip_Error && req->ioip_Error != IOERR_ABORTED) {
    ssc->ss_if.if_oerrors++;		/* a genuine media/device transmit error */
    if (debug_sana)
      sana2perror("sana_output", (struct IOSana2Req *)req);
  }
  AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
  /* This write freed a request -- drain the next packet parked on if_snd, if
   * any. This is what lets a full transmit window flow without the pool-empty
   * ENOBUFS drop that would otherwise collapse the TCP congestion window. */
  sana_start(ssc);
  splx(s);
}

/*
 * sana_start(): drain the interface send queue (ss_if.if_snd) into free write
 * requests. MUST be called with splimp() held -- both callers, sana_output() and
 * free_written_packet(), already hold it. Transmits FIFO while BOTH a free request
 * and a queued packet are available, so a full transmit window flows through the
 * device write ring instead of being dropped (dropping returns ENOBUFS, which
 * collapses the TCP congestion window to a single segment). Each queued item is a
 * sana_sendtag mbuf carrying the resolved framing, whose m_next is the real packet.
 */
static void
sana_start(struct sana_softc *ssc)
{
  struct ifnet *ifp = &ssc->ss_if;
  struct IOIPReq *req;
  struct mbuf *tag, *m;
  struct sana_sendtag *st;

  while (ifp->if_snd.ifq_head != NULL) {
    if (!(req = (struct IOIPReq*)RemHead((struct List*)&ssc->ss_freereq))) {
      /*
       * PORT (AmiTCP_NG): the request pool is empty while a packet is waiting
       * to go out. Wake the network task.
       *
       * This is the one place the stack can notice that it may be about to
       * strand itself. `ss_freereq` is ONE pool shared by reads and writes
       * (sana_send_read() and the RemHead above both draw from it), and the
       * only code that ever puts a request back is sana_poll() -- via
       * sana_read() for a completed read, or free_written_packet() for a
       * completed write. So if sana_poll() ever stops running, both directions
       * drain this pool and nothing refills it. At zero, nothing is posted to
       * the device at all; the device therefore completes nothing, signals
       * nothing, and sana_poll() can never be reached again. The interface is
       * then silently and permanently dead while the rest of the stack runs
       * perfectly -- with no error anywhere, because an inbound frame has no
       * posted read buffer to land in and is dropped below our visibility, and
       * an outbound one simply parks on if_snd (sana_output returns 0, queued
       * not failed, so send() succeeds and the caller just times out).
       *
       * Unlike looutput(), which calls schednetisr() and so wakes the network
       * task in software, the whole SANA transmit path is fire-and-forget
       * BeginIO() -- it never signals. That leaves recovery depending on the
       * very device that has gone quiet. Signalling here breaks that circle:
       * ordinary outbound traffic can restart the poll loop.
       *
       * Deliberately NOT on the per-packet path: this fires only when the pool
       * is genuinely exhausted with work pending, which is either burst
       * overload or the failure above. A spurious signal costs one sana_poll()
       * that finds nothing.
       */
      /* Guarded for the same reason as sana_unrun() above: reachable from an
       * application task (sana_output() on the send path) and so able to race a
       * shutdown that has already nulled SanaPort. */
      if (SanaPort)
	Signal(SanaPort->mp_SigTask, 1L << SanaPort->mp_SigBit);
      break;				/* no free request -- leave packets queued */
    }

    IF_DEQUEUE(&ifp->if_snd, tag);	/* tag != NULL: head was non-empty */
    st = mtod(tag, struct sana_sendtag *);
    m  = tag->m_next;			/* the real packet chain */
    tag->m_next = NULL;

    /* Restore the resolved framing into the request. */
    bcopy(st->st_dstaddr, req->ioip_s2.ios2_DstAddr, ifp->if_addrlen);
    req->ioip_s2.ios2_Req.io_Flags = st->st_ioflags;
    req->ioip_s2.ios2_Req.io_Message.mn_Node.ln_Pri = st->st_pri;
    req->ioip_s2.ios2_PacketType = st->st_type;
    (void)m_free(tag);			/* free ONLY the tag, not the packet */

    req->ioip_Command  = (m->m_flags & M_BCAST) ? S2_BROADCAST : CMD_WRITE;
    req->ioip_dispatch = free_written_packet;
    req->ioip_packet   = m;
    req->ioip_s2.ios2_DataLength = m->m_pkthdr.len;

    sana_submit(req);

    ifp->if_obytes += m->m_pkthdr.len;
    if (m->m_flags & M_BCAST)
      ifp->if_omcasts++;
  }
}
