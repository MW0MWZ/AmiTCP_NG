/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ug_lib.c -- the LIBS:usergroup.library skeleton: RomTag, vector tables, and
 * the Open/Close/Expunge trio.
 *
 * WHY THIS LIBRARY EXISTS. Roadshow ships LIBS:usergroup.library; AmiTCP_NG has
 * always shipped its headers and never the library itself, so a Roadshow
 * machine that installed our stack lost the one piece of it that is not part of
 * bsdsocket.library. This closes that gap. The ABI is not a guess: our FD file
 * (inherited from AmiTCP 3.0b2) and Roadshow's SFD list the same 39 functions in
 * the same order from bias 30, and the pragmas agree on the offsets
 * (ug_SetupContextTagList 0x1E = 30, getuid 0x30 = 48).
 *
 * TWO KINDS OF BASE, the AmiTCP model (see amiga_api.c, which does the same for
 * bsdsocket.library):
 *   - the MASTER base, made once at load and added to the system library list.
 *     Its vectors are Open/Close/Expunge only.
 *   - a PER-OPENER base, made by Open() for each caller and handed back as the
 *     library base. This is what carries the caller's cursors, error code and
 *     buffers. Its Open/Expunge vectors are never reached (it is not in the
 *     system list); its Close vector frees it.
 * The two are the same struct, so a vector can be reached through either
 * without caring which.
 */
#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <sys/cdefs.h>				/* SAVEDS */
#include <api/amiga_raf.h>

#include "ug_base.h"
#include "usergroup.library_rev.h"
#include "bsdsocket.library_rev.h"	/* AMITCP_NG_VER -- the project version */

#define UG_XSTR(x) #x
#define UG_STR(x)  UG_XSTR(x)

/*
 * Library runtime. A -nostartfiles library has no crt0, so it owns SysBase and
 * exit() itself -- same arrangement as src/lib/bsdsocket_lib.c.
 */
struct ExecBase  *SysBase = NULL;
struct DosLibrary *DOSBase = NULL;
void exit(int code) { (void)code; }

typedef VOID (* REGARGFUN f_void)();

extern f_void UserGroup_funcTable[];

/*
 * Entry stub: a library must never be run as a program. OpenLibrary() ignores
 * this and uses the RomTag.
 */
asm("  .text                \n"
    "  .even                \n"
    "  .globl _start        \n"
    "_start:                \n"
    "  moveq #-1,d0         \n"
    "  rts                  \n");

/* ------------------------------------------------------------------ */
/* The vector implementations, in UserGroup_funcTable order.	      */

extern REGARGFUN VOID ug_v_SetupContextTagList();
extern REGARGFUN VOID ug_v_GetErr();
extern REGARGFUN VOID ug_v_StrError();
extern REGARGFUN VOID ug_v_getuid();
extern REGARGFUN VOID ug_v_geteuid();
extern REGARGFUN VOID ug_v_setreuid();
extern REGARGFUN VOID ug_v_setuid();
extern REGARGFUN VOID ug_v_getgid();
extern REGARGFUN VOID ug_v_getegid();
extern REGARGFUN VOID ug_v_setregid();
extern REGARGFUN VOID ug_v_setgid();
extern REGARGFUN VOID ug_v_getgroups();
extern REGARGFUN VOID ug_v_setgroups();
extern REGARGFUN VOID ug_v_initgroups();
extern REGARGFUN VOID ug_v_getpwnam();
extern REGARGFUN VOID ug_v_getpwuid();
extern REGARGFUN VOID ug_v_setpwent();
extern REGARGFUN VOID ug_v_getpwent();
extern REGARGFUN VOID ug_v_endpwent();
extern REGARGFUN VOID ug_v_getgrnam();
extern REGARGFUN VOID ug_v_getgrgid();
extern REGARGFUN VOID ug_v_setgrent();
extern REGARGFUN VOID ug_v_getgrent();
extern REGARGFUN VOID ug_v_endgrent();
extern REGARGFUN VOID ug_v_crypt();
extern REGARGFUN VOID ug_v_GetSalt();
extern REGARGFUN VOID ug_v_getpass();
extern REGARGFUN VOID ug_v_umask();
extern REGARGFUN VOID ug_v_getumask();
extern REGARGFUN VOID ug_v_setsid();
extern REGARGFUN VOID ug_v_getpgrp();
extern REGARGFUN VOID ug_v_getlogin();
extern REGARGFUN VOID ug_v_setlogin();
extern REGARGFUN VOID ug_v_setutent();
extern REGARGFUN VOID ug_v_getutent();
extern REGARGFUN VOID ug_v_endutent();
extern REGARGFUN VOID ug_v_getlastlog();
extern REGARGFUN VOID ug_v_setlastlog();
extern REGARGFUN VOID ug_v_getcredentials();

