/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * CheckAmiTCPNGConfig -- read every configuration file the stack uses and say what
 * is wrong with it. Our equivalent of Roadshow's CheckRoadshowConfig.
 *
 * The point is the silent failures. Almost nothing here is rejected at boot: an
 * unknown keyword in an interface file is ignored on purpose so that files written
 * for a newer version still work, which also means "adress=192.168.0.10" configures
 * nothing and says nothing. This tool is where those become visible.
 *
 * IT DOES NOT TOUCH THE MACHINE. No interface is opened, no device is probed, and
 * bsdsocket.library is deliberately NOT opened -- our library starts the whole stack
 * on the first OpenLibrary(), and a program called "check my configuration" must not
 * bring the network up as a side effect of being run. Everything here is reading
 * files. That does mean it validates the SHAPE of saved settings rather than asking
 * a running stack whether it likes them; AmiTCPControl does the latter.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/CheckAmiTCPNGConfig.c -o CheckAmiTCPNGConfig
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <ng_hostname.h>
#include "ng_ifkeys.h"

#define PROG		"CheckAmiTCPNGConfig"
#define LINELEN		512
/* NOT a local #define any more. This used to say 255 while the stack said 64, so
 * the checker passed a HOSTNAME= the stack then silently truncated. One value,
 * one header, shared with the library -- see <ng_hostname.h>. */
#define MAXHOSTNAMELEN	NG_MAXHOSTNAME

static int errors = 0, warnings = 0, quiet = 0, verbose = 0;

/* ------------------------------------------------------------------ */

static void say(const char *fmt, long a1, long a2)
{
  if (!quiet) Printf((STRPTR)fmt, a1, a2);
}

static void note(const char *fmt, long a1, long a2)
{
  if (verbose && !quiet) Printf((STRPTR)fmt, a1, a2);
}

static void problem(int fatal, const char *fmt, long a1, long a2)
{
  if (fatal) errors++; else warnings++;
  /* Problems print even with QUIET: a checker that can be told to say nothing at
   * all about what is broken has no reason to exist. QUIET suppresses the "ok"
   * lines, not the findings. */
  Printf((STRPTR)(fatal ? "  ERROR: " : "  WARNING: "));
  Printf((STRPTR)fmt, a1, a2);
}

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

/* Case-insensitive "does s start with pfx". */
static int ci_eq_prefix(const char *s, const char *pfx)
{
  while (*pfx) {
    char a = *s++, b = *pfx++;
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return 0;
  }
  return 1;
}

static int exists(const char *path)
{
  BPTR l = Lock((STRPTR)path, ACCESS_READ);
  if (l) { UnLock(l); return 1; }
  return 0;
}

/* A dotted quad, and nothing else. Returns 1 if valid. */
static int valid_ipv4(const char *s)
{
  int part, digits, dots = 0;

  if (!s || !*s) return 0;
  for (;;) {
    part = 0; digits = 0;
    while (*s >= '0' && *s <= '9') {
      part = part * 10 + (*s - '0');
      if (++digits > 3 || part > 255) return 0;
      s++;
    }
    if (digits == 0) return 0;
    if (*s == '.') { dots++; s++; if (dots > 3) return 0; continue; }
    break;
  }
  return (*s == '\0' && dots == 3);
}

/*
 * The same rule the stack applies in ng_hostname_valid() (kern/amiga_cstat.c):
 * letters, digits and hyphens, no empty label, no label starting or ending with a
 * hyphen, no trailing dot. Mirrored rather than shared because that one lives in
 * the library; if the two ever disagree, the library wins and this is the bug.
 */
static int valid_hostname(const char *s)
{
  int i, label, len = 0;

  while (s[len]) len++;
  if (len <= 0 || len > MAXHOSTNAMELEN) return 0;
  for (i = 0, label = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '.') {
      if (label == 0 || s[i-1] == '-') return 0;
      label = 0;
      continue;
    }
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-'))
      return 0;
    if (label == 0 && c == '-') return 0;
    if (++label > 63) return 0;
  }
  return !(label == 0 || s[len-1] == '-');
}

