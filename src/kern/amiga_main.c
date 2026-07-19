/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: amiga_main.c,v 3.2 1994/04/02 10:28:28 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 * 
 * $Log: amiga_main.c,v $
 * Revision 3.2  1994/04/02  10:28:28  jraja
 * Removed res_init(), which is done for each SocketBase now.
 * Raised version to beta2.
 *
 * Revision 3.1  1994/03/26  09:45:33  too
 * Added netdb_deinit() call. Raised revision major number to 3.
 *
 * Revision 1.45  1994/01/23  22:33:35  jraja
 * Raised version number to 3.0s.
 *
 * Revision 1.44  1994/01/05  10:25:00  jraja
 * Cosmetic changes.
 *
 * Revision 1.43  1993/12/22  08:49:31  jraja
 * Changed CTRL-C code to try to break applications upto 3 times if necessary.
 *
 * Revision 1.42  1993/12/20  18:02:27  jraja
 * Added include for dos protos&pragmas.
 *
 * Revision 1.41  1993/12/20  08:22:37  jraja
 * Raised revision number to 2.3.
 *
 * Revision 1.40  1993/11/26  16:21:51  too
 * Added task signalling and delaying after receiving break signal
 *
 * Revision 1.39  1993/11/07  00:04:44  ppessi
 * Raised release number to 2.2.
 *
 * Revision 1.38  1993/10/29  02:18:26  ppessi
 * Raised release number to 2.1a.
 *
 * Revision 1.37  1993/10/11  01:43:56  jraja
 * Raised release number to 2.1.
 *
 * Revision 1.36  1993/08/10  16:30:19  too
 * Added version DATE from bumprevved bsdsocket.library_rev.h
 *
 * Revision 1.35  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.1  93/02/04  18:55:53  18:55:53  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 */

/*
 * amiga_main.c --- the stack's entry point and service loop.
 *
 * This is where a student should start reading. AmiTCP_NG is not a disk library;
 * it is a *program* that, when run, builds bsdsocket.library in RAM and then
 * becomes the TCP/IP stack. That whole story lives here:
 *
 *   main()      grabs SysBase, opens utility.library (see the PORT note below),
 *               calls init_all(), then runs the service loop until CTRL-C, then
 *               calls deinit_all().
 *   init_all()  brings the subsystems up in dependency order -- the definitive
 *               list of "what a TCP/IP stack is made of" (config, logging, mbufs,
 *               timer, the library itself via api_init(), SANA-II, the protocol
 *               domains, the host database). Read it as a table of contents.
 *   the loop    a single Wait() sleeps on three signal sources -- SANA-II device
 *               I/O, protocol timers, and CTRL-C -- and dispatches each. The whole
 *               stack is event-driven from this one loop.
 *
 * Architecture: docs/ARCHITECTURE.md sections 1 (what this is), 4 (lifecycle),
 * 6 (the task model / why one Wait() drives everything).
 *
 * The stack runs as one AmigaOS Process; init_all() spawns a second (NETTRACE,
 * see amiga_log.c) for logging and the ARexx control port.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/synch.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

/* PORT (AmiTCP_NG): removed <signal.h>. It was only used for signal(SIGINT,
 * SIG_IGN) below to suppress the C runtime's CTRL-C handling. As a -noixemul
 * shared library there is no POSIX signal delivery, and bebbo's libc <signal.h>
 * drags in pthread/tv_sec decls that clash with AmiTCP's BSD headers. Native
 * break handling (SIGBREAKF_CTRL_C) is done explicitly in the wait loop below. */

#include <kern/amiga_includes.h>

#if __SASC
#include <proto/dos.h>
#elif __GNUC__
#include <inline/dos.h>
#else
#error Compiler not supported!
#endif

/* PORT (AmiTCP_NG): compiler-independent, needed for the CreateNewProcTags()
 * that spawns the stack Process in the self-starting library build. */
#include <dos/dostags.h>
#include <dos/dosextens.h>

#include <kern/amiga_time.h>
#include <api/amiga_api.h>
#include <api/dns_cache.h>	/* DNS cache: ng_dns_cache_max + per-tier sizes */
#include <kern/amiga_log.h>
/* #include <net/if_sana.h> */
#include <net/bpf.h>			/* ng_bpf_init() */

