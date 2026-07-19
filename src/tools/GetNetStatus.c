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
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <utility/tagitem.h>

#include "ng_lvo.h"

struct Library *SocketBase = 0;

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
  LONG   args[2] = { 0, 0 };		/* CHECK/K, QUIET/S */
  STRPTR check;
  int    quiet;
  ULONG  status = 0;
  struct TagItem tg[2];
  long   rc = RETURN_OK;

  rda = ReadArgs((STRPTR)"CHECK/K,QUIET/S", args, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)"GetNetStatus"); return RETURN_ERROR; }
  check = (STRPTR)args[0];
  quiet = args[1] ? 1 : 0;

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

  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
