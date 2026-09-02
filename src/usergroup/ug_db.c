/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ug_db.c -- the user and group database behind usergroup.library.
 *
 * WHERE THE DATA COMES FROM. DEVS:Internet/users and DEVS:Internet/groups, in
 * the same format Roadshow uses, so a machine that already has those files
 * keeps working when this stack is installed over it. They are NOT Unix
 * colon-separated /etc/passwd files -- each line is AmigaDOS ReadArgs style:
 *
 *   users   NAME/A,PASSWORD/K,UID/A/N,GID/A/N,GECOS,DIR,SHELL
 *           e.g.  NAME=root UID=0 GID=0
 *   groups  NAME/A,ID/A/N,USERS/M
 *           e.g.  NAME=wheel ID=0 USERS=root
 *
 * Those two template strings, the DEVS:Internet directory, the SYS: default
 * home and the root/nobody/wheel/nogroup fallbacks below are the conventional
 * AmigaOS values, and match what an existing usergroup.library on the platform
 * uses. Checked against that rather than guessed.
 *
 * Lines beginning with '#' are comments. Both files are optional: AmigaOS has
 * no accounts, so the whole notion of a user here exists to let ported Unix
 * software get a plausible answer rather than fail. When a file is missing we
 * therefore fall back to the same entries Roadshow ships (root and nobody)
 * instead of returning nothing, because "no such user" makes callers fail in
 * ways that are far harder to diagnose than a sensible default.
 *
 * See ug_db.h for the lifetime rule -- load once, never rebuild while callers
 * hold pointers into it.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "ug_db.h"

#define UG_USERS	"DEVS:Internet/users"
#define UG_GROUPS	"DEVS:Internet/groups"
#define UG_LINEMAX	512		/* the format's documented maximum */

extern struct ExecBase  *SysBase;
extern struct DosLibrary *DOSBase;

static struct ug_user  *ug_users;
static struct ug_group *ug_groups;
static volatile LONG    ug_loaded;
static struct SignalSemaphore ug_db_sem;

/* ------------------------------------------------------------------ */

static char *ug_strdup(const char *s)
{
  ULONG n = 0;
  char *p;

  if (s == NULL) return NULL;
  while (s[n]) n++;
  if ((p = (char *)AllocVec(n + 1, MEMF_PUBLIC | MEMF_CLEAR)) == NULL)
    return NULL;
  for (n = 0; s[n]; n++) p[n] = s[n];
  p[n] = '\0';
  return p;
}

/*
 * Free ONE record. Split out from the list teardown because the loaders need it
 * too: a record whose strings did not all allocate must be thrown away whole
 * rather than published half-built -- see ug_user_complete() below.
 */
static void ug_free_one_user(struct ug_user *u)
{
  if (u == NULL) return;
  if (u->u_pw.pw_name)   FreeVec(u->u_pw.pw_name);
  if (u->u_pw.pw_passwd) FreeVec(u->u_pw.pw_passwd);
  if (u->u_pw.pw_gecos)  FreeVec(u->u_pw.pw_gecos);
  if (u->u_pw.pw_dir)    FreeVec(u->u_pw.pw_dir);
  if (u->u_pw.pw_shell)  FreeVec(u->u_pw.pw_shell);
  FreeVec(u);
}

static void ug_free_one_group(struct ug_group *g)
{
  int i;

  if (g == NULL) return;
  if (g->g_gr.gr_name)   FreeVec(g->g_gr.gr_name);
  if (g->g_gr.gr_passwd) FreeVec(g->g_gr.gr_passwd);
  for (i = 0; i < g->g_nmem; i++)
    if (g->g_memv[i]) FreeVec(g->g_memv[i]);
  if (g->g_gr.gr_mem) FreeVec(g->g_gr.gr_mem);
  FreeVec(g);
}

/*
 * Is this record safe to hand to a caller? EVERY string a caller may
 * dereference must have allocated, and gr_mem must exist and be terminated.
 *
 * This matters because these records are published directly: getgrnam() hands
 * back &g_gr, and the standard way to use it is `for (p = gr->gr_mem; *p; p++)`.
 * A gr_mem that failed to allocate is NULL, and with no MMU that walk is not a
 * catchable error, it is a dead machine. Out of memory while parsing is not
 * hypothetical here -- this stack still targets 512K.
 */
static int ug_user_complete(const struct ug_user *u)
{
  return (u->u_pw.pw_name && u->u_pw.pw_passwd && u->u_pw.pw_gecos &&
	  u->u_pw.pw_dir  && u->u_pw.pw_shell);
}