static int all_digits(const char *s)
{
  if (!s || !*s) return 0;
  while (*s) { if (*s < '0' || *s > '9') return 0; s++; }
  return 1;
}

/* Split "key = value" in place. Returns 0 for a blank or comment line. */
static int split_kv(char *line, char **kw, char **val)
{
  char *p = line, *v;

  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0' || *p == '#' || *p == ';' || *p == '\n' || *p == '\r')
    return 0;
  *kw = p;
  while (*p && *p != '=' && *p != ' ' && *p != '\t' &&
         *p != '\n' && *p != '\r')
    p++;
  if (*p) { *p = '\0'; p++; }
  while (*p == '=' || *p == ' ' || *p == '\t') p++;
  v = p;
  while (*p && *p != '#' && *p != ';' && *p != '\n' && *p != '\r') p++;
  while (p > v && (p[-1] == ' ' || p[-1] == '\t')) p--;
  *p = '\0';
  *val = v;
  return 1;
}

/* ------------------------------------------------------------------ */

static int  seen_hostname = 0;

/*
 * One config file, following WITH includes. WITH is a real directive
 * (kern/amiga_config.c recurses through parsefile), so checking only the top file
 * would silently approve a config whose actual settings live somewhere else --
 * and report a missing HOSTNAME that is in fact set. Depth-limited: a config that
 * includes itself is a mistake, not a case to support.
 */