#include <bsdsocket.library_rev.h>

ULONG sana_init(void);
void sana_deinit(void);
BOOL sana_poll(void);


#include <kern/amiga_main_protos.h>
#include <kern/amiga_config.h>
#include <kern/amiga_netdb.h>

/*
 * include prototypes for initialization functions
 */
#include <kern/uipc_domain_protos.h>    /* domaininit() */

/*
 * The main module of the AMITCP/IP.
 */

/*
 * Global variable so AMITCP/IP task information can be utilized.
 */
struct Task * AmiTCP_Task;

extern struct ExecBase * SysBase;
extern struct Library * MasterSocketBase;
extern WORD nthLibrary;

static ULONG sanamask = 0, 
  sig = 0, sigmask = 0, timermask = 0, 
  breakmask = 0;

UBYTE *taskname = NULL;
ULONG EnableDebug = 0;
BOOL  initialized = FALSE;
/* PORT (AmiTCP_NG): TRUE once the stack subsystems are up. Set here (defined
 * once) so both main() (program build) and ng_stack_ensure_running() (library
 * build) can see it; the program sets it after init_all() so ELL_Open() does not
 * try to spawn a second stack Process. */
volatile BOOL ng_stack_running = FALSE;
/* PORT (AmiTCP_NG): serialises the one-time lazy spawn of the stack Process. If
 * two tasks call socket() before the stack is up, only the first spawns it; the
 * others block on this until it is running. Initialised in api_libinit()/api_init(). */
struct SignalSemaphore ng_spawn_semaphore = { 0 };

STRPTR version = (STRPTR)"\0$VER: AmiTCP_NG " AMITCP_NG_VER " (" DATE ")\r\n"
             "Copyright \251 2026 Andy Taylor (MW0MWZ). Based on AmiTCP/IP 3.0b2, "
             "Copyright \251 1993-1994 AmiTCP/IP Group.\r\n";

/*
 * main --- become the TCP/IP stack.
 *
 * Runs as an ordinary AmigaOS Process (started from the CLI / a startup script).
 * After init_all() succeeds, bsdsocket.library is live and applications can use
 * the network; this Process then stays resident as the stack, sleeping in Wait()
 * and waking to service work, until it receives CTRL-C. Returns 0 on clean
 * shutdown, 20 (RETURN_FAIL) if initialisation failed.
 *
 * Note there is no argument parsing of argc/argv here: configuration comes from
 * AmiTCP:db/AmiTCP.config, read by readconfig() inside init_all().
 */
