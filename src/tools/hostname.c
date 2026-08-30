/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * hostname -- print this machine's network name.
 *
 * READ ONLY, deliberately. On Unix `hostname foo` also sets the name; here the
 * name belongs to the configuration (AmiTCP:db/AmiTCP.config, or whatever DHCP
 * supplied at bring-up), so a command that changed it in the running stack would
 * report one thing and the config another, and the change would evaporate on the
 * next restart. If you want a different name, change it where it lives.
 *
 * OPTIONS ARE THE ONES YOUR FINGERS ALREADY KNOW. -f, -s and -d mean what they
 * mean on BSD and Linux; there is nothing to be gained from inventing a private
 * spelling for a command people have been typing for thirty years. Following
 * netstat in this same directory, a leading '-' or '/' is optional and the long
 * names work too, so -f, /f, f and FQDN are all the same switch:
 *
 *   hostname             the name as the stack holds it        a3000.lan
 *   hostname -f          the same, but fails if not qualified  (--fqdn, FQDN)
 *   hostname -s          up to the first dot                   a3000  (--short)
 *   hostname -d          after the first dot                   lan    (--domain)
 *   hostname -?          usage                                 (-h, --help, ?)
 *
 * TWO SEPARATE QUESTIONS, ASKED SEPARATELY. gethostname() returns the name the
 * stack holds; GetDefaultDomainName() returns the domain it holds (from DHCP's
 * domain-name option, or `domain` in the config). This command asks BOTH and never
 * guesses one from the other -- an earlier version derived the domain by hunting
 * for a dot in the host name, which reported nothing whenever the stack had a
 * domain but had not qualified the name. It was blind in precisely the case it
 * exists to diagnose, and an empty -d got read as "no domain arrived" when it only
 * ever meant "there is no dot in this string".
 *
 * ONE NORMALISATION, APPLIED TO EVERY FORM INCLUDING THE BARE ONE: a single
 * trailing dot is trimmed before anything reads the name. "a3000." is the DNS
 * spelling for an absolute name and carries nothing a person or a script wants
 * kept, and leaving it satisfied neither test below -- the name looked qualified
 * to the dot scan and unqualified to -f, which then joined the domain onto it and
 * printed "a3000..lan". So the bare form is normalised too, not byte-for-byte what
 * gethostname() returned. Noted here so nobody has to rediscover it by testing.
 *
 * -f and -s are the two worth asking separately: "is my name qualified?" is
 * exactly the thing that is hard to answer otherwise, and it is why this command
 * exists -- gethostname() returning a bare name when the network offered a domain
 * is a real fault, and until now nothing on the machine would show it to you.
 *
 * Exit codes: RETURN_OK when the requested part exists, RETURN_WARN when it does
 * not (no domain at all, or -f asked for and THE STACK did not hand back a
 * qualified name -- even though -f still prints the joined form, so the code is
 * what distinguishes "the stack qualified it" from "this command joined it"),
 * and RETURN_FAIL if the stack cannot be reached at all. A script can therefore
 * test `hostname -d` without parsing anything.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

/* The stack's own limit, TAKEN FROM THE STACK rather than written out again.
 * This said 256 -- correct today, and exactly the kind of independent copy that
 * put the host-name length out of step in the first place (the config checker
 * said 255 while the stack said 64, so the checker passed names the stack then
 * truncated in silence). One definition, one header. */
#include <ng_hostname.h>
#define NAMELEN (NG_MAXHOSTNAME + 1)

struct Library *SocketBase;

/*
 * gethostname (LVO -282, a0 = buffer, d0 = length).
 *
 * "+r" on every register the call can touch, not just the ones we read back --
 * without it gcc is entitled to assume a0 still holds our buffer across the jsr
 * and to keep using it, which is exactly the bug that broke DHCP in this project
 * once already.
 */
static long v_gethostname(void *buf, long len)
{
  register long   _d0 __asm("d0") = len;
  register void  *_a0 __asm("a0") = buf;
  register struct Library *_a6 __asm("a6") = SocketBase;
  __asm__ __volatile__("jsr a6@(-282)"
		       : "+r"(_d0), "+r"(_a0)
		       : "r"(_a6)
		       : "d1", "a1", "memory");
  return _d0;
}

/*
 * GetDefaultDomainName (LVO -702, a0 = buffer, d0 = size, BOOL back in d0).
 *
 * LVO verified three ways before use, because getting this wrong writes through a
 * wild vector: the comment beside _GetDefaultDomainName in api/amiga_libtables.c,
 * a recount of the table with C comments properly stripped, and the fact that the
 * same recount puts _gethostname at -282 -- the value this command is already
 * known to work with on real hardware.
 */
static int v_getdefaultdomainname(void *buf, long len)
{
  register long   _d0 __asm("d0") = len;
  register void  *_a0 __asm("a0") = buf;
  register struct Library *_a6 __asm("a6") = SocketBase;
  __asm__ __volatile__("jsr a6@(-702)"
		       : "+r"(_d0), "+r"(_a0)
		       : "r"(_a6)
		       : "d1", "a1", "memory");
  return (int)_d0;
}

