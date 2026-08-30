/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ug_funcs.c -- the 39 usergroup.library vectors.
 *
 * WHAT "CREDENTIALS" MEAN ON A MACHINE WITH NO ACCOUNTS. AmigaOS has no login,
 * no privilege separation and no file ownership, so none of this can be
 * enforced -- a program that wants to be root simply is one. The point of the
 * library is not to restrict anything; it is to give ported Unix software a
 * coherent set of answers, so that `whoami` agrees with the FTP daemon it
 * launched and a getpwnam() lookup returns a home directory that exists. The
 * permission checks below (EPERM when the effective uid is not 0) are here
 * because callers test for them and behave differently when they fail, not
 * because they defend anything.
 *
 * WHERE THE EFFECTIVE GID LIVES. struct UserGroupCredentials has cr_ruid,
 * cr_euid and cr_rgid but no cr_egid -- that is not an omission, it is the
 * 4.3BSD layout: the effective group id is cr_groups[0], and setgroups()
 * therefore changes it. getegid() reads cr_groups[0] accordingly.
 *
 * LOCK ORDER. ug_cred_sem before ug_db_sem, never the other way round --
 * ug_cred_ensure() holds the credentials lock while it looks a user up in the
 * database, and nothing in the database path ever wants the credentials.
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <sys/cdefs.h>
#include <sys/errno.h>
#include <api/amiga_raf.h>

/* ug_base.h -> libraries/usergroup.h pulls in pwd.h, grp.h and utmp.h in the
 * order they need each other; including any of them ahead of it does not. */
#include "ug_base.h"

extern struct ExecBase	  *SysBase;
extern struct DosLibrary  *DOSBase;

/* From api/amiga_errlists.c -- the same tables the stack hands out. */
extern const char * const __sys_errlist[];
extern const int	  __sys_nerr;
extern const char * const io_errlist[];
extern const short	  io_nerr;

struct UserGroupCredentials ug_cred;
struct SignalSemaphore	    ug_cred_sem;
static volatile LONG	    ug_cred_ready;

#define UG_DEFAULT_UMASK	022

/* ------------------------------------------------------------------ */
/* Small self-contained string helpers. This library links -nostartfiles;
 * keeping these local means no argument about which runtime supplies what.  */

static int ug_streq(const char *a, const char *b)
{
  if (a == NULL || b == NULL) return 0;
  while (*a && *a == *b) { a++; b++; }
  return (*a == '\0' && *b == '\0');
}