static void scan_config_file(const char *path, int depth, char *line)
{
  BPTR fh;

  if (depth > 4) {
    problem(0, "WITH includes are nested more than 4 deep; not following further.\n",
            0, 0);
    return;
  }
  if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0) {
    if (depth > 0)
      problem(1, "WITH names '%s', which cannot be opened.\n", (LONG)path, 0);
    else
      problem(1, "%s is missing or unreadable.\n", (LONG)path, 0);
    return;
  }

  while (FGets(fh, (STRPTR)line, LINELEN)) {
    char *kw, *val;
    char inc[128];
    int  k;

    if (!split_kv(line, &kw, &val))
      continue;

    if (ci_eq(kw, "WITH")) {
      if (val[0] == '\0') {
        problem(1, "a WITH line names no file.\n", 0, 0);
        continue;
      }
      /* The line buffer is shared, so copy the name before recursing into it. */
      for (k = 0; val[k] && k < (int)sizeof(inc) - 1; k++) inc[k] = val[k];
      inc[k] = '\0';
      note("  WITH %s\n", (LONG)inc, 0);
      scan_config_file(inc, depth + 1, line);
      continue;
    }

    if (ci_eq(kw, "HOSTNAME")) {
      seen_hostname = 1;
      if (!valid_hostname(val))
        problem(1, "HOSTNAME '%s' is not a valid host name. Use letters, digits and\n"
                   "         hyphens only; no leading or trailing hyphen.\n", (LONG)val, 0);
      else
        note("  HOSTNAME = %s\n", (LONG)val, 0);
    } else if (ci_eq(kw, "LOGLEVEL") || ci_eq(kw, "LOGL")) {
      if (!all_digits(val) || val[0] > '7' || val[1] != '\0')
        problem(0, "LOGLEVEL '%s' should be a single digit 0-7 (7 = most detail).\n",
                (LONG)val, 0);
      else
        note("  LOGLEVEL = %s\n", (LONG)val, 0);
    } else if (ci_eq(kw, "LOGGING") || ci_eq(kw, "LOGCONSOLE") ||
               ci_eq(kw, "LOGCON") || ci_eq(kw, "MBUFCHECK") ||
               ci_eq(kw, "MBCHK")) {
      /*
       * Accept every spelling the STACK accepts, not a subset of them. The
       * parser's boolean_enum (kern/amiga_config.h) is
       *     NO=FALSE=OFF=0,YES=TRUE=ON=1
       * so LOGGING=TRUE and MBUFCHECK=1 are perfectly valid and this warned
       * about both. A checker that flags working configuration teaches people
       * to ignore it, which costs more than the check is worth. MBCHK is the
       * abbreviation config_var.c registers alongside MBUFCHECK.
       */
      if (!(ci_eq(val, "ON")   || ci_eq(val, "OFF") ||
            ci_eq(val, "YES")  || ci_eq(val, "NO")  ||
            ci_eq(val, "TRUE") || ci_eq(val, "FALSE") ||
            ci_eq(val, "1")    || ci_eq(val, "0")))
        problem(0, "%s should be ON or OFF (YES/NO, TRUE/FALSE and 1/0 also work).\n",
                (LONG)kw, 0);
      else
        note("  %s = %s\n", (LONG)kw, (LONG)val);
    } else if (ci_eq(kw, "LOGFILENAME") || ci_eq(kw, "LOGF")) {
      if (val[0] == '\0')
        problem(0, "LOGFILENAME is empty; logging would fall back to its default.\n", 0, 0);
      else
        note("  LOGFILENAME = %s\n", (LONG)val, 0);
    } else if (ci_eq(kw, "CONSOLENAME") || ci_eq(kw, "CON")) {
      /*
       * CONSOLENAME is a SECOND destination, independent of LOGFILENAME. It is
       * meant to be a console (CON:/RAW:), and pointing it at a plain file is
       * legal but produces a second log that NetLogViewer does not show -- it
       * follows LOGFILENAME, which is the complete record. Worth saying out loud,
       * because the two files then diverge and only one of them has the banner.
       */
      if (val[0] == '\0')
        problem(0, "CONSOLENAME is empty; the log console would fall back to its\n"
                   "           default.\n", 0, 0);
      else if (!(ci_eq_prefix(val, "CON:") || ci_eq_prefix(val, "RAW:") ||
                 ci_eq_prefix(val, "NIL:")))
        problem(0, "CONSOLENAME is '%s', which is not a console. That is allowed, but\n"
                   "           with LOGCONSOLE=ON it writes a SECOND log file, and\n"
                   "           NetLogViewer shows the LOGFILENAME one.\n", (LONG)val, 0);
      else
        note("  CONSOLENAME = %s\n", (LONG)val, 0);
    }
    /*
     * Anything else is left alone ON PURPOSE. The stack's configuration table
     * carries hundreds of names, most of them read-only statistics, and a tool
     * that guessed which are legal would cry wolf over perfectly good lines.
     * Silence here means "not checked", not "approved".
     */
  }
  Close(fh);
}

static void check_stack_config(char *line)
{
  const char *path = "AmiTCP:db/AmiTCP.config";
  int before_errors = errors;	/* so the "ok" below can tell whether it is true */

  if (!exists("AmiTCP:")) {
    problem(1, "the AmiTCP: assign does not exist. Nothing can find the stack's\n"
               "         configuration without it -- add it to S:User-Startup, or\n"
               "         re-run the installer.\n", 0, 0);
    return;
  }
  say("AmiTCP: assign                 ok\n", 0, 0);

  seen_hostname = 0;
  scan_config_file(path, 0, line);

  if (!seen_hostname)
    problem(0, "no HOSTNAME line. The stack will use a default, and DHCP will\n"
               "           override it anyway, but setting one is tidier.\n", 0, 0);
  /*
   * Only say "ok" if we actually read it. scan_config_file() returns void, so
   * this line used to print unconditionally -- including immediately after the
   * tool had reported the very same file as missing or unreadable. A checker
   * that contradicts itself in consecutive lines is worse than no checker,
   * because the whole value of this program is being believed.
   */
  if (errors == before_errors)
    say("AmiTCP:db/AmiTCP.config        ok\n", 0, 0);

  if (!exists("AmiTCP:db/netdb"))
    problem(0, "AmiTCP:db/netdb is missing. Service and protocol names\n"
               "           (http, tcp, ...) will not resolve.\n", 0, 0);
}

