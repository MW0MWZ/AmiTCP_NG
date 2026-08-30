/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * Online / Offline -- switch a SANA-II network device driver online or offline,
 * a name-and-behaviour-compatible replacement for Roadshow's Online/Offline
 * commands (template NAME/A,UNIT/N,TIMEOUT/N; NAME is the SANA-II device driver,
 * e.g. "wifipi.device"). These talk to the driver directly via S2_ONLINE/S2_OFFLINE
 * -- they do not use bsdsocket.library -- and our stack reacts to the resulting
 * S2ERR_OUTOFSERVICE / S2EVENT_ONLINE (see net/if_sana.c) by taking the interface
 * down and re-raising it automatically.
 *
 * Build twice from this one source:
 *   m68k-amigaos-gcc -noixemul -O2 -m68000 -DDO_ONLINE  src/tools/netonoff.c -o Online
 *   m68k-amigaos-gcc -noixemul -O2 -m68000 -DDO_OFFLINE src/tools/netonoff.c -o Offline
 */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <exec/nodes.h>
#include <exec/ports.h>		/* PA_IGNORE -- neutralising an abandoned reply port */
#include <devices/sana2.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>	/* BeginIO() -- amiga.lib, not an Exec vector */

/* Seconds to wait for the driver before giving up, when TIMEOUT is not given. */
#define DEFAULT_TIMEOUT_SECS 15

#if defined(DO_ONLINE)
#  define CMD_NAME   "Online"
#  define S2_CMD     S2_ONLINE
#  define WANT_EVENT 1
#elif defined(DO_OFFLINE)
#  define CMD_NAME   "Offline"
#  define S2_CMD     S2_OFFLINE
#  define WANT_EVENT 0
#else
#  error define DO_ONLINE or DO_OFFLINE
#endif

/* Descriptive text for the common SANA-II open/io errors, matching the spirit of
 * Roadshow's Online/Offline diagnostics. */
static const char *io_error_string(long err)
{
  switch (err) {
    case S2ERR_NO_ERROR:        return "no error";
    case S2ERR_NO_RESOURCES:    return "resource allocation failure";
    case S2ERR_BAD_ARGUMENT:    return "garbage somewhere";
    case S2ERR_BAD_STATE:       return "already in requested state";
    case S2ERR_BAD_ADDRESS:     return "improper address";
    case S2ERR_MTU_EXCEEDED:    return "maximum transmission unit exceeded";
    case S2ERR_NOT_SUPPORTED:   return "command not supported by hardware";
    case S2ERR_SOFTWARE:        return "software error detected";
    case S2ERR_OUTOFSERVICE:    return "driver is offline";
    case S2ERR_TX_FAILURE:      return "transmission attempt failed";
    case IOERR_OPENFAIL:        return "could not open device";
    /* Reachable since the bounded wait existed: when the driver is slow but DOES
     * honour the abort, the request comes back IOERR_ABORTED inside the grace
     * window and takes the ordinary error path. Without this it printed
     * "unknown error (-2)", which reads like a fault in us rather than a timeout. */
    case IOERR_ABORTED:         return "timed out and was aborted";
    default:                    return "unknown error";
  }
}

/* Return a pointer to the file-name part of a path (after the last / or :). */
static const char *file_part(const char *p)
{
  const char *s = p;
  while (*p) { if (*p == '/' || *p == ':') s = p + 1; p++; }
  return s;
}

static void s_cat(char *dst, int cap, const char *src, int *k)
{
  int j;
  for (j = 0; src[j] && *k < cap - 1; j++) dst[(*k)++] = src[j];
  dst[*k] = '\0';
}