static int ug_group_complete(const struct ug_group *g)
{
  int i;

  if (g->g_gr.gr_name == NULL || g->g_gr.gr_passwd == NULL ||
      g->g_gr.gr_mem == NULL)
    return 0;
  for (i = 0; i < g->g_nmem; i++)
    if (g->g_memv[i] == NULL)
      return 0;
  return 1;
}

static void ug_free_users(void)
{
  while (ug_users) {
    struct ug_user *n = ug_users->u_next;
    ug_free_one_user(ug_users);
    ug_users = n;
  }
}

static void ug_free_groups(void)
{
  while (ug_groups) {
    struct ug_group *n = ug_groups->g_next;
    ug_free_one_group(ug_groups);
    ug_groups = n;
  }
}

/* Append to the tail so the file's order is preserved -- getpwent() walks it. */
static void ug_add_user(struct ug_user *u)
{
  struct ug_user **pp = &ug_users;
  while (*pp) pp = &(*pp)->u_next;
  *pp = u;
}

static void ug_add_group(struct ug_group *g)
{
  struct ug_group **pp = &ug_groups;
  while (*pp) pp = &(*pp)->g_next;
  *pp = g;
}

/* ------------------------------------------------------------------ */

/*
 * Parse one line with ReadArgs(). Using DOS's own parser (rather than hand
 * splitting) is what makes NAME=root and "NAME root" and quoting all behave
 * exactly as they do everywhere else on the machine -- and it is how the file
 * documents itself ("read and parsed according to the following template").
 *
 * Returns the RDArgs handle on success, NULL on failure. The parsed strings in
 * args[] point INTO that handle's storage, so the caller must copy what it wants
 * and only then call parse_done() -- freeing here would hand back dangling
 * pointers. `buf` must stay alive until parse_done() too, hence the caller-owned
 * buffer.
 */
static struct RDArgs *parse_line(UBYTE *buf, int buflen, const char *line,
				 const char *template, LONG *args, int nargs)
{
  struct RDArgs *rda;
  int i, n = 0;

  while (line[n] && n < buflen - 2) { buf[n] = (UBYTE)line[n]; n++; }
  while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) n--;
  buf[n++] = '\n';			/* ReadArgs wants a newline-terminated line */
  buf[n]   = '\0';

  for (i = 0; i < nargs; i++) args[i] = 0;

  if ((rda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL)) == NULL)
    return NULL;
  rda->RDA_Source.CS_Buffer = buf;
  rda->RDA_Source.CS_Length = n;
  rda->RDA_Source.CS_CurChr = 0;
  rda->RDA_Flags |= RDAF_NOPROMPT;

  if (ReadArgs((STRPTR)template, args, rda) == NULL) {
    FreeDosObject(DOS_RDARGS, rda);
    return NULL;
  }
  return rda;
}

static void parse_done(struct RDArgs *rda)
{
  FreeArgs(rda);
  FreeDosObject(DOS_RDARGS, rda);
}

/*
 * FGets(), plus the thing it does not tell you: a line longer than the buffer
 * comes back truncated, and DOS then hands you the REST as though it were the
 * next line. Parsed blindly, the back half of one long line can form a
 * plausible second record. So detect the truncation, drain to the real end of
 * line, and tell the caller to drop the whole thing.
 *
 * A last line with no trailing newline is legitimate and is NOT truncation:
 * the drain reads nothing at EOF, so *toolong stays clear.
 */
static UBYTE *ug_getline(BPTR f, UBYTE *buf, int max, int *toolong)
{
  int n;

  *toolong = 0;
  if (FGets(f, buf, max) == NULL)
    return NULL;

  for (n = 0; n < max && buf[n]; n++)
    ;
  if (n > 0 && buf[n - 1] != '\n') {
    LONG c;
    while ((c = FGetC(f)) != -1 && c != '\n')
      *toolong = 1;
  }
  return buf;
}

static int is_blank_or_comment(const char *s)
{
  while (*s == ' ' || *s == '\t') s++;
  return (*s == '\0' || *s == '#' || *s == '\n' || *s == '\r');
}

/* ------------------------------------------------------------------ */

/*
 * The two line buffers are ~512 bytes each and would be a kilobyte of stack in
 * a library vector running on whatever stack the caller happened to have --
 * a shell process gets 4K by default. Heap instead.
 */
struct ug_lines {
  UBYTE line[UG_LINEMAX + 2];		/* the raw file line */
  UBYTE parse[UG_LINEMAX + 2];		/* the copy ReadArgs consumes */
};

