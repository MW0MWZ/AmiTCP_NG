/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING). */
/*
 * DNS response cache. See dns_cache.h for the tunables and the design summary.
 *
 * Each entry stores the raw DNS response packet keyed by (lowercased query
 * name, record type). A hit copies the stored response out under the lock and
 * then replays it, OUTSIDE the lock, through gethostnamadr.c's
 * ng_hostent_from_response() -- so a cached answer materialises exactly as a
 * fresh one would, with no separate re-serialisation to get wrong. The slot
 * table is sized once from the installed-RAM tier (ng_dns_cache_max, set by
 * ng_ram_tier()); each entry's name and response storage is allocated on
 * demand, so an idle cache costs only the small slot table.
 *
 * Concurrency: the resolver runs in each caller's own task/SocketBase context
 * and shares no lock, but this table IS cross-task shared, so it has its own
 * SignalSemaphore (mirroring NDB's ndb_Lock). The lock is held only for the
 * table search and the small response copy; parsing/allocation happen with it
 * dropped.
 */

#include <conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <api/arpa_nameser.h>		/* T_A, T_PTR */
#include <kern/amiga_includes.h>	/* AllocVec/FreeVec, SignalSemaphore, GetSysTime */
#include <api/amiga_api.h>		/* struct SocketBase */
#include <api/dns_cache.h>

int ng_dns_cache_max = 0;		/* set by ng_ram_tier(); 0 = cache disabled */

struct dns_ent {
  char   *name;		/* lowercased query name (AllocVec'd); NULL = empty slot */
  UBYTE  *response;	/* AllocVec'd raw DNS response, resp_len bytes (NULL if negative) */
  int     resp_len;
  int     type;		/* T_A / T_PTR */
  UBYTE   negative;	/* 1 = a cached "not found" (no response) */
  ULONG   expiry;	/* GetSysTime tv_secs at which this entry dies */
};

static struct dns_ent      *dns_tab;	/* dns_tab_n slots */
static int                  dns_tab_n;
static struct SignalSemaphore dns_lock;
static int                  dns_ready;

/* Current time in whole seconds (GetSysTime is already used by res_mkquery). */
static ULONG
dns_now(void)
{
  struct timeval tv;
  GetSysTime(&tv);
  return (ULONG)tv.tv_secs;
}

/* Case-insensitive (ASCII) compare of a stored-lowercase name against a raw
 * query name. Returns 1 if equal. */
static int
name_ieq(const char *lc, const char *raw)
{
  for (;; lc++, raw++) {
    char r = *raw;
    if (r >= 'A' && r <= 'Z')
      r += 'a' - 'A';
    if (*lc != r)
      return 0;
    if (r == '\0')
      return 1;
  }
}

/* AllocVec a lowercased copy of a name. NULL on out-of-memory. */
static char *
name_dup_lc(const char *s)
{
  int n = strlen(s), i;
  char *d = AllocVec((ULONG)n + 1, MEMF_PUBLIC);
  if (d != NULL)
    for (i = 0; i <= n; i++) {
      char c = s[i];
      d[i] = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
    }
  return d;
}

/* Free an entry's storage and mark the slot empty. Caller holds dns_lock. */
static void
ent_clear(struct dns_ent *e)
{
  if (e->name)     { FreeVec(e->name);     e->name = NULL; }
  if (e->response) { FreeVec(e->response); e->response = NULL; }
  e->resp_len = 0;
  e->type = 0;
  e->negative = 0;
  e->expiry = 0;
}

/* Pick a slot to (re)use for a new entry: an empty one, else an expired one,
 * else the one that expires soonest. Caller holds dns_lock; never NULL while
 * dns_tab_n > 0. */
static struct dns_ent *
pick_slot(ULONG now)
{
  struct dns_ent *victim = NULL;
  int i;
  for (i = 0; i < dns_tab_n; i++) {
    struct dns_ent *c = &dns_tab[i];
    if (c->name == NULL)			/* empty */
      return c;
    if ((LONG)(now - c->expiry) >= 0)		/* expired */
      return c;
    if (victim == NULL || (LONG)(c->expiry - victim->expiry) < 0)
      victim = c;				/* oldest-expiry so far */
  }
  return victim;
}

/* Find a live entry for (name, type); clears and skips an expired match.
 * Caller holds dns_lock. */