/*
 * Declared with their real return types rather than the REGARGFUN VOID shorthand
 * used for the vectors above: these three are also DEFINED in this file, so the
 * declaration has to agree with the definition.
 */
extern struct Library *SAVEDS UG_Open(VOID);
extern ULONG	      *SAVEDS UG_Close(VOID);
extern ULONG	      *SAVEDS UG_Expunge(VOID);

static LONG UG_Null(void) { return 0L; }

/*
 * THE ABI. This order is the binary interface: bias 30 means the first entry
 * after the four standard vectors is reached as jsr a6@(-30), the next -36, and
 * so on. It matches ref/roadshow-sdk/sfd/usergroup_lib.sfd and
 * src/netinclude/fd/usergroup_lib.fd line for line. Never insert -- only
 * append, and only if a future SFD does.
 */
f_void UserGroup_funcTable[] = {
  (f_void)UG_Null,		/* Open() -- per-opener bases are never opened */
  (f_void)UG_Close,
  (f_void)UG_Null,		/* Expunge() -- likewise never reached */
  (f_void)UG_Null,		/* Reserved() */

  ug_v_SetupContextTagList,	/* -30 */
  ug_v_GetErr,			/* -36 */
  ug_v_StrError,		/* -42 */
  ug_v_getuid,			/* -48 */
  ug_v_geteuid,			/* -54 */
  ug_v_setreuid,		/* -60 */
  ug_v_setuid,			/* -66 */
  ug_v_getgid,			/* -72 */
  ug_v_getegid,			/* -78 */
  ug_v_setregid,		/* -84 */
  ug_v_setgid,			/* -90 */
  ug_v_getgroups,		/* -96 */
  ug_v_setgroups,		/* -102 */
  ug_v_initgroups,		/* -108 */
  ug_v_getpwnam,		/* -114 */
  ug_v_getpwuid,		/* -120 */
  ug_v_setpwent,		/* -126 */
  ug_v_getpwent,		/* -132 */
  ug_v_endpwent,		/* -138 */
  ug_v_getgrnam,		/* -144 */
  ug_v_getgrgid,		/* -150 */
  ug_v_setgrent,		/* -156 */
  ug_v_getgrent,		/* -162 */
  ug_v_endgrent,		/* -168 */
  ug_v_crypt,			/* -174 */
  ug_v_GetSalt,			/* -180 */
  ug_v_getpass,			/* -186 */
  ug_v_umask,			/* -192 */
  ug_v_getumask,		/* -198 */
  ug_v_setsid,			/* -204 */
  ug_v_getpgrp,			/* -210 */
  ug_v_getlogin,		/* -216 */
  ug_v_setlogin,		/* -222 */
  ug_v_setutent,		/* -228 */
  ug_v_getutent,		/* -234 */
  ug_v_endutent,		/* -240 */
  ug_v_getlastlog,		/* -246 */
  ug_v_setlastlog,		/* -252 */
  ug_v_getcredentials,		/* -258 */

  (f_void)-1
};

/* The master base's vectors. */
static f_void UserGroupMaster_funcTable[] = {
  (f_void)UG_Open,
  (f_void)UG_Null,		/* the master is never Closed directly */
  (f_void)UG_Expunge,
  (f_void)UG_Null,		/* Reserved() */
  (f_void)-1
};

/* ------------------------------------------------------------------ */

static const char ug_LibName[] = "usergroup.library";
static const char ug_LibID[]   = "usergroup.library " UG_STR(UG_VERSION) "."
				 UG_STR(UG_REVISION) " (AmiTCP_NG " AMITCP_NG_VER ")\r\n";

/*
 * The $VER: tag, so `Version LIBS:usergroup.library` can read the file without
 * loading it. Marked used, because nothing references it and -O2 would
 * otherwise drop the one string whose entire purpose is to sit in the binary.
 */
static const char ug_VerTag[] __attribute__((used)) =
  "$VER: usergroup.library " UG_STR(UG_VERSION) "." UG_STR(UG_REVISION)
  " (AmiTCP_NG " AMITCP_NG_VER ")";

/*
 * The dataInit table, hand-built exactly as amiga_api.c does (exec's
 * initializers.h macros do not survive a compiler that cannot fold a string
 * address into a UWORD at compile time).
 */
#define OFFSET(structName, structEntry) \
  ((LONG)(&(((struct structName *) 0)->structEntry)))
#define id_byte 0xe000
#define id_word 0xd000
#define id_long 0xc000