int
main(int argc, char *argv[])
{
  BYTE oldpri;
  STRPTR oldname;
  int retval;

  /* SysBase lives at absolute address 4 on every Amiga -- the one fixed pointer
   * in the system. Everything else (DOSBase, UtilityBase, ...) is reached through
   * it via OpenLibrary(). We fetch it by hand rather than trust the C startup. */
  SysBase = *(struct ExecBase **)4;

  /*
   * PORT (AmiTCP_NG): open utility.library up front. bebbo's libgcc __mulsi3 /
   * __divsi3 implement 32-bit multiply/divide by calling utility.library's
   * SMult32/UMult32 through the global UtilityBase. AmiTCP defines its own
   * UtilityBase (amiga_rexx.c) and only opens it later in rexx_init(), so the
   * first 32-bit multiply (in log_init, well before that) would jump through a
   * NULL base and crash. Opening it here makes integer math work from the start.
   */
  {
    extern struct Library *UtilityBase;
    if (UtilityBase == NULL)
      UtilityBase = OpenLibrary((STRPTR)"utility.library", 37L);
  }

  /*
   * Disable CTRL-C(D) Break signal.
   * PORT (AmiTCP_NG): was signal(SIGINT, SIG_IGN) — a no-op in a -noixemul
   * library (no POSIX signal delivery). CTRL-C arrives only as the native
   * SIGBREAKF_CTRL_C exec signal, which the main wait loop handles explicitly.
   */

  /*
   * Initialize AmiTCP_Task to point the Task structure of this task.
   */
  AmiTCP_Task = FindTask(NULL);

  /*
   * Save pointer to this tasks old name
   */
  oldname = (STRPTR)AmiTCP_Task->tc_Node.ln_Name;

  if (init_all()) {
    /*
     * Set our priority 
     */
    oldpri = SetTaskPri(AmiTCP_Task, 5);

    /*
     * Set our Task name 
     */
    if (!taskname) {
#ifdef DEBUG
      if (nthLibrary) {
	if (taskname = bsd_malloc(16, M_CFGVAR, M_WAITOK)) {
	  strcpy(taskname, "AmiTCP");
	  taskname[6] = '.'; taskname[7] = '0' + nthLibrary;
	}
      } else {
#endif
	taskname = (UBYTE *)"AmiTCP";
#ifdef DEBUG
      }
#endif
    }
    if (taskname)
      AmiTCP_Task->tc_Node.ln_Name = (char *)taskname;

    /*
     * Global initialization flag;
     */
    initialized = TRUE;
    /* PORT (AmiTCP_NG): the program build starts the stack itself, so mark it
     * running -- this keeps ELL_Open() from spawning a second stack Process. */
    ng_stack_running = TRUE;

#ifdef DEBUG
    /* 
     * Show our task address
     */
    printf("%s Task address : %lx\n", taskname, (long) AmiTCP_Task);
#endif

    /*
     * Build the wait mask. The entire stack is driven by ONE Wait() on three
     * kinds of Exec signal (docs/ARCHITECTURE.md section 6):
     *   sanamask   - a SANA-II network device finished a read/write; there are
     *                packets to process or transmit buffers to reclaim.
     *   timermask  - a protocol timer fired (TCP retransmit/keepalive, IP
     *                fragment reassembly timeout, ARP expiry, ...).
     *   breakmask  - the user pressed CTRL-C; time to shut down.
     * Each mask is a set of signal bits allocated by the relevant subsystem
     * (sana_init/timer_init) during init_all().
     */
    breakmask = SIGBREAKF_CTRL_C;
    sigmask = timermask | breakmask | sanamask;

    /*
     * Now when everything else is succesfully initialized,
     * let the timeouts roll!
     */
    timer_send();

    for(;;) {
      /*
       * Sleep until we are signalled. Wait() yields the CPU to the rest of the
       * system; the stack consumes no time while idle. It returns the set of
       * signal bits that woke us.
       */
      sig = Wait(sigmask);

      /*
       * Drain all pending work before sleeping again. sana_poll()/timer_poll()
       * each return FALSE when that source has nothing left to do, at which point
       * we clear its bit. SetSignal(0, sigmask) re-reads any signals that arrived
       * *while* we were processing (so a burst of packets doesn't cost one wakeup
       * each). We loop until only the CTRL-C bit (if any) remains.
       */
      do {
	if (sig & sanamask) {
	  if (!sana_poll())
	    sig &= ~sanamask;
	}

	if (sig & timermask) {
	  if (!timer_poll())
	    sig &= ~timermask;
	}

	sig |= SetSignal(0L, sigmask) & sigmask;
      } while (sig && sig != breakmask);

      if (sig & breakmask) {
	int i;
	/*
	 * We got CTRL-C
	 *
	 * NETTRACE task keeps one base open, it is not counted.
	 */
	api_hide();		/* hides the API from users */

	/*
	 * Try three times with a short delay
	 */
	for (i = 0; i < 3 && MasterSocketBase->lib_OpenCnt > 1; i++) {
	  api_sendbreaktotasks(); /* send brk to all tasks w/ SBase open */ 
	  Delay(50);		  /* give tasks time to close socket base */
	}
	if (MasterSocketBase->lib_OpenCnt > 1) {
	  log(LOG_ERR, "Got CTRL-C while %ld %s still open.\n",
	      MasterSocketBase->lib_OpenCnt - 1,
	      (MasterSocketBase->lib_OpenCnt == 2) ? "library" : "libraries");
	  api_show(); /* stopping not successfull, show API to users */ 
	} else {
	  break;
	}
      }
    }
    retval = 0;
  } else
    retval = 20;

  /*
   * free all resources
   */
  deinit_all();
  initialized = FALSE;

  SetTaskPri(AmiTCP_Task, oldpri);
  AmiTCP_Task->tc_Node.ln_Name = (char *)oldname;

  return retval;
}

