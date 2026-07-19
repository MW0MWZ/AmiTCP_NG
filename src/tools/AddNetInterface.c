/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * AddNetInterface -- bring up a network interface on the AmiTCP_NG stack from a
 * Roadshow-style interface configuration file.
 *
 * This is our own, freely-distributable equivalent of Roadshow's `AddNetInterface`
 * command. It reads the SAME simple keyword=value configuration files Roadshow uses
 * (so existing DEVS:NetInterfaces/ files work unchanged) and drives the interface up
 * through the public bsdsocket.library extension API -- the very calls proved out by
 * the nictest/dhcptest harness (AddInterfaceTagList + ConfigureInterfaceTagList for
 * static, AddInterfaceTagList + CreateAddrAllocMessage + BeginInterfaceConfig for
 * DHCP). The interface NAME is the config file's own name (e.g.
 * DEVS:NetInterfaces/eth0 -> interface "eth0"), matching Roadshow's convention.
 *
 * Usage:   AddNetInterface <interface>... [QUIET] [TIMEOUT=<seconds>]
 *   e.g.   AddNetInterface wifipi                (bare name -> DEVS:NetInterfaces/wifipi)
 *          AddNetInterface DEVS:NetInterfaces/A2065
 *          AddNetInterface DEVS:NetInterfaces/~(#?.info) QUIET   (Network-Startup form)
 * A bare interface name (no path) is looked up like Roadshow's AddNetInterface: in the
 * current directory, then DEVS:NetInterfaces, then SYS:Storage/NetInterfaces.
 * The argument template matches Roadshow's AddNetInterface (INTERFACE/M,QUIET/S,
 * TIMEOUT/K/N) so an existing Roadshow S:Network-Startup drives this unchanged.
 *
 * Recognised configuration keywords (Roadshow-compatible; `=` or whitespace between
 * keyword and value, `#' begins a comment):
 *   device=<name.device>      SANA-II driver (bare name -> DEVS:Networks/<name>)
 *   unit=<n>                  device unit number (default 0)
 *   configure=dhcp|none       dhcp = obtain the address via DHCP; none/omitted =
 *                             static (needs address=) or leave unconfigured
 *   address=<dotted-quad>     static IP address
 *   netmask=<dotted-quad>     static subnet mask
 *   gateway=<dotted-quad>     static default route gateway
 *   nameserver=<dotted-quad>  add a DNS server (may appear more than once)
 *   domain=<name>             default domain name
 *   requiresinitdelay=yes|no  pause after opening the device before configuring it
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 src/tools/AddNetInterface.c -o AddNetInterface
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase;

/* --- Roadshow extension-API tag values (see amiga_roadshow.c) --------------- */
#define TU                      0x80000000UL	/* TAG_USER */
#define IFC_Address             (TU + 1801)
#define IFC_NetMask             (TU + 1802)
#define IFC_State               (TU + 1808)
#define SM_Up                   3
#define RTA_DefaultGateway      (TU + 1603)
#define CAAMTA_RouterTableSize  (TU + 2006)
#define CAAMTA_DNSTableSize     (TU + 2007)
#define CAAMTA_ReplyPort        (TU + 2013)
#define AAMP_DHCP               1
#define AAM_VERSION             2
#define NG_EADDRINUSE           48	/* errno: interface of this name already exists */

/* --- direct bsdsocket vector calls (LVOs, bias 30). The scratch address/data
 *     registers an inlined caller might reuse are marked "+r" (in-out) so -O2
 *     never keeps a stale copy across the jsr -- the recurring test-harness gotcha. */
static long v_addif(void *nm, void *dev, long unit, void *tags) {          /* AddInterfaceTagList -444 */
  register long _d0 __asm("d0")=unit; register void *_a0 __asm("a0")=nm;
  register void *_a1 __asm("a1")=dev; register void *_a2 __asm("a2")=tags;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-444)":"+r"(_d0),"+r"(_a0),"+r"(_a1),"+r"(_a2):"r"(_a6):"d1","memory");
  return _d0;
}
/* NB: RemoveInterface (unlike the other vectors here) returns a BOOL -- non-zero =
 * SUCCESS, 0 = failure -- the OPPOSITE of v_addif()'s 0-on-success. add_iface()
 * deliberately ignores the result (see there), so this asymmetry is harmless today;
 * a future caller inspecting it must NOT treat 0 as success. */
static long v_removeif(void *nm, long force) {                             /* RemoveInterface -732 (a0,d0) */
  register long _d0 __asm("d0")=force; register void *_a0 __asm("a0")=nm;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-732)":"+r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
static long v_addroute(void *tags) {                                       /* AddRouteTagList -414 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=tags;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-414)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
static long v_adddns(void *addr) {                                         /* AddDomainNameServer -516 */
  register long _d0 __asm("d0"); register void *_a0 __asm("a0")=addr;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-516)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory");
  return _d0;
}
static void v_setdomain(void *name) {                                      /* SetDefaultDomainName -708 */
  register void *_a0 __asm("a0")=name; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-708)":"+r"(_a0):"r"(_a6):"d0","d1","a1","memory");
}
static long v_createaam(long ver, long proto, const char *ifn, void **res, void *tags) { /* CreateAddrAllocMessageA -474 */
  register long _d0 __asm("d0")=ver; register long _d1 __asm("d1")=proto;
  register const char *_a0 __asm("a0")=ifn; register void **_a1 __asm("a1")=res;
  register void *_a2 __asm("a2")=tags; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-474)":"+r"(_d0),"+r"(_d1),"+r"(_a0),"+r"(_a1):"r"(_a2),"r"(_a6):"memory");
  return _d0;
}
static void v_begincfg(void *aam) {                                        /* BeginInterfaceConfig -486 */
  register void *_a0 __asm("a0")=aam; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-486)":"+r"(_a0):"r"(_a6):"d0","d1","a1","memory");
}
static long v_errno(void) {                                                /* Errno -162 */
  register long _d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory");
  return _d0;
}

/* AddressAllocationMessage -- struct Message (20-byte prefix) then the fields we
 * touch. Layout mirrors amiga_roadshow.c's `struct ng_aam`. */
struct AAMX {
  char mn[20];
  long aam_Reserved, aam_Result, aam_Version, aam_Protocol;
  char aam_InterfaceName[16];
  long aam_Timeout;
  unsigned long aam_LeaseTime, aam_RequestedAddress;
  char *aam_ClientIdentifier;
  unsigned long aam_Address, aam_ServerAddress, aam_SubnetMask;
};

/* ---- tiny helpers (avoid dragging in stdio) -------------------------------- */
#define MAXNS 6
#define VALLEN 64
#define LINELEN 256

static int  ci_eq(const char *a, const char *b) {           /* ASCII case-insensitive equal */
  for (; *a && *b; a++, b++) {
    int ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return 0;
  }
  return *a == *b;
}
static void s_copy(char *d, const char *s, int max) {        /* bounded strcpy */
  int i = 0;
  while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

/* Parsed interface configuration. */
struct ifcfg {
  char device[VALLEN];
  long unit;
  int  dhcp;                 /* configure=dhcp */
  int  have_address;
  char address[VALLEN];
  char netmask[VALLEN];
  char gateway[VALLEN];
  char domain[VALLEN];
  char ns[MAXNS][VALLEN];
  int  nns;
  int  initdelay;
};

/* Parse one keyword=value (or "keyword value") line into cfg. */
static void parse_line(char *line, struct ifcfg *cfg)
{
  char *p = line, *kw, *val;
  int   n;

  while (*p == ' ' || *p == '\t') p++;
  if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n' || *p == '\r')
    return;                                  /* blank / comment */

  kw = p;
  while (*p && *p != '=' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
    p++;
  if (*p) { *p = '\0'; p++; }
  while (*p == '=' || *p == ' ' || *p == '\t') p++;      /* skip separator(s) */

  val = p;
  while (*p && *p != '#' && *p != ';' && *p != '\n' && *p != '\r') p++;
  while (p > val && (p[-1] == ' ' || p[-1] == '\t')) p--; /* rstrip */
  *p = '\0';

  if      (ci_eq(kw, "device"))    s_copy(cfg->device, val, VALLEN);
  else if (ci_eq(kw, "unit"))      { cfg->unit = 0; for (n=0; val[n] >= '0' && val[n] <= '9'; n++) cfg->unit = cfg->unit*10 + (val[n]-'0'); }
  else if (ci_eq(kw, "configure")) cfg->dhcp = ci_eq(val, "dhcp");
  else if (ci_eq(kw, "address"))   { s_copy(cfg->address, val, VALLEN); cfg->have_address = 1; }
  else if (ci_eq(kw, "netmask"))   s_copy(cfg->netmask, val, VALLEN);
  else if (ci_eq(kw, "gateway"))   s_copy(cfg->gateway, val, VALLEN);
  else if (ci_eq(kw, "domain"))    s_copy(cfg->domain, val, VALLEN);
  else if (ci_eq(kw, "nameserver")){ if (cfg->nns < MAXNS) s_copy(cfg->ns[cfg->nns++], val, VALLEN); }
  else if (ci_eq(kw, "requiresinitdelay")) cfg->initdelay = ci_eq(val, "yes");
  /* unknown keywords are ignored (forward-compatible, like Roadshow) */
}

/* Read + parse a whole interface config file. Returns 1 on success. */
static int read_cfg(const char *path, struct ifcfg *cfg)
{
  char  line[LINELEN];
  BPTR  fh;

  cfg->unit = 0; cfg->dhcp = 0; cfg->have_address = 0; cfg->nns = 0; cfg->initdelay = 0;
  cfg->device[0] = cfg->address[0] = cfg->netmask[0] = cfg->gateway[0] = cfg->domain[0] = '\0';

  fh = Open((STRPTR)path, MODE_OLDFILE);
  if (!fh) return 0;
  while (FGets(fh, (STRPTR)line, LINELEN))
    parse_line(line, cfg);
  Close(fh);
  return cfg->device[0] != '\0';
}

/* Return a pointer to the file-name part of a path (after the last / or :). */
static const char *file_part(const char *path)
{
  const char *p = path, *last = path;
  for (; *p; p++)
    if (*p == '/' || *p == ':') last = p + 1;
  return last;
}

/* Add the interface; if one of this name already exists (EADDRINUSE), tear it
 * down and recreate it. Roadshow's AddNetInterface just fails when the interface
 * is already present, but our Online flow re-runs AddNetInterface to come back up
 * after an Offline (which only de-configures the interface -- it stays registered,
 * with its SANA-II device still bound). Re-configuring that half-alive interface
 * was unreliable, so a clean teardown+recreate -- force-remove (closes the device
 * and frees the interface) then add fresh (reopens the device, re-posts reads) --
 * gives the following DHCP a known-good starting point. Returns the add's result. */
static long add_iface(const char *ifname, void *dev, long unit, void *tags, int quiet)
{
  long r = v_addif((void *)ifname, dev, unit, tags);
  if (r != 0 && v_errno() == NG_EADDRINUSE) {
    if (!quiet)
      Printf((STRPTR)"%s: interface already present -- tearing it down and recreating\n",
             (LONG)ifname);
    /* force=1: remove even if still up. Result ignored on purpose: a force-remove
     * of an interface we just found cannot fail in this stack, and if it somehow
     * did not remove, the retry v_addif() below simply reports EADDRINUSE again --
     * no worse than before. (v_removeif's BOOL return is inverted -- see there.) */
    (void)v_removeif((void *)ifname, 1);
    r = v_addif((void *)ifname, dev, unit, tags);
  }
  return r;
}

/* Bring one interface up from its parsed config. Returns 0 on success.
 * timeout (seconds, 0 = default) is the DHCP wait, from the TIMEOUT argument. */
static long bring_up(const char *ifname, struct ifcfg *cfg, int quiet, long timeout)
{
  char  devpath[VALLEN];
  long  r, i;

  /* Pass the driver name to the stack exactly as the config gives it. The stack
   * resolves it (bare name -> a resident driver like wifipi.device; else the
   * DEVS:Networks/ file, like a2065.device -- see iface_make in net/if_sana.c).
   * Forcing a DEVS:Networks/ prefix here reloaded an already-resident driver and
   * failed with ENXIO, so we no longer do that. */
  s_copy(devpath, cfg->device, sizeof(devpath));

  if (cfg->dhcp) {
    /* --- DHCP: add the interface unaddressed, then let the stack lease one. --- */
    struct MsgPort *port;
    struct AAMX    *aam = 0;
    struct TagItem  tg[4];

    r = add_iface(ifname, devpath, cfg->unit, (void *)0, quiet);
    if (r != 0) { if (!quiet) Printf((STRPTR)"%s: AddInterface failed, errno %ld\n", (LONG)ifname, v_errno()); return r; }
    if (cfg->initdelay) Delay(150);          /* ~3s device warm-up */

    port = CreateMsgPort();
    if (!port) { if (!quiet) Printf((STRPTR)"%s: no message port\n", (LONG)ifname); return 20; }

    tg[0].ti_Tag = CAAMTA_RouterTableSize; tg[0].ti_Data = 1;
    tg[1].ti_Tag = CAAMTA_DNSTableSize;    tg[1].ti_Data = 2;
    tg[2].ti_Tag = CAAMTA_ReplyPort;       tg[2].ti_Data = (ULONG)port;
    tg[3].ti_Tag = TAG_END;                tg[3].ti_Data = 0;
    r = v_createaam(AAM_VERSION, AAMP_DHCP, ifname, (void **)&aam, tg);
    if (r != 0 || !aam) { if (!quiet) Printf((STRPTR)"%s: CreateAddrAllocMessage failed, errno %ld\n", (LONG)ifname, v_errno());
                          DeleteMsgPort(port); return r ? r : 20; }
    aam->aam_Timeout = (timeout > 0) ? timeout : 30;

    if (!quiet) Printf((STRPTR)"%s: requesting an address via DHCP...\n", (LONG)ifname);
    v_begincfg(aam);
    WaitPort(port);
    (void)GetMsg(port);

    if (aam->aam_Result == 0 && aam->aam_Address != 0) {
      if (!quiet) {
        unsigned long a = aam->aam_Address;
        Printf((STRPTR)"%s: up, address %ld.%ld.%ld.%ld (DHCP)\n", (LONG)ifname,
               (a>>24)&0xFF, (a>>16)&0xFF, (a>>8)&0xFF, a&0xFF);
      }
      r = 0;
    } else {
      if (!quiet) Printf((STRPTR)"%s: DHCP failed (result %ld)\n", (LONG)ifname, aam->aam_Result);
      r = 20;
    }
    DeleteMsgPort(port);
    return r;
  } else {
    /* --- Static (or unconfigured if no address given). --------------------- */
    struct TagItem it[4];
    int n = 0;

    if (cfg->have_address) {
      it[n].ti_Tag = IFC_Address; it[n].ti_Data = (ULONG)cfg->address; n++;
      if (cfg->netmask[0]) { it[n].ti_Tag = IFC_NetMask; it[n].ti_Data = (ULONG)cfg->netmask; n++; }
      it[n].ti_Tag = IFC_State; it[n].ti_Data = SM_Up; n++;
    }
    it[n].ti_Tag = TAG_END; it[n].ti_Data = 0;

    r = add_iface(ifname, devpath, cfg->unit, cfg->have_address ? (void *)it : (void *)0, quiet);
    if (r != 0) { if (!quiet) Printf((STRPTR)"%s: AddInterface failed, errno %ld\n", (LONG)ifname, v_errno()); return r; }
    if (cfg->initdelay) Delay(150);

    if (cfg->gateway[0]) {
      struct TagItem rt[2];
      rt[0].ti_Tag = RTA_DefaultGateway; rt[0].ti_Data = (ULONG)cfg->gateway;
      rt[1].ti_Tag = TAG_END;            rt[1].ti_Data = 0;
      if (v_addroute(rt) != 0 && !quiet) Printf((STRPTR)"%s: default route failed, errno %ld\n", (LONG)ifname, v_errno());
    }
    for (i = 0; i < cfg->nns; i++)
      if (v_adddns(cfg->ns[i]) != 0 && !quiet) Printf((STRPTR)"%s: nameserver %ld failed\n", (LONG)ifname, i);
    if (cfg->domain[0]) v_setdomain(cfg->domain);

    if (!quiet) {
      if (cfg->have_address) Printf((STRPTR)"%s: up, address %s\n", (LONG)ifname, (LONG)cfg->address);
      else                   Printf((STRPTR)"%s: interface added (unconfigured)\n", (LONG)ifname);
    }
    return 0;
  }
}

/* Match one path/pattern and bring up every interface config it names. Returns the
 * number of config files matched (0 = the pattern matched nothing on disk).
 * A bare name with no AmigaDOS wildcard characters matches its own literal file, so
 * this same MatchFirst path doubles as the literal lookup used for the bare-name
 * DEVS:NetInterfaces/ fallback below (real interface names carry no pattern
 * metacharacters -- Roadshow probes bare names with a literal Lock(), we unify both). */
static int try_pattern(const char *pat, struct AnchorPath *ap, int quiet, long timeout, int *rc)
{
  int  matched = 0;
  LONG err;

  ap->ap_Strlen   = 256;
  ap->ap_BreakBits = SIGBREAKF_CTRL_C;    /* let Ctrl-C interrupt a slow drawer scan */
  for (err = MatchFirst((STRPTR)pat, ap); err == 0; err = MatchNext(ap)) {
    struct ifcfg cfg;
    const char  *ifname;
    if (ap->ap_Info.fib_DirEntryType > 0) continue;   /* skip directories */
    matched++;
    if (!read_cfg((const char *)ap->ap_Buf, &cfg)) {
      if (!quiet) Printf((STRPTR)"%s: no usable interface config (missing 'device=')\n", (LONG)ap->ap_Buf);
      *rc = RETURN_WARN;
      continue;
    }
    ifname = file_part((const char *)ap->ap_Buf);
    if (bring_up(ifname, &cfg, quiet, timeout) != 0)
      *rc = RETURN_WARN;
  }
  MatchEnd(ap);
  return matched;
}

int main(void)
{
  struct RDArgs   *rda;
  LONG             args[3] = { 0, 0, 0 };    /* INTERFACE/M, QUIET/S, TIMEOUT/K/N */
  struct AnchorPath *ap;
  STRPTR          *iflist;
  int              quiet, rc = RETURN_OK;
  long             timeout;

  /* Same argument template as Roadshow's AddNetInterface, so scripts written for it
   * (e.g. an existing S:Network-Startup) invoke this drop-in unchanged: one or more
   * interface config files/patterns, an optional QUIET switch, an optional TIMEOUT. */
  rda = ReadArgs((STRPTR)"INTERFACE/M,QUIET/S,TIMEOUT/K/N", args, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)"AddNetInterface"); return RETURN_ERROR; }
  iflist  = (STRPTR *)args[0];
  quiet   = args[1] != 0;
  timeout = args[2] ? *(LONG *)args[2] : 0;

  if (iflist == NULL || *iflist == NULL) {
    Printf((STRPTR)"AddNetInterface: no interface specified.\n"
           "Usage: AddNetInterface <config-file>... [QUIET] [TIMEOUT=<seconds>]\n"
           "  e.g. AddNetInterface DEVS:NetInterfaces/eth0\n");
    FreeArgs(rda);
    return RETURN_WARN;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 3L);
  if (!SocketBase) {
    if (!quiet) Printf((STRPTR)"AddNetInterface: cannot open bsdsocket.library\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  ap = AllocVec(sizeof(struct AnchorPath) + 256, MEMF_CLEAR);
  if (!ap) { CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL; }

  /* Each INTERFACE argument is either a path/pattern (a plain path matches itself;
   * `DEVS:NetInterfaces/~(#?.info)` enumerates the whole drawer -- the Network-Startup
   * form) or a bare interface name. Missing files are reported, not silently ignored. */
  for (; *iflist != NULL; iflist++) {
    STRPTR arg = *iflist;
    int    matched;

    /* First take the argument exactly as given (a full path or a pattern). */
    matched = try_pattern((const char *)arg, ap, quiet, timeout, &rc);

    /* A bare interface NAME (no path part) that matched nothing in the current
     * directory is resolved the way Roadshow's AddNetInterface does it: look in
     * DEVS:NetInterfaces, then SYS:Storage/NetInterfaces. So `AddNetInterface wifipi`
     * finds `DEVS:NetInterfaces/wifipi`. */
    if (matched == 0 && FilePart(arg) == arg) {
      char path[256];
      s_copy(path, "DEVS:NetInterfaces", sizeof(path));
      AddPart((STRPTR)path, arg, sizeof(path));
      matched = try_pattern(path, ap, quiet, timeout, &rc);
      if (matched == 0) {
        s_copy(path, "SYS:Storage/NetInterfaces", sizeof(path));
        AddPart((STRPTR)path, arg, sizeof(path));
        matched = try_pattern(path, ap, quiet, timeout, &rc);
      }
    }

    if (matched == 0) {
      if (!quiet) Printf((STRPTR)"AddNetInterface: no interface configuration matched \"%s\"\n", (LONG)arg);
      rc = RETURN_WARN;
    }
  }

  FreeVec(ap);
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