static struct dns_ent *
ent_find(const char *name, int type, ULONG now)
{
  int i;
  for (i = 0; i < dns_tab_n; i++) {
    struct dns_ent *e = &dns_tab[i];
    if (e->name != NULL && e->type == type && name_ieq(e->name, name)) {
      if ((LONG)(now - e->expiry) >= 0) {	/* expired */
	ent_clear(e);
	return NULL;
      }
      return e;
    }
  }
  return NULL;
}

void
ng_dnscache_init(void)
{
  if (dns_ready)
    return;
  InitSemaphore(&dns_lock);
  dns_tab_n = ng_dns_cache_max;
  if (dns_tab_n > 0) {
    dns_tab = AllocVec((ULONG)dns_tab_n * sizeof(struct dns_ent),
		       MEMF_PUBLIC | MEMF_CLEAR);
    if (dns_tab == NULL)		/* no memory -> cache stays disabled */
      dns_tab_n = 0;
  }
  dns_ready = 1;
}

struct hostent *
ng_dnscache_get(struct SocketBase *libPtr, const char *name, int type, int *neg)
{
  UBYTE resp[DNS_CACHE_MAXRESP];
  int len = 0, hit = 0;
  ULONG now;
  struct dns_ent *e;

  *neg = 0;
  if (!dns_ready || dns_tab_n <= 0 || name == NULL)
    return NULL;

  now = dns_now();
  ObtainSemaphore(&dns_lock);
  e = ent_find(name, type, now);
  if (e != NULL) {
    if (e->negative)
      *neg = 1;					/* cached "not found" */
    else if (e->response != NULL &&
	     e->resp_len > 0 && e->resp_len <= DNS_CACHE_MAXRESP) {
      bcopy(e->response, resp, e->resp_len);	/* copy out under the lock */
      len = e->resp_len;
      hit = 1;
    }
  }
  ReleaseSemaphore(&dns_lock);

  if (hit)					/* replay OUTSIDE the lock */
    return ng_hostent_from_response(libPtr, resp, len,
				    (type == T_PTR), (unsigned long *)0);
  return NULL;
}

void
ng_dnscache_put(const char *name, int type, const void *response,
		int resp_len, unsigned long min_ttl)
{
  ULONG now, ttl;
  struct dns_ent *e;
  char *lc;
  UBYTE *rc;

  if (!dns_ready || dns_tab_n <= 0 || name == NULL ||
      response == NULL || resp_len <= 0 || resp_len > DNS_CACHE_MAXRESP)
    return;

  ttl = (min_ttl < DNS_CACHE_TTL_MIN) ? DNS_CACHE_TTL_MIN :
	(min_ttl > DNS_CACHE_TTL_MAX) ? DNS_CACHE_TTL_MAX : (ULONG)min_ttl;
  now = dns_now();

  /* Build the copies before taking the lock, so the critical section is just
   * the table update. */
  if ((lc = name_dup_lc(name)) == NULL)
    return;
  if ((rc = AllocVec((ULONG)resp_len, MEMF_PUBLIC)) == NULL) {
    FreeVec(lc);
    return;
  }
  bcopy((void *)response, rc, resp_len);

  ObtainSemaphore(&dns_lock);
  e = ent_find(name, type, now);		/* replace an existing entry, if any */
  if (e == NULL)				/* else pick a slot to (re)use */
    e = pick_slot(now);
  if (e != NULL) {
    ent_clear(e);				/* free any previous contents */
    e->name = lc;     lc = NULL;
    e->response = rc; rc = NULL;
    e->resp_len = resp_len;
    e->type = type;
    e->expiry = now + ttl;
  }
  ReleaseSemaphore(&dns_lock);

  if (lc) FreeVec(lc);				/* not stored: release the copies */
  if (rc) FreeVec(rc);
}

void
ng_dnscache_put_negative(const char *name, int type)
{
  ULONG now;
  struct dns_ent *e;
  char *lc;

  if (!dns_ready || dns_tab_n <= 0 || name == NULL)
    return;
  now = dns_now();
  if ((lc = name_dup_lc(name)) == NULL)		/* build the copy before the lock */
    return;

  ObtainSemaphore(&dns_lock);
  e = ent_find(name, type, now);		/* replace an existing entry, if any */
  if (e == NULL)
    e = pick_slot(now);
  if (e != NULL) {
    ent_clear(e);				/* frees any previous positive response */
    e->name = lc; lc = NULL;
    e->negative = 1;				/* no response stored */
    e->type = type;
    e->expiry = now + DNS_CACHE_NEG_TTL;
  }
  ReleaseSemaphore(&dns_lock);

  if (lc) FreeVec(lc);
}