/*
 * ng_ram_tier --- size the memory subsystem to the machine's installed RAM.
 *
 * PORT (AmiTCP_NG): the stock stack ships fixed compile-time defaults for the
 * mbuf pool ceiling and the socket-buffer sizes. That forces one compromise
 * across the whole 68k range: generous enough for throughput starves a 512 KB
 * A500; lean enough for the A500 throttles a big-RAM PiStorm. Instead, probe the
 * installed RAM once at startup and pick a tier.
 *
 * The mbuf pool CEILING (mbconf.maxmem) grows with RAM -- and because the pool
 * allocates on demand (m_clalloc grows up to maxmem*1024 only as traffic needs
 * it, see uipc_mbuf.c), a bigger ceiling costs a lightly-loaded machine nothing.
 * The lowest tier also shrinks the socket buffers so a single bulk connection
 * still fits a 512 KB machine.
 *
 * With RFC 1323 window scaling now implemented, the big-RAM tiers also raise
 * sb_max and the socket buffers above 64 KB, so a single connection can scale
 * past the old 64 KB per-window wall. The <=1 MB and 2-4 MB tiers keep sb_max at
 * 64 KB (buffers <= 61320), so they negotiate scaling with a shift of 0 -- inert
 * and interoperable, but with no >64 KB windows, saving memory on small machines.
 * Every buffer stays under sbreserve()'s cap (sb_max*mclbytes/(MSIZE+mclbytes)),
 * so soreserve() never fails.
 *
 * Must run BEFORE readconfig()/ng_readconfig_noargs(): those write any explicit
 * "MBUF_CONF MAXMEM" / "tcp.sendspace" config or RoadshowData tunables straight
 * into these same globals, so running first makes the tier a DEFAULT the user
 * can still override. Runs before mbinit() too, so the chosen maxmem takes hold.
 */
#ifndef MEMF_TOTAL
#define MEMF_TOTAL	0x00080000L	/* installed RAM; exec V36+ */
#endif
static void
ng_ram_tier(void)
{
  extern u_long tcp_sendspace, tcp_recvspace, udp_sendspace, udp_recvspace;
  /* sb_max is in <sys/socketvar.h>; mbconf in <sys/mbuf.h> -- both included above. */
  ULONG total;

  /* AvailMem(MEMF_TOTAL) reports installed RAM but needs exec V36+ (Kickstart
   * 2.0). On older Kickstart fall back to free RAM (MEMF_ANY == 0), which
   * under-tiers -- a safe, conservative choice -- rather than over-committing on
   * a small machine. A zero result (probe failure) falls through to the lowest
   * tier below. */
  if (SysBase->LibNode.lib_Version >= 36)
    total = AvailMem(MEMF_TOTAL);
  else
    total = AvailMem(MEMF_ANY);

  udp_sendspace = 9216;			/* really the max datagram size; unchanged */

  if (total == 0 || total <= 1024UL * 1024) {
    /* <= 1 MB (e.g. a 512 KB A500): lean, must still boot. Pool ceiling BELOW
     * the stock 256 KB; buffers small so one bulk conn (~2*buf of pool) fits.
     * sb_max 64 KB -> scaling shift computes to 0 (no >64 KB windows). */
    mbconf.maxmem = 128;			/* KB */
    sb_max        = 64UL * 1024;
    tcp_sendspace = tcp_recvspace = 11 * 1460;	/* 16060, MSS-aligned */
    udp_recvspace = 16 * 1024;
    ng_dns_cache_max = DNS_CACHE_ENTRIES_1MB;
  } else if (total <= 4UL * 1024 * 1024) {
    /* 2-4 MB: stock buffers; sb_max 64 KB -> scaling shift 0 (no >64 KB windows). */
    mbconf.maxmem = 256;
    sb_max        = 64UL * 1024;
    tcp_sendspace = tcp_recvspace = 42 * 1460;	/* 61320 */
    udp_recvspace = 41600;			/* 40*(1024+sizeof(sockaddr_in)) */
    ng_dns_cache_max = DNS_CACHE_ENTRIES_4MB;
  } else if (total <= 16UL * 1024 * 1024) {
    /* 8-16 MB: window scaling engaged -- ~128 KB buffers/windows. */
    mbconf.maxmem = 1024;
    sb_max        = 256UL * 1024;
    tcp_sendspace = tcp_recvspace = 90 * 1460;	/* 131400 (~128 KB), MSS-aligned */
    udp_recvspace = 41600;
    ng_dns_cache_max = DNS_CACHE_ENTRIES_16MB;
  } else {
    /* 32 MB+ (PiStorm-class): window scaling engaged -- ~256 KB buffers/windows. */
    mbconf.maxmem = 4096;
    sb_max        = 512UL * 1024;
    tcp_sendspace = tcp_recvspace = 180 * 1460;	/* 262800 (~256 KB), MSS-aligned */
    udp_recvspace = 41600;
    ng_dns_cache_max = DNS_CACHE_ENTRIES_32MB;
  }
}