/* Copies at most size-1 characters and always terminates. */
static void ug_strlcpy(char *dst, const char *src, int size)
{
  int i = 0;

  if (size <= 0) return;
  if (src != NULL)
    while (i < size - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

/* ------------------------------------------------------------------ */

/*
 * Record an error. It goes into the base for ug_GetErr(), and is mirrored into
 * the caller's errno only when the CALLER IS THE OWNER: ug_ErrnoPtr is an
 * address in the owning task's storage, and writing it on behalf of some other
 * task that happened to call in would corrupt a variable belonging to a task
 * that is not even in this code. That is the hazard the AmiTCP documentation
 * describes as "non-owning tasks cannot recover error codes".
 */
VOID ug_seterr(struct UserGroupBase *base, LONG err)
{
  base->ug_Err = err;

  if (base->ug_ErrnoPtr != NULL && base->ug_Owner == SysBase->ThisTask) {
    switch (base->ug_ErrnoSize) {
    case 1: *(BYTE  *)base->ug_ErrnoPtr = (BYTE)err;  break;
    case 2: *(WORD  *)base->ug_ErrnoPtr = (WORD)err;  break;
    case 4: *(LONG  *)base->ug_ErrnoPtr = err;	      break;
    default: break;
    }
  }
}

/* ------------------------------------------------------------------ */

/*
 * Suppress DOS requesters for the duration of a DOS call, and put the caller's
 * setting back afterwards.
 *
 * This is not tidiness. getuid() reads an environment variable, which sends DOS
 * to ENV:; if that assign is missing, DOS puts up "Please insert volume ENV:"
 * and WAITS. A library called from someone else's program must never do that --
 * it turns a question with an obvious default answer into a hung machine. It
 * cost an emulator run here to find, on a boot script that happened not to
 * assign ENV:, and the same applies to DEVS: for the database files.
 *
 * pr_WindowPtr = -1 means "fail the call instead of asking". Only meaningful on
 * a Process; every caller of this already established that.
 */
static APTR ug_noreq(void)
{
  struct Process *p = (struct Process *)SysBase->ThisTask;
  APTR old = p->pr_WindowPtr;

  p->pr_WindowPtr = (APTR)-1;
  return old;
}

static void ug_reqback(APTR old)
{
  ((struct Process *)SysBase->ThisTask)->pr_WindowPtr = old;
}

VOID ug_cred_init(VOID)
{
  InitSemaphore(&ug_cred_sem);

  /*
   * Start as root. On a machine with no accounts that is the honest answer --
   * every program really can do everything -- and it is what stops a caller
   * concluding it has insufficient privilege for something it is about to be
   * allowed to do anyway. ug_cred_ensure() refines it from the environment on
   * first use; it cannot run here because reading a variable needs DOS and a
   * Process, and library init has neither guaranteed.
   */
  ug_cred.cr_ruid    = 0;
  ug_cred.cr_euid    = 0;
  ug_cred.cr_rgid    = 0;
  ug_cred.cr_ngroups = 1;
  ug_cred.cr_groups[0] = 0;
  ug_cred.cr_umask   = UG_DEFAULT_UMASK;
  ug_cred.cr_session = NULL;
  ug_strlcpy(ug_cred.cr_login, "root", MAXLOGNAME);
}

/*
 * Adopt the machine's configured user, once, on first use of any credentials
 * vector. Roadshow's usergroup.library reads USER and USERNAME for this and so
 * do we, so `whoami` gives the same answer on both.
 *
 * Deliberately does NOT mark itself done when called from a bare Task: GetVar()
 * needs a Process, and a plain Task asking for getuid() first would otherwise
 * lock the machine into the root default for good.
 */
static void ug_cred_ensure(void)
{
  char name[MAXLOGNAME];
  struct Task *me;
  struct ug_user *u;

  if (ug_cred_ready) return;

  me = SysBase->ThisTask;
  if (DOSBase == NULL || me == NULL || me->tc_Node.ln_Type != NT_PROCESS)
    return;

  ObtainSemaphore(&ug_cred_sem);
  if (!ug_cred_ready) {
    APTR win = ug_noreq();

    name[0] = '\0';
    if (GetVar((STRPTR)"USER", (STRPTR)name, sizeof(name), GVF_GLOBAL_ONLY) <= 0)
      GetVar((STRPTR)"USERNAME", (STRPTR)name, sizeof(name), GVF_GLOBAL_ONLY);
    ug_reqback(win);

    if (name[0] != '\0') {
      for (u = ug_db_users(); u != NULL; u = u->u_next) {
	if (ug_streq(u->u_pw.pw_name, name)) {
	  ug_cred.cr_ruid	= u->u_pw.pw_uid;
	  ug_cred.cr_euid	= u->u_pw.pw_uid;
	  ug_cred.cr_rgid	= u->u_pw.pw_gid;
	  ug_cred.cr_ngroups	= 1;
	  ug_cred.cr_groups[0]	= u->u_pw.pw_gid;
	  ug_strlcpy(ug_cred.cr_login, u->u_pw.pw_name, MAXLOGNAME);
	  break;
	}
      }
      /*
       * A name that is not in the database still names the session. It is what
       * the user called themselves; only the ids stay at the defaults.
       */
      if (u == NULL)
	ug_strlcpy(ug_cred.cr_login, name, MAXLOGNAME);
    }
    ug_cred_ready = 1;
  }
  ReleaseSemaphore(&ug_cred_sem);
}

/* True if the caller may change ids -- i.e. is the superuser. */
static int ug_issuper(void)
{
  return (ug_cred.cr_euid == 0);
}

/* ------------------------------------------------------------------ */
/* Setup and errors							*/

LONG SAVEDS RAF3(ug_v_SetupContextTagList,
		 struct UserGroupBase *, base,	  a6,
		 const UBYTE *,		 pname,	  a0,
		 struct TagItem *,	 taglist, a1)
#if 0
{
#endif
  struct TagItem *ti = taglist;
  /*
   * Staged here and only written back once the whole list has been accepted, so
   * a list that turns out to be bad half way through does not leave the context
   * half-applied. Note this is NOT a restore-on-error: the documented behaviour
   * for an illegal tag is that the context is CLEARED, which is what the
   * default: case below does.
   */
  APTR   errptr  = base->ug_ErrnoPtr;
  ULONG  errsize = base->ug_ErrnoSize;
  ULONG  intr	 = base->ug_IntrMask;
  struct Task *owner = base->ug_Owner;

  if (pname != NULL)
    ug_strlcpy(base->ug_Name, (const char *)pname, sizeof(base->ug_Name));

  while (ti != NULL) {
    switch (ti->ti_Tag) {
    case TAG_DONE:
      ti = NULL;
      continue;
    case TAG_IGNORE:
      break;
    case TAG_MORE:
      ti = (struct TagItem *)ti->ti_Data;
      continue;
    case TAG_SKIP:
      /* Skip ti_Data FOLLOWING items. A negative count is meaningless and would
       * walk the list backwards, so treat it as zero rather than wander off. */
      if ((LONG)ti->ti_Data > 0)
	ti += (LONG)ti->ti_Data;
      break;

    case UGT_ERRNOBPTR:
      errptr = (APTR)ti->ti_Data; errsize = 1; break;
    case UGT_ERRNOWPTR:
      errptr = (APTR)ti->ti_Data; errsize = 2; break;
    case UGT_ERRNOLPTR:
      errptr = (APTR)ti->ti_Data; errsize = 4; break;

    case UGT_INTRMASK:
      /*
       * Accepted and remembered for API compatibility, but nothing consults it:
       * the signal mask exists to interrupt blocking library calls, and this
       * library has exactly one blocking call (getpass), which waits on console
       * input rather than on signals. Recorded here rather than silently
       * dropped so a caller reading this knows where it stands -- getpass()
       * aborts on a typed Ctrl-C, not on a signal in this mask.
       */
      intr = (ULONG)ti->ti_Data;
      break;

    case UGT_OWNER:
      /*
       * NULL means "no owner", after which any task may claim it. Anything
       * else is taken as a Task pointer; there is no way to validate one, and
       * pretending otherwise would be worse than trusting the caller.
       */
      owner = (struct Task *)ti->ti_Data;
      break;

    default:
      /* Documented behaviour: an illegal input clears the context. */
      base->ug_ErrnoPtr  = NULL;
      base->ug_ErrnoSize = 0;
      base->ug_Err	 = EINVAL;
      return -1;
    }
    ti++;
  }

  /* A NULL errno pointer means "stop redirecting", not "size zero". */
  if (errptr == NULL) errsize = 0;

  base->ug_ErrnoPtr  = errptr;
  base->ug_ErrnoSize = errsize;
  base->ug_IntrMask  = intr;
  base->ug_Owner     = owner;
  base->ug_Err	     = 0;
  return 0;
}

LONG SAVEDS RAF1(ug_v_GetErr,
		 struct UserGroupBase *, base, a6)
#if 0
{
#endif
  return base->ug_Err;
}

/*
 * Negative codes are AmigaOS io_Error values, which is what the AmiTCP
 * documentation means by "understands also the negative IO error codes".
 */
STRPTR SAVEDS RAF2(ug_v_StrError,
		   struct UserGroupBase *, base, a6,
		   LONG,		   code, d1)
#if 0
{
#endif
  (void)base;

  if (code < 0) {
    /* -code must actually be positive: LONG_MIN negates to itself, so a plain
     * `-code < io_nerr` succeeds with a hugely negative index and reads wild
     * memory -- and with no MMU that read is silent. */
    if (-code > 0 && -code < io_nerr)
      return (STRPTR)io_errlist[-code];
  } else if (code < __sys_nerr) {
    return (STRPTR)__sys_errlist[code];
  }
  return (STRPTR)"Unknown error";
}

/* ------------------------------------------------------------------ */
/* User and group identification					*/

LONG SAVEDS RAF1(ug_v_getuid, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
  ug_cred_ensure();
  return ug_cred.cr_ruid;
}

LONG SAVEDS RAF1(ug_v_geteuid, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
  ug_cred_ensure();
  return ug_cred.cr_euid;
}

LONG SAVEDS RAF1(ug_v_getgid, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
  ug_cred_ensure();
  return ug_cred.cr_rgid;
}

LONG SAVEDS RAF1(ug_v_getegid, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
  ug_cred_ensure();
  /* The effective gid is groups[0]; with an empty set, fall back to the real. */
  return (ug_cred.cr_ngroups > 0) ? ug_cred.cr_groups[0] : ug_cred.cr_rgid;
}

LONG SAVEDS RAF3(ug_v_setreuid,
		 struct UserGroupBase *, base, a6,
		 LONG,			 ruid, d0,
		 LONG,			 euid, d1)
#if 0
{
#endif
  LONG rc = 0;

  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);

  /* -1 means "leave this one alone" -- the BSD convention. */
  if (!ug_issuper() &&
      ((ruid != -1 && ruid != ug_cred.cr_ruid && ruid != ug_cred.cr_euid) ||
       (euid != -1 && euid != ug_cred.cr_ruid && euid != ug_cred.cr_euid))) {
    rc = -1;
  } else {
    if (ruid != -1) ug_cred.cr_ruid = ruid;
    if (euid != -1) ug_cred.cr_euid = euid;
  }

  ReleaseSemaphore(&ug_cred_sem);
  ug_seterr(base, rc ? EPERM : 0);
  return rc;
}

LONG SAVEDS RAF2(ug_v_setuid,
		 struct UserGroupBase *, base, a6,
		 LONG,			 uid,  d0)
#if 0
{
#endif
  LONG rc = 0;

  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);

  if (ug_issuper()) {
    ug_cred.cr_ruid = uid;
    ug_cred.cr_euid = uid;
  } else if (uid == ug_cred.cr_ruid) {
    ug_cred.cr_euid = uid;
  } else {
    rc = -1;
  }

  ReleaseSemaphore(&ug_cred_sem);
  ug_seterr(base, rc ? EPERM : 0);
  return rc;
}