/* ------------------------------------------------------------------ */

static void check_one_interface(const char *name, char *line)
{
  char path[128];
  BPTR fh;
  int  i = 0, k;
  int  have_device = 0, have_address = 0, dhcp = 0, have_netmask = 0;
  char devname[128];

  devname[0] = '\0';

  { const char *pfx = "DEVS:NetInterfaces/";
    while (pfx[i] && i < (int)sizeof(path) - 1) { path[i] = pfx[i]; i++; }
    for (k = 0; name[k] && i < (int)sizeof(path) - 1; k++) path[i++] = name[k];
    path[i] = '\0'; }

  if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0) {
    problem(1, "cannot read %s\n", (LONG)path, 0);
    return;
  }

  while (FGets(fh, (STRPTR)line, LINELEN)) {
    char *kw, *val;
    int known = 0;

    if (!split_kv(line, &kw, &val))
      continue;

    /* Validate against the public keywords AND the unadvertised ones: a setting
     * that is deliberately undocumented is still a setting, and reporting it as a
     * spelling mistake would be worse than not mentioning it at all. Only
     * NG_IFKEYS is ever shown to the user. */
#define CHECKKEY(k) if (ci_eq(kw, k)) known = 1;
    NG_IFKEYS(CHECKKEY)
    NG_IFKEYS_UNADVERTISED(CHECKKEY)
#undef CHECKKEY

    if (kw[0] == '\0') {
      /* A line starting with '=' parses to an empty keyword. Saying "'' is not a
       * keyword" is technically true and completely unhelpful. */
      problem(0, "%s: a line begins with '=' and has no keyword before it.\n",
              (LONG)name, 0);
      continue;
    }
    if (!known) {
      problem(0, "%s: '%s' is not a keyword this stack understands, so the line\n"
                 "           does nothing. Check the spelling.\n", (LONG)name, (LONG)kw);
      continue;
    }

    if (ci_eq(kw, "device")) {
      have_device = 1;
      for (k = 0; val[k] && k < (int)sizeof(devname) - 1; k++) devname[k] = val[k];
      devname[k] = '\0';
    } else if (ci_eq(kw, "configure")) {
      if (ci_eq(val, NG_IFCONFIGURE_SUPPORTED)) {
        dhcp = 1;
      } else if (ci_eq(val, "auto") || ci_eq(val, "fastauto")) {
        problem(1, "%s: 'configure=%s' is a Roadshow setting this stack does not\n"
                   "         implement, and it is ignored -- the interface would come up\n"
                   "         with no address at all. Use configure=dhcp (link-local is\n"
                   "         the automatic fallback when no DHCP server answers), or set\n"
                   "         address= and netmask= for a static address.\n",
                (LONG)name, (LONG)val);
      } else {
        problem(1, "%s: 'configure=%s' is not understood; only 'dhcp' is.\n",
                (LONG)name, (LONG)val);
      }
    } else if (ci_eq(kw, "address")) {
      have_address = 1;
      if (!valid_ipv4(val))
        problem(1, "%s: address '%s' is not a dotted quad.\n", (LONG)name, (LONG)val);
    } else if (ci_eq(kw, "netmask")) {
      have_netmask = 1;
      if (!valid_ipv4(val))
        problem(1, "%s: netmask '%s' is not a dotted quad.\n", (LONG)name, (LONG)val);
    } else if (ci_eq(kw, "gateway")) {
      if (!valid_ipv4(val))
        problem(1, "%s: gateway '%s' is not a dotted quad.\n", (LONG)name, (LONG)val);
    } else if (ci_eq(kw, "nameserver")) {
      if (!valid_ipv4(val))
        problem(1, "%s: nameserver '%s' is not a dotted quad.\n", (LONG)name, (LONG)val);
    } else if (ci_eq(kw, "unit") || ci_eq(kw, "iprequests") ||
               ci_eq(kw, "writerequests") || ci_eq(kw, "mtu") ||
               ci_eq(kw, "tcp.sendspace") || ci_eq(kw, "tcp.recvspace") ||
               ci_eq(kw, "tcp.mssdflt")) {
      if (!all_digits(val))
        problem(1, "%s: %s must be a number.\n", (LONG)name, (LONG)kw);
    } else if (ci_eq(kw, "requiresinitdelay")) {
      if (!(ci_eq(val, "yes") || ci_eq(val, "no")))
        problem(0, "%s: requiresinitdelay should be yes or no.\n", (LONG)name, 0);
    }
  }
  Close(fh);

  if (!have_device) {
    problem(1, "%s: no 'device=' line, so there is nothing to open.\n", (LONG)name, 0);
  } else {
    /*
     * Look for the driver as a file. A device may legitimately be resident in
     * memory instead, so a miss is a WARNING -- but a name that is neither a
     * file nor in DEVS:Networks/ is very often just a typo, and the alternative
     * is "could not open device" at boot with no clue which half is wrong.
     */
    char full[160];
    const char *pfx = "DEVS:Networks/";
    int j = 0;

    if (!exists(devname)) {
      while (pfx[j] && j < (int)sizeof(full) - 1) { full[j] = pfx[j]; j++; }
      for (k = 0; devname[k] && j < (int)sizeof(full) - 1; k++) full[j++] = devname[k];
      full[j] = '\0';
      if (!exists(full))
        problem(0, "%s: '%s' was not found as a file, here or in DEVS:Networks/.\n"
                   "           That is fine if the driver is resident; otherwise check\n"
                   "           the name.\n", (LONG)name, (LONG)devname);
    }
  }

  if (!have_address && !dhcp)
    problem(1, "%s: neither 'address=' nor 'configure=dhcp', so this interface\n"
               "         would come up with no address.\n", (LONG)name, 0);
  if (have_address && !have_netmask && !dhcp)
    problem(0, "%s: address= without netmask=; the stack will guess one from the\n"
               "           address class, which is rarely what you want.\n", (LONG)name, 0);

  /* Two conversions, two arguments -- this said %s three times with two, which
   * would have printed whatever followed on the stack. And it was gated on the
   * GLOBAL error count, so one bad file silenced the notes for every good one. */
  note("  %s: device %s\n", (LONG)name, (LONG)devname);
}

