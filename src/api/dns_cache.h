#ifndef NG_DNS_CACHE_H
#define NG_DNS_CACHE_H
/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * DNS response cache -- all tunables live here so future adjustment is a
 * single-line edit.
 *
 * The cache stores the raw DNS response packet keyed by (lowercased query name,
 * record type). On a hit the stored response is replayed through the normal
 * getanswer()/makehostent() path, so a cached answer is materialised exactly as
 * a fresh one would be. Positive entries honour the answer's minimum TTL,
 * clamped between a floor and a cap. Capacity is chosen at start-up from the
 * installed-RAM tier (ng_ram_tier() sets ng_dns_cache_max), the same way the
 * socket buffers and mbuf pool are tiered -- small on a low-RAM machine, larger
 * when there is RAM to spare. There is no config knob or flush (Roadshow
 * exposes none either); entry storage is allocated on demand, so an idle cache
 * costs only its small slot table.
 *
 * Only forward lookups (gethostbyname / getaddrinfo, and their negative
 * results) are cached. Reverse lookups (gethostbyaddr / PTR) are deliberately
 * left uncached: they are rarely used, and skipping them keeps the cache lean
 * on a low-RAM machine. The get/put interface is type-agnostic, so this is a
 * wiring choice, not a limitation of the cache itself.
 */

/* Maximum cache entries, by installed-RAM tier. */
#define DNS_CACHE_ENTRIES_1MB	8	/* <= 1 MB   */
#define DNS_CACHE_ENTRIES_4MB	24	/* 2 - 4 MB  */
#define DNS_CACHE_ENTRIES_16MB	64	/* 8 - 16 MB */
#define DNS_CACHE_ENTRIES_32MB	128	/* 32 MB+    */

/* Positive entry lifetime = clamp(answer minimum TTL, floor, cap), seconds. */
#define DNS_CACHE_TTL_MIN	30UL	/* floor: don't re-query more often than this */
#define DNS_CACHE_TTL_MAX	3600UL	/* cap: bound staleness at one hour */

/* Negative (NXDOMAIN / lookup-failure) entry lifetime, seconds. */
#define DNS_CACHE_NEG_TTL	30UL

/* Largest DNS response we will store (matches the resolver's MAXPACKET). */
#define DNS_CACHE_MAXRESP	1024

struct SocketBase;
struct hostent;

/* Cache capacity for this machine; set by ng_ram_tier(). 0 disables the cache. */
extern int ng_dns_cache_max;

/* Allocate the slot table and init the lock. Call once, after ng_ram_tier(). */
void ng_dnscache_init(void);

/*
 * Look up (name, type) [type is T_A or T_PTR]. On a fresh positive hit, the
 * cached answer is materialised into libPtr's per-opener hostent buffer and
 * returned. On a fresh negative hit, returns NULL with *neg set to 1. On a miss
 * (or expiry), returns NULL with *neg 0.
 */
struct hostent *ng_dnscache_get(struct SocketBase *libPtr, const char *name,
				int type, int *neg);

/* Store a positive answer: the raw DNS response bytes plus its minimum TTL. */
void ng_dnscache_put(const char *name, int type,
		     const void *response, int resp_len, unsigned long min_ttl);

/* Store a negative (definitive "not found") result for DNS_CACHE_NEG_TTL. */
void ng_dnscache_put_negative(const char *name, int type);

/*
 * Replay a stored (or freshly received) raw DNS response into libPtr's
 * per-opener hostent buffer. Lives in gethostnamadr.c (needs the static
 * getanswer()/makehostent()); declared here for the cache. iquery is nonzero
 * for a reverse (PTR) lookup. If min_ttl_out is non-NULL it receives the
 * answer's minimum RR TTL. Returns NULL on parse failure.
 */
struct hostent *ng_hostent_from_response(struct SocketBase *libPtr,
					 void *response, int len, int iquery,
					 unsigned long *min_ttl_out);

#endif /* NG_DNS_CACHE_H */