LONG SAVEDS RAF3(ug_v_setregid,
		 struct UserGroupBase *, base, a6,
		 LONG,			 rgid, d0,
		 LONG,			 egid, d1)
#if 0
{
#endif
  LONG rc = 0;
  LONG cur_egid;

  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);
  cur_egid = (ug_cred.cr_ngroups > 0) ? ug_cred.cr_groups[0] : ug_cred.cr_rgid;

  if (!ug_issuper() &&
      ((rgid != -1 && rgid != ug_cred.cr_rgid && rgid != cur_egid) ||
       (egid != -1 && egid != ug_cred.cr_rgid && egid != cur_egid))) {
    rc = -1;
  } else {
    if (rgid != -1) ug_cred.cr_rgid = rgid;
    if (egid != -1) {
      ug_cred.cr_groups[0] = egid;
      if (ug_cred.cr_ngroups < 1) ug_cred.cr_ngroups = 1;
    }
  }

  ReleaseSemaphore(&ug_cred_sem);
  ug_seterr(base, rc ? EPERM : 0);
  return rc;
}

LONG SAVEDS RAF2(ug_v_setgid,
		 struct UserGroupBase *, base, a6,
		 LONG,			 gid,  d0)
#if 0
{
#endif
  LONG rc = 0;

  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);

  if (ug_issuper()) {
    ug_cred.cr_rgid	 = gid;
    ug_cred.cr_groups[0] = gid;
    if (ug_cred.cr_ngroups < 1) ug_cred.cr_ngroups = 1;
  } else if (gid == ug_cred.cr_rgid) {
    ug_cred.cr_groups[0] = gid;
    if (ug_cred.cr_ngroups < 1) ug_cred.cr_ngroups = 1;
  } else {
    rc = -1;
  }

  ReleaseSemaphore(&ug_cred_sem);
  ug_seterr(base, rc ? EPERM : 0);
  return rc;
}