/*
 * ng_cpu_tune --- turn on the CPU-costly RFC 1323 options only where the
 * processor can afford them.
 *
 * RFC 1323 timestamps put a 12-byte option -- built, byte-swapped and PAWS-
 * checked -- on every data segment (RSTs and keepalive probes carry no option),
 * and run a 2 Hz software clock. On a 7 MHz
 * 68000 that per-segment tax is not worth the benefit; on an '020 or better
 * it is lost in the noise beside the payoff: round-trip timing and PAWS for
 * the large scaled windows the upper RAM tiers open. So timestamps default
 * ON for a 68020/030/040/060 and OFF for a bare 68000/68010. Window scaling
 * (tcp_do_rfc1323) stays on everywhere -- it costs one shift at connection
 * setup, nothing per segment.
 *
 * Like ng_ram_tier(), this must run BEFORE readconfig() so an explicit config
 * tunable still overrides the CPU-derived default.
 */
static void
ng_cpu_tune(void)
{
  extern int tcp_do_rfc1323_tstmp;

  /* exec sets AFF_680x0 in AttnFlags for the detected processor (or better);
   * any of '020/'030/'040/'060 is "performant" for our purposes. */
  if (SysBase->AttnFlags &
      (AFF_68020 | AFF_68030 | AFF_68040 | AFF_68060))
    tcp_do_rfc1323_tstmp = 1;
  else
    tcp_do_rfc1323_tstmp = 0;
}

/*
 * init_all --- bring every subsystem up, in dependency order.
 *
 * Read this function as the anatomy of a TCP/IP stack: each step depends on the
 * ones before it, so the order is deliberate and not to be shuffled. If any step
 * fails we return FALSE and main() exits with an error (deinit_all() then unwinds
 * whatever did come up). The two load-bearing steps for a student to notice:
 *   readconfig()  - reads AmiTCP:db/AmiTCP.config; nothing works without it.
 *   api_init()    - THIS is where bsdsocket.library is actually created
 *                   (MakeLibrary, in api/amiga_api.c). api_show() at the end then
 *                   AddLibrary()s it so applications can open it.
 * Everything between is the machinery the library needs to do useful work.
 */
BOOL
init_all(void)
{
  /* malloc_init: the semaphore guarding the mbuf/kernel allocator (kern_malloc.c).
   * Must be first -- almost everything below allocates memory. */
  malloc_init();

  /*
   * initialize concurrency control subsystem
   */
  spl_init();

  /*
   * initialize sleep queues
   */
  sleep_init();

  /* PORT (AmiTCP_NG): size the memory defaults to installed RAM, and gate the
   * CPU-costly RFC 1323 timestamps on the processor, BEFORE readconfig(), so
   * explicit config-file tunables still override either default. */
  ng_ram_tier();
  ng_cpu_tune();
  ng_dnscache_init();		/* size the DNS cache from the RAM tier set above */
  ng_bpf_init();		/* reset the BPF capture channel table */

  /*
   * Read command line arguments and configuration file
   */
  if (!readconfig())
    return FALSE;

  /*
   * initialize logging system
   */
  if (!log_init())
    return FALSE;

  /*
   * initialize the mbuf subsystem
   */
  if (!mbinit())
    return FALSE;

  /*
   * initialize timer
   */
  if ((timermask = timer_init()) == 0L)
    return FALSE;

  /*
   * initialize API
   */
  if (!api_init())
    return FALSE;
	
  /*
   * initialize SANA-II subsystem
   */
  if ((sanamask = sana_init()) == 0L)
    return FALSE;
	    
  /*
   * initialize domains (initializes all protocols)
   */
  domaininit();
	    
  /*
   * Initialize NetDataBase
   */
  if (init_netdb() != 0)
    return FALSE;

  /*
   * Make API visible
   */
  if (api_show() == FALSE)
    return FALSE;

  if (Nettrace_Task)
    Signal(Nettrace_Task, SIGBREAKF_CTRL_F);
  else
    return FALSE;

  return TRUE;
}

