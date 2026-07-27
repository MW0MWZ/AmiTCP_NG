/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * GetNetStatus -- report whether the TCP/IP stack is up and which facilities are
 * configured (interfaces, point-to-point/broadcast interfaces, name resolution,
 * routing, default route). Our own name-and-behaviour-compatible replacement for
 * Roadshow's GetNetStatus; it uses the same ReadArgs template (CHECK/K,QUIET/S), the
 * same SBTC_SYSTEM_STATUS query and the same wording and return codes, so scripts and
 * front-ends such as Roadie -- which run `GetNetStatus >RAM:getnetstatus` and read the
 * result -- work against the AmiTCP_NG stack unchanged.
 *
 *   GetNetStatus                     print the human-readable status summary
 *   GetNetStatus >file               (same, redirected) -- what Roadie parses
 *   GetNetStatus CHECK INTERFACES    RETURN_WARN unless interfaces are configured
 *   GetNetStatus CHECK "INTERFACES DEFAULTGATEWAY" QUIET   silent multi-condition test
 *   GetNetStatus CHECK ?             list the CHECK condition keywords
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/GetNetStatus.c -o GetNetStatus
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <utility/tagitem.h>

#include "ng_lvo.h"

struct Library *SocketBase = 0;
extern struct ExecBase *SysBase;

/*
 * DEBUG switch: show what the stack's RAM-aware auto-tuning computes on THIS machine.
 *
 * The library sizes the default TCP send/receive windows from total installed RAM at
 * startup (src/kern/amiga_main.c, ng_ram_tier). It detects RAM by summing the exec
 * memory list rather than AvailMem(MEMF_TOTAL) (whose flag is missing on pre-2.0
 * Kickstarts). This dump repeats that exact walk and mirrors the tier thresholds, so it
 * prints precisely the RAM the stack sees and the window it therefore selects. If a
 * NetInterface config sets tcp.sendspace=/tcp.recvspace= those override the auto value
 * (whole-stack -- the knobs are global, not per-interface: last interface configured wins).
 */
static void ram_tier(ULONG total, ULONG *snd, ULONG *rcv, ULONG *sbmax,
                     ULONG *maxmem, const char **name)
{
  /* Mirror of ng_ram_tier() in src/kern/amiga_main.c -- keep the two in step. */
  if      (total <= 1UL*1024*1024)   { *snd=*rcv=16060;   *sbmax=64UL*1024;   *maxmem=128;   *name="<=1MB"; }
  else if (total <= 4UL*1024*1024)   { *snd=*rcv=61320;   *sbmax=64UL*1024;   *maxmem=256;   *name="2-4MB"; }
  else if (total <= 16UL*1024*1024)  { *snd=*rcv=131400;  *sbmax=256UL*1024;  *maxmem=1024;  *name="8-16MB"; }
  else if (total <= 64UL*1024*1024)  { *snd=*rcv=262800;  *sbmax=512UL*1024;  *maxmem=4096;  *name="16-64MB"; }
  else if (total <= 128UL*1024*1024) { *snd=*rcv=524140;  *sbmax=1024UL*1024; *maxmem=8192;  *name="64-128MB"; }
  else                               { *snd=*rcv=1048280; *sbmax=2048UL*1024; *maxmem=16384; *name="128MB+"; }
}

