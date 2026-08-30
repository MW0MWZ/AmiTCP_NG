/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ug_base.h -- the usergroup.library base, and what is per-opener vs global.
 *
 * PER-OPENER. Every OpenLibrary() gets its own base (the same model AmiTCP and
 * Roadshow document: "each time the usergroup.library is opened, it creates a
 * new instance of the library base"). The base carries the things that must not
 * be shared between callers: the getpwent()/getgrent() cursors, the errno
 * redirection set up by ug_SetupContextTagList(), the last error, and the
 * scratch buffers whose addresses get handed back to that caller.
 *
 * GLOBAL. The credentials (uid/gid/groups/umask/login/session) are deliberately
 * NOT per-opener. AmiTCP's documentation is explicit that "all tasks belong to
 * one session and they share common credentials", and that getuid() and friends
 * may be called by any task at all -- including one that never opened the
 * library. A per-opener copy would make `id` and the program it just launched
 * disagree about who is logged in. So there is one credentials record, guarded
 * by a semaphore because setgroups() writes an array and getgroups() reads it.
 *
 * The user and group database is global too, and read-only once loaded; see
 * ug_db.h for why that matters.
 */
#ifndef UG_BASE_H
#define UG_BASE_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <libraries/usergroup.h>

#include "ug_db.h"

struct UserGroupBase {
  struct Library   ug_Lib;
  BPTR		   ug_SegList;		/* master base only */
  /*
   * The master, recorded when this base was made. Close() has to decrement the
   * master's open count, and looking it up by name in SysBase->LibList would
   * find whichever usergroup.library is currently visible -- not necessarily
   * the one this base came from.
   */
  struct UserGroupBase *ug_Master;

  /*
   * The owner. ug_SetupContextTagList(UGT_OWNER) moves it. Only the owner gets
   * errno written through ug_ErrnoPtr -- a non-owner calling in would otherwise
   * scribble on a variable belonging to a different task entirely, which is
   * exactly the hazard the AmiTCP docs describe as "non-owning tasks cannot
   * recover error codes".
   */
  struct Task	  *ug_Owner;
  APTR		   ug_ErrnoPtr;		/* UGT_ERRNOPTR, NULL = no redirect */
  ULONG		   ug_ErrnoSize;	/* 1, 2 or 4 */
  ULONG		   ug_IntrMask;		/* UGT_INTRMASK */
  LONG		   ug_Err;		/* what ug_GetErr() returns */

  /* Database iteration state -- pointers into the global lists, never copies. */
  struct ug_user  *ug_PwCursor;
  int		   ug_PwStarted;
  struct ug_group *ug_GrCursor;
  int		   ug_GrStarted;

  /* Buffers whose addresses are returned to the caller. */
  struct UserGroupCredentials ug_CredBuf;
  char		   ug_LoginBuf[32];	/* getlogin()'s result (MAXLOGNAME; the copy
					 * is bounded by sizeof, so a larger
					 * MAXLOGNAME truncates, never overflows) */
  /* Program name from ug_SetupContextTagList(UGT_PROGRAMNAME). Stored only:
   * nothing reads it. Roadshow's own library takes the tag, so we accept and
   * keep it rather than discarding a caller's value, but there is no consumer
   * here and no behaviour depends on it. */
  char		   ug_Name[32];
  char		   ug_CryptBuf[4];	/* crypt()'s result -- see ug_funcs.c */
  char		   ug_PassBuf[_PASSWORD_LEN + 2];
};

/* The one shared credentials record, and its lock. Defined in ug_funcs.c. */
extern struct UserGroupCredentials ug_cred;
extern struct SignalSemaphore	   ug_cred_sem;

VOID ug_cred_init(VOID);

/* Set ug_Err, and mirror it into the owner's errno if one was registered. */
VOID ug_seterr(struct UserGroupBase *base, LONG err);

#endif /* !UG_BASE_H */