/*
 * getgroups(0, ...) is the documented way to ask how many there are without
 * providing a buffer, so a zero length is a query and not an error.
 */
LONG SAVEDS RAF3(ug_v_getgroups,
		 struct UserGroupBase *, base,	  a6,
		 LONG,			 ngroups, d0,
		 LONG *,		 gidset,  a1)
#if 0
{
#endif
  LONG rc, i;

  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);

  if (ngroups == 0) {
    rc = ug_cred.cr_ngroups;
  } else if (gidset == NULL || ngroups < 0 || ngroups < ug_cred.cr_ngroups) {
    rc = -1;
  } else {
    for (i = 0; i < ug_cred.cr_ngroups; i++)
      gidset[i] = ug_cred.cr_groups[i];
    rc = ug_cred.cr_ngroups;
  }

  ReleaseSemaphore(&ug_cred_sem);
  ug_seterr(base, (rc < 0) ? EINVAL : 0);
  return rc;
}

LONG SAVEDS RAF3(ug_v_setgroups,
		 struct UserGroupBase *, base,	  a6,
		 LONG,			 ngroups, d0,
		 LONG *,		 gidset,  a1)
#if 0
{
#endif
  LONG rc = 0, i, err = 0;

  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);

  if (ngroups < 0 || ngroups > NGROUPS || (ngroups > 0 && gidset == NULL)) {
    rc = -1; err = EINVAL;
  } else if (!ug_issuper()) {
    rc = -1; err = EPERM;
  } else {
    for (i = 0; i < ngroups; i++)
      ug_cred.cr_groups[i] = gidset[i];
    ug_cred.cr_ngroups = (short)ngroups;
  }

  ReleaseSemaphore(&ug_cred_sem);
  ug_seterr(base, err);
  return rc;
}