static void debug_dump(void)
{
  struct MemHeader *mh;
  ULONG total = 0, snd, rcv, sbmax, maxmem;
  const char *tname;

  Printf((STRPTR)"\n--- Debug: RAM detection and TCP window auto-tuning ---\n");
  Printf((STRPTR)"Memory regions (exec MemList):\n");

  Forbid();
  for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
       mh->mh_Node.ln_Succ != NULL;
       mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
    total += (ULONG)((UBYTE *)mh->mh_Upper - (UBYTE *)mh->mh_Lower);
  Permit();

  /* Re-walk (outside Forbid) just to list the region names/sizes. */
  for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
       mh->mh_Node.ln_Succ != NULL;
       mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
    Printf((STRPTR)"  %-20s %10ld bytes  (attr 0x%lx)\n",
           (LONG)(mh->mh_Node.ln_Name ? (STRPTR)mh->mh_Node.ln_Name : (STRPTR)"?"),
           (LONG)((UBYTE *)mh->mh_Upper - (UBYTE *)mh->mh_Lower),
           (LONG)mh->mh_Attributes);

  {
    ULONG avail = (ULONG)AvailMem(MEMF_TOTAL);	/* secondary cross-check only */
    Printf((STRPTR)"Total installed RAM (MemList walk): %ld bytes (%ld MB)\n",
           (LONG)total, (LONG)(total / (1024 * 1024)));
    Printf((STRPTR)"AvailMem(MEMF_TOTAL) cross-check:   %ld bytes (%ld MB)\n",
           (LONG)avail, (LONG)(avail / (1024 * 1024)));
  }

  ram_tier(total, &snd, &rcv, &sbmax, &maxmem, &tname);

  /*
   * Ask the RUNNING stack for its live values, so we can show what the auto-tuning
   * IMPLIES (from the RAM this walk just measured) next to what the stack is ACTUALLY
   * using. If the two disagree, the two RAM figures say why: a smaller "stack detected"
   * than "walk" means the library sampled RAM before it was all registered; equal RAM
   * but a different window means a tcp.sendspace=/tcp.recvspace= config override is in
   * effect. An older library predating these codes answers 0 -> "query not supported".
   */
  {
    struct TagItem q[6];
    ULONG live_ram, live_snd, live_rcv, live_sbmax, live_baud;

    q[0].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_DETECTED_RAM);  q[0].ti_Data = 0;
    q[1].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_SENDSPACE); q[1].ti_Data = 0;
    q[2].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_TCP_RECVSPACE); q[2].ti_Data = 0;
    q[3].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_SB_MAX);        q[3].ti_Data = 0;
    q[4].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_LINK_SPEED);    q[4].ti_Data = 0;
    q[5].ti_Tag = TAG_END;                               q[5].ti_Data = 0;
    ng_sbtaglist(q);
    live_ram   = (ULONG)q[0].ti_Data;
    live_snd   = (ULONG)q[1].ti_Data;
    live_rcv   = (ULONG)q[2].ti_Data;
    live_sbmax = (ULONG)q[3].ti_Data;
    live_baud  = (ULONG)q[4].ti_Data;

    Printf((STRPTR)"RAM the running stack detected:     %ld bytes (%ld MB)\n",
           (LONG)live_ram, (LONG)(live_ram / (1024 * 1024)));
    if (live_baud > 0)
      Printf((STRPTR)"Last interface link speed:          %ld bps (~%ld Mbit) -- auto-tune sized to this\n",
             (LONG)live_baud, (LONG)(live_baud / 1000000));
    else
      Printf((STRPTR)"Last interface link speed:          not reported by the driver -- RAM ceiling used\n");
    Printf((STRPTR)"RAM tier (the ceiling):             %s (mbuf pool max %ld KB)\n",
           (LONG)tname, (LONG)maxmem);
    Printf((STRPTR)"Effective window = min(link-speed target, RAM tier); a config tcp.* overrides.\n");
    Printf((STRPTR)"                            RAM tier         actual (live)\n");
    Printf((STRPTR)"  tcp.sendspace         %14ld     %14ld\n", (LONG)snd,   (LONG)live_snd);
    Printf((STRPTR)"  tcp.recvspace         %14ld     %14ld\n", (LONG)rcv,   (LONG)live_rcv);
    Printf((STRPTR)"  sb_max                %14ld     %14ld\n", (LONG)sbmax, (LONG)live_sbmax);
    if (live_snd == 0)
      Printf((STRPTR)"(This bsdsocket.library predates the live-value query -- "
                     "\"actual\" reads 0; upgrade to compare.)\n");
    else if (live_rcv < rcv)
      Printf((STRPTR)"(actual < RAM tier: sized down by the link-speed auto-tune or a tcp.recvspace= override.)\n");
  }
  Printf((STRPTR)"tcp.sendspace/tcp.recvspace are WHOLE-STACK (global), not per-interface;\n"
                 "a tcp.recvspace= in any NetInterface config overrides the auto value.\n");
}

static int stricmp_ng(const char *a, const char *b)
{
  for (;;) {
    char x = *a++, y = *b++;
    if (x >= 'a' && x <= 'z') x -= 32;
    if (y >= 'a' && y <= 'z') y -= 32;
    if (x != y) return 1;
    if (x == 0) return 0;
  }
}

/* Pull the next whitespace/comma-separated token from *pp into buf; advance *pp.
 * Returns 1 if a token was produced, 0 at end of string. */
static int next_token(const char **pp, char *buf, int bufsz)
{
  const char *s = *pp;
  int n = 0;
  while (*s == ' ' || *s == '\t' || *s == ',') s++;
  if (*s == 0) { *pp = s; return 0; }
  while (*s && *s != ' ' && *s != '\t' && *s != ',' && n < bufsz - 1)
    buf[n++] = *s++;
  buf[n] = 0;
  *pp = s;
  return 1;
}

