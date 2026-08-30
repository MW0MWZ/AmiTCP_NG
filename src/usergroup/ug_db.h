/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ug_db.h -- the cached user and group database read from DEVS:Internet.
 *
 * LIFETIME, and why it matters. The database is loaded once, on first use, and
 * then never modified: getpwent()/getgrent() hand out pointers straight INTO
 * these records rather than copying into a static buffer, so anything that
 * freed or rebuilt an entry while a caller still held its `struct passwd *`
 * would be a use-after-free -- and with no MMU that corrupts silently instead
 * of trapping. So setpwent()/setgrent() rewind a cursor and nothing more; the
 * only thing that frees the list is ug_db_flush(), called from the library's
 * expunge, when the open count is zero and there are by definition no callers.
 *
 * The cost is that editing DEVS:Internet/users does not take effect until the
 * library is flushed from memory. That is the right trade: a stale account
 * list is a puzzle, a dangling pointer is a crash somewhere else entirely.
 */
#ifndef UG_DB_H
#define UG_DB_H

#include <exec/types.h>
#include <pwd.h>
#include <grp.h>

/*
 * Members per group. The file format's USERS/M is unbounded, but a fixed cap
 * keeps each record a single allocation; groups with more members than this
 * are truncated rather than rejected, so one long line cannot lose the whole
 * entry.
 */
#define UG_MAXMEM	32

struct ug_user {
  struct ug_user *u_next;
  struct passwd	  u_pw;
};

struct ug_group {
  struct ug_group *g_next;
  struct group	   g_gr;
  int		   g_nmem;
  char		  *g_memv[UG_MAXMEM];		/* owns the strings gr_mem points at */
};

VOID		 ug_db_init(VOID);	/* once, from LibInit: set up the lock */
VOID		 ug_db_flush(VOID);	/* expunge only -- see the note above */
struct ug_user  *ug_db_users(VOID);	/* head of the list, loading if needed */
struct ug_group *ug_db_groups(VOID);

#endif /* !UG_DB_H */
