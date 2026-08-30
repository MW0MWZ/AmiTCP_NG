/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 * AmiTCPControl -- display and change internal stack configuration options.
 *
 * Same shape as Roadshow's RoadshowControl, so scripts written for that port
 * unchanged: same template, same dotted option names, same ENVARC: layout.
 * Template SAVE/S,QUIET/S,GET/K,SET/K/F
 *
 * With no arguments it lists every option the running stack exposes. Options
 * the stack tunes for itself -- socket buffer sizes (from installed RAM and
 * link speed) and TCP timestamps (gated on the CPU) -- are shown with a
 * "(auto)" marker and cannot be set: the library refuses the write with EPERM.
 * They are worth SEEING, which is why they are listed at all.
 *
 * SAVE writes ENVARC:AmiTCP_NG/<group>/<name> (dots become slashes) and mirrors
 * it into ENV: for the current session, matching how Roadshow persists its own.
 */
#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

struct Library *SocketBase;
#include "ng_lvo.h"

#define PROG	"AmiTCPControl"
#define ENVDIR	"AmiTCP_NG"

/* ------------------------------------------------------------------ */

static void trim(STRPTR s)
{
  int i = 0, j;
  while (s[i] == ' ' || s[i] == '\t') i++;
  if (i) { j = 0; while ((s[j] = s[i + j]) != '\0') j++; }
  j = (int)strlen((char *)s);
  while (j > 0 && (s[j-1] == ' ' || s[j-1] == '\t' ||
		   s[j-1] == '\n' || s[j-1] == '\r')) s[--j] = '\0';
}

static int ci_eq(const char *a, const char *b)
{
  unsigned char ca, cb;
  for (;;) {
    ca = (unsigned char)*a++; cb = (unsigned char)*b++;
    if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    if (ca != cb) return 0;
    if (ca == 0)  return 1;
  }
}

static struct NGRoadshowDataNode *find_opt(struct List *l, const char *name)
{
  struct Node *n;
  for (n = l->lh_Head; n->ln_Succ; n = n->ln_Succ) {
    struct NGRoadshowDataNode *rn = (struct NGRoadshowDataNode *)n;
    if (ci_eq((char *)rn->rdn_Name, name))
      return rn;
  }
  return NULL;
}

static void show(struct NGRoadshowDataNode *rn)
{
  Printf((STRPTR)"%s = %ld%s\n", (LONG)rn->rdn_Name,
	 (LONG)(*(LONG *)rn->rdn_Data),
	 (LONG)((rn->rdn_Flags & NG_RDNF_ReadOnly) ? "  (auto)" : ""));
}

/*
 * Persist to ENVARC: (survives a reboot) and mirror into ENV: (this session),
 * exactly as Roadshow does for its own namespace. Dots in the option name
 * become directory separators, so tcp.iw -> AmiTCP_NG/tcp/iw.
 */