int main(void)
{
  struct RDArgs *rda;
  LONG   args[3] = { 0, 0, 0 };		/* CHECK/K, QUIET/S, DEBUG/S */
  STRPTR check;
  int    quiet, debug;
  ULONG  status = 0;
  struct TagItem tg[2];
  long   rc = RETURN_OK;

  rda = ReadArgs((STRPTR)"CHECK/K,QUIET/S,DEBUG/S", args, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)"GetNetStatus"); return RETURN_ERROR; }
  check = (STRPTR)args[0];
  quiet = args[1] ? 1 : 0;
  debug = args[2] ? 1 : 0;

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    if (!quiet) Printf((STRPTR)"GetNetStatus: bsdsocket.library v4+ not available.\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  tg[0].ti_Tag = NG_SBTM_GETVAL(NG_SBTC_SYSTEM_STATUS); tg[0].ti_Data = 0;
  tg[1].ti_Tag = TAG_END;                               tg[1].ti_Data = 0;
  if (ng_sbtaglist(tg) != 0) {
    if (!quiet)
      Printf((STRPTR)"GetNetStatus: %s %ld.%ld has no status API.\n",
             (LONG)SocketBase->lib_Node.ln_Name,
             (LONG)SocketBase->lib_Version, (LONG)SocketBase->lib_Revision);
    CloseLibrary(SocketBase);
    FreeArgs(rda);
    return RETURN_FAIL;
  }
  status = tg[0].ti_Data;

  if (check != NULL) {
    if (stricmp_ng((const char *)check, "?") == 0) {
      Printf((STRPTR)"INTERFACES/S,PTPINTERFACES=PTP/S,BCASTINTERFACES=BCAST=BROADCAST/S,"
             "RESOLVER=NAMERESOLUTION=DNS/S,ROUTES/S,DEFAULTROUTE=DEFAULTGATEWAY/S\n");
    } else {
      const char *p = (const char *)check;
      char tok[40];
      while (next_token(&p, tok, sizeof(tok))) {
        ULONG bit = 0;
        if      (!stricmp_ng(tok, "INTERFACES"))                              bit = SBSYSSTAT_Interfaces;
        else if (!stricmp_ng(tok, "PTPINTERFACES") || !stricmp_ng(tok, "PTP")) bit = SBSYSSTAT_PTP_Interfaces;
        else if (!stricmp_ng(tok, "BCASTINTERFACES") || !stricmp_ng(tok, "BCAST") || !stricmp_ng(tok, "BROADCAST")) bit = SBSYSSTAT_BCast_Interfaces;
        else if (!stricmp_ng(tok, "RESOLVER") || !stricmp_ng(tok, "NAMERESOLUTION") || !stricmp_ng(tok, "DNS")) bit = SBSYSSTAT_Resolver;
        else if (!stricmp_ng(tok, "ROUTES"))                                  bit = SBSYSSTAT_Routes;
        else if (!stricmp_ng(tok, "DEFAULTROUTE") || !stricmp_ng(tok, "DEFAULTGATEWAY")) bit = SBSYSSTAT_DefaultRoute;
        else { if (!quiet) Printf((STRPTR)"GetNetStatus: unknown condition \"%s\".\n", (LONG)tok); continue; }

        if ((status & bit) == 0) {
          rc = RETURN_WARN;			/* a requested condition is not met */
          if (!quiet) {
            switch (bit) {
            case SBSYSSTAT_Interfaces:      Printf((STRPTR)"No networking interfaces are available and configured.\n"); break;
            case SBSYSSTAT_PTP_Interfaces:  Printf((STRPTR)"No point-to-point networking interfaces are available and configured.\n"); break;
            case SBSYSSTAT_BCast_Interfaces:Printf((STRPTR)"No broadcast networking interfaces are available and configured.\n"); break;
            case SBSYSSTAT_Resolver:        Printf((STRPTR)"No name resolution servers are configured.\n"); break;
            case SBSYSSTAT_Routes:          Printf((STRPTR)"No routing information is configured.\n"); break;
            case SBSYSSTAT_DefaultRoute:    Printf((STRPTR)"The default route is not configured.\n"); break;
            }
          }
        }
      }
    }
  } else if (!quiet) {
    /* Human summary: library id line, then one line per facility. */
    if (SocketBase->lib_IdString)
      Printf((STRPTR)"%s\n", (LONG)SocketBase->lib_IdString);

    Printf((STRPTR)((status & SBSYSSTAT_Interfaces)
           ? "Networking interfaces are available and configured.\n"
           : "No networking interfaces are available and configured.\n"));
    Printf((STRPTR)((status & SBSYSSTAT_PTP_Interfaces)
           ? "Point-to-point networking interfaces are available and configured.\n"
           : "No point-to-point networking interfaces are available and configured.\n"));
    Printf((STRPTR)((status & SBSYSSTAT_BCast_Interfaces)
           ? "Broadcast networking interfaces are available and configured.\n"
           : "No broadcast networking interfaces are available and configured.\n"));
    Printf((STRPTR)((status & SBSYSSTAT_Resolver)
           ? "Name resolution servers are configured.\n"
           : "No name resolution servers are configured.\n"));
    Printf((STRPTR)((status & SBSYSSTAT_Routes)
           ? "Routing information is configured.\n"
           : "No routing information is configured.\n"));
    Printf((STRPTR)((status & SBSYSSTAT_DefaultRoute)
           ? "The default route is configured.\n"
           : "The default route is not configured.\n"));
  }

  if (debug) debug_dump();

  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