/*
 * Abort a SANA-II request the way SANA-II drivers actually expect.
 *
 * Exec's AbortIO() calls the device's AbortIO vector with the request in A1 and
 * leaves A3 undefined. A number of SANA-II drivers read their Unit out of A3 there
 * and, without it, silently do nothing -- so the abort below would be a no-op and
 * this command would always fall through to "ignored the abort", which is the exact
 * opposite of what a bounded wait is for. The library carries the same workaround
 * (AbortSanaIO in net/if_sana.c) and this is deliberately the same eight lines.
 *
 * A caution about WHY, so nobody re-derives the wrong reason: this A3 business is
 * NOT what makes wifipi.device ignore an abort, whatever the older comments in this
 * project say. Its AbortIO reads only A1 -- but it refuses to touch a request whose
 * ln_Type is not NT_MESSAGE, which is why do_cmd_bounded() sets that before
 * BeginIO(). Both are needed, for different drivers, and neither substitutes for
 * the other.
 *
 * Device AbortIO is vector -36 (Open -6, Close -12, Expunge -18, reserved -24,
 * BeginIO -30, AbortIO -36). Every register carrying a parameter is "+r": d0/d1/
 * a0/a1 are scratch across an AmigaOS call, and declaring one input-only lets the
 * compiler keep a live value there -- a bug that has already cost this project a
 * release.
 */
#ifdef __GNUC__
static void abort_sana_io(struct IORequest *ioRequest)
{
  register struct IORequest *_a1 __asm("a1") = ioRequest;
  register struct Unit      *_a3 __asm("a3") = ioRequest->io_Unit;
  register struct Device    *_a6 __asm("a6") = ioRequest->io_Device;
  __asm__ __volatile__("jsr a6@(-36)"
		       : "+r"(_a1), "+r"(_a3)
		       : "r"(_a6)
		       : "d0", "d1", "a0", "a2", "cc", "memory");
}
#else
#define abort_sana_io AbortIO
#endif

/*
 * Send ONE SANA-II command and wait a BOUNDED time for it.
 *
 * This used to be a bare DoIO(), which is fine right up until a driver accepts a
 * command and never completes it -- and then the command hangs for ever, with no
 * message, and the only way out is a reboot. That is not hypothetical: asking
 * x-surf-100.device to go OFFLINE when it is ALREADY offline hangs exactly here.
 * A command that talks to third-party hardware drivers has no business trusting
 * them; the library learned this the hard way (net/if_sana.c sana_doio_bounded)
 * and this is the same shape, kept deliberately similar so the two can be read
 * against each other.
 *
 * The TIMEOUT/N argument has existed in the template since the beginning and was
 * silently ignored -- parsed into args[2] and never looked at again. It means
 * something now.
 *
 * Two details that are easy to get wrong, both learned from the library version:
 *
 *   IOF_QUICK is SET before BeginIO, exactly as DoIO() does it. A driver that can
 *   answer immediately then does so inside BeginIO() and never replies through the
 *   port -- so there is nothing to wait for and nothing to abort, and touching the
 *   reply port afterwards would hang. Test the flag AFTER the call: the device
 *   clears it to mean "I went asynchronous, I will reply".
 *
 *   ln_Type = NT_MESSAGE is set before BeginIO, which SendIO()/DoIO() both do and a
 *   bare BeginIO() does not. CreateIORequest() leaves a request at NT_REPLYMSG, so
 *   without this CheckIO() would call a request that has never been sent "already
 *   complete", and many drivers' AbortIO refuses to touch a request that is not
 *   NT_MESSAGE -- so the abort below would quietly do nothing.
 *
 * On timeout the request belongs to the driver: it is NOT freed and the device is
 * NOT closed, because the driver may still write into both. That leaks, on purpose,
 * and says so. Returns the io_Error, or IOERR_ABORTED with *timedout set.
 */
