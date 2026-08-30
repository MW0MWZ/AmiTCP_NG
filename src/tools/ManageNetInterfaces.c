/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ManageNetInterfaces -- keep DEVS:NetInterfaces to the interfaces this machine
 * can actually bring up, parking the rest in SYS:Storage/NetInterfaces.
 *
 * WHY THIS IS NOT JUST "DOES THE FILE EXIST". The case it exists for is a driver
 * that is present but cannot be used: a2065.device sitting in DEVS:Networks on a
 * machine with no Zorro bus is the example this project already knows, because
 * the emulator harness hit it -- the file is right there and OpenDevice() fails
 * every time. A config left in DEVS:NetInterfaces for a device like that makes
 * every boot try, fail, and complain. So the test is an actual OpenDevice(),
 * immediately closed, sending no command: the minimum that answers "could the
 * network startup use this?", and nothing that would put hardware online.
 *
 * IT DOES NOT MOVE ANYTHING UNLESS ASKED. With neither CHECK nor COMMIT it
 * reports what it would do and stops -- the safe default for a tool whose whole
 * job is moving a user's configuration files around. CHECK is accepted as the
 * explicit spelling of that; COMMIT is the one that acts.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/ManageNetInterfaces.c -o ManageNetInterfaces
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <exec/ports.h>
#include <devices/sana2.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define PROG		"ManageNetInterfaces"
#define ACTIVE_DIR	"DEVS:NetInterfaces"
#define PARKED_DIR	"SYS:Storage/NetInterfaces"
#define LINELEN		512
#define NAMELEN		64
#define DEVLEN		128

static int quiet = 0, verbose = 0, commit = 0;
static int moved = 0, would = 0, problems = 0;
static char g_ignore[256];

/* ------------------------------------------------------------------ */

