/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * Online / Offline -- switch a SANA-II network device driver online or offline,
 * a name-and-behaviour-compatible replacement for Roadshow's Online/Offline
 * commands (template NAME/A,UNIT/N,TIMEOUT/N; NAME is the SANA-II device driver,
 * e.g. "wifipi.device"). These talk to the driver directly via S2_ONLINE/S2_OFFLINE
 * -- they do not use bsdsocket.library -- and our stack reacts to the resulting
 * S2ERR_OUTOFSERVICE / S2EVENT_ONLINE (see net/if_sana.c) by taking the interface
 * down and re-raising it automatically.
 *
 * Build twice from this one source:
 *   m68k-amigaos-gcc -noixemul -O2 -m68000 -DDO_ONLINE  src/tools/netonoff.c -o Online
 *   m68k-amigaos-gcc -noixemul -O2 -m68000 -DDO_OFFLINE src/tools/netonoff.c -o Offline
 */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <devices/sana2.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#if defined(DO_ONLINE)
#  define CMD_NAME   "Online"
#  define S2_CMD     S2_ONLINE
#  define WANT_EVENT 1
#elif defined(DO_OFFLINE)
#  define CMD_NAME   "Offline"
#  define S2_CMD     S2_OFFLINE
#  define WANT_EVENT 0
#else
#  error define DO_ONLINE or DO_OFFLINE
#endif

/* Descriptive text for the common SANA-II open/io errors, matching the spirit of
 * Roadshow's Online/Offline diagnostics. */
static const char *io_error_string(long err)
{
  switch (err) {
    case S2ERR_NO_ERROR:        return "no error";
    case S2ERR_NO_RESOURCES:    return "resource allocation failure";
    case S2ERR_BAD_ARGUMENT:    return "garbage somewhere";
    case S2ERR_BAD_STATE:       return "already in requested state";
    case S2ERR_BAD_ADDRESS:     return "improper address";
    case S2ERR_MTU_EXCEEDED:    return "maximum transmission unit exceeded";
    case S2ERR_NOT_SUPPORTED:   return "command not supported by hardware";
    case S2ERR_SOFTWARE:        return "software error detected";
    case S2ERR_OUTOFSERVICE:    return "driver is offline";
    case S2ERR_TX_FAILURE:      return "transmission attempt failed";
    case IOERR_OPENFAIL:        return "could not open device";
    default:                    return "unknown error";
  }
}

int main(void)
{
  struct RDArgs   *rda;
  LONG             args[3] = { 0, 0, 0 };     /* NAME/A, UNIT/N, TIMEOUT/N */
  struct MsgPort  *mp = NULL;
  struct IOSana2Req *io = NULL;
  STRPTR           name;
  LONG             unit, err, rc = RETURN_OK;

  rda = ReadArgs((STRPTR)"NAME/A,UNIT/N,TIMEOUT/N", args, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)CMD_NAME); return RETURN_ERROR; }
  name = (STRPTR)args[0];
  unit = args[1] ? *(LONG *)args[1] : 0;

  mp = CreateMsgPort();
  io = mp ? (struct IOSana2Req *)CreateIORequest(mp, sizeof(struct IOSana2Req)) : NULL;
  if (!io) { Printf((STRPTR)"%s: no memory.\n", (LONG)CMD_NAME); rc = RETURN_FAIL; goto out; }

  /* Open the driver. As Roadshow does, if a bare name fails to open, retry via the
   * conventional DEVS:Networks/ location. (A bare name also finds a resident driver.) */
  err = OpenDevice(name, unit, (struct IORequest *)io, 0);
  if (err == IOERR_OPENFAIL) {
    char full[128]; int k = 0, j;
    const char *pfx = "DEVS:Networks/";
    for (j = 0; pfx[j] && k < (int)sizeof(full) - 1; j++) full[k++] = pfx[j];
    for (j = 0; name[j] && k < (int)sizeof(full) - 1; j++) full[k++] = name[j];
    full[k] = 0;
    err = OpenDevice((STRPTR)full, unit, (struct IORequest *)io, 0);
  }
  if (err != 0) {
    Printf((STRPTR)"%s: could not open '%s' unit %ld (%ld, %s).\n",
           (LONG)CMD_NAME, (LONG)name, unit, err, (LONG)io_error_string(err));
    io->ios2_Req.io_Device = NULL;   /* mark not-open so we don't CloseDevice */
    rc = RETURN_FAIL; goto out;
  }

  io->ios2_Req.io_Command = S2_CMD;
  DoIO((struct IORequest *)io);
  err = io->ios2_Req.io_Error;

  /* S2ERR_BAD_STATE == already in the requested state; treat as success (a warning),
   * matching Roadshow's note that some drivers report this instead of "already so". */
  if (err == 0 || err == S2ERR_BAD_STATE) {
    rc = (err == S2ERR_BAD_STATE) ? RETURN_WARN : RETURN_OK;
  } else {
    Printf((STRPTR)"%s: '%s' unit %ld: %s (%ld).\n",
           (LONG)CMD_NAME, (LONG)name, unit, (LONG)io_error_string(err), err);
    rc = RETURN_ERROR;
  }

  CloseDevice((struct IORequest *)io);

out:
  if (io) DeleteIORequest((struct IORequest *)io);
  if (mp) DeleteMsgPort(mp);
  FreeArgs(rda);
  return rc;
}