static void check_interfaces(char *line)
{
  BPTR lock;
  struct FileInfoBlock *fib;
  int n = 0;

  if ((lock = Lock((STRPTR)"DEVS:NetInterfaces", ACCESS_READ)) == 0) {
    problem(0, "DEVS:NetInterfaces does not exist, so no interface is configured.\n"
               "           Copy an example from SYS:Storage/NetInterfaces into it.\n", 0, 0);
    return;
  }
  if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) == NULL) {
    UnLock(lock);
    problem(1, "out of memory reading DEVS:NetInterfaces\n", 0, 0);
    return;
  }

  if (Examine(lock, fib)) {
    while (ExNext(lock, fib)) {
      const char *nm = (const char *)fib->fib_FileName;
      int len = 0;

      if (fib->fib_DirEntryType > 0)		/* a drawer, not a config */
        continue;
      while (nm[len]) len++;
      if (len > 5 && ci_eq(nm + len - 5, ".info"))	/* Workbench icon */
        continue;
      n++;
      check_one_interface(nm, line);
    }
  }
  FreeDosObject(DOS_FIB, fib);
  UnLock(lock);

  if (n == 0)
    problem(0, "DEVS:NetInterfaces is empty, so no interface is configured.\n", 0, 0);
  else
    say("DEVS:NetInterfaces             %ld interface file(s)\n", (LONG)n, 0);
}

/* ------------------------------------------------------------------ */

