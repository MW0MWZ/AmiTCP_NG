/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: sana2config.c,v 3.2 1994/02/03 19:10:09 ppessi Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * sana2config.c - Configuration parameters for SANA-II network interfaces
 *
 * HISTORY
 * $Log: sana2config.c,v $
 * Revision 3.2  1994/02/03  19:10:09  ppessi
 * Changed the interface database format
 *
 * Revision 3.1  1994/02/03  03:50:38  ppessi
 * Initially tested version
 *
 */

/*
 * sana2config.c --- parse SANA-II network-interface definitions.
 *
 * Turns a textual interface description (from AmiTCP:db/interfaces, or an ARexx
 * "add interface" command) into a `struct ssconfig` that net/if_sana.c's
 * iface_make() uses to create and open the interface. A definition names the
 * interface, the SANA-II device and unit (e.g. DEVICE=devs:networks/a2065.device
 * UNIT=0), the packet types/framing for IP and ARP, how many read/write IORequests
 * to allocate, and flags (NOARP, POINT2POINT, LOOPBACK, ...). The template it
 * parses against is SSC_TEMPLATE in net/sana2config.h.
 *   ssconfig_parse()   parse one interface definition (via ReadArgs over a CSource).
 *   ssconfig_make()    build a config record; ssconfig_free() releases it.
 *   ssconfig()         apply a parsed config to a sana_softc.
 * This is the front half of "how does the stack learn about a network card". See
 * the SANA-II bridge in net/if_sana.c for the back half.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <net/if.h>
#include <net/if_types.h>
#include <assert.h>

#if NS
#error NS is not supported
#endif

#include <kern/amiga_includes.h>
#if __SASC
#include <proto/dos.h>
#elif __GNUC__
#include <inline/dos.h>
/*
 * PORT (AmiTCP_NG): under this GCC/libnix build, plain <string.h> is not
 * includable here -- libnix's copy needs ssize_t, which this build's own
 * (shadowing) <sys/types.h> does not provide, and it would also redeclare
 * strlen()/strcpy() against the static-inline versions kern/amiga_subr.h
 * supplies for GCC. Pull those inline definitions in directly (as other
 * GCC-built files reach them via <sys/systm.h>) for strlen/strcpy/strncpy;
 * strcmp() has no declaration anywhere in this tree for the GCC path (same
 * pre-existing gap as kern/amiga_netdb.c), so declare it locally -- it
 * still resolves to the real libnix strcmp() at link time, just typed now.
 */
#include <kern/amiga_subr.h>
extern int strcmp(const char *, const char *);
#else
#error Your compiler is not supported in this release.
#endif

#include <netinet/in.h>
#include <devices/sana2.h>
#include <utility/tagitem.h>
#include <net/sana2tags.h>
#include <net/sana2config.h>
#include <net/if_arp.h>
#include <net/if_sana.h>

static const char template[] = SSC_TEMPLATE;

#define CONFIGLINELEN 1024

/*
 * Parse the configuration 
 */
static struct ssconfig *
ssconfig_parse(struct CSource argfile[])
{
  struct ssconfig *config = AllocVec(sizeof(*config), MEMF_CLEAR|MEMF_PUBLIC);
  struct RDArgs *rdargs;

  /*
   * PORT (AmiTCP_NG) fix: AllocVec() can fail (this is a
   * memory-constrained machine, and interface config runs at boot / on demand).
   * The original read config->rdargs BEFORE this NULL check -> NULL deref crash.
   */
  if (config != NULL) {
    rdargs = config->rdargs;
    rdargs->RDA_Source = *argfile;
    rdargs->RDA_DAList = NULL;
    rdargs->RDA_Buffer = NULL;
    rdargs->RDA_BufSiz = 0;
    rdargs->RDA_ExtHelp = NULL;
    rdargs->RDA_Flags = RDAF_NOPROMPT;
    
    if (ReadArgs((CONST_STRPTR)template, (LONG *)config->args, rdargs)) {
      config->flags |= SSCF_RDARGS;
      return config;
    } else {
      FreeVec(config);
    }
  }
  return NULL;
}

/*
 * Free the configuration
 */
void
ssconfig_free(struct ssconfig config[])
{
  if (config->flags & SSCF_RDARGS)
    FreeArgs(config->rdargs);
  FreeVec(config);
}

