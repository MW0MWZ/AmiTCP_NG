/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ConfigureNetInterface -- reconfigure an already-added network interface: change its
 * address/netmask/broadcast/destination/metric/MTU, bring it up or down, take it
 * online or offline, or (re)configure it via DHCP. Our own name-and-behaviour-
 * compatible replacement for Roadshow's ConfigureNetInterface command; it uses the
 * exact same ReadArgs template so existing Roadshow scripts and tools (Roadie etc.)
 * drive it unchanged. It talks to the AmiTCP_NG stack through the public
 * bsdsocket.library extension API (ConfigureInterfaceTagList, and for CONFIGURE=dhcp
 * the CreateAddrAllocMessage/BeginInterfaceConfig pair, exactly as AddNetInterface).
 *
 * The full Roadshow template is accepted so no invocation is rejected; the options we
 * act on are ADDRESS/NETMASK/BROADCASTADDR/DESTINATION/METRIC/MTU, the state switches
 * ONLINE/OFFLINE/UP/DOWN, and CONFIGURE=dhcp (with TIMEOUT). The remaining Roadshow
 * options are parsed-and-ignored (documented as no-ops) rather than erroring out, to
 * preserve drop-in argument compatibility.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/ConfigureNetInterface.c -o ConfigureNetInterface
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "ng_lvo.h"

struct Library *SocketBase = 0;

/* Template positions (Roadshow ConfigureNetInterface, verbatim order). */
enum {
  A_INTERFACE, A_QUIET, A_ADDRESS, A_NETMASK, A_BROADCASTADDR, A_DESTINATION,
  A_METRIC, A_MTU, A_ALIASADDR, A_DELETEADDR, A_ONLINE, A_OFFLINE, A_UP, A_DOWN,
  A_DEBUG, A_COMPLETE, A_CONFIGURE, A_LEASE, A_RELEASE, A_ID, A_TIMEOUT,
  A_DHCPUNICAST, A_LINKSTATUSCOMMAND, A_COUNT
};

static const char *TEMPLATE =
  "INTERFACE/A,QUIET/S,ADDRESS/K,NETMASK/K,BROADCASTADDR/K,"
  "DESTINATION=DESTINATIONADDR/K,METRIC/K/N,MTU/K/N,ALIASADDR/K,DELETEADDR/K,"
  "ONLINE/S,OFFLINE/S,UP/S,DOWN/S,DEBUG/K,COMPLETE/K,CONFIGURE/K,LEASE/K,"
  "RELEASE=RELEASEADDRESS/S,ID/K,TIMEOUT/K/N,DHCPUNICAST/K,LINKSTATUSCOMMAND/K";

/* case-insensitive "is this string equal to lit?" for the CONFIGURE keyword */
static int eqi(const char *s, const char *lit)
{
  if (!s) return 0;
  while (*s && *lit) {
    char a = *s++, b = *lit++;
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b) return 0;
  }
  return *s == 0 && *lit == 0;
}

static long do_dhcp(const char *ifname, long timeout, int quiet)
{
  struct MsgPort *port;
  struct ng_aamx *aam = 0;
  struct TagItem  tg[4];
  long r;

  port = CreateMsgPort();
  if (!port) { if (!quiet) Printf((STRPTR)"%s: no message port\n", (LONG)ifname); return RETURN_FAIL; }

  tg[0].ti_Tag = CAAMTA_RouterTableSize; tg[0].ti_Data = 1;
  tg[1].ti_Tag = CAAMTA_DNSTableSize;    tg[1].ti_Data = 2;
  tg[2].ti_Tag = CAAMTA_ReplyPort;       tg[2].ti_Data = (ULONG)port;
  tg[3].ti_Tag = TAG_END;                tg[3].ti_Data = 0;

  r = ng_createaam(NG_AAM_VERSION, NG_AAMP_DHCP, ifname, (void **)&aam, tg);
  if (r != 0 || !aam) {
    if (!quiet) Printf((STRPTR)"%s: CreateAddrAllocMessage failed, errno %ld\n", (LONG)ifname, ng_errno());
    DeleteMsgPort(port);
    return r ? r : RETURN_FAIL;
  }
  aam->aam_Timeout = (timeout > 0) ? timeout : 30;

  if (!quiet) Printf((STRPTR)"%s: requesting an address via DHCP...\n", (LONG)ifname);
  ng_begincfg(aam);
  WaitPort(port);
  (void)GetMsg(port);

  if (aam->aam_Result == 0 && aam->aam_Address != 0) {
    if (!quiet) {
      unsigned long a = aam->aam_Address;
      Printf((STRPTR)"%s: up, address %ld.%ld.%ld.%ld (DHCP)\n", (LONG)ifname,
             (a>>24)&0xFF, (a>>16)&0xFF, (a>>8)&0xFF, a&0xFF);
    }
    r = RETURN_OK;
  } else {
    if (!quiet) Printf((STRPTR)"%s: DHCP failed (result %ld)\n", (LONG)ifname, aam->aam_Result);
    r = RETURN_ERROR;
  }
  DeleteMsgPort(port);
  return r;
}