/* Case-insensitive compare of a whole word; opt has already had any -/ stripped. */
static int is_opt(const char *opt, const char *shortname, const char *longname)
{
  const char *w;
  int i;

  for (w = shortname; ; w = longname) {
    for (i = 0; opt[i] && w[i]; i++) {
      char a = opt[i], b = w[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (a != b)
	break;
    }
    if (opt[i] == '\0' && w[i] == '\0')
      return 1;
    if (w == longname)
      return 0;
  }
}

static void usage(void)
{
  Printf((STRPTR)"hostname -- print this machine's network name\n"
		 "  hostname        the name as the stack holds it\n"
		 "  hostname -f     fully qualified (--fqdn); warns if it is not\n"
		 "  hostname -s     short name, up to the first dot (--short)\n"
		 "  hostname -d     the domain the stack holds (--domain)\n"
		 "Read only: the name comes from DHCP or the config, so it is set there.\n");
}

int main(int argc, char **argv)
{
  UBYTE  name[NAMELEN];
  UBYTE  domain[NAMELEN];
  const char *dot = NULL;
  int    want_f = 0, want_s = 0, want_d = 0;
  int    have_domain = 0;
  int    i, rc = RETURN_OK;

  for (i = 1; i < argc; i++) {
    char *a = argv[i];

    /* Following netstat in this directory: accept -f, /f and f alike. Long
     * options may arrive as --fqdn, so strip a second leading dash too. */
    if (a[0] == '-' || a[0] == '/') a++;
    if (a[0] == '-') a++;

    if (a[0] == '?' || is_opt(a, "h", "help")) { usage(); return RETURN_OK; }
    else if (is_opt(a, "f", "fqdn"))   want_f = 1;
    else if (is_opt(a, "s", "short"))  want_s = 1;
    else if (is_opt(a, "d", "domain")) want_d = 1;
    else {
      Printf((STRPTR)"hostname: unknown option '%s'\n", (LONG)argv[i]);
      usage();
      return RETURN_FAIL;
    }
  }

  /* Mutually exclusive: asking for two different parts of one string is a
   * mistake worth pointing out rather than silently answering one of them. */
  if (want_f + want_s + want_d > 1) {
    Printf((STRPTR)"hostname: choose at most one of -f, -s, -d\n");
    return RETURN_FAIL;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 3L);
  if (SocketBase == NULL) {
    Printf((STRPTR)"hostname: no bsdsocket.library -- is the network running?\n");
    return RETURN_FAIL;
  }

  name[0] = '\0';
  if (v_gethostname(name, (long)sizeof(name) - 1) != 0 || name[0] == '\0') {
    Printf((STRPTR)"hostname: the stack has no host name set\n");
    CloseLibrary(SocketBase);
    return RETURN_FAIL;
  }
  name[sizeof(name) - 1] = '\0';	/* belt and braces: never trust the length back */

  /*
   * Drop ONE trailing dot before anything looks at the string. "a3000." is the DNS
   * spelling for an absolute name, and without this it satisfies neither test: the
   * dot scan below finds a dot whose dot[1] is '\0', so -f decides the name is not
   * qualified and then joins the domain onto it anyway -- printing "a3000..lan".
   * Trimming first means every branch sees a clean name. Guarded so a name that is
   * nothing but a dot cannot be emptied out from under the checks already made.
   */
  for (i = 0; name[i]; i++)
    ;
  if (i > 1 && name[i - 1] == '.')
    name[i - 1] = '\0';

  for (i = 0; name[i]; i++)
    if (name[i] == '.') { dot = (const char *)&name[i]; break; }

  /*
   * ASK the stack for the domain; do NOT infer it from the name.
   *
   * This command used to derive -d and -f by looking for a dot in gethostname()'s
   * answer, which makes it blind in exactly the case it exists to diagnose: if the
   * stack holds a domain but never qualified the host name, the domain is real and
   * this reported nothing. That is not a cosmetic difference -- an empty -d was
   * read as "no domain arrived" when it only ever meant "the name has no dot in it".
   */
  domain[0] = '\0';
  if (v_getdefaultdomainname(domain, (long)sizeof(domain) - 1)) {
    domain[sizeof(domain) - 1] = '\0';
    if (domain[0])
      have_domain = 1;
  }

  if (want_d) {
    /* The registered domain first -- that is the authoritative answer. Fall back
     * to the name's own suffix only if the stack reports no domain at all, so a
     * qualified name still yields something sensible. */
    if (have_domain)
      Printf((STRPTR)"%s\n", (LONG)domain);
    else if (dot && dot[1])
      Printf((STRPTR)"%s\n", (LONG)(dot + 1));
    else
      rc = RETURN_WARN;			/* no domain: print nothing, say so in rc */
  } else if (want_s) {
    if (dot)
      name[dot - (const char *)name] = '\0';
    Printf((STRPTR)"%s\n", (LONG)name);
  } else if (want_f) {
    /*
     * Print the most useful answer, but keep the exit code HONEST: RETURN_WARN
     * means "the stack itself did not hand back a qualified name". So a script --
     * or a person testing this exact defect -- can still tell a name the stack
     * qualified from one this command joined together for display.
     */
    if (dot && dot[1]) {
      Printf((STRPTR)"%s\n", (LONG)name);
    } else if (have_domain) {
      Printf((STRPTR)"%s.%s\n", (LONG)name, (LONG)domain);
      rc = RETURN_WARN;
    } else {
      Printf((STRPTR)"%s\n", (LONG)name);
      rc = RETURN_WARN;			/* asked for qualified, it is not */
    }
  } else {				/* bare: whatever the stack holds */
    Printf((STRPTR)"%s\n", (LONG)name);
  }

  CloseLibrary(SocketBase);
  return rc;
}