/*
 * clean up everything
 */
void
deinit_all(void)
{
  /*
   * make sure we are out of critical section
   */
  spl0();

  api_hide();			/* hides the API from users */

  /*
   * Deinitialize network database.
   */
  netdb_deinit();

  /*
   * Close any open BPF capture channels (frees their buffers and detaches them
   * from interfaces) BEFORE the interfaces themselves are torn down.
   */
  ng_bpf_shutdown();

  /*
   * Deinitialize network interfaces
   */
  sana_deinit();

  /*
   * Deinitialize timers
   */
  timer_deinit();

  /*
   * Free all resources allocated by mbufs.
   */
  mbdeinit();

  log_deinit();

  /*
   * Check that there are no libraries open (to our API). We can continue only
   * if all bases are closed.
   */
  api_deinit();  /* NOTICE: this waits until every api user has exited */
}

/* ------------------------------------------------------------------------- *
 *  PORT (AmiTCP_NG): self-starting-library support.
 *
 *  In the LIBS:bsdsocket.library build the stack is not a program with a main();
 *  the library base is created by the loader (RomTag/AUTOINIT -> LibInit ->
 *  api_libinit), and the stack's subsystems + service loop run as a Process that
 *  ELL_Open() spawns the FIRST time an application opens the library. This mirrors
 *  how Roadshow's library self-starts, and is what makes AmiTCP_NG a genuine
 *  drop-in (install the one file, no boot-time `run amitcp`).
 *
 *  ng_stack_process()      the spawned Process: bring the subsystems up (the
 *                          init_all() body MINUS api_init()/api_show(), which the
 *                          loader already did), signal the opener we are ready,
 *                          run the same Wait()/poll service loop as main(), and on
 *                          shutdown tear the subsystems down again.
 *  ng_stack_ensure_running() called from ELL_Open(): spawn the process once and
 *                          block until it signals ready (or failed).
 * ------------------------------------------------------------------------- */
static struct Task *ng_stack_opener = NULL;	/* task to signal when ready     */

/* NOT SAVEDS: this is a spawned Process, entered by CreateNewProc with a6 = an
 * arbitrary value -- NOT the library base. The __saveds prologue would store that
 * garbage a6 into the global SysBase (correct for a library vector, fatal here)
 * and fault the first jsr a6@(-LVO). A process reads globals directly. */