static void check_hosts(char *line)
{
  const char *path = "DEVS:Internet/hosts";
  BPTR fh;
  int n = 0, seen_localhost = 0;

  if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0) {
    problem(0, "%s is missing. Names you have written down there (and\n"
               "           'localhost') will not resolve.\n", (LONG)path, 0);
    return;
  }
  while (FGets(fh, (STRPTR)line, LINELEN)) {
    char *p = line, *addr;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#' || *p == '\n' || *p == '\r') continue;
    addr = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    if (*p) { *p = '\0'; p++; }
    if (!valid_ipv4(addr)) {
      problem(0, "%s: '%s' is not a dotted quad.\n", (LONG)path, (LONG)addr);
      continue;
    }
    n++;
    /* Actually look for localhost, which the documentation says this checks.
     * Only the name field is scanned -- any of the names on the line counts,
     * since aliases resolve too. */
    while (*p) {
      char *name = p;
      while (*p == ' ' || *p == '\t') { *p = '\0'; p++; name = p; }
      if (*name == '\0' || *name == '\n' || *name == '\r' || *name == '#') break;
      while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
      if (*p) { *p = '\0'; p++; }
      if (ci_eq(name, "localhost")) { seen_localhost = 1; break; }
    }
  }
  Close(fh);
  if (n == 0)
    problem(0, "%s has no entries at all -- not even 'localhost'. Software that\n"
               "           looks up localhost by name will fail.\n", (LONG)path, 0);
  else if (!seen_localhost)
    problem(0, "%s has no 'localhost' entry. Software that looks it up by name\n"
               "           will fail; add a line reading  127.0.0.1  localhost\n",
            (LONG)path, 0);
  say("DEVS:Internet/hosts            %ld entr(y/ies)\n", (LONG)n, 0);
}

/* ------------------------------------------------------------------ */

/*
 * The other DEVS:Internet databases: services, protocols and networks.
 *
 * These are checked because the stack now READS them (kern/amiga_netdb.c). A
 * malformed line there does not fail loudly -- the entry is dropped and a notice
 * goes to the network log, which is exactly the sort of message this tool exists
 * to save people from having to find. A missing file is not a problem: most
 * machines have never had Roadshow, and our own AmiTCP:db/netdb carries the
 * standard entries anyway.
 *
 * Deliberately shallow. The checks are the ones that decide whether the stack
 * will accept a line at all -- a name, then a value of the right shape -- and
 * not whether the numbers are the IANA-blessed ones. Someone's private service
 * on a private port is not an error, and a checker that says it is gets ignored.
 */