/*
 * Build the group set for a user: the base group first (that is what makes it
 * the effective gid), then every group in the database listing them.
 */
LONG SAVEDS RAF3(ug_v_initgroups,
		 struct UserGroupBase *, base,	    a6,
		 const UBYTE *,		 name,	    a1,
		 LONG,			 basegroup, d0)
#if 0
{
#endif
  struct ug_group *g;
  LONG	 groups[NGROUPS];
  int	 n = 0, i, j, dup;

  if (name == NULL) {
    ug_seterr(base, EINVAL);
    return -1;
  }

  ug_cred_ensure();

  /*
   * Build the set BEFORE taking the lock (the database walk is the slow part
   * and needs no credentials), then re-check privilege and publish under it.
   * Checking ug_issuper() out here and writing in there would let a concurrent
   * setuid() drop us to non-root in between, and the write would still land --
   * every sibling setter in this file checks while holding the lock.
   */
  groups[n++] = basegroup;

  for (g = ug_db_groups(); g != NULL && n < NGROUPS; g = g->g_next) {
    for (i = 0; i < g->g_nmem; i++) {
      if (!ug_streq(g->g_memv[i], (const char *)name))
	continue;
      for (dup = 0, j = 0; j < n; j++)
	if (groups[j] == (LONG)g->g_gr.gr_gid) { dup = 1; break; }
      if (!dup)
	groups[n++] = (LONG)g->g_gr.gr_gid;
      break;
    }
  }

  ObtainSemaphore(&ug_cred_sem);
  if (!ug_issuper()) {
    ReleaseSemaphore(&ug_cred_sem);
    ug_seterr(base, EPERM);
    return -1;
  }
  for (i = 0; i < n; i++)
    ug_cred.cr_groups[i] = groups[i];
  ug_cred.cr_ngroups = (short)n;
  ReleaseSemaphore(&ug_cred_sem);

  ug_seterr(base, 0);
  return 0;
}

/* ------------------------------------------------------------------ */
/* The user database							*/

struct passwd * SAVEDS RAF2(ug_v_getpwnam,
			    struct UserGroupBase *, base, a6,
			    const UBYTE *,	    name, a1)
#if 0
{
#endif
  struct ug_user *u;

  if (name == NULL) {
    ug_seterr(base, EINVAL);
    return NULL;
  }
  for (u = ug_db_users(); u != NULL; u = u->u_next)
    if (ug_streq(u->u_pw.pw_name, (const char *)name))
      return &u->u_pw;

  ug_seterr(base, 0);		/* "not found" is not an error condition */
  return NULL;
}

struct passwd * SAVEDS RAF2(ug_v_getpwuid,
			    struct UserGroupBase *, base, a6,
			    LONG,		    uid,  d0)
#if 0
{
#endif
  struct ug_user *u;

  for (u = ug_db_users(); u != NULL; u = u->u_next)
    if ((LONG)u->u_pw.pw_uid == uid)
      return &u->u_pw;

  ug_seterr(base, 0);
  return NULL;
}

VOID SAVEDS RAF1(ug_v_setpwent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  base->ug_PwCursor  = ug_db_users();
  base->ug_PwStarted = 1;
}

struct passwd * SAVEDS RAF1(ug_v_getpwent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  struct ug_user *u;

  if (!base->ug_PwStarted) {
    base->ug_PwCursor  = ug_db_users();
    base->ug_PwStarted = 1;
  }
  if ((u = base->ug_PwCursor) == NULL)
    return NULL;

  base->ug_PwCursor = u->u_next;
  return &u->u_pw;
}

VOID SAVEDS RAF1(ug_v_endpwent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  base->ug_PwCursor  = NULL;
  base->ug_PwStarted = 0;
}

/* ------------------------------------------------------------------ */
/* The group database							*/

struct group * SAVEDS RAF2(ug_v_getgrnam,
			   struct UserGroupBase *, base, a6,
			   const UBYTE *,	   name, a1)