static LONG do_cmd_bounded(struct IOSana2Req *io, LONG secs, int *timedout)
{
  struct MsgPort     *tport;
  struct timerequest *treq;
  ULONG               devbit, timbit, got;
  LONG                err;

  *timedout = 0;

  tport = CreateMsgPort();
  treq  = tport ? (struct timerequest *)CreateIORequest(tport, sizeof(*treq)) : NULL;
  if (!treq || OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
			  (struct IORequest *)treq, 0)) {
    /*
     * No timer. Fall back to the plain call rather than refusing to work at all --
     * unbounded, but this is the old behaviour and it is better than nothing.
     */
    if (treq)  DeleteIORequest((struct IORequest *)treq);
    if (tport) DeleteMsgPort(tport);
    DoIO((struct IORequest *)io);
    return io->ios2_Req.io_Error;
  }

  io->ios2_Req.io_Message.mn_Node.ln_Type = NT_MESSAGE;
  io->ios2_Req.io_Flags |= IOF_QUICK;
  BeginIO((struct IORequest *)io);

  if (io->ios2_Req.io_Flags & IOF_QUICK) {
    err = io->ios2_Req.io_Error;		/* answered inside BeginIO */
    goto done;
  }

  treq->tr_node.io_Command = TR_ADDREQUEST;
  treq->tr_time.tv_secs    = (secs > 0) ? secs : DEFAULT_TIMEOUT_SECS;
  treq->tr_time.tv_micro   = 0;
  SendIO((struct IORequest *)treq);

  devbit = 1UL << io->ios2_Req.io_Message.mn_ReplyPort->mp_SigBit;
  timbit = 1UL << tport->mp_SigBit;

  got = Wait(devbit | timbit);

  if (CheckIO((struct IORequest *)io)) {
    WaitIO((struct IORequest *)io);		/* complete; cannot block */
    err = io->ios2_Req.io_Error;
    if (!CheckIO((struct IORequest *)treq))
      AbortIO((struct IORequest *)treq);
    WaitIO((struct IORequest *)treq);
  } else {
    /* The timer fired first. Ask for the command back and give it a moment. */
    (void)got;
    WaitIO((struct IORequest *)treq);
    abort_sana_io((struct IORequest *)io);	/* NOT plain AbortIO -- see above */
    {
      int spin;
      for (spin = 0; spin < 25 && !CheckIO((struct IORequest *)io); spin++)
	Delay(2);				/* ~40 ms a turn, ~1 s in all */
    }
    if (CheckIO((struct IORequest *)io)) {
      WaitIO((struct IORequest *)io);
      err = io->ios2_Req.io_Error;
    } else {
      *timedout = 1;
      err = IOERR_ABORTED;
    }
  }

done:
  CloseDevice((struct IORequest *)treq);
  DeleteIORequest((struct IORequest *)treq);
  DeleteMsgPort(tport);
  return err;
}

/*
 * Open a SANA-II driver, trying the same five spellings the stack itself tries
 * (net/if_sana.c iface_make). Kept identical on purpose: a name that AddNetInterface
 * accepts in a `device=` line must be a name these commands accept too, or the two
 * disagree about what the machine's hardware is called.
 *
 *   1. exactly as given          -- finds an ALREADY-RESIDENT driver (what Roadshow
 *                                   does); trying a path first can force-load a second
 *                                   copy of a hardware driver, which is the ENXIO the
 *                                   stack's version of this comment warns about.
 *   2. <name>.device             -- Roadshow's config convention is a bare name.
 *   3. the bare FilePart         -- a resident driver that was named by a path.
 *   4. DEVS:Networks/<base>
 *   5. DEVS:Networks/<base>.device
 *
 * Prefixing always uses the FilePart, never the raw argument, so a name that already
 * carries a path cannot produce "DEVS:Networks/DEVS:Networks/...".
 */