static void load_users(struct ug_lines *lb)
{
  BPTR f;
  int toolong;

  if ((f = Open((STRPTR)UG_USERS, MODE_OLDFILE)) == 0)
    return;

  while (ug_getline(f, lb->line, UG_LINEMAX, &toolong)) {
    /* NAME/A,PASSWORD/K,UID/A/N,GID/A/N,GECOS,DIR,SHELL */
    LONG a[7];
    struct ug_user *u;
    struct RDArgs *rda;

    if (toolong) continue;		/* over 512 chars: not a record we trust */
    if (is_blank_or_comment((char *)lb->line)) continue;
    rda = parse_line(lb->parse, sizeof(lb->parse), (char *)lb->line,
		     "NAME/A,PASSWORD/K,UID/A/N,GID/A/N,GECOS,DIR,SHELL", a, 7);
    if (rda == NULL) continue;		/* malformed: skip it, keep going */
    if (a[0] == 0 || a[2] == 0 || a[3] == 0) { parse_done(rda); continue; }

    if ((u = (struct ug_user *)AllocVec(sizeof(*u), MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
      parse_done(rda);
      break;
    }
    u->u_pw.pw_name   = ug_strdup((char *)a[0]);
    u->u_pw.pw_passwd = ug_strdup(a[1] ? (char *)a[1] : "");
    u->u_pw.pw_uid    = (uid_t)(*(LONG *)a[2]);
    u->u_pw.pw_gid    = (gid_t)(*(LONG *)a[3]);
    u->u_pw.pw_gecos  = ug_strdup(a[4] ? (char *)a[4] : "");
    u->u_pw.pw_dir    = ug_strdup(a[5] ? (char *)a[5] : "SYS:");
    u->u_pw.pw_shell  = ug_strdup(a[6] ? (char *)a[6] : "");
    parse_done(rda);

    if (ug_user_complete(u))
      ug_add_user(u);
    else
      ug_free_one_user(u);	/* out of memory: drop it, never publish a partial */
  }
  Close(f);
}

static void load_groups(struct ug_lines *lb)
{
  BPTR f;
  int toolong;

  if ((f = Open((STRPTR)UG_GROUPS, MODE_OLDFILE)) == 0)
    return;

  while (ug_getline(f, lb->line, UG_LINEMAX, &toolong)) {
    LONG a[3];				/* NAME/A,ID/A/N,USERS/M */
    struct ug_group *g;
    struct RDArgs *rda;
    STRPTR *mem;
    int i;

    if (toolong) continue;		/* over 512 chars: not a record we trust */
    if (is_blank_or_comment((char *)lb->line)) continue;
    rda = parse_line(lb->parse, sizeof(lb->parse), (char *)lb->line,
		     "NAME/A,ID/A/N,USERS/M", a, 3);
    if (rda == NULL) continue;
    if (a[0] == 0 || a[1] == 0) { parse_done(rda); continue; }

    if ((g = (struct ug_group *)AllocVec(sizeof(*g), MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
      parse_done(rda);
      break;
    }
    g->g_gr.gr_name   = ug_strdup((char *)a[0]);
    g->g_gr.gr_passwd = ug_strdup("");
    g->g_gr.gr_gid    = (gid_t)(*(LONG *)a[1]);

    /* USERS/M yields a NULL-terminated array of STRPTR. */
    mem = (STRPTR *)a[2];
    if (mem) while (mem[g->g_nmem] && g->g_nmem < UG_MAXMEM) g->g_nmem++;
    g->g_gr.gr_mem = (char **)AllocVec((g->g_nmem + 1) * sizeof(char *),
				       MEMF_PUBLIC | MEMF_CLEAR);
    if (g->g_gr.gr_mem) {
      for (i = 0; i < g->g_nmem; i++) {
	g->g_memv[i]      = ug_strdup((char *)mem[i]);
	g->g_gr.gr_mem[i] = g->g_memv[i];
      }
      g->g_gr.gr_mem[g->g_nmem] = NULL;
    }
    parse_done(rda);

    if (ug_group_complete(g))
      ug_add_group(g);
    else
      ug_free_one_group(g);	/* out of memory: drop it, never publish a partial */
  }
  Close(f);
}

/*
 * The built-in fallback, used only when a file is absent or unreadable. These
 * are the same entries Roadshow ships, so software behaves the same on a
 * machine that never had either file.
 */
static void load_defaults_users(void)
{
  static const struct { const char *n; long uid, gid; } d[] = {
    { "root",   0,     0     },
    { "nobody", 65534, 65534 },
  };
  int i;

  for (i = 0; i < 2; i++) {
    struct ug_user *u =
      (struct ug_user *)AllocVec(sizeof(*u), MEMF_PUBLIC | MEMF_CLEAR);
    if (!u) return;
    u->u_pw.pw_name   = ug_strdup(d[i].n);
    u->u_pw.pw_passwd = ug_strdup("");
    u->u_pw.pw_uid    = (uid_t)d[i].uid;
    u->u_pw.pw_gid    = (gid_t)d[i].gid;
    u->u_pw.pw_gecos  = ug_strdup("");
    u->u_pw.pw_dir    = ug_strdup("SYS:");
    u->u_pw.pw_shell  = ug_strdup("");
    if (ug_user_complete(u))
      ug_add_user(u);
    else
      ug_free_one_user(u);
  }
}

static void load_defaults_groups(void)
{
  static const struct { const char *n; long gid; const char *m; } d[] = {
    { "wheel",   0,     "root"   },
    { "nogroup", 65534, "nobody" },
  };
  int i;

  for (i = 0; i < 2; i++) {
    struct ug_group *g =
      (struct ug_group *)AllocVec(sizeof(*g), MEMF_PUBLIC | MEMF_CLEAR);
    if (!g) return;
    g->g_gr.gr_name   = ug_strdup(d[i].n);
    g->g_gr.gr_passwd = ug_strdup("");
    g->g_gr.gr_gid    = (gid_t)d[i].gid;
    g->g_nmem         = 1;
    g->g_memv[0]      = ug_strdup(d[i].m);
    g->g_gr.gr_mem    = (char **)AllocVec(2 * sizeof(char *),
					  MEMF_PUBLIC | MEMF_CLEAR);
    if (g->g_gr.gr_mem) {
      g->g_gr.gr_mem[0] = g->g_memv[0];
      g->g_gr.gr_mem[1] = NULL;
    }
    if (ug_group_complete(g))
      ug_add_group(g);
    else
      ug_free_one_group(g);
  }
}

/* ------------------------------------------------------------------ */

VOID ug_db_init(VOID)
{
  InitSemaphore(&ug_db_sem);
}

/*
 * Load on first use. Two callers can race here, so the whole load runs under
 * the semaphore and the flag is checked again inside it.
 *
 * ug_loaded is set LAST, after the lists are complete. Setting it on entry
 * would be the obvious way to mark "in progress", and would be wrong: the fast
 * path below reads the flag without the lock, so a second task would sail past
 * it and walk a half-built list.
 */
static void ug_db_load(void)
{
  struct ug_lines *lb;
  struct Task *me;

  if (ug_loaded) return;			/* fast path, no lock */

  ObtainSemaphore(&ug_db_sem);
  if (!ug_loaded) {
    /*
     * Reading the files needs DOS, and DOS needs a Process -- Open() on a bare
     * Task walks a pr_MsgPort that is not there. A plain Task asking for the
     * account list is odd but legal, so give it the built-in defaults rather
     * than taking the machine down.
     */
    me = SysBase->ThisTask;
    if (DOSBase != NULL && me != NULL && me->tc_Node.ln_Type == NT_PROCESS &&
	(lb = (struct ug_lines *)AllocVec(sizeof(*lb), MEMF_PUBLIC)) != NULL) {
      /*
       * Requesters off while we touch DEVS:. A missing assign must make the
       * open FAIL (and us fall back to the built-in entries), never put up
       * "Please insert volume DEVS:" and wait -- we are running inside somebody
       * else's program. See ug_noreq() in ug_funcs.c for what this cost.
       */
      struct Process *p = (struct Process *)me;
      APTR win = p->pr_WindowPtr;

      p->pr_WindowPtr = (APTR)-1;
      load_users(lb);
      load_groups(lb);
      p->pr_WindowPtr = win;
      FreeVec(lb);
    }

    if (ug_users  == NULL) load_defaults_users();
    if (ug_groups == NULL) load_defaults_groups();

    ug_loaded = 1;
  }
  ReleaseSemaphore(&ug_db_sem);
}

/*
 * Expunge only. See ug_db.h: callers hold pointers into these records, so this
 * is safe only when there are none left, which the library's open count
 * guarantees at expunge time.
 */
VOID ug_db_flush(VOID)
{
  ObtainSemaphore(&ug_db_sem);
  ug_free_users();
  ug_free_groups();
  ug_loaded = 0;
  ReleaseSemaphore(&ug_db_sem);
}

struct ug_user  *ug_db_users(VOID)  { ug_db_load(); return ug_users;  }
struct ug_group *ug_db_groups(VOID) { ug_db_load(); return ug_groups; }