static const struct {
  UWORD byte1; UWORD offset1; UWORD ln_type;
  UWORD byte2; UWORD offset2; UWORD lib_flags;
  UWORD long3; UWORD offset3; ULONG ln_Name;
  UWORD word4; UWORD offset4; UWORD lib_Version;
  UWORD word5; UWORD offset5; UWORD lib_Revision;
  UWORD long6; UWORD offset6; ULONG lib_IdString;
  UWORD end7;
} UserGroup_initTable = {
  id_byte, OFFSET(Node, ln_Type),	 NT_LIBRARY << 8,
  id_byte, OFFSET(Library, lib_Flags),	 (LIBF_SUMUSED|LIBF_CHANGED) << 8,
  id_long, OFFSET(Node, ln_Name),	 (ULONG)ug_LibName,
  id_word, OFFSET(Library, lib_Version), UG_VERSION,
  id_word, OFFSET(Library, lib_Revision), UG_REVISION,
  id_long, OFFSET(Library, lib_IdString), (ULONG)ug_LibID,
  0x0000
};

#undef id_byte
#undef id_word
#undef id_long

/* ------------------------------------------------------------------ */

/*
 * RTF_AUTOINIT init. d0 = the MakeLibrary()'d master base, a0 = the seglist,
 * a6 = SysBase. Return the base and exec AddLibrary()s it; return NULL and exec
 * unloads us again.
 */
struct Library * SAVEDS LibInit(VOID)
{
  register struct Library  *_base    __asm("d0");
  register BPTR		    _seglist __asm("a0");
  register struct ExecBase *_sysbase __asm("a6");
  struct Library  *base    = _base;		/* capture the arguments before */
  BPTR		   seglist = _seglist;		/* anything can clobber them */
  struct ExecBase *sysbase = _sysbase;
  struct UserGroupBase *ugb = (struct UserGroupBase *)base;

  SysBase = sysbase;

  /*
   * utility.library, FIRST and not optional on a 68000.
   *
   * gcc compiles a 32x32 multiply or divide into a call to libnix's __mulsi3 /
   * __divsi3, and on -m68000 (no muls.l) those reach utility.library's
   * SMult32/UMult32 through this very global. libnix DEFINES UtilityBase but
   * never opens it, so left alone it is NULL and the first multiply anywhere in
   * this library jumps through it. It cost two emulator runs to find, in
   * ug_GetSalt() of all places -- the only multiply in the file. An 020+ build
   * emits the native instruction and would never have shown it, which is
   * exactly why this is opened unconditionally rather than where it is needed.
   *
   * UNCONDITIONALLY, and that word is load-bearing: libnix pre-seeds its
   * UtilityBase to -1, not NULL, for the auto-open machinery that a
   * -nostartfiles binary never runs. The obvious `if (UtilityBase == NULL)`
   * guard therefore looks satisfied, opens nothing, and leaves every multiply
   * jumping through -1. (src/kern/amiga_main.c assigns unconditionally in the
   * library path for the same reason, and guards only in the program path,
   * where the base is AmiTCP's own and really does start NULL.)
   *
   * Failing to open it is fatal: a library whose arithmetic crashes is worse
   * than one that refuses to load, and utility.library has been in ROM since
   * 2.0, so this only fires on a machine we could not have worked on anyway.
   */
  {
    extern struct Library *UtilityBase;
    if ((UtilityBase = OpenLibrary((STRPTR)"utility.library", 37L)) == NULL)
      return NULL;
  }

  /*
   * dos.library is needed to read DEVS:Internet and for getpass()'s CONSOLE:.
   * Without it the database falls back to its built-in entries, which is a
   * working library, so this is not fatal -- but on any real machine it opens.
   */
  DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 36);

  ugb->ug_SegList = seglist;
  ug_db_init();
  ug_cred_init();
  return base;
}

/* AUTOINIT table: { dataSize, funcTable, dataInit, initFunc }. */
static const ULONG ug_InitTable[4] = {
  (ULONG) sizeof(struct UserGroupBase),
  (ULONG) UserGroupMaster_funcTable,
  (ULONG) &UserGroup_initTable,
  (ULONG) LibInit
};

extern const struct Resident ug_RomTag;
const struct Resident ug_RomTag = {
  RTC_MATCHWORD,			/* rt_MatchWord */
  (struct Resident *)&ug_RomTag,	/* rt_MatchTag  */
  (APTR)(&ug_RomTag + 1),		/* rt_EndSkip   */
  RTF_AUTOINIT,				/* rt_Flags     */
  UG_VERSION,				/* rt_Version   */
  NT_LIBRARY,				/* rt_Type      */
  0,					/* rt_Pri       */
  (char *)ug_LibName,			/* rt_Name      */
  (char *)ug_LibID,			/* rt_IdString  */
  (APTR)ug_InitTable			/* rt_Init      */
};

/* ------------------------------------------------------------------ */