static LONG open_sana(struct IOSana2Req *io, const char *dev, LONG unit)
{
  const char *base = file_part(dev);
  char  nm[160];
  int   k, dl = 0, has_dev;
  LONG  err;

  while (dev[dl]) dl++;
  has_dev = (dl >= 7 && dev[dl-7]=='.' && dev[dl-6]=='d' && dev[dl-5]=='e' &&
	     dev[dl-4]=='v' && dev[dl-3]=='i' && dev[dl-2]=='c' && dev[dl-1]=='e');

  err = OpenDevice((STRPTR)dev, unit, (struct IORequest *)io, 0);	/* 1 */
  if (err && !has_dev) {						/* 2 */
    k = 0; nm[0] = '\0'; s_cat(nm, sizeof(nm), dev, &k); s_cat(nm, sizeof(nm), ".device", &k);
    err = OpenDevice((STRPTR)nm, unit, (struct IORequest *)io, 0);
  }
  if (err && base != dev)						/* 3 */
    err = OpenDevice((STRPTR)base, unit, (struct IORequest *)io, 0);
  if (err) {								/* 4 */
    k = 0; nm[0] = '\0';
    s_cat(nm, sizeof(nm), "DEVS:Networks/", &k);
    s_cat(nm, sizeof(nm), base, &k);
    err = OpenDevice((STRPTR)nm, unit, (struct IORequest *)io, 0);
    if (err && !has_dev) {						/* 5 */
      s_cat(nm, sizeof(nm), ".device", &k);
      err = OpenDevice((STRPTR)nm, unit, (struct IORequest *)io, 0);
    }
  }
  return err;
}

/*
 * Resolve an INTERFACE name to the device it is configured on.
 *
 * The reported failure was `Offline X-Surf-100` -> "could not open device". That is not
 * a device name, it is an interface name -- and an interface's name IS the basename of
 * its file in DEVS:NetInterfaces (see AddNetInterface), so it is exactly the name the
 * operator has to hand. Roadshow's Offline documents a DEVICE name and would fail here
 * too, so this is an addition rather than a compatibility fix; nothing that worked
 * before behaves differently, because this is reached only after every device spelling
 * has already failed.
 *
 * Deliberately reads the config FILE rather than asking the running stack. Every
 * bsdsocket.library vector begins with NG_ENSURE_STACK(), so a single query call would
 * START the stack on a machine that had no networking up -- these commands must keep
 * working with no stack running, and quietly bringing one up because somebody typed
 * `Offline` would be far worse than the error message this replaces.
 *
 * Searches the same two places, in the same order, as AddNetInterface. Returns 1 if a
 * device was found, and writes the unit only if the file names one.
 */
static int iface_device(const char *ifname, char *devout, int devcap,
			LONG *unitout, int *have_unit)
{
  static const char *dirs[2] = { "DEVS:NetInterfaces", "SYS:Storage/NetInterfaces" };
  char  path[256], line[256];
  int   d, k, n;
  BPTR  fh;

  *have_unit = 0;
  devout[0] = '\0';

  for (d = 0; d < 2; d++) {
    /* Reset PER DIRECTORY. A first-directory file that names a unit but no device
     * leaves the search running, and without this its unit would then be applied to
     * a device found in the SECOND directory -- a different file entirely. Wrong
     * unit, opened silently, no diagnostic. A unit may only ever come from the same
     * file that supplied the device. */
    *have_unit = 0;
    devout[0]  = '\0';

    k = 0; path[0] = '\0';
    s_cat(path, sizeof(path), dirs[d], &k);
    s_cat(path, sizeof(path), "/", &k);
    s_cat(path, sizeof(path), ifname, &k);

    if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0)
      continue;

    while (FGets(fh, (STRPTR)line, sizeof(line))) {
      char *p = line, *val;
      while (*p == ' ' || *p == '\t') p++;
      if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\0')
	continue;
      /* keyword */
      { char kw[24]; int i = 0;
	/* Advance past the whole keyword even if it is longer than kw[] -- stopping
	 * the scan at the buffer bound would leave p mid-word, the separator would
	 * never be found, and the entire LINE would be dropped rather than one
	 * unknown key ignored. */
	while (*p && *p != '=' && *p != ' ' && *p != '\t') {
	  if (i < (int)sizeof(kw) - 1) kw[i++] = *p;
	  p++;
	}
	kw[i] = '\0';
	/* '=' and whitespace are interchangeable separators -- `device=x` and
	 * `device x` are both valid, which is what AddNetInterface's parse_line()
	 * accepts and what the config format is documented to allow. Requiring '='
	 * here made every space-separated file invisible to this lookup, which is
	 * precisely the case this fallback exists to serve. */
	while (*p == '=' || *p == ' ' || *p == '\t') p++;
	val = p;
	while (*p && *p != '#' && *p != ';' && *p != '\n' && *p != '\r') p++;
	while (p > val && (p[-1] == ' ' || p[-1] == '\t')) p--;
	*p = '\0';

	/* Only the two keys this command needs; everything else is ignored, exactly
	 * as AddNetInterface ignores keys it does not know. */
	if ((kw[0]=='d'||kw[0]=='D') && (kw[1]=='e'||kw[1]=='E') &&
	    (kw[2]=='v'||kw[2]=='V')) {
	  int i2 = 0; while (val[i2] && i2 < devcap - 1) { devout[i2] = val[i2]; i2++; }
	  devout[i2] = '\0';
	} else if ((kw[0]=='u'||kw[0]=='U') && (kw[1]=='n'||kw[1]=='N') &&
		   (kw[2]=='i'||kw[2]=='I')) {
	  LONG u = 0;
	  for (n = 0; val[n] >= '0' && val[n] <= '9'; n++) u = u * 10 + (val[n] - '0');
	  if (n > 0) { *unitout = u; *have_unit = 1; }
	}
      }
    }
    Close(fh);
    if (devout[0] != '\0')
      return 1;
  }
  return 0;
}