static void check_internet_db(char *line, const char *path, const char *label,
			      int kind)
{
  BPTR fh;
  int n = 0, bad = 0;

  /* kind: 0 = services (port/proto), 1 = protocols (number), 2 = networks (addr).
   * `label` is the column-padded name for the report; `path` is what gets
   * opened -- padding the path would simply fail to find the file. */
  if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0)
    return;			/* optional -- silence is the right answer */

  while (FGets(fh, (STRPTR)line, LINELEN)) {
    char *p = line, *name, *val, *sep;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#' || *p == ';' || *p == '\n' || *p == '\r') continue;

    name = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r') {
      /* A name with nothing after it. */
      *p = '\0';
      problem(0, "%s: '%s' has a name but no value.\n", (LONG)path, (LONG)name);
      bad++;
      continue;
    }
    *p++ = '\0';
    while (*p == ' ' || *p == '\t') p++;
    val = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    *p = '\0';

    if (kind == 0) {
      /* name port/proto -- Roadshow accepts ',' as well as '/', and so do we. */
      long port = 0;
      char *q;
      for (sep = val; *sep && *sep != '/' && *sep != ','; sep++)
	;
      if (*sep == '\0') {
	problem(0, "%s: '%s' needs a port/protocol, e.g. 80/tcp.\n",
		(LONG)path, (LONG)name);
	bad++;
	continue;
      }
      *sep++ = '\0';
      /* Bound as we go, not just at the end: a long enough digit run would
       * otherwise overflow the accumulator and could wrap into the valid
       * range, so a nonsense port would be reported as fine. */
      for (q = val; *q; q++) {
	if (*q < '0' || *q > '9' || port > 65535) { port = -1; break; }
	port = port * 10 + (*q - '0');
      }
      if (val[0] == '\0' || port < 0 || port > 65535) {
	problem(0, "%s: '%s' has a port that is not 0-65535.\n",
		(LONG)path, (LONG)name);
	bad++;
	continue;
      }
      if (*sep == '\0') {
	problem(0, "%s: '%s' names no protocol after the port.\n",
		(LONG)path, (LONG)name);
	bad++;
	continue;
      }
    } else if (kind == 1) {
      char *q;
      /*
       * Numeric, and that is all. NOT bounded to 0-255, even though a real IP
       * protocol number is eight bits: the stock Debian protocols file that
       * ships on many machines lists mptcp as 262 (a Linux-internal marker),
       * our own parser accepts it, and Roadshow's validator bounds it only
       * below. Flagging it would put a warning on an unmodified stock file,
       * and a checker that cries wolf over something the stack is perfectly
       * happy with is one people stop reading.
       */
      for (q = val; *q >= '0' && *q <= '9'; q++)
	;
      if (val[0] == '\0' || *q != '\0') {
	problem(0, "%s: '%s' has a protocol number that is not a number.\n",
		(LONG)path, (LONG)name);
	bad++;
	continue;
      }
    } else {
      /* networks: dotted, and a partial form such as "127" is allowed -- the
       * stock Roadshow file's own example is written that way. */
      char *q;
      int digits = 0, part = 0;
      for (q = val; *q; q++) {
	if (*q == '.') {
	  if (digits == 0) break;
	  digits = 0; part = 0;
	  continue;
	}
	if (*q < '0' || *q > '9') break;
	part = part * 10 + (*q - '0');
	if (++digits > 3 || part > 255) break;
      }
      if (val[0] == '\0' || *q != '\0' || digits == 0) {
	problem(0, "%s: '%s' has a network address that is not numeric.\n",
		(LONG)path, (LONG)name);
	bad++;
	continue;
      }
    }
    n++;
  }
  Close(fh);

  /* Report the rejects too. bad was counted and then thrown away, so a file
   * in which EVERY line was malformed printed a cheerful "0 entr(y/ies)" and
   * nothing else -- the one case where the user most needs telling. */
  if (bad > 0) {
    /* say() only carries two arguments; this needs three. Gated the same way. */
    if (!quiet)
      Printf((STRPTR)"%s%ld entr(y/ies), %ld unusable\n",
             (LONG)label, (LONG)n, (LONG)bad);
  } else
    say("%s%ld entr(y/ies)\n", (LONG)label, (LONG)n);
}

static void check_internet_dbs(char *line)
{
  check_internet_db(line, "DEVS:Internet/services",
			  "DEVS:Internet/services         ", 0);
  check_internet_db(line, "DEVS:Internet/protocols",
			  "DEVS:Internet/protocols        ", 1);
  check_internet_db(line, "DEVS:Internet/networks",
			  "DEVS:Internet/networks         ", 2);
}

/* ------------------------------------------------------------------ */

/*
 * Saved tunables. Only the SHAPE is checked -- that each file holds a number --
 * because deciding whether a name is a real option means asking a running stack,
 * and this tool does not start one. AmiTCPControl lists the live options.
 */