int main(void)
{
  struct RDArgs *rda;
  LONG    args[A_COUNT];
  STRPTR  ifname;
  int     quiet;
  long    rc = RETURN_OK;
  int     i;

  for (i = 0; i < A_COUNT; i++) args[i] = 0;

  rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)"ConfigureNetInterface"); return RETURN_ERROR; }
  ifname = (STRPTR)args[A_INTERFACE];
  quiet  = args[A_QUIET] ? 1 : 0;

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    if (!quiet) Printf((STRPTR)"ConfigureNetInterface: bsdsocket.library v4+ not available.\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  /* CONFIGURE=dhcp -> re-lease via DHCP; other CONFIGURE values are accepted but a
   * no-op here (Roadshow's built-in configurators). */
  if (args[A_CONFIGURE]) {
    if (eqi((const char *)args[A_CONFIGURE], "dhcp")) {
      long to = args[A_TIMEOUT] ? *(LONG *)args[A_TIMEOUT] : 0;
      rc = do_dhcp((const char *)ifname, to, quiet);
    } else if (!quiet) {
      Printf((STRPTR)"ConfigureNetInterface: CONFIGURE=%s not supported (ignored).\n",
             (LONG)args[A_CONFIGURE]);
    }
  } else {
    struct TagItem tg[8];
    int  n = 0;
    long state = -1;

    if (args[A_ADDRESS])       { tg[n].ti_Tag = IFC_Address;             tg[n].ti_Data = (ULONG)args[A_ADDRESS];       n++; }
    if (args[A_NETMASK])       { tg[n].ti_Tag = IFC_NetMask;             tg[n].ti_Data = (ULONG)args[A_NETMASK];       n++; }
    if (args[A_BROADCASTADDR]) { tg[n].ti_Tag = IFC_BroadcastAddress;    tg[n].ti_Data = (ULONG)args[A_BROADCASTADDR]; n++; }
    if (args[A_DESTINATION])   { tg[n].ti_Tag = IFC_DestinationAddress;  tg[n].ti_Data = (ULONG)args[A_DESTINATION];   n++; }
    if (args[A_METRIC])        { tg[n].ti_Tag = IFC_Metric;              tg[n].ti_Data = *(LONG *)args[A_METRIC];      n++; }
    if (args[A_MTU])           { tg[n].ti_Tag = IFC_MTU;                 tg[n].ti_Data = *(LONG *)args[A_MTU];         n++; }

    /* State switches: UP/DOWN drive the interface up flag, ONLINE/OFFLINE the SANA-II
     * service state. A single IFC_State carries the strongest request. */
    if (args[A_UP])           state = NG_SM_Up;
    else if (args[A_DOWN])    state = NG_SM_Down;
    else if (args[A_ONLINE])  state = NG_SM_Online;
    else if (args[A_OFFLINE]) state = NG_SM_Offline;
    if (state >= 0) { tg[n].ti_Tag = IFC_State; tg[n].ti_Data = (ULONG)state; n++; }

    tg[n].ti_Tag = TAG_END; tg[n].ti_Data = 0;

    if (n == 0) {
      if (!quiet) Printf((STRPTR)"ConfigureNetInterface: nothing to do for '%s'.\n", (LONG)ifname);
    } else if (ng_configif((void *)ifname, tg) != 0) {
      if (!quiet) Printf((STRPTR)"ConfigureNetInterface: '%s' failed, errno %ld\n", (LONG)ifname, ng_errno());
      rc = RETURN_ERROR;
    } else if (!quiet) {
      Printf((STRPTR)"%s: reconfigured.\n", (LONG)ifname);
    }
  }

  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