int main(void)
{
  struct RDArgs   *rda;
  LONG             args[3] = { 0, 0, 0 };     /* NAME/A, UNIT/N, TIMEOUT/N */
  struct MsgPort  *mp = NULL;
  struct IOSana2Req *io = NULL;
  STRPTR           name;
  LONG             unit, err, rc = RETURN_OK;
  LONG             tmo = DEFAULT_TIMEOUT_SECS;	/* the timeout ACTUALLY used */
  int              timedout = 0;

  rda = ReadArgs((STRPTR)"NAME/A,UNIT/N,TIMEOUT/N", args, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)CMD_NAME); return RETURN_ERROR; }
  name = (STRPTR)args[0];
  unit = args[1] ? *(LONG *)args[1] : 0;

  mp = CreateMsgPort();
  io = mp ? (struct IOSana2Req *)CreateIORequest(mp, sizeof(struct IOSana2Req)) : NULL;
  if (!io) { Printf((STRPTR)"%s: no memory.\n", (LONG)CMD_NAME); rc = RETURN_FAIL; goto out; }

  /* Open the driver, trying every spelling the stack itself would try. */
  err = open_sana(io, (const char *)name, unit);

  /*
   * Still nothing. Before giving up, consider that the operator may have named an
   * INTERFACE rather than a device -- which is the natural mistake, because that is
   * the name that appears in DEVS:NetInterfaces and in every other command. Look the
   * name up there and retry with the device it is configured on.
   *
   * Only for a bare name: an argument carrying a path was meant as a file, and going
   * looking for an interface of that name would be second-guessing it.
   */
  if (err != 0 && file_part((const char *)name) == (const char *)name) {
    char cfgdev[128];
    LONG cfgunit = 0;
    int  have_unit = 0;

    if (iface_device((const char *)name, cfgdev, sizeof(cfgdev), &cfgunit, &have_unit)) {
      /* An explicit UNIT on the command line wins; otherwise take the file's. */
      LONG use = (args[1] != 0) ? unit : (have_unit ? cfgunit : unit);
      err = open_sana(io, cfgdev, use);
      if (err == 0) {
	unit = use;
	Printf((STRPTR)"%s: '%s' is an interface, not a device -- using '%s' unit %ld.\n",
	       (LONG)CMD_NAME, (LONG)name, (LONG)cfgdev, use);
      } else {
	/* Naming the device we found makes the difference between "your card is
	 * missing" and "you typed the wrong name" obvious. */
	Printf((STRPTR)"%s: interface '%s' is configured on device '%s' unit %ld, "
	       "which would not open (%ld, %s).\n",
	       (LONG)CMD_NAME, (LONG)name, (LONG)cfgdev, use, err,
	       (LONG)io_error_string(err));
	io->ios2_Req.io_Device = NULL;
	rc = RETURN_FAIL; goto out;
      }
    }
  }

  if (err != 0) {
    Printf((STRPTR)"%s: could not open '%s' unit %ld (%ld, %s).\n",
           (LONG)CMD_NAME, (LONG)name, unit, err, (LONG)io_error_string(err));
    Printf((STRPTR)"%s: give the SANA-II DEVICE name (e.g. \"a2065.device\"), or the "
	   "name of an interface in DEVS:NetInterfaces.\n", (LONG)CMD_NAME);
    io->ios2_Req.io_Device = NULL;   /* mark not-open so we don't CloseDevice */
    rc = RETURN_FAIL; goto out;
  }

  io->ios2_Req.io_Command = S2_CMD;
  /* Work the effective timeout out ONCE, so the wait and the message that reports
   * it can never disagree. TIMEOUT=0 means "use the default", and saying "did not
   * answer within 0 seconds" after waiting fifteen is just a lie in a diagnostic. */
  tmo = args[2] ? *(LONG *)args[2] : 0;
  if (tmo <= 0)
    tmo = DEFAULT_TIMEOUT_SECS;
  err = do_cmd_bounded(io, tmo, &timedout);

  if (timedout) {
    /*
     * The driver took the command and would not give it back. Say so plainly and
     * name the way out, because the alternative -- what this command used to do --
     * was to hang here for ever with nothing printed at all.
     */
    Printf((STRPTR)"%s: '%s' unit %ld did not answer %s within %ld seconds, and "
	   "ignored the abort.\n", (LONG)CMD_NAME, (LONG)name, unit,
	   (LONG)(WANT_EVENT ? "S2_ONLINE" : "S2_OFFLINE"), tmo);
    Printf((STRPTR)"%s: the request has been left with the driver rather than freed "
	   "under it. Use TIMEOUT to wait longer.\n", (LONG)CMD_NAME);
    /*
     * The driver still owns the request and the open device -- do not free or close
     * either. Leak deliberately; a reboot is cheaper than corruption.
     *
     * THE REPLY PORT MUST LEAK TOO, and that is the whole point. `mp` is this
     * request's mn_ReplyPort. Deleting it while the driver still holds the request
     * hands back its memory AND its signal bit, so a driver that finally does
     * ReplyMsg() writes through a freed port and Signal()s whatever task now owns
     * that bit -- on a machine with no MMU, silent corruption somewhere else
     * entirely, which is exactly the outcome the leak above exists to prevent.
     * Freeing the port would have made the request leak pointless.
     *
     * Neutralise it first so a late reply cannot signal anybody, then drop it on
     * the floor. This mirrors sana_doio_bounded() in net/if_sana.c, which does the
     * same two stores under Forbid() for the same reason.
     */
    {
      struct MsgPort *rport = io->ios2_Req.io_Message.mn_ReplyPort;
      Forbid();
      rport->mp_Flags   = PA_IGNORE;
      rport->mp_SigTask = NULL;
      Permit();
    }
    io = NULL;
    mp = NULL;			/* do NOT DeleteMsgPort() what the driver may reply into */
    rc = RETURN_FAIL;
    goto out;
  }

  /* S2ERR_BAD_STATE == already in the requested state; treat as success (a warning),
   * matching Roadshow's note that some drivers report this instead of "already so". */
  if (err == 0 || err == S2ERR_BAD_STATE) {
    rc = (err == S2ERR_BAD_STATE) ? RETURN_WARN : RETURN_OK;
  } else {
    Printf((STRPTR)"%s: '%s' unit %ld: %s (%ld).\n",
           (LONG)CMD_NAME, (LONG)name, unit, (LONG)io_error_string(err), err);
    rc = RETURN_ERROR;
  }

  CloseDevice((struct IORequest *)io);

out:
  if (io) DeleteIORequest((struct IORequest *)io);
  if (mp) DeleteMsgPort(mp);
  FreeArgs(rda);
  return rc;
}
