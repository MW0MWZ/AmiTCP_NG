/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: sana2perror.c,v 1.1 1994/01/23 22:00:52 jraja Exp $";
/*
 * sana2perror.c --- print SANA-II error message
 *
 * Author: ppessi <Pekka.Pessi@hut.fi>
 *
 * Copyright � 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                  Helsinki University of Technology, Finland.
 *                  All rights reserved.
 *
 * Created      : Sat Mar 20 02:10:14 1993 ppessi
 * Last modified: Mon Jan 24 00:00:44 1994 jraja
 */

/*
 * sana2perror.c --- render a SANA-II device error as a log message.
 *
 * A SANA-II IORequest reports failures in two fields: io_Error (the general error)
 * and, for wire-level problems, ios2_WireError. sana2perror() maps those numeric
 * codes to readable text and logs them, so a driver problem ("device is offline",
 * "no such unit") shows up intelligibly instead of as a bare number. Called from
 * net/if_sana.c wherever a request comes back failed. The perror() analogue for
 * the SANA-II world.
 */

#include <conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>

#include <devices/sana2.h>
#include <net/sana2errno.h>

extern const char * const io_errlist[];
extern const short io_nerr;
extern const char * const sana2io_errlist[];
extern const short sana2io_nerr;
extern const char * const sana2wire_errlist[];
extern const short sana2wire_nerr;

void 
sana2perror(const char *banner, struct IOSana2Req *ios2)
{
  register WORD err = ios2->ios2_Req.io_Error;
  register ULONG werr = ios2->ios2_WireError;
  const char *errstr;

  /* PORT (AmiTCP_NG) fix: `-err > io_nerr` let -err == io_nerr
   * through, then read io_errlist[io_nerr] -- one past the last valid index --
   * and deref it as a string. Use >= to match the positive-err branch. */
  if (err >= sana2io_nerr || -err >= io_nerr) {
    errstr = io_errlist[0];
  } else { 
    if (err < 0) 
      /* Negative error codes are common with all IO devices */
      errstr = io_errlist[-err];
    else 
      /* Positive error codes are SANA-II specific */ 
      errstr = sana2io_errlist[err];
  }

  if (werr == 0 || werr >= sana2wire_nerr) {
    log(LOG_ERR, "%s: %s\n", banner, errstr);
  } else {
    log(LOG_ERR, "%s: %s (%s)\n", banner, errstr, sana2wire_errlist[werr]);
  }
}