static int
getconfs(BPTR iffh, UBYTE *buf)
{
  LONG i, quoted = 0, escaped = 0;

  if (FGets(iffh, buf, CONFIGLINELEN - 1)) {
    for (i = 0; buf[i]; i++) {
      UBYTE c = buf[i];
      if (c == '\n') {
	if (quoted) {
	  return ERROR_UNMATCHED_QUOTES;
	}
	if (i > 0 && buf[i - 1] == '+') {
	  i--;
	  if (i < CONFIGLINELEN - 2) {
	    if (FGets(iffh, buf + i, CONFIGLINELEN - 1 - i))
	      continue;
	    return IoErr();
	  }	    
	  return ERROR_LINE_TOO_LONG;
	}
	return 0;
      } else if (quoted) {
	if (escaped) {
	  escaped = 0;
	} else if (c == '*') {
	  escaped = 1;
	  continue;
	} else if (c == '"') {
	  quoted = 0;
	}
      } else if (c == ';' || c == '#') {
	buf[i++] = '\n';
	buf[i] = '\0';
	return 0;
      } else if (escaped) {
	escaped = 0;
      } else if (c == '*') {
	escaped = 1;
      } else if (c == '"') {
	quoted = 1;
      }
    }
    return ERROR_LINE_TOO_LONG;
  }

  buf[0] = '\0';
  return IoErr();
}

/*
 * Read configuration file 
 */
struct ssconfig *
ssconfig_make(int how, char *unitname, long unit)
{
  LONG ioerr = 0;
  BPTR iffh;
  UBYTE *buf;
  struct ssconfig *ssc = NULL, *ssc_generic = NULL;
  char devname[IFNAMSIZ];
  char *cp;

  if (how == SSC_ALIAS) {
    /* Copy the interface name */
    cp = strncpy(devname, unitname, IFNAMSIZ); 
    devname[IFNAMSIZ-1] = '\0';
  
    for (; *cp; cp++)
      if (*cp >= '0' && *cp <= '9')
	break;
    *cp = '\0';
  }
#ifdef COMPAT_AMITCP2
  else if (how == SSC_COMPAT) {
  }
#endif
  else {
    return NULL;
  }

  buf = AllocVec(CONFIGLINELEN, MEMF_PUBLIC);
  iffh = Open((CONST_STRPTR)_PATH_SANA2CONFIG, MODE_READWRITE);
  if (iffh && buf) {
    BPTR oldinfh = SelectInput(iffh);
    struct CSource arg[1];

    /* There is bug in FGets -- the last char is not necessary 0 */
    buf[CONFIGLINELEN - 1] = '\0';

    while ((ioerr = getconfs(iffh, buf)) == 0) {
      struct ssconfig *s; 
      if (buf[0] == '\n')
	continue;
      if (buf[0] == '\0')
	break;

      arg->CS_Buffer = buf;
      arg->CS_Length = strlen((char *)buf);
      arg->CS_CurChr = 0;
      if ((s = ssconfig_parse(arg))) {
	if (how == SSC_ALIAS) {
	  s->unit = unit;
	  if (strcmp((char *)s->args->a_name, unitname) == 0) {
	    ssc = s;
	    break;
	  } else if (ssc_generic == NULL &&
		     strcmp((char *)s->args->a_name, devname) == 0) {
	    ssc_generic = s;
	    continue;
	  }
	} 
#ifdef COMPAT_AMITCP2
	else if (how == SSC_COMPAT) {
	  /*
	   * Try to find interface with matching exec device and unit
	   */
	  if (strcmp(FilePart(s->args->a_dev), FilePart(unitname)) == 0 &&
	      (s->args->a_unit == NULL || *s->args->a_unit == unit)) {
	    /* Copy the devname */
	    cp = strncpy(devname,  s->args->a_name, IFNAMSIZ); 
	    devname[IFNAMSIZ-1] = '\0';
  
	    for (; *cp; cp++)
	      if (*cp >= '0' && *cp <= '9')
		break;

	    if (*cp) { 
	      /* an interface with unit number, eg. "slip0" */
	      int ifunit = *cp - '0';
	      *cp++ = '\0';

	      while (*cp >= '0' && *cp <= '9')
		ifunit = ifunit * 10 + *cp++ - '0';

	      if (s->args->a_unit == NULL && ifunit != unit) {
		continue;
	      }
	      s->unit = ifunit;
	    } else {
	      /* an interface sans unit number, eg. "slip" */
	      s->unit = unit - (s->args->a_unit ? *s->args->a_unit : 0);
	    }
	    *cp = '\0';
	    ssc = s;
	    break;
	  }
	}
#endif
	ssconfig_free(s);
	continue;
      }
      break;
    } 
    SelectInput(oldinfh);
  } else {
    if (buf)
      ioerr = IoErr();
    else
      ioerr = ERROR_NO_FREE_STORE;
  }

  if (iffh) Close(iffh);
  if (buf) FreeVec(buf);

  if (ioerr) {
    if (ssc)
      ssconfig_free(ssc);
    if (ssc_generic)
      ssconfig_free(ssc_generic);
    ssc_generic = ssc = NULL;
  } 

  if (ssc == NULL) {
    ssc = ssc_generic;
  } else {
    if (ssc && ssc_generic)
      ssconfig_free(ssc_generic);
    ssc_generic = NULL;
  }

  if (ssc) {
    if (ssc->args->a_unit) {
      if (ssc_generic)
	*ssc->args->a_unit += unit;
    } else {
      ssc->args->a_unit = &ssc->unit;
    }

    strcpy(ssc->name, devname);
  }

  return ssc;
}