#if 0
{
#endif
  struct ug_group *g;

  if (name == NULL) {
    ug_seterr(base, EINVAL);
    return NULL;
  }
  for (g = ug_db_groups(); g != NULL; g = g->g_next)
    if (ug_streq(g->g_gr.gr_name, (const char *)name))
      return &g->g_gr;

  ug_seterr(base, 0);
  return NULL;
}

struct group * SAVEDS RAF2(ug_v_getgrgid,
			   struct UserGroupBase *, base, a6,
			   LONG,		   gid,  d0)
#if 0
{
#endif
  struct ug_group *g;

  for (g = ug_db_groups(); g != NULL; g = g->g_next)
    if ((LONG)g->g_gr.gr_gid == gid)
      return &g->g_gr;

  ug_seterr(base, 0);
  return NULL;
}

VOID SAVEDS RAF1(ug_v_setgrent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  base->ug_GrCursor  = ug_db_groups();
  base->ug_GrStarted = 1;
}

struct group * SAVEDS RAF1(ug_v_getgrent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  struct ug_group *g;

  if (!base->ug_GrStarted) {
    base->ug_GrCursor  = ug_db_groups();
    base->ug_GrStarted = 1;
  }
  if ((g = base->ug_GrCursor) == NULL)
    return NULL;

  base->ug_GrCursor = g->g_next;
  return &g->g_gr;
}

VOID SAVEDS RAF1(ug_v_endgrent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  base->ug_GrCursor  = NULL;
  base->ug_GrStarted = 0;
}

/* ------------------------------------------------------------------ */
/* Password handling							*/

/*
 * crypt() -- NOT IMPLEMENTED, and deliberately failing closed.
 *
 * The real function is the 4.3BSD DES-based password hash. Producing it
 * correctly means reproducing the DES tables exactly; getting a single S-box
 * entry wrong yields a crypt() that computes confident, wrong hashes and
 * silently rejects every correct password on a machine whose `users` file came
 * from somewhere else. So this is not something to write from memory, and
 * nothing in AmiTCP_NG calls it.
 *
 * Roadshow's own usergroup.library 4.31 appears to be in the same position: the
 * whole binary is 10KB and contains neither the DES tables nor the `./0-9A-Za-z`
 * salt alphabet that any implementation needs. (That is inference from its
 * strings, not a claim about its source.)
 *
 * Returning NULL would be worse than useless -- callers pass the result
 * straight to strcmp() and would dereference it. So return "*", the traditional
 * Unix marker for an account that cannot be logged into: it is a valid string,
 * it can never equal a stored hash, and it can never equal an empty password
 * field either. Every password check therefore fails, which is the safe
 * direction, and ug_GetErr() reports ENOSYS for a caller that thinks to look.
 */
UBYTE * SAVEDS RAF3(ug_v_crypt,
		    struct UserGroupBase *, base, a6,
		    const UBYTE *,	    key,  a0,
		    const UBYTE *,	    salt, a1)
#if 0
{
#endif
  (void)key;
  (void)salt;

  base->ug_CryptBuf[0] = '*';
  base->ug_CryptBuf[1] = '\0';
  ug_seterr(base, ENOSYS);
  return (UBYTE *)base->ug_CryptBuf;
}

static const char ug_saltchars[] =
  "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/*
 * Generate a Version 7 style two-character salt. `user` is the existing entry,
 * which a system with more than one password format would consult to keep the
 * format stable; we only produce the one format, so it is unused rather than
 * ignored by accident.
 */
UBYTE * SAVEDS RAF4(ug_v_GetSalt,
		    struct UserGroupBase *, base,   a6,
		    const struct passwd *,  user,   a0,
		    UBYTE *,		    buffer, a1,
		    ULONG,		    size,   d0)
#if 0
{
#endif
  static ULONG seq;
  struct DateStamp ds;
  ULONG r;

  (void)user;

  if (buffer == NULL || size < 3) {
    ug_seterr(base, EINVAL);
    return NULL;
  }
  if (DOSBase == NULL) {
    ug_seterr(base, ENOSYS);
    return NULL;
  }

  /*
   * The sequence bump is the only thing separating two salts requested inside
   * the same DateStamp tick, so a lost update between two tasks would hand both
   * of them the SAME salt. Harmless while crypt() is a stub that ignores the
   * salt entirely -- but the day crypt() is implemented, two accounts sharing a
   * salt is a real weakness, and it would be invisible. Cheap to close now.
   */
  ObtainSemaphore(&ug_cred_sem);
  seq++;
  r = seq;
  ReleaseSemaphore(&ug_cred_sem);

  DateStamp(&ds);
  r = ((ULONG)ds.ds_Tick * 1103515245UL) + (ULONG)ds.ds_Minute +
      ((ULONG)ds.ds_Days << 11) + (r * 2654435761UL);

  buffer[0] = (UBYTE)ug_saltchars[(r >> 6) & 63];
  buffer[1] = (UBYTE)ug_saltchars[r & 63];
  buffer[2] = '\0';
  ug_seterr(base, 0);
  return buffer;
}

