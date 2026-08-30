/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * The interface-configuration file parser. See net/ng_ifconfig.h for why this is
 * one shared implementation rather than a copy in each program that needs it.
 *
 * Deliberately free of anything but DOS and plain C: it is linked into the tools
 * (which have no stack) and into the library (which must not drag tool code in).
 */

#include <exec/types.h>
#include <dos/dos.h>

/* proto/dos.h, not inline/dos.h: the inline header uses DOSBase without declaring
 * it, which is fine inside the library (it defines its own) but not in a tool. This
 * file is linked into both. */
#include <proto/dos.h>

#include <net/ng_ifconfig.h>

/* ---- tiny helpers (avoid dragging in stdio) -------------------------------- */

static int ci_eq(const char *a, const char *b)		/* ASCII case-insensitive equal */
{
  for (; *a && *b; a++, b++) {
    int ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return 0;
  }
  return *a == *b;
}

static void s_copy(char *d, const char *s, int max)	/* bounded strcpy */
{
  int i = 0;
  while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

/* Parse a non-negative decimal from val, stopping at the first non-digit. */
static long ng_atol(const char *val)
{
  long v = 0; int n;
  for (n = 0; val[n] >= '0' && val[n] <= '9'; n++) v = v * 10 + (val[n] - '0');
  return v;
}

void
ng_ifcfg_clear(struct ng_ifcfg *cfg)
{
  /*
   * EVERY field, every time. This struct is routinely an auto-local reused across
   * several files in one run, so a field left out of here does not read as
   * "garbage" -- it reads as the PREVIOUS interface's value. Leaving bps= out once
   * gave an interface whose own config never mentions it the link speed of the one
   * configured before it.
   */
  cfg->unit = 0;
  cfg->dhcp = 0;
  cfg->have_address = 0;
  cfg->nns = 0;
  cfg->initdelay = 0;
  cfg->ipreq = 0;
  cfg->wreq = 0;
  cfg->mtu = 0;
  cfg->sendspace = 0;
  cfg->recvspace = 0;
  cfg->mssdflt = 0;
  cfg->bps = 0;
  cfg->device[0] = cfg->address[0] = cfg->netmask[0] = '\0';
  cfg->gateway[0] = cfg->domain[0] = '\0';
}

void
ng_ifcfg_parse_line(char *line, struct ng_ifcfg *cfg)
{
  char *p = line, *kw, *val;
  int   n;

  while (*p == ' ' || *p == '\t') p++;
  if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n' || *p == '\r')
    return;					/* blank / comment */

  kw = p;
  while (*p && *p != '=' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
    p++;
  if (*p) { *p = '\0'; p++; }
  while (*p == '=' || *p == ' ' || *p == '\t') p++;	/* skip separator(s) */

  val = p;
  while (*p && *p != '#' && *p != ';' && *p != '\n' && *p != '\r') p++;
  while (p > val && (p[-1] == ' ' || p[-1] == '\t')) p--;	/* rstrip */
  *p = '\0';

  if      (ci_eq(kw, "device"))    s_copy(cfg->device, val, NG_IFCFG_VALLEN);
  else if (ci_eq(kw, "unit"))      { cfg->unit = 0; for (n=0; val[n] >= '0' && val[n] <= '9'; n++) cfg->unit = cfg->unit*10 + (val[n]-'0'); }
  else if (ci_eq(kw, "configure")) cfg->dhcp = ci_eq(val, "dhcp");
  else if (ci_eq(kw, "address"))   { s_copy(cfg->address, val, NG_IFCFG_VALLEN); cfg->have_address = 1; }
  else if (ci_eq(kw, "netmask"))   s_copy(cfg->netmask, val, NG_IFCFG_VALLEN);
  else if (ci_eq(kw, "gateway"))   s_copy(cfg->gateway, val, NG_IFCFG_VALLEN);
  else if (ci_eq(kw, "domain"))    s_copy(cfg->domain, val, NG_IFCFG_VALLEN);
  else if (ci_eq(kw, "nameserver")){ if (cfg->nns < NG_IFCFG_MAXNS) s_copy(cfg->ns[cfg->nns++], val, NG_IFCFG_VALLEN); }
  else if (ci_eq(kw, "requiresinitdelay")) cfg->initdelay = ci_eq(val, "yes");
  /* Clamp to 65535: the stack stores these in a UWORD ring field, so an oversized
   * value would otherwise silently wrap (e.g. 100000 -> 34464) with no diagnostic. */
  else if (ci_eq(kw, "iprequests"))    { cfg->ipreq = 0; for (n=0; val[n] >= '0' && val[n] <= '9'; n++) cfg->ipreq = cfg->ipreq*10 + (val[n]-'0'); if (cfg->ipreq > 65535) cfg->ipreq = 65535; }
  else if (ci_eq(kw, "writerequests")) { cfg->wreq  = 0; for (n=0; val[n] >= '0' && val[n] <= '9'; n++) cfg->wreq  = cfg->wreq*10 + (val[n]-'0'); if (cfg->wreq  > 65535) cfg->wreq  = 65535; }
  /* mtu= (bytes): the stack stores if_mtu in a short and clamps to the device MTU, so
   * cap at 32767 here to keep the value non-negative in the tag. */
  else if (ci_eq(kw, "mtu"))           { cfg->mtu = ng_atol(val); if (cfg->mtu > 32767) cfg->mtu = 32767; }
  /* tcp.sendspace= / tcp.recvspace= (bytes): override the RAM-tiered socket buffers.
   * Cap at 1 MB -- far above any sensible 68k window and enough headroom to raise
   * sb_max in the stack without risking an absurd allocation. */
  else if (ci_eq(kw, "tcp.sendspace")) { cfg->sendspace = ng_atol(val); if (cfg->sendspace > 1048576) cfg->sendspace = 1048576; }
  else if (ci_eq(kw, "tcp.recvspace")) { cfg->recvspace = ng_atol(val); if (cfg->recvspace > 1048576) cfg->recvspace = 1048576; }
  /* tcp.mssdflt= (bytes): off-subnet MSS cap; 0 = auto (interface MTU - 40). Cap at
   * 65535 -- t_maxseg is a u_short in the stack, so a larger value would truncate. */
  else if (ci_eq(kw, "tcp.mssdflt"))   { cfg->mssdflt = ng_atol(val); if (cfg->mssdflt > 65535) cfg->mssdflt = 65535; }
  /* bps= (bits/sec): override the link speed the SANA-II driver reports, which is what
   * the stack's TCP window auto-tune and SANA-II ring sizing are both driven from. For a
   * driver that reports the wrong figure, or none at all. 0/absent = believe the driver.
   * Capped at 2 Gbit: the stack holds if_baudrate in an int, so a larger value would go
   * negative there. The <0 test catches ng_atol() wrapping on an over-long digit run --
   * a negative baud reaching ng_effective_window()'s unsigned divide would size a ring
   * from a colossal number. */
  else if (ci_eq(kw, "bps"))           { cfg->bps = ng_atol(val);
					 if (cfg->bps < 0) cfg->bps = 0;
					 else if (cfg->bps > 2000000000L) cfg->bps = 2000000000L; }
  /* unknown keywords are ignored (forward-compatible, like Roadshow) -- see the
   * header for why, and for the obligation that comes with it. */
}

int
ng_ifcfg_read_path(const char *path, struct ng_ifcfg *cfg)
{
  char line[NG_IFCFG_LINELEN];
  BPTR fh;

  ng_ifcfg_clear(cfg);

  fh = Open((STRPTR)path, MODE_OLDFILE);
  if (!fh)
    return 0;
  while (FGets(fh, (STRPTR)line, NG_IFCFG_LINELEN))
    ng_ifcfg_parse_line(line, cfg);
  Close(fh);
  return cfg->device[0] != '\0';
}

int
ng_ifcfg_find(const char *ifname, struct ng_ifcfg *cfg)
{
  static const char *dirs[2] = { "DEVS:NetInterfaces/", "SYS:Storage/NetInterfaces/" };
  char path[NG_IFCFG_VALLEN + 40];
  int  d, i, k, pass;

  if (ifname == NULL || ifname[0] == '\0') {
    ng_ifcfg_clear(cfg);
    return 0;
  }

  /*
   * The interface is NOT necessarily named after its file.
   *
   * A config file called `smoke` produces an interface the stack calls `smoke0`:
   * ifunit() splits a name into a base and a TRAILING RUN OF DIGITS, and
   * AddNetInterface derives the interface name from the file name plus the unit.
   * Going the other way -- which is what the stack has to do when a device comes
   * back and all it has is the interface name -- means trying the name as given
   * and then again with those trailing digits removed.
   *
   * Exact match first, deliberately: a site may genuinely have a file called
   * `eth0`, and that file must win over `eth` if both exist.
   */
  for (pass = 0; pass < 2; pass++) {
    int namelen = 0;

    while (ifname[namelen])
      namelen++;
    if (pass == 1) {
      /* Strip the trailing unit digits, exactly as ifunit() finds them. */
      while (namelen > 0 && ifname[namelen - 1] >= '0' && ifname[namelen - 1] <= '9')
	namelen--;
      if (namelen == 0)
	break;			/* a name that is ALL digits has no base to try */
    }

    for (d = 0; d < 2; d++) {
      k = 0;
      for (i = 0; dirs[d][i] && k < (int)sizeof(path) - 1; i++)
	path[k++] = dirs[d][i];
      for (i = 0; i < namelen && k < (int)sizeof(path) - 1; i++)
	path[k++] = ifname[i];
      path[k] = '\0';

      if (ng_ifcfg_read_path(path, cfg))
	return 1;
    }
    /* Nothing to strip means the second pass would repeat the first. */
    if (pass == 0) {
      int base = namelen;
      while (base > 0 && ifname[base - 1] >= '0' && ifname[base - 1] <= '9')
	base--;
      if (base == namelen)
	break;
    }
  }
  ng_ifcfg_clear(cfg);		/* a partial parse must not look like a good one */
  return 0;
}