/*
 * Default configuration as per hardware type
 */
static const struct wire_defaults {
  LONG  wd_wiretype;
  LONG  wd_iptype;		/* IP packet type */
  WORD  wd_ipno;		/* SANA-II requests reserved for receiving */
  WORD  wd_writeno;		/* SANA-II requests reserved for sending */
  LONG  wd_arptype;		/* ARP packet type */
  WORD  wd_arpno;		/* SANA-II requests reserved for ARP */
  WORD  wd_arphdr;		/* ARP version */
  WORD  wd_ifflags;		/* Interface flags */
  BYTE  wd_pad[2];
} wire_defaults[] = {
  { 
    S2WireType_Ethernet, 
    ETHERTYPE_IP, 16, 32,	/* wd_ipno 16 reads, wd_writeno 32 write ring */ 
    ETHERTYPE_ARP, 4, 1, 
    IFF_NOTRAILERS|IFF_BROADCAST|IFF_SIMPLEX, 
  },
  { 
    S2WireType_Arcnet, 
    ARCOTYPE_IP, 16, 32,	/* wd_ipno 16 reads, wd_writeno 32 write ring */ 
    ARCOTYPE_ARP, 4, 7, 
    IFF_NOTRAILERS|IFF_BROADCAST|IFF_SIMPLEX, 
  },
  { 
    S2WireType_SLIP, 
    SLIPTYPE_IP, 8, 8,
    0, 0, 0,
    IFF_NOTRAILERS|IFF_POINTOPOINT|IFF_NOARP, 
  },
  { 
    S2WireType_CSLIP, 
    SLIPTYPE_IP, 8, 8,
    0, 0, 0,
    IFF_NOTRAILERS|IFF_POINTOPOINT|IFF_NOARP, 
  },
  { 
    S2WireType_PPP, 
    PPPTYPE_IP, 8, 8,
    0, 0, 0,
    IFF_NOTRAILERS|IFF_POINTOPOINT|IFF_NOARP, 
  },
  /* Use ethernet as default */
  {
    0,
    ETHERTYPE_IP, 16, 32,	/* wd_ipno 16 reads, wd_writeno 32 write ring */
    ETHERTYPE_ARP, 4, 1,
    IFF_NOTRAILERS|IFF_BROADCAST|IFF_SIMPLEX, 
  },
};

/*
 * Initialize sana_softc
 */
void
ssconfig(struct sana_softc *ifp, struct ssconfig *ifc)
{
  const struct ssc_args *args = ifc->args;
  const struct wire_defaults *wd;
  LONG wt = ifp->ss_hwtype;
  LONG reqtotal = 0;

  assert(ifp != NULL);
  assert(ifp->ss_if.if_type == IFT_SANA);

  for (wd = wire_defaults; wd->wd_wiretype != 0; wd++) {
    if (wt == wd->wd_wiretype)
      break;
  }
  
  ifp->ss_ip.type = args->a_iptype ? *args->a_iptype : wd->wd_iptype;
  reqtotal += ifp->ss_ip.reqno = args->a_ipno ? *args->a_ipno : wd->wd_ipno;

  ifp->ss_arp.type = args->a_arptype ? *args->a_arptype : wd->wd_arptype;
  reqtotal += ifp->ss_arp.reqno = args->a_arpno ? *args->a_arpno : wd->wd_arpno;
  ifp->ss_arp.hrd = args->a_arphdr ? *args->a_arphdr : wd->wd_arphdr;

  reqtotal += args->a_writeno ? *args->a_writeno : wd->wd_writeno;

  if (reqtotal > 65535)
    reqtotal = 65535;
  ifp->ss_reqno = reqtotal;

  {
    UWORD ifflags = wd->wd_ifflags;

    if (args->a_noarp)
      ifflags |= IFF_NOARP;
    if (args->a_point2point) {
      ifflags |= IFF_POINTOPOINT;
      ifflags &= ~IFF_BROADCAST;
    }
    if (args->a_nosimplex)
      ifflags &= ~IFF_SIMPLEX;
    if (args->a_loopback)
      ifflags |= IFF_LOOPBACK;

    ifp->ss_if.if_flags = ifflags;
  }

  /* Flags for soft_sanac */
  ifp->ss_cflags = SS_CFLAGS;

  if (args->a_notrack)
    ifp->ss_cflags &= ~(SSF_TRACK);

  /* Set up name */
  ifp->ss_if.if_name = strcpy((char *)ifp->ss_name, ifc->name);
  ifp->ss_if.if_unit = ifc->unit;
  ifp->ss_execname = (UBYTE *)strcpy((char *)(ifp + 1), (char *)ifc->args->a_dev);
  ifp->ss_execunit = *ifc->args->a_unit;
}