/*
 * Read a password without echoing it. CONSOLE: is a single filehandle that is
 * both the input and the output; when it cannot be opened (a program run
 * without a console, say) fall back to the standard streams, which is what the
 * documented behaviour asks for.
 */
UBYTE * SAVEDS RAF2(ug_v_getpass,
		    struct UserGroupBase *, base,   a6,
		    const UBYTE *,	    prompt, a1)
#if 0
{
#endif
  BPTR in, out, opened = 0;
  struct Task *me = SysBase->ThisTask;
  int i = 0;
  LONG c;

  if (DOSBase == NULL || me == NULL || me->tc_Node.ln_Type != NT_PROCESS) {
    ug_seterr(base, ENOSYS);
    return NULL;
  }

  if ((opened = Open((STRPTR)"CONSOLE:", MODE_OLDFILE)) != 0) {
    in = out = opened;
  } else {
    in  = Input();
    out = Output();
    if (in == 0 || out == 0) {
      ug_seterr(base, EIO);
      return NULL;
    }
  }

  if (prompt != NULL)
    FPuts(out, (STRPTR)prompt);
  Flush(out);

  /*
   * Raw mode is what turns the echo off; anything that returns early from here
   * must put the console back, or the caller's shell is left unusable.
   */
  SetMode(in, 1);

  while ((c = FGetC(in)) != -1) {
    if (c == '\n' || c == '\r')
      break;
    if (c == 3) {			/* Ctrl-C */
      i = -1;
      break;
    }
    if (c == 8 || c == 127) {		/* backspace / delete */
      if (i > 0) i--;
      continue;
    }
    if (i < _PASSWORD_LEN)
      base->ug_PassBuf[i++] = (char)c;
    /* Anything longer is discarded, as documented, not an error. */
  }

  SetMode(in, 0);
  FPuts(out, (STRPTR)"\n");
  Flush(out);
  if (opened != 0)
    Close(opened);

  if (i < 0) {
    base->ug_PassBuf[0] = '\0';
    ug_seterr(base, EINTR);
    return NULL;
  }

  base->ug_PassBuf[i] = '\0';
  ug_seterr(base, 0);
  return (UBYTE *)base->ug_PassBuf;
}

/* ------------------------------------------------------------------ */
/* Default protections							*/

ULONG SAVEDS RAF2(ug_v_umask,
		  struct UserGroupBase *, base, a6,
		  ULONG,		  mask, d0)
#if 0
{
#endif
  ULONG old;

  (void)base;
  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);
  old = ug_cred.cr_umask;
  ug_cred.cr_umask = (mode_t)(mask & 0777);
  ReleaseSemaphore(&ug_cred_sem);
  return old;
}

ULONG SAVEDS RAF1(ug_v_getumask, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
  ug_cred_ensure();
  return ug_cred.cr_umask;
}

/* ------------------------------------------------------------------ */
/* Sessions								*/

/*
 * There is one session and every task is in it -- AmiTCP documents exactly
 * that, and nothing in AmigaOS provides the process groups the real call
 * manipulates. So the first caller names the session and later callers get the
 * same answer instead of an error: a program that calls setsid() as a matter of
 * routine before forking a daemon should not fail here.
 */
LONG SAVEDS RAF1(ug_v_setsid, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  LONG sid;

  (void)base;
  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);
  if (ug_cred.cr_session == NULL)
    ug_cred.cr_session = SysBase->ThisTask;
  sid = (LONG)ug_cred.cr_session;
  ReleaseSemaphore(&ug_cred_sem);
  return sid;
}

LONG SAVEDS RAF1(ug_v_getpgrp, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
  ug_cred_ensure();
  return (LONG)ug_cred.cr_session;
}