/*
 * Open. Build a fresh base for this caller. Note what is NOT done here: no file
 * is read and no process is created. Exec runs Open() under Forbid(), so
 * anything that could block would break it; the database loads lazily on the
 * first call that needs it, in the caller's own context.
 */
struct Library * SAVEDS RAF2(UG_Open,
			     struct UserGroupBase *,	master,	a6,
			     ULONG,			version, d0)
#if 0
{
#endif
  struct UserGroupBase *base;
  WORD *i;
  (void)version;

  base = (struct UserGroupBase *)MakeLibrary(UserGroup_funcTable,
					     (UWORD *)&UserGroup_initTable,
					     NULL,
					     sizeof(struct UserGroupBase),
					     NULL);
  if (base == NULL)
    return NULL;

  /* MakeLibrary only fills what initTable names; clear the rest ourselves. */
  for (i = (WORD *)((struct Library *)base + 1);
       i < (WORD *)(base + 1);
       i++)
    *i = 0;

  base->ug_Lib.lib_OpenCnt = 1;
  base->ug_Master	   = master;
  base->ug_Owner	   = SysBase->ThisTask;
  base->ug_ErrnoPtr	   = NULL;
  base->ug_ErrnoSize	   = 0;
  base->ug_IntrMask	   = SIGBREAKF_CTRL_C;

  master->ug_Lib.lib_OpenCnt++;
  master->ug_Lib.lib_Flags &= ~LIBF_DELEXP;
  return (struct Library *)base;
}

/*
 * Close. Frees the per-opener base. The master's count was bumped by Open(), so
 * drop it here too -- leaving it inflated would pin the library in memory
 * forever.
 *
 * There is no cleanup to do beyond the memory: this library owns no sockets, no
 * signals and no processes on a caller's behalf, and the database it read is
 * global and outlives every opener.
 *
 * When this was the last opener AND exec has already asked us to go
 * (LIBF_DELEXP), expunge now and return the seglist. Exec's CloseLibrary() does
 * the UnLoadSeg() itself, AFTER this function has fully returned -- so this is
 * not "unloading the code that is mid-return", which is what an earlier version
 * of this comment claimed as its reason for never expunging. It is the same
 * thing UL_Close() does in api/amiga_api.c, and the reason matters: a library
 * left resident at zero openers is the classic Amiga upgrade trap, where
 * replacing the file on disk changes nothing because the next OpenLibrary()
 * finds the old copy still in the system list.
 */
ULONG * SAVEDS RAF1(UG_Close,
		    struct UserGroupBase *, base, a6)
#if 0
{
#endif
  struct UserGroupBase *master = base->ug_Master;
  VOID  *freestart;
  ULONG  size;

  if (--base->ug_Lib.lib_OpenCnt > 0)
    return NULL;

  freestart = (void *)((ULONG)base - (ULONG)base->ug_Lib.lib_NegSize);
  size      = base->ug_Lib.lib_NegSize + base->ug_Lib.lib_PosSize;
  FreeMem(freestart, size);

  if (master != NULL && master->ug_Lib.lib_OpenCnt > 0) {
    if (--master->ug_Lib.lib_OpenCnt == 0 &&
	(master->ug_Lib.lib_Flags & LIBF_DELEXP)) {
      register struct UserGroupBase *_a6 __asm("a6") = master;
      (void)_a6;
      return UG_Expunge();
    }
  }

  return NULL;
}

/*
 * Expunge. Only ever called on the master base, and only by exec, with the open
 * count at zero. Returns the seglist so exec can UnLoadSeg() us.
 */
ULONG * SAVEDS RAF1(UG_Expunge,
		    struct UserGroupBase *, base, a6)
#if 0
{
#endif
  BPTR   seglist;
  VOID  *freestart;
  ULONG  size;

  if (base->ug_Lib.lib_OpenCnt > 0) {
    base->ug_Lib.lib_Flags |= LIBF_DELEXP;	/* try again when they close */
    return NULL;
  }

  seglist = base->ug_SegList;

  /*
   * Safe here and nowhere else: with no openers left there can be no caller
   * holding a `struct passwd *` into the cached database. See ug_db.h.
   */
  ug_db_flush();

  if (DOSBase != NULL) {
    CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
  }
  {
    extern struct Library *UtilityBase;
    /* -1 is libnix's "never opened" seed, not a base -- see LibInit. */
    if (UtilityBase != NULL && UtilityBase != (struct Library *)-1) {
      CloseLibrary(UtilityBase);
      UtilityBase = NULL;
    }
  }

  Remove((struct Node *)base);
  freestart = (void *)((ULONG)base - (ULONG)base->ug_Lib.lib_NegSize);
  size      = base->ug_Lib.lib_NegSize + base->ug_Lib.lib_PosSize;
  FreeMem(freestart, size);

  return (ULONG *)seglist;
}