static void check_saved_settings(char *line)
{
  BPTR lock, sub;
  struct FileInfoBlock *fib, *sfib;
  int groups = 0, files = 0;

  if ((lock = Lock((STRPTR)"ENVARC:AmiTCP_NG", ACCESS_READ)) == 0) {
    say("ENVARC:AmiTCP_NG               none saved\n", 0, 0);
    return;
  }
  fib  = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
  sfib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
  if (!fib || !sfib) {
    if (fib)  FreeDosObject(DOS_FIB, fib);
    if (sfib) FreeDosObject(DOS_FIB, sfib);
    UnLock(lock);
    problem(1, "out of memory reading ENVARC:AmiTCP_NG\n", 0, 0);
    return;
  }

  if (Examine(lock, fib)) {
    while (ExNext(lock, fib)) {
      char gpath[160];
      int j = 0, k;
      const char *pfx = "ENVARC:AmiTCP_NG/";

      if (fib->fib_DirEntryType <= 0)		/* settings live one level down */
        continue;
      groups++;
      while (pfx[j] && j < (int)sizeof(gpath) - 1) { gpath[j] = pfx[j]; j++; }
      for (k = 0; fib->fib_FileName[k] && j < (int)sizeof(gpath) - 1; k++)
        gpath[j++] = fib->fib_FileName[k];
      gpath[j] = '\0';

      if ((sub = Lock((STRPTR)gpath, ACCESS_READ)) == 0)
        continue;
      if (Examine(sub, sfib)) {
        while (ExNext(sub, sfib)) {
          char fpath[224];
          BPTR fh;
          int m = 0;

          if (sfib->fib_DirEntryType > 0) continue;
          for (k = 0; gpath[k] && m < (int)sizeof(fpath) - 2; k++) fpath[m++] = gpath[k];
          fpath[m++] = '/';
          for (k = 0; sfib->fib_FileName[k] && m < (int)sizeof(fpath) - 1; k++)
            fpath[m++] = sfib->fib_FileName[k];
          fpath[m] = '\0';
          files++;

          if ((fh = Open((STRPTR)fpath, MODE_OLDFILE)) != 0) {
            if (FGets(fh, (STRPTR)line, LINELEN)) {
              char *p = line;
              while (*p && *p != '\n' && *p != '\r') p++;
              *p = '\0';
              p = line;
              if (*p == '-') p++;
              if (!all_digits(p))
                problem(0, "%s holds '%s', which is not a number.\n",
                        (LONG)fpath, (LONG)line);
              else
                note("  %s = %s\n", (LONG)fpath, (LONG)line);
            } else {
              problem(0, "%s is empty.\n", (LONG)fpath, 0);
            }
            Close(fh);
          }
        }
      }
      UnLock(sub);
    }
  }
  FreeDosObject(DOS_FIB, sfib);
  FreeDosObject(DOS_FIB, fib);
  UnLock(lock);

  say("ENVARC:AmiTCP_NG               %ld saved setting(s)\n", (LONG)files, 0);
  (void)groups;
}

/* ------------------------------------------------------------------ */

int main(void)
{
  struct RDArgs *rda;
  LONG a[2] = { 0, 0 };
  char *line;
  int rc;

  rda = ReadArgs((STRPTR)"QUIET/S,VERBOSE/S", a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }
  quiet   = a[0] != 0;
  verbose = a[1] != 0;

  /* One heap line buffer, shared: 512 bytes per checker on the caller's stack
   * adds up, and a Shell process starts with 4K. */
  if ((line = (char *)AllocVec(LINELEN + 2, MEMF_PUBLIC)) == NULL) {
    Printf((STRPTR)PROG ": out of memory\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  say("Checking the AmiTCP_NG configuration...\n\n", 0, 0);

  check_stack_config(line);
  check_interfaces(line);
  check_hosts(line);
  check_internet_dbs(line);
  check_saved_settings(line);

  if (errors == 0 && warnings == 0) {
    say("\nNo problems found.\n", 0, 0);
    rc = RETURN_OK;
  } else {
    Printf((STRPTR)"\n%ld error(s), %ld warning(s).\n", (LONG)errors, (LONG)warnings);
    rc = errors ? RETURN_ERROR : RETURN_WARN;
  }

  FreeVec(line);
  FreeArgs(rda);
  return rc;
}