static int ci_eq(const char *a, const char *b)
{
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

static void say(const char *fmt, long a1, long a2)
{
  if (!quiet) Printf((STRPTR)fmt, a1, a2);
}

static void note(const char *fmt, long a1, long a2)
{
  if (verbose && !quiet) Printf((STRPTR)fmt, a1, a2);
}

static void copystr(char *dst, int sz, const char *src)
{
  int i = 0;
  if (sz <= 0) return;
  while (src && src[i] && i < sz - 1) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

static void joinpath(char *dst, int sz, const char *dir, const char *name)
{
  int i = 0, k;
  for (k = 0; dir[k] && i < sz - 2; k++) dst[i++] = dir[k];
  if (i > 0 && dst[i-1] != ':' && dst[i-1] != '/') dst[i++] = '/';
  for (k = 0; name[k] && i < sz - 1; k++) dst[i++] = name[k];
  dst[i] = '\0';
}

/* Is this device named in IGNOREDEVICES? The list is space or comma separated. */
static int is_ignored(const char *dev)
{
  char item[DEVLEN];
  int i = 0, n;

  if (!g_ignore[0] || !dev[0]) return 0;
  while (g_ignore[i]) {
    while (g_ignore[i] == ' ' || g_ignore[i] == ',' || g_ignore[i] == '\t') i++;
    n = 0;
    while (g_ignore[i] && g_ignore[i] != ' ' && g_ignore[i] != ',' &&
           g_ignore[i] != '\t' && n < (int)sizeof(item) - 1)
      item[n++] = g_ignore[i++];
    item[n] = '\0';
    if (n > 0 && ci_eq(item, dev)) return 1;
  }
  return 0;
}

/* ------------------------------------------------------------------ */

/*
 * Pull device= and unit= out of an interface config. Returns 0 if there is no
 * device line, which makes the file unusable whatever the hardware says.
 */
static int read_ifcfg(const char *path, char *dev, int devsz, LONG *unit)
{
  /* static, not automatic. A Shell process starts with about 4K of stack, and
   * this sits two frames below consider(), which is itself holding ~390 bytes
   * of path and device name. traceroute.c and CheckAmiTCPNGConfig.c both moved
   * their equivalents off the stack after exactly this overflowed and corrupted
   * a return address -- with no MMU it does not fault, it just returns
   * somewhere else. Safe as a static here: this tool is single-threaded and
   * read_ifcfg() is neither recursive nor re-entered. */
  static char line[LINELEN + 2];
  BPTR fh;
  int got = 0;

  dev[0] = '\0';
  *unit = 0;
  if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0)
    return 0;

  while (FGets(fh, (STRPTR)line, LINELEN)) {
    char *p = line, *kw, *val;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#' || *p == ';' || *p == '\n' || *p == '\r') continue;
    kw = p;
    while (*p && *p != '=' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    if (*p) { *p = '\0'; p++; }
    while (*p == '=' || *p == ' ' || *p == '\t') p++;
    val = p;
    while (*p && *p != '#' && *p != ';' && *p != '\n' && *p != '\r') p++;
    while (p > val && (p[-1] == ' ' || p[-1] == '\t')) p--;
    *p = '\0';

    if (ci_eq(kw, "device")) { copystr(dev, devsz, val); got = 1; }
    else if (ci_eq(kw, "unit")) {
      LONG u = 0; int k;
      for (k = 0; val[k] >= '0' && val[k] <= '9'; k++) u = u * 10 + (val[k] - '0');
      *unit = u;
    }
  }
  Close(fh);
  return got;
}

/*
 * Can the network startup actually use this device?
 *
 * OpenDevice and immediately CloseDevice -- no command is sent, so nothing is put
 * online and no packet moves. The bare name is tried first (which also finds a
 * driver already resident in memory) and then DEVS:Networks/, exactly as
 * netonoff.c and Roadshow do, so a config naming "a2065.device" works whichever
 * way the driver is present.
 */
/*
 * Returns 1 = usable, 0 = the device really would not open, and -1 = WE could
 * not run the test at all.
 *
 * The third case is not pedantry. Failing to allocate a message port says
 * nothing whatever about the interface, but the caller's rule is "not usable ->
 * park it", so folding it in with a genuine open failure meant a moment of low
 * memory could take a perfectly good interface out of DEVS:NetInterfaces and
 * leave the machine with no network on the next boot. The tool exists to
 * disable configs that cannot work; disabling one that can, because of a hiccup
 * in the tool itself, is the worst thing it could do.
 */
static int device_usable(const char *dev, LONG unit, LONG *errout)
{
  struct MsgPort *mp;
  struct IOSana2Req *io;
  LONG err;

  *errout = 0;
  if ((mp = CreateMsgPort()) == NULL)
    return -1;				/* could not test -- NOT "unusable" */
  if ((io = (struct IOSana2Req *)CreateIORequest(mp, sizeof(*io))) == NULL) {
    DeleteMsgPort(mp);
    return -1;				/* likewise */
  }

  err = OpenDevice((STRPTR)dev, unit, (struct IORequest *)io, 0);
  if (err == IOERR_OPENFAIL) {
    char full[DEVLEN + 32];
    joinpath(full, (int)sizeof(full), "DEVS:Networks", dev);
    err = OpenDevice((STRPTR)full, unit, (struct IORequest *)io, 0);
  }
  if (err == 0)
    CloseDevice((struct IORequest *)io);

  DeleteIORequest((struct IORequest *)io);
  DeleteMsgPort(mp);
  *errout = err;
  return err == 0;
}

/* ------------------------------------------------------------------ */

/*
 * Move one config between the drawers. Rename() first because the two drawers are
 * normally on the same volume (DEVS: is usually an assign into SYS:), and a
 * rename keeps the file's date and comment. A cross-volume setup makes Rename()
 * fail, so fall back to a copy followed by a delete -- and only delete once the
 * copy has been written and closed, so a failure half way leaves the original
 * where it was rather than losing it.
 */
static int move_file(const char *fromdir, const char *todir, const char *name)
{
  static char from[256], to[256];	/* off the stack -- see read_ifcfg() */
  BPTR in, out;
  char *buf;
  LONG n;
  int ok = 0;

  joinpath(from, (int)sizeof(from), fromdir, name);
  joinpath(to,   (int)sizeof(to),   todir,   name);

  if (Rename((STRPTR)from, (STRPTR)to))
    return 1;

  if ((buf = (char *)AllocVec(4096, MEMF_PUBLIC)) == NULL)
    return 0;
  if ((in = Open((STRPTR)from, MODE_OLDFILE)) != 0) {
    if ((out = Open((STRPTR)to, MODE_NEWFILE)) != 0) {
      ok = 1;
      while ((n = Read(in, buf, 4096)) > 0)
        if (Write(out, buf, n) != n) { ok = 0; break; }
      if (n < 0) ok = 0;
      Close(out);
      if (!ok) DeleteFile((STRPTR)to);
    }
    Close(in);
  }
  FreeVec(buf);

  if (ok && !DeleteFile((STRPTR)from)) {
    /* Copied but could not remove the original: that would leave the same
     * interface in BOTH drawers, which is worse than not moving it. */
    DeleteFile((STRPTR)to);
    ok = 0;
  }
  return ok;
}

/*
 * Consider one config file. `active` says which drawer it is in now.
 *
 * INSTALL promotes a parked config whose device works; CLEANUP parks an active
 * config whose device does not. Anything else is left alone and, in verbose mode,
 * said so -- "nothing to do" is a result worth being able to see.
 */
static void consider(const char *name, int active, int do_install, int do_cleanup)
{
  char path[256], dev[DEVLEN];
  LONG unit, err;
  int  usable;

  joinpath(path, (int)sizeof(path), active ? ACTIVE_DIR : PARKED_DIR, name);

  if (!read_ifcfg(path, dev, (int)sizeof(dev), &unit)) {
    /* No device= at all. Left where it is on purpose: moving a broken file does
     * not fix it, and CheckAmiTCPNGConfig is what explains it. */
    problems++;
    if (!quiet)
      Printf((STRPTR)"  %s: no 'device=' line -- left alone (see "
                     "CheckAmiTCPNGConfig)\n", (LONG)name);
    return;
  }

  if (is_ignored(dev)) {
    note("  %s: device %s is in IGNOREDEVICES -- skipped\n", (LONG)name, (LONG)dev);
    return;
  }

  usable = device_usable(dev, unit, &err);

  /*
   * Could not run the probe at all. Say so and change nothing: with no answer
   * about the device, every branch below would be acting on a guess, and the
   * one that guesses wrong disables a working interface.
   */
  if (usable < 0) {
    problems++;
    Printf((STRPTR)"  %s: could not test %s (out of memory) -- left alone\n",
           (LONG)name, (LONG)dev);
    return;
  }

  if (active && !usable && do_cleanup) {
    /*
     * Refuse if something of that name is ALREADY parked, exactly as the
     * install branch below refuses to overwrite an active config.
     *
     * move_file() renames first and falls back to a copy, and that copy opens
     * the destination MODE_NEWFILE -- which truncates. So without this check a
     * second cleanup of the same interface name silently destroys the config
     * parked by the first one, and CHECK gave no warning either because the
     * probe did not exist to warn from. Two install/cleanup cycles is all it
     * takes; the file is not recoverable.
     */
    char parked[256];
    joinpath(parked, (int)sizeof(parked), PARKED_DIR, name);
    { BPTR l = Lock((STRPTR)parked, ACCESS_READ);
      if (l) {
	UnLock(l);			/* a probe, not a claim */
	problems++;
	Printf((STRPTR)"  %s: already in " PARKED_DIR " -- left alone rather "
		       "than overwriting it\n", (LONG)name);
	return;
      }
    }
    if (commit) {
      if (move_file(ACTIVE_DIR, PARKED_DIR, name)) {
        moved++;
        /* Two conversions, two arguments. This said "%s: %s ... (error %ld)" with
         * only the name and device, so it printed whatever followed them on the
         * stack as the error code. The error itself is reported by the CHECK
         * wording below and by CheckAmiTCPNGConfig; the count is what matters
         * here. */
        say("  %s: %s cannot be opened -- parked in Storage\n",
            (LONG)name, (LONG)dev);
      } else {
        problems++;
        Printf((STRPTR)"  %s: could not be moved to " PARKED_DIR "\n", (LONG)name);
      }
    } else {
      would++;
      say("  %s: %s cannot be opened -- would park it in Storage\n",
          (LONG)name, (LONG)dev);
    }
  } else if (!active && usable && do_install) {
    char there[256];
    joinpath(there, (int)sizeof(there), ACTIVE_DIR, name);
    { BPTR l = Lock((STRPTR)there, ACCESS_READ);
      if (l) UnLock(l);			/* a probe, not a claim -- release it */
      if (!l) goto do_install_it;
    }
    {
      /* Same name already active: leave both alone rather than overwrite a
       * configuration the user is presumably using. */
      problems++;
      if (!quiet)
        Printf((STRPTR)"  %s: already present in " ACTIVE_DIR " -- not replaced\n",
               (LONG)name);
      return;
    }
do_install_it:
    if (commit) {
      if (move_file(PARKED_DIR, ACTIVE_DIR, name)) {
        moved++;
        say("  %s: %s works -- installed\n", (LONG)name, (LONG)dev);
      } else {
        problems++;
        Printf((STRPTR)"  %s: could not be moved to " ACTIVE_DIR "\n", (LONG)name);
      }
    } else {
      would++;
      say("  %s: %s works -- would install it\n", (LONG)name, (LONG)dev);
    }
  } else {
    note(usable ? "  %s: %s opens -- no change\n"
                : "  %s: %s does not open -- no change\n",
         (LONG)name, (LONG)dev);
  }
}

/* Walk one drawer. Missing is not an error: a machine may have neither. */
static void walk(const char *dir, int active, int do_install, int do_cleanup)
{
  BPTR lock;
  struct FileInfoBlock *fib;
  int n = 0;

  if ((lock = Lock((STRPTR)dir, ACCESS_READ)) == 0) {
    note("  %s does not exist\n", (LONG)dir, 0);
    return;
  }
  if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) == NULL) {
    UnLock(lock);
    Printf((STRPTR)PROG ": out of memory reading %s\n", (LONG)dir);
    problems++;
    return;
  }
  if (Examine(lock, fib)) {
    while (ExNext(lock, fib)) {
      const char *nm = (const char *)fib->fib_FileName;
      int len = 0;

      if (fib->fib_DirEntryType > 0) continue;		/* a drawer */
      while (nm[len]) len++;
      if (len > 5 && ci_eq(nm + len - 5, ".info")) continue;	/* icon */
      n++;
      consider(nm, active, do_install, do_cleanup);
    }
  }
  FreeDosObject(DOS_FIB, fib);
  UnLock(lock);
  note("  (%ld file(s) in %s)\n", (LONG)n, (LONG)dir);
}