static int save_setting(const char *name, LONG value)
{
  TEXT var[256], path[300], val[24];
  int  i;
  BPTR f;

  if (strlen(name) > 200) return 0;
  strcpy((char *)var, ENVDIR "/");
  strcat((char *)var, name);
  for (i = 0; var[i]; i++)
    if (var[i] == '.') var[i] = '/';

  {					/* no SPrintf here; format by hand */
    TEXT tmp[16];
    /*
     * Accumulate on the NEGATIVE side. The obvious `if (v < 0) v = -v;` is
     * wrong for exactly one input -- LONG_MIN has no positive counterpart, so
     * negating it overflows back to itself and the loop below emits the digits
     * of a negative number. Going the other way has no such hole.
     */
    LONG v = (value < 0) ? value : -value;
    int  k = 0, m = 0, negv = (value < 0);

    do { tmp[k++] = (TEXT)('0' - (v % 10)); v /= 10; } while (v);
    if (negv) val[m++] = '-';
    while (k > 0) val[m++] = tmp[--k];
    val[m++] = '\n';
    val[m]   = '\0';
  }

  /* ENV: first -- takes effect for anything started later this session. */
  if (!SetVar((STRPTR)var, (STRPTR)val, -1, GVF_GLOBAL_ONLY))
    return 0;

  /* ENVARC: needs the directories to exist; CreateDir fails harmlessly if so. */
  strcpy((char *)path, "ENVARC:");
  strcat((char *)path, (char *)var);
  for (i = 7; path[i]; i++) {
    if (path[i] == '/') {
      BPTR d;
      path[i] = '\0';
      if ((d = CreateDir((STRPTR)path)) != 0) UnLock(d);
      path[i] = '/';
    }
  }
  if ((f = Open((STRPTR)path, MODE_NEWFILE)) == 0)
    return 0;
  {
    /*
     * Check the write. A full ENVARC: (a small boot volume is normal on real
     * hardware) would otherwise be reported to the user as a successful SAVE,
     * with a truncated value left on disk that only turns up much later as a
     * setting that did not survive the reboot -- or worse, as a value that
     * came back as something they never set.
     */
    LONG len = (LONG)strlen((char *)val);
    LONG wrote = Write(f, val, len);
    Close(f);
    return wrote == len;
  }
}

/* ------------------------------------------------------------------ */