static void ng_stack_process(void)
{
  struct Task *opener = ng_stack_opener;	/* captured before we could block */

  /* PORT (AmiTCP_NG): re-fetch SysBase from its canonical absolute address (4)
   * before ANY library call. This spawned Process must not rely on the value in
   * the global being correct in its context; a bad base register in a6 faults
   * the first jsr a6@(-LVO). This absolute read never uses a library base. */
  SysBase = *(struct ExecBase **)4;

  /*
   * PORT (AmiTCP_NG): open the library bases that libnix's C startup (crt0)
   * would have opened for the program build but which this -nostartfiles library
   * lacks. DOSBase is the critical one -- readconfig()/log_init() and much else
   * call dos.library through the global DOSBase; without it they dereference NULL
   * and crash. UtilityBase is needed for 32-bit math (see main()).
   */
  { extern struct DosLibrary *DOSBase;
    DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 0L); }
  { extern struct Library *UtilityBase;
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 37L); }

  /* PORT (AmiTCP_NG): a CreateNewProc'd Process has no Input()/Output() streams.
   * readconfig()'s ReadArgs() and any Printf() need them, so give the process
   * NIL: for both -- ReadArgs then sees an empty command line (all defaults) and
   * Printf output is harmlessly discarded. */
  { BPTR _in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    BPTR _out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (_in)  SelectInput(_in);
    if (_out) SelectOutput(_out); }

  /* PORT (AmiTCP_NG): suppress AmigaDOS requesters from this process. The optional
   * config/database opens below (AmiTCP:db/AmiTCP.config, AmiTCP:db/netdb) would
   * otherwise pop a "Please insert volume AmiTCP:" requester on a system that has no
   * AmiTCP: assign -- exactly the "device not mounted" case. We treat those files as
   * optional (missing -> built-in defaults), so a missing path must fail silently,
   * never prompt. pr_WindowPtr = -1 tells DOS to auto-fail requesters for this task. */
  { struct Process *me = (struct Process *)FindTask(NULL);
    me->pr_WindowPtr = (APTR)-1L; }

  /* PORT (AmiTCP_NG): make sure AmiTCP: resolves before we read the database. The
   * stack loads its optional config + netdb from AmiTCP:db/... . If AmiTCP: is already
   * assigned (the usual state -- the installer adds 'Assign AmiTCP: <chosen>' to
   * User-Startup for whatever location the user picked) we use it as-is. Otherwise fall
   * back to the installer's default drawer -- SYS:Programs/AmiTCP first (the default
   * install location when a Programs drawer exists), then SYS:AmiTCP -- so the database
   * loads no matter how the startup scripts are ordered. The requester suppression
   * above guarantees the AmiTCP: probe cannot pop a "please insert volume" requester. */
  { BPTR _l = Lock((STRPTR)"AmiTCP:", ACCESS_READ);
    if (_l) {
      UnLock(_l);
    } else {
      BPTR _d = Lock((STRPTR)"SYS:Programs/AmiTCP", ACCESS_READ);
      if (_d == 0)
        _d = Lock((STRPTR)"SYS:AmiTCP", ACCESS_READ);
      if (_d && !AssignLock((STRPTR)"AmiTCP", _d))
        UnLock(_d);			/* assign failed -> release the lock */
    } }

  AmiTCP_Task = FindTask(NULL);


  /* Subsystems, in the same dependency order init_all() uses -- but WITHOUT
   * api_init()/api_show(): the loader created and added the library already. */
  spl_init();
  sleep_init();
  malloc_init();		/* init_all() does this too -- MUST precede bsd_malloc() */
  ng_ram_tier();		/* size memory to installed RAM before config overrides */
  ng_cpu_tune();		/* gate RFC 1323 timestamps on the CPU, likewise */
  ng_dnscache_init();		/* size the DNS cache from the RAM tier set above */
  { extern BOOL ng_readconfig_noargs(void);
    if (!ng_readconfig_noargs())	goto fail; }
  if (!log_init())			goto fail;
  if (!mbinit())			goto fail;
  if ((timermask = timer_init()) == 0L)	goto fail;
  if ((sanamask  = sana_init())  == 0L)	goto fail;
  domaininit();
  if (init_netdb() != 0)		goto fail;
  if (Nettrace_Task)
    Signal(Nettrace_Task, SIGBREAKF_CTRL_F);
  else
    goto fail;

  SetTaskPri(AmiTCP_Task, 5);
  AmiTCP_Task->tc_Node.ln_Name = "AmiTCP_NG";
  initialized = TRUE;

  breakmask = SIGBREAKF_CTRL_C;			/* expunge signals us with this  */
  sigmask   = timermask | breakmask | sanamask;
  timer_send();

  ng_stack_running = TRUE;

  /* Configure the loopback interface (lo0 = 127.0.0.1 + its host route) now, while the
   * stack is fully initialised (initialized == TRUE, protocols/routing up) but BEFORE we
   * release the caller that triggered our lazy self-start. Doing it here -- rather than
   * after Signal() -- guarantees the caller (and any app) can never observe an
   * unconfigured lo0: the earlier "after Signal, in the service loop" placement left a
   * window of up to one timer tick where 127.0.0.1 had no route. It is safe here because
   * the stack is running; the only unsafe spot was the pre-`initialized` init block. This
   * touches no SANA-II hardware NIC -- lo0 is a pure software interface. */
  { extern int ng_config_loopback(void); (void)ng_config_loopback(); }

  if (opener) Signal(opener, SIGBREAKF_CTRL_F);	/* tell ELL_Open we are up       */

  /* Service loop -- identical to main()'s, but shutdown is a plain CTRL-C from
   * the library expunge path (all bases are already closed by then). */
  for (;;) {
    sig = Wait(sigmask);
    do {
      if (sig & sanamask) { if (!sana_poll())  sig &= ~sanamask; }
      if (sig & timermask){ if (!timer_poll()) sig &= ~timermask; }
      sig |= SetSignal(0L, sigmask) & sigmask;
    } while (sig && sig != breakmask);
    if (sig & breakmask)
      break;
  }

  initialized = FALSE;
  ng_stack_running = FALSE;
  /* Subsystem teardown (deinit_all() MINUS the api_* calls -- the library's
   * Close/Expunge own the library lifecycle). */
  spl0();
  netdb_deinit();
  sana_deinit();
  timer_deinit();
  mbdeinit();
  log_deinit();
  return;