STRPTR SAVEDS RAF1(ug_v_getlogin, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  /*
   * Copy under the lock into this opener's own buffer, exactly as
   * ug_v_getcredentials() does, rather than returning a pointer into the one
   * shared record.
   *
   * Two things were wrong with handing back &ug_cred.cr_login directly. The read
   * took no lock at all while ug_v_setlogin() writes that same array under
   * ug_cred_sem with a byte-at-a-time copy that terminates LAST, so a caller
   * walking the string could see a splice of the old and new names. And the
   * pointer stayed live afterwards: any task may call setlogin() (every task
   * starts at euid 0), so a caller that held the result while doing something
   * else would find the name had changed underneath it.
   */
  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);
  ug_strlcpy(base->ug_LoginBuf, ug_cred.cr_login, sizeof(base->ug_LoginBuf));
  ReleaseSemaphore(&ug_cred_sem);

  return (base->ug_LoginBuf[0] != '\0') ? (STRPTR)base->ug_LoginBuf : NULL;
}

LONG SAVEDS RAF2(ug_v_setlogin,
		 struct UserGroupBase *, base, a6,
		 const UBYTE *,		 name, a1)
#if 0
{
#endif
  if (name == NULL) {
    ug_seterr(base, EINVAL);
    return -1;
  }

  /*
   * Check the privilege INSIDE the lock, like every other setter here does.
   * This was the one that checked outside it -- exactly the anti-pattern
   * ug_v_initgroups() has a comment warning against: a concurrent setuid()
   * lands between the check and the write, and the write still goes through on
   * a privilege that stopped applying. Low stakes on this machine, but there is
   * no reason for this one function to be the odd one out.
   */
  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);
  if (!ug_issuper()) {
    ReleaseSemaphore(&ug_cred_sem);
    ug_seterr(base, EPERM);
    return -1;
  }
  ug_strlcpy(ug_cred.cr_login, (const char *)name, MAXLOGNAME);
  ReleaseSemaphore(&ug_cred_sem);

  ug_seterr(base, 0);
  return 0;
}

/* ------------------------------------------------------------------ */
/* The login databases (utmp, lastlog) -- not implemented		*/

/*
 * These record who is logged in and when each user last logged in. Both are
 * only meaningful with a login program maintaining them; AmiTCP_NG ships none,
 * nothing in the stack writes them, and Roadshow's own usergroup.library
 * contains no utmp, wtmp or lastlog path at all -- so an empty database is the
 * truthful answer here rather than a shortfall.
 *
 * getutent() returning NULL reads as "end of database", which is what a caller
 * walking it expects and handles. setlastlog() reports ENOSYS rather than
 * claiming to have recorded something it did not.
 */
VOID SAVEDS RAF1(ug_v_setutent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
}

struct utmp * SAVEDS RAF1(ug_v_getutent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  ug_seterr(base, 0);
  return NULL;
}

VOID SAVEDS RAF1(ug_v_endutent, struct UserGroupBase *, base, a6)
#if 0
{
#endif
  (void)base;
}

struct lastlog * SAVEDS RAF2(ug_v_getlastlog,
			     struct UserGroupBase *, base, a6,
			     LONG,		     uid,  d0)
#if 0
{
#endif
  (void)uid;
  ug_seterr(base, 0);
  return NULL;
}

LONG SAVEDS RAF4(ug_v_setlastlog,
		 struct UserGroupBase *, base, a6,
		 LONG,			 uid,  d0,
		 const UBYTE *,		 name, a0,
		 const UBYTE *,		 host, a1)
#if 0
{
#endif
  (void)uid;
  (void)name;
  (void)host;
  ug_seterr(base, ENOSYS);
  return -1;
}

/* ------------------------------------------------------------------ */
/* Credentials								*/

/*
 * A snapshot, into this opener's own buffer. The documented interface returns a
 * pointer to static storage that the next call overwrites; making that storage
 * per-opener rather than global at least keeps two programs from overwriting
 * each other's copy.
 *
 * `task` is accepted and ignored: there is one credentials record shared by
 * every task, so the answer is the same whoever is asked about. Validating a
 * Task pointer is not possible, so an invalid one cannot be reported.
 */
struct UserGroupCredentials * SAVEDS RAF2(ug_v_getcredentials,
					  struct UserGroupBase *, base, a6,
					  struct Task *,	  task, a0)
#if 0
{
#endif
  (void)task;

  ug_cred_ensure();
  ObtainSemaphore(&ug_cred_sem);
  base->ug_CredBuf = ug_cred;
  ReleaseSemaphore(&ug_cred_sem);

  ug_seterr(base, 0);
  return &base->ug_CredBuf;
}