int main(void)
{
  struct RDArgs *rda;
  LONG  a[4] = { 0, 0, 0, 0 };		/* SAVE/S, QUIET/S, GET/K, SET/K/F */
  int   save, quiet, rc = RETURN_OK;
  struct List *list = NULL;
  struct NGRoadshowDataNode *rn;
  LONG  have_api = 0;

  rda = ReadArgs((STRPTR)"SAVE/S,QUIET/S,GET/K,SET/K/F", a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }
  save  = a[0] != 0;
  quiet = a[1] != 0;

  /*
   * SAVE only means anything alongside SET -- it writes the value that SET just
   * applied. On its own, or with GET, it was accepted and silently did nothing,
   * so "AmiTCPControl SAVE" looked like it had persisted the current settings
   * and had not. Say so rather than letting the user find out at the next
   * reboot. Not fatal: the rest of the command still does what it says.
   */
  if (save && a[3] == 0) {
    if (!quiet)
      Printf((STRPTR)PROG ": SAVE only applies together with SET -- it stores the\n"
                     "  value SET has just applied. Nothing has been saved.\n");
    rc = RETURN_WARN;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4L);
  if (!SocketBase) {
    if (!quiet) Printf((STRPTR)PROG ": cannot open bsdsocket.library\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  /* Refuse politely against a stack that has no configuration API at all,
   * rather than calling a vector that may not be there. */
  {
    struct TagItem q[2];
    q[0].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_HAVE_ROADSHOWDATA_API); q[0].ti_Data = 0;
    q[1].ti_Tag = TAG_END;                                       q[1].ti_Data = 0;
    if (ng_sbtaglist(q) == 0) have_api = (LONG)q[0].ti_Data;
  }
  if (!have_api) {
    if (!quiet)
      Printf((STRPTR)PROG ": this bsdsocket.library has no configuration API\n");
    CloseLibrary(SocketBase); FreeArgs(rda);
    return RETURN_FAIL;
  }

  list = (struct List *)ng_obtain_rsd(a[3] ? NG_ORD_WriteAccess
					   : NG_ORD_ReadAccess);
  if (!list) {
    if (!quiet)
      Printf((STRPTR)PROG ": cannot read stack configuration, errno %ld\n",
	     ng_errno());
    CloseLibrary(SocketBase); FreeArgs(rda);
    return RETURN_FAIL;
  }

  if (a[3]) {				/* SET name=value  (or "name value") */
    TEXT  buf[256];
    STRPTR key = buf, value;
    LONG  number = 0;
    int   neg = 0;

    if (strlen((char *)a[3]) >= sizeof(buf)) {
      if (!quiet) Printf((STRPTR)PROG ": option too long\n");
      rc = RETURN_ERROR; goto out;
    }
    strcpy((char *)buf, (char *)a[3]);
    value = (STRPTR)strchr((char *)buf, '=');
    if (!value) value = (STRPTR)strchr((char *)buf, ' ');
    if (!value) {
      if (!quiet) Printf((STRPTR)PROG ": SET needs option=value\n");
      rc = RETURN_ERROR; goto out;
    }
    *value++ = '\0';
    trim(key); trim(value);

    if (*value == '-') { neg = 1; value++; }
    if (*value < '0' || *value > '9') {
      if (!quiet) Printf((STRPTR)PROG ": '%s' is not a number\n", (LONG)value);
      rc = RETURN_ERROR; goto out;
    }
    /*
     * Bounded accumulation. Left to itself this overflows silently on a long
     * digit string and stores whatever 32 bits fall out -- the user asked for
     * one number and got another, with no complaint. Refuse instead.
     */
    while (*value >= '0' && *value <= '9') {
      LONG digit = *value++ - '0';
      if (number > (2147483647L - digit) / 10) {
	if (!quiet) Printf((STRPTR)PROG ": value is too large\n");
	rc = RETURN_ERROR; goto out;
      }
      number = number * 10 + digit;
    }
    if (neg) number = -number;
    /*
     * Reject trailing junk rather than quietly using the leading digits.
     * SET is /F (takes the rest of the line), so "SET x=1 SAVE" puts SAVE
     * INSIDE the value and the SAVE switch is never seen -- silently doing
     * half of what was asked. Say so instead; SAVE has to come first.
     */
    while (*value == ' ' || *value == '\t') value++;
    if (*value != '\0') {
      if (!quiet)
	Printf((STRPTR)PROG ": trailing '%s' after the value. Note SET takes the\n"
	       "  rest of the line, so switches go BEFORE it: %s SAVE SET %s=...\n",
	       (LONG)value, (LONG)PROG, (LONG)key);
      rc = RETURN_ERROR; goto out;
    }

    rn = find_opt(list, (char *)key);
    if (!rn) {
      if (!quiet) Printf((STRPTR)"%s: Object not found\n", (LONG)key);
      rc = RETURN_WARN; goto out;
    }
    if (rn->rdn_Flags & NG_RDNF_ReadOnly) {
      if (!quiet)
	Printf((STRPTR)"%s: set automatically from this machine's RAM, link "
	       "speed and CPU -- not changeable\n", (LONG)key);
      rc = RETURN_WARN; goto out;
    }
    if (!ng_change_rsd(list, key, 4UL, &number)) {
      if (!quiet)
	Printf((STRPTR)"%s: could not be changed, errno %ld\n",
	       (LONG)key, ng_errno());
      rc = RETURN_ERROR; goto out;
    }
    if (!quiet) show(rn);
    /* Save under the table's own spelling (rdn_Name), not the user's: the
     * lookup is case-insensitive, so "TCP.IW" and "tcp.iw" are the same option
     * and must not become two different ENVARC: files. */
    if (save && !save_setting((char *)rn->rdn_Name, number)) {
      if (!quiet)
	Printf((STRPTR)PROG ": changed, but could NOT be saved to ENVARC:\n");
      rc = RETURN_WARN;
    }
  } else if (a[2]) {			/* GET name */
    rn = find_opt(list, (char *)a[2]);
    if (!rn) {
      if (!quiet) Printf((STRPTR)"%s: Object not found\n", a[2]);
      rc = RETURN_WARN;
    } else if (!quiet) {
      show(rn);
    }
  } else {				/* no arguments: list everything */
    struct Node *n;
    if (!quiet)
      for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
	show((struct NGRoadshowDataNode *)n);
  }

 out:
  ng_release_rsd(list);
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