fail:
  ng_stack_running = FALSE;
  if (opener) Signal(opener, SIGBREAKF_CTRL_F);	/* unblock ELL_Open on failure   */
}

/*
 * Called by ELL_Open() the first time the library is opened. Spawns the stack
 * Process and blocks until it reports ready (or failure). Returns TRUE if the
 * stack is running. Safe to call on every open -- a no-op once running.
 */
BOOL ng_stack_ensure_running(void)
{
  extern struct DosLibrary *DOSBase;
  extern struct SignalSemaphore ng_spawn_semaphore;
  struct Process *proc;

  if (ng_stack_running)
    return TRUE;

  /* CreateNewProcTags() is a dos.library call -- and the self-starting library
   * has no crt0, so its SysBase/DOSBase globals cannot be trusted here. Re-fetch
   * SysBase from its canonical absolute address (4) and open DOSBase through it
   * UNCONDITIONALLY: the library's uninitialised global may hold garbage (not
   * NULL), which a `== NULL` test would miss, leaving CreateNewProcTags to jump
   * through a bad base. (OpenLibrary is a cheap exec call, safe on a small stack.) */
  SysBase = *(struct ExecBase **)4;

  /* Serialise the spawn: two tasks calling socket() near-simultaneously before the
   * stack is up must not each spawn a Process. The first through wins; the rest
   * block here, then see ng_stack_running == TRUE on the re-check and return. Held
   * across the Wait() below -- semaphores (unlike Forbid()) survive Wait(). */
  ObtainSemaphore(&ng_spawn_semaphore);
  if (ng_stack_running) {
    ReleaseSemaphore(&ng_spawn_semaphore);
    return TRUE;
  }

  DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 0L);
  if (DOSBase == NULL) {
    ReleaseSemaphore(&ng_spawn_semaphore);
    return FALSE;
  }

  /* Keep the caller's-stack footprint minimal: only spawn + wait here (the caller
   * may be a small-stack program). All the heavy lifting -- DOS, device I/O --
   * happens inside ng_stack_process on its own 16 KB stack. */
  ng_stack_opener = FindTask(NULL);
  SetSignal(0L, SIGBREAKF_CTRL_F);		/* clear stale ready signal      */

  proc = CreateNewProcTags(NP_Entry,     (LONG)&ng_stack_process,
			   NP_Name,      (LONG)"AmiTCP_NG stack",
			   NP_Priority,  0,
			   NP_StackSize, 16384,
			   TAG_DONE, 0);
  if (proc == NULL) {
    ReleaseSemaphore(&ng_spawn_semaphore);
    return FALSE;
  }

  Wait(SIGBREAKF_CTRL_F);			/* until ng_stack_process signals */
  ReleaseSemaphore(&ng_spawn_semaphore);
  return ng_stack_running;
}

/*
 * Notification function for taskname
 */ 
int taskname_changed(void *p, LONG new)
{
  UBYTE *newname = (UBYTE *)new;

  AmiTCP_Task->tc_Node.ln_Name = (char *)newname;
  if (initialized)
    printf("New task name %s\n", newname);

  return TRUE;
}