/* ------------------------------------------------------------------ */

int main(void)
{
  struct RDArgs *rda;
  LONG a[6] = { 0, 0, 0, 0, 0, 0 };
  const char *action;
  int do_install, do_cleanup, rc = RETURN_OK;

  rda = ReadArgs((STRPTR)"ACTION,CHECK/S,COMMIT/S,QUIET/S,VERBOSE/S,"
                         "IGNORE=IGNOREDEVICES/K", a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }

  action  = a[0] ? (const char *)a[0] : "SYNC";
  commit  = a[2] != 0;
  quiet   = a[3] != 0;
  verbose = a[4] != 0;
  copystr(g_ignore, (int)sizeof(g_ignore), a[5] ? (const char *)a[5] : "");

  if (ci_eq(action, "INSTALL"))      { do_install = 1; do_cleanup = 0; }
  else if (ci_eq(action, "CLEANUP")) { do_install = 0; do_cleanup = 1; }
  else if (ci_eq(action, "SYNC"))    { do_install = 1; do_cleanup = 1; }
  else {
    Printf((STRPTR)PROG ": ACTION must be INSTALL, CLEANUP or SYNC (default SYNC)\n");
    FreeArgs(rda);
    return RETURN_ERROR;
  }

  say(PROG ": %s%s\n", (LONG)action,
      (LONG)(commit ? "" : " (reporting only -- add COMMIT to apply)"));

  /* CLEANUP first when doing both: parking a dead interface frees its name
   * before INSTALL might want to promote a parked config that uses it. */
  if (do_cleanup) walk(ACTIVE_DIR, 1, do_install, do_cleanup);
  if (do_install) walk(PARKED_DIR, 0, do_install, do_cleanup);

  if (commit)
    say("\n%ld file(s) moved", (LONG)moved, 0);
  else
    say("\n%ld change(s) would be made", (LONG)would, 0);
  /* say(), not Printf(): the surrounding sentence is gated on QUIET, so a bare
   * Printf here emitted an orphan ", 3 left alone" with no sentence in front
   * of it. The count still reaches the caller as RETURN_WARN either way. */
  if (problems)
    say(", %ld left alone", (LONG)problems, 0);
  say(".\n", 0, 0);

  if (problems) rc = RETURN_WARN;
  FreeArgs(rda);
  return rc;
}
