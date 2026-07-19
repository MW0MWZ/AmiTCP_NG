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

int debug_sana = 1;

/* Global port for all SANA-II network interfaces */
struct MsgPort *SanaPort = NULL;

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
static void free_written_packet(struct sana_softc *ssc, struct IOIPReq *req);
static void sana_start(struct sana_softc *ssc);
static void sana_scrub_inet(struct ifnet *ifp);
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

  SanaPort = CreateMsgPort();	/* V36 function, creates a PA_SIGNAL port */

  if (SanaPort) {
    SanaPort->mp_Node.ln_Name = (void *)"sana_if.port";
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

  while (ssq) {
    sana_down(ssq);
    if (ssq->ss_if.if_flags & IFF_RUNNING) {
      sana_unrun(ssq);
    }
    ssc = ssq;
    ssq = ssc->ss_next;
    /* Close device */ 
    req = CreateIOSana2Req(ssc);
    if (req) {
      CloseDevice((struct IORequest*)req);
      DeleteIOSana2Req(req);
    } else {
      log(LOG_ERR, "sana_deinit(): Couldn't close device %s\n",
	  ssc->ss_name);
    }
  }

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
BOOL
sana_poll(void)
{
  struct IOIPReq * io;
  spl_t s = splnet();

  while (io = (struct IOIPReq *)GetMsg(SanaPort)) {
    /* touch the network interface */
    get_time(&io->ioip_if->ss_if.if_lastchange);
    if (io->ioip_dispatch) {
      (*io->ioip_dispatch)(io->ioip_if, io);
     } else {
       log(LOG_ERR, "No dispatch function in request for %s\n",
	   io->ioip_if->ss_name);
     }
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
    BOOL cleaned = FALSE;
    spl_t s2;
    extern void ng_flush_dynamic_nameservers(void);

    s2 = splimp();
    for (p = ssq; p != NULL; p = p->ss_next) {
      if (p->ss_offcleanup) {
	p->ss_offcleanup = 0;
	sana_offline_cleanup(p);
	cleaned = TRUE;
      }
    }
    splx(s2);

    if (cleaned)
      ng_flush_dynamic_nameservers();
  }

  return FALSE;
}

#ifdef COMPAT_AMITCP2
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

#ifdef NG_DHCP_DEBUG
/* TEMP DIAGNOSTIC (-DNG_DHCP_DEBUG): append "<label> <decimal>\n" to SYS:sana.log to
 * trace SANA-II interface bring-up (OpenDevice + device queries) on real hardware.
 * iface_make runs in the caller's Process context (DOS available). */
static void s_dbg(const char *label, long v)
{
  BPTR f = Open((STRPTR)"SYS:sana.log", MODE_READWRITE);
  if (f) {
    char b[16]; int n = 0; unsigned long u; int neg = 0;
    long len = 0; while (label[len]) len++;
    Seek(f, 0, OFFSET_END);
    Write(f, (APTR)label, len);
    Write(f, (APTR)" ", 1);
    if (v < 0) { neg = 1; u = (unsigned long)(-v); } else u = (unsigned long)v;
    { char t[12]; int k = 0;
      if (u == 0) t[k++] = '0';
      while (u) { t[k++] = '0' + (int)(u % 10); u /= 10; }
      if (neg) b[n++] = '-';
      while (k) b[n++] = t[--k]; }
    b[n++] = '\n';
    Write(f, (APTR)b, n);
    Close(f);
  }
}
#define S_LOG(label, v) s_dbg((label), (long)(v))
#else
#define S_LOG(label, v)
#endif

static struct ifnet *
iface_make(struct ssconfig *ifc)
{
  register struct sana_softc *ssc = NULL;
  register struct IOSana2Req *req;
  struct Sana2DeviceQuery devicequery;
  LONG oerr = 0;

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
    S_LOG("open_err", oerr);
    S_LOG("open_wire", req->ios2_WireError);
    if (oerr) {
      sana2perror("OpenDevice", req);
    } else {
      S_LOG("open_ok", 0);
      /* Ask for our type, address length, MTU
       * Obl. bitch: nobody tells, WHO is supplying
       * DevQueryFormat and DeviceLevel
       */
      req->ios2_Req.io_Command   = S2_DEVICEQUERY;
      req->ios2_StatData         = &devicequery;
      devicequery.SizeAvailable  = sizeof(devicequery);
      devicequery.DevQueryFormat = 0L;
      
      DoIO((struct IORequest *)req);
      S_LOG("devq_err", req->ios2_Req.io_Error);
      S_LOG("addrbits", devicequery.AddrFieldSize);
      if (req->ios2_Req.io_Error)
	sana2perror("S2_DEVICEQUERY", req);
      else if (((devicequery.AddrFieldSize + 7) >> 3) > MAXADDRSANA) {
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
	    (long)((devicequery.AddrFieldSize + 7) >> 3), (long)MAXADDRSANA);
      }
      else {
	/* Get Our Station address */
	req->ios2_StatData = NULL;
	req->ios2_Req.io_Command = S2_GETSTATIONADDRESS;
	DoIO((struct IORequest *)req);
	S_LOG("getaddr_err", req->ios2_Req.io_Error);
	if (req->ios2_Req.io_Error)
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
	    ssc->ss_if.if_addrlen  = (devicequery.AddrFieldSize + 7) >> 3;
	    bcopy(req->ios2_DstAddr, ssc->ss_hwaddr, ssc->ss_if.if_addrlen);
	    ssc->ss_if.if_mtu      = devicequery.MTU;
	    ssc->ss_maxmtu         = devicequery.MTU;
	    ssc->ss_if.if_baudrate = devicequery.BPS;
	    ssc->ss_hwtype         = devicequery.HardwareType;	
	    
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
      if (!ssc)
	CloseDevice((struct IORequest *)req);
    }
    DeleteIOSana2Req(req);
  }

  S_LOG("iface_ret", ssc ? 1 : 0);
  return (struct ifnet *)ssc;
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
 * Returns the new ifnet, or NULL if the device could not be opened -- iface_make
 * cleans up entirely after itself on that path, leaving no phantom interface.
 */
struct ifnet *
sana_add_interface(char *ifname, char *devname, long devunit)
{
  struct ssconfig ssc;
  LONG unit_val = devunit;
  int i;

  aligned_bzero_const((caddr_t)&ssc, sizeof ssc);

  for (i = 0; i < IFNAMSIZ - 1 && ifname[i] != '\0' &&
	      !(ifname[i] >= '0' && ifname[i] <= '9'); i++)
    ssc.name[i] = ifname[i];
  ssc.name[i] = '\0';
  for (ssc.unit = 0; ifname[i] >= '0' && ifname[i] <= '9'; i++)
    ssc.unit = ssc.unit * 10 + (ifname[i] - '0');
  S_LOG("split_unit", ssc.unit);

  ssc.flags = 0;			/* no ReadArgs RDArgs to free */
  ssc.args->a_name = (UBYTE *)ssc.name;
  ssc.args->a_dev  = (UBYTE *)devname;
  ssc.args->a_unit = &unit_val;
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
sana_scrub_inet(struct ifnet *ifp)
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

/*
 * Delete the route to `dstaddr` (a host route if `host`, else a network route
 * with an all-ones... no: with the given prefix -- see below) IF it exits via
 * THIS interface. Used to drop routes the DHCP client / interface config
 * installed whose rt_ifa binds to an ifaddr we are about to free: the default
 * route (0.0.0.0/0, RTF_GATEWAY) and the 255.255.255.255 limited-broadcast host
 * route the DHCP client adds so ip_output() can egress a DISCOVER. Leaving either
 * behind would dangle rt_ifa at freed memory once the ifaddr is scrubbed -- silent
 * corruption on this no-MMU port. rtalloc1() does a longest-prefix match, so we
 * only delete on an EXACT key hit via this interface (never a covering route).
 * The CALLER must hold splimp(); rtalloc1()/rtrequest() nest their own splnet().
 */
static void
sana_del_iface_route(struct ifnet *ifp, ULONG dstaddr, int host)
{
  struct rtentry *rt;
  struct sockaddr_in dst;

  bzero((caddr_t)&dst, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_len    = sizeof(dst);
  dst.sin_addr.s_addr = dstaddr;
  if ((rt = rtalloc1((struct sockaddr *)&dst, 0)) != NULL) {
    struct sockaddr_in *key = (struct sockaddr_in *)rt_key(rt);
    if (rt->rt_ifp == ifp && key != NULL && key->sin_addr.s_addr == dstaddr) {
      struct sockaddr_in mask;
      bzero((caddr_t)&mask, sizeof(mask));
      mask.sin_family = AF_INET; mask.sin_len = sizeof(mask);
      mask.sin_addr.s_addr = 0;			/* net route: 0.0.0.0 mask (default) */
      /* RTM_DELETE re-finds the node by dst(+netmask) and never dereferences the
       * gateway argument, so pass rt->rt_gateway directly (no copy). We still hold
       * rtalloc1()'s reference, so rtrequest() only clears RTF_UP; the rtfree()
       * below then releases that reference and frees the node. A host route carries
       * no netmask. */
      (void)rtrequest(RTM_DELETE, (struct sockaddr *)&dst, rt->rt_gateway,
		      host ? (struct sockaddr *)0 : (struct sockaddr *)&mask,
		      rt->rt_flags, (struct rtentry **)0);
    }
    rtfree(rt);
  }
}

/*
 * Drop the routes an interface owns beyond its connected-network route (that one
 * is handled by in_ifscrub() inside sana_scrub_inet()): the default route
 * (0.0.0.0/0) and the 255.255.255.255 DHCP broadcast host route. Both bind rt_ifa
 * to one of this interface's ifaddrs, so they MUST go before those ifaddrs are
 * freed. Caller holds splimp().
 */
static void
sana_flush_iface_routes(struct ifnet *ifp)
{
  sana_del_iface_route(ifp, (ULONG)0,            0);	/* default route 0.0.0.0/0 */
  sana_del_iface_route(ifp, (ULONG)0xFFFFFFFFUL, 1);	/* DHCP 255.255.255.255 host route */
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

  sana_flush_iface_routes(ifp);
  sana_scrub_inet(ifp);
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
  sana_scrub_inet(ifp);

  /* Unlink from the interface list (no if_detach() exists). */
  for (q = &ifnet; *q != NULL; q = &(*q)->if_next)
    if (*q == ifp) { *q = ifp->if_next; break; }

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

    if ((req = CreateIOSana2Req(ssc))) {
      req->ios2_Req.io_Command = S2_CONFIGINTERFACE;
      bcopy(ssc->ss_hwaddr, req->ios2_SrcAddr, ssc->ss_if.if_addrlen);

      DoIO((struct IORequest*)req);
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
	DoIO((struct IORequest*)req);
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
	  DoIO((struct IORequest*)req);
	  /* It is *not* safe to turn tracking off */
	  if (req->ios2_Req.io_Error &&
	      req->ios2_Req.io_Error != S2ERR_NOT_SUPPORTED &&
	      req->ios2_Req.io_Error != IOERR_NOCMD &&
	      req->ios2_WireError != S2WERR_ALREADY_TRACKED)
	    sana2perror("S2_TRACKTYPE for IP", req);
	  if (ssc->ss_arp.reqno) {
	    req->ios2_Req.io_Command = S2_TRACKTYPE;
	    req->ios2_PacketType = ssc->ss_arp.type;
	    DoIO((struct IORequest*)req);
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
  
  for ( next = ssc->ss_reqs; (req = next) ;) {
    next = req -> ioip_next;
    WaitIO((struct IORequest *)req);
    DeleteIORequest(req);
  }
  ssc->ss_reqs = next;
  
  ssc->ss_if.if_flags &= ~IFF_RUNNING;
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
    if ((ifr->ifr_flags & IFF_NOARP) == 0)
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
	       UWORD command, UBYTE flags)
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
    BeginIO((struct IORequest*)req);
  }
  return i;

 no_resources:
  if (req)
    AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
  log(LOG_ERR, "sana_up: could not queue enough read requests\n");
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
  ssc->ss_if.if_flags |= IFF_UP;
  
  /* Send read requests to device driver */
#if	INET
  /* IP */
  ssc->ss_ip.sent += 
    sana_send_read(ssc, ssc->ss_ip.reqno - ssc->ss_ip.sent, ssc->ss_ip.type,
		   ssc->ss_if.if_mtu, sana_ip_read, CMD_READ, 0);

  ssc->ss_arp.sent += 
    sana_send_read(ssc, ssc->ss_arp.reqno - ssc->ss_arp.sent, ssc->ss_arp.type,
		   ARP_MTU, sana_arp_read, CMD_READ, 0);

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
		   S2_READORPHAN, SANA2_IOF_RAW);
#endif
  splx(s);
  return;
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

      /* Free mbufs allocated for packet */
      m_freem(req->ioip_reserved);
      req->ioip_reserved = NULL;

      /* Order an notify when driver is put back online */
      ssc->ss_eventsent++;
      req->ioip_s2.ios2_Req.io_Command = S2_ONEVENT;
      req->ioip_s2.ios2_WireError = S2EVENT_ONLINE;
      req->ioip_dispatch = sana_online;
      BeginIO((struct IORequest*)req);
      req = NULL;
    }
    ssc->ss_if.if_flags &= ~IFF_UP;
    m_freem(m);
    m = NULL;
    break;
  default:
    if (debug_sana && req->ioip_Error != IOERR_ABORTED) 
      sana2perror(banner, (struct IOSana2Req *)req);
    m_freem(m);
    m = NULL;
  }

  if (ssc->ss_if.if_flags & IFF_UP) {
    /* Return request to the Sana-II driver */
    if (ioip_alloc_mbuf(req, mtu)) {
      req->ioip_s2.ios2_Req.io_Flags = flags;
      BeginIO((struct IORequest*)req); 
      splx(s);
      return m;
    }
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
   * the sender's hardware address now (needed to reconstruct the BPF frame). */
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
      events & S2EVENT_ONLINE) {
    ssc->ss_eventsent--;
    req->ioip_dispatch = NULL;
    AddHead((struct List*)&ssc->ss_freereq, (struct Node*)req);
    log(LOG_NOTICE, "%s is online again.", ssc->ss_name);
    sana_up(ssc);
    return;
  }

  /* An error? */
  if (debug_sana && req->ioip_Error != IOERR_ABORTED) { 
    sana2perror("sana_online", (struct IOSana2Req *)req);
    req->ioip_s2.ios2_Req.io_Command = S2_ONEVENT;
    req->ioip_s2.ios2_WireError = S2EVENT_ONLINE;
    BeginIO((struct IORequest*)req);
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
  UBYTE dst_hw[MAXADDRSANA];		/* resolved destination, stashed in the tag */
  UBYTE ioflags = 0;
  BYTE  pri = 0;
  register struct mbuf *m = m0;

  spl_t s = splimp();

  ifp->if_opackets++;		/* stats */

  /* Check if we are up and running... */
  if ((ssc->ss_if.if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING)) {
    error = ENETDOWN;
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
      /* Unresolved: ARP holds the packet on its own queue and re-injects it via
       * if_output once it resolves. Nothing to send now. */
      splx(s);
      return (0);
    }
    type = ssc->ss_ip.type;

    /* Send to loopback if we do not hear our broadcasts */
    if ((ssc->ss_if.if_flags & IFF_SIMPLEX) && (m->m_flags & M_BCAST)) {
      mcopy = m_copy(m, 0, (int)M_COPYALL);
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
      ng_bpf_tap_ether(ifp, dst_hw, ssc->ss_hwaddr, (u_short)type, m);
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
    error = ENOBUFS;			/* genuine mbuf exhaustion */
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
    IF_DROP(&ssc->ss_if.if_snd);
    ifp->if_oerrors++;
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
  ifp->if_oerrors++;
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
  if (debug_sana && req->ioip_Error)
    sana2perror("sana_output", (struct IOSana2Req *)req);
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
    if (!(req = (struct IOIPReq*)RemHead((struct List*)&ssc->ss_freereq)))
      break;				/* no free request -- leave packets queued */

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

    BeginIO((struct IORequest*)req);

    ifp->if_obytes += m->m_pkthdr.len;
    if (m->m_flags & M_BCAST)
      ifp->if_omcasts++;
  }
}
