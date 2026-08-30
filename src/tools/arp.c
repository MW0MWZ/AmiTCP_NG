/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 * arp -- show and edit the ARP cache (the IP-to-hardware-address table).
 *
 * Template HOST,DELETE=DEL/S,SET/K,PUB=PUBLISH/S,TEMP/S,QUIET/S
 *
 *   arp                            list every interface's ARP table
 *   arp 192.168.0.1                show one entry
 *   arp 192.168.0.1 DELETE         remove it (re-resolved on next use)
 *   arp 192.168.0.1 SET aa:bb:cc:dd:ee:ff [PUB] [TEMP]
 *                                  add a static entry. TEMP lets it age out
 *                                  normally; PUB answers ARP requests for that
 *                                  address on the other host's behalf.
 *
 * The stack has carried the ioctls for this since AmiTCP (SIOCGARPT, SIOCGARP,
 * SIOCSARP, SIOCDARP) with nothing to drive them, so the cache could not be
 * looked at on a running machine -- which is exactly what you want to see when
 * name lookups work and traffic does not.
 *
 * SIOCGARPT identifies the table by an address ON the interface, so the listing
 * walks the interface list and dumps each one in turn.
 */
#include <exec/types.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

/*
 * ARP structures and ioctls, defined here rather than included.
 *
 * -Isrc/netinclude is not usable from a command (inline/socket.h and the netlib
 * prototypes collide with the toolchain's own headers), and the toolchain's
 * net/if_arp.h is stock BSD: it has no arptabreq, the AmiTCP extension that
 * makes a whole-table dump possible.
 *
 * These must be BYTE-IDENTICAL to the kernel's, because an ioctl number encodes
 * sizeof() of its argument -- get the layout wrong and the request code itself
 * differs, so the call fails outright. Measured against the library build:
 * arpreq 38, arptabreq 50, sockaddr 16. The size assert below fails the BUILD if
 * that ever drifts, rather than shipping a command that cannot talk to its stack.
 * (Same local-definition-plus-assert idiom netstat.c uses for struct ng_sin.)
 */
#define NG_MAXADDRARP	16

struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr;
		UBYTE sin_zero[8]; };

struct ng_arpreq {
  struct ng_sin arp_pa;				/* protocol address (sockaddr_in) */
  struct { UBYTE sa_len, sa_family; char sa_data[NG_MAXADDRARP]; } arp_ha;
  LONG  arp_flags;
};

struct ng_arptabreq {
  struct ng_arpreq  atr_arpreq;			/* identifies the interface */
  LONG              atr_size;			/* entries in atr_table     */
  LONG              atr_inuse;			/* entries actually filled  */
  struct ng_arpreq *atr_table;
};

typedef char ng_arp_layout_check[(sizeof(struct ng_sin)       == 16 &&
				  sizeof(struct ng_arpreq)    == 38 &&
				  sizeof(struct ng_arptabreq) == 50) ? 1 : -1];

#define NG_ATF_COM	0x02			/* entry complete           */
#define NG_ATF_PERM	0x04			/* never ages out           */
#define NG_ATF_PUBL	0x08			/* answer ARP for this host */

#define NG_IOW(g,n,t)	(0x80000000UL | ((sizeof(t) & 0x1fff) << 16) | \
			 (((ULONG)(g)) << 8) | (n))
#define NG_IOWR(g,n,t)	(0xC0000000UL | ((sizeof(t) & 0x1fff) << 16) | \
			 (((ULONG)(g)) << 8) | (n))

#define NG_SIOCSARP	NG_IOW ('I', 30, struct ng_arpreq)
#define NG_SIOCDARP	NG_IOW ('I', 32, struct ng_arpreq)
#define NG_SIOCGARP	NG_IOWR('I', 38, struct ng_arpreq)
#define NG_SIOCGARPT	NG_IOWR('I', 66, struct ng_arptabreq)

#define NG_AF_INET	2
#define NG_AF_UNSPEC	0
#define NG_SOCK_DGRAM	2

struct Library *SocketBase;
#include "ng_lvo.h"

#define PROG	"arp"
#define MAXENT	64		/* entries fetched per interface in one call */

/* ------------------------------------------------------------------ */

static int parse_ip(const char *s, ULONG *out)
{
  ULONG v = 0;
  int part, i, d;

  for (part = 0; part < 4; part++) {
    if (*s < '0' || *s > '9') return 0;
    for (i = 0, d = 0; *s >= '0' && *s <= '9'; s++) {
      d = d * 10 + (*s - '0');
      if (++i > 3 || d > 255) return 0;
    }
    v = (v << 8) | (ULONG)d;
    if (part < 3) { if (*s != '.') return 0; s++; }
  }
  if (*s != '\0') return 0;
  *out = v;
  return 1;
}

static int hexval(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* "aa:bb:cc:dd:ee:ff" (or '-' separated) -> bytes; returns count, 0 on error. */
static int parse_hw(const char *s, UBYTE *out, int max)
{
  int n = 0, hi, lo;

  for (;;) {
    if ((hi = hexval(*s++)) < 0) return 0;
    if ((lo = hexval(*s++)) < 0) return 0;
    if (n >= max) return 0;
    out[n++] = (UBYTE)((hi << 4) | lo);
    if (*s == '\0') break;
    if (*s != ':' && *s != '-') return 0;
    s++;
  }
  return n;
}

static void zero(void *p, int n)
{
  char *c = (char *)p;
  while (n-- > 0) *c++ = 0;
}

static int dec_width(ULONG v)
{
  return (v >= 100) ? 3 : (v >= 10) ? 2 : 1;
}

static int ip_width(ULONG a)
{
  return dec_width((a >> 24) & 0xff) + dec_width((a >> 16) & 0xff) +
	 dec_width((a >>  8) & 0xff) + dec_width( a        & 0xff) + 3;
}

static void show_entry(struct ng_arpreq *ar)
{
  static const char hexd[] = "0123456789abcdef";
  ULONG a = ar->arp_pa.sin_addr;
  int hl = (ar->arp_ha.sa_len >= 2) ? ar->arp_ha.sa_len - 2 : 0;
  TEXT hw[3 * NG_MAXADDRARP + 1];
  int i, p = 0;

  if (hl > NG_MAXADDRARP) hl = NG_MAXADDRARP;
  if ((ar->arp_flags & NG_ATF_COM) == 0 || hl == 0) {
    const char *inc = "(incomplete)";
    while (*inc) hw[p++] = *inc++;
  } else {
    for (i = 0; i < hl; i++) {
      if (i) hw[p++] = ':';
      hw[p++] = hexd[(ar->arp_ha.sa_data[i] >> 4) & 0xf];
      hw[p++] = hexd[ ar->arp_ha.sa_data[i]       & 0xf];
    }
  }
  hw[p] = '\0';

  Printf((STRPTR)"  %ld.%ld.%ld.%ld", (LONG)((a >> 24) & 0xff),
	 (LONG)((a >> 16) & 0xff), (LONG)((a >> 8) & 0xff), (LONG)(a & 0xff));
  /* Pad to a column so the hardware addresses line up; dotted-quads vary from
   * 7 to 15 characters, and a plain tab misaligns as soon as one is short. */
  { int pad = 16 - ip_width(a); while (pad-- > 0) Printf((STRPTR)" "); }
  Printf((STRPTR)"%s", (LONG)hw);
  if (ar->arp_flags & NG_ATF_PERM) Printf((STRPTR)" permanent");
  if (ar->arp_flags & NG_ATF_PUBL) Printf((STRPTR)" published");
  Printf((STRPTR)"\n");
}

/* The ioctls want AF_INET in arp_pa and AF_UNSPEC in arp_ha. */
static void set_pa(struct ng_arpreq *ar, ULONG addr)
{
  zero(ar, sizeof(*ar));
  ar->arp_pa.sin_len    = sizeof(ar->arp_pa);
  ar->arp_pa.sin_family = NG_AF_INET;
  ar->arp_pa.sin_addr   = addr;
  ar->arp_ha.sa_family  = NG_AF_UNSPEC;
}

/* ------------------------------------------------------------------ */

static int list_all(long s, int quiet)
{
  struct List *l;
  struct Node *n;
  int shown = 0;

  if ((l = ng_obtainiflist()) == NULL) {
    if (!quiet) Printf((STRPTR)PROG ": no interfaces\n");
    return RETURN_WARN;
  }

  for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
    struct TagItem tg[2];
    struct ng_sin  sin;
    struct ng_arptabreq atr;
    static struct ng_arpreq tab[MAXENT];	/* static: keep it off the stack */
    int i;

    zero(&sin, sizeof(sin));
    tg[0].ti_Tag = IFQ_Address; tg[0].ti_Data = (ULONG)&sin;
    tg[1].ti_Tag = TAG_END;     tg[1].ti_Data = 0;
    if (ng_queryif((void *)n->ln_Name, tg) != 0) continue;
    if (sin.sin_addr == 0) continue;		/* unnumbered: no ARP table */

    zero(&atr, sizeof(atr));
    set_pa(&atr.atr_arpreq, sin.sin_addr);
    atr.atr_size  = MAXENT;
    atr.atr_table = tab;

    if (ng_ioctl(s, NG_SIOCGARPT, &atr) != 0) continue;

    Printf((STRPTR)"%s: %ld entr%s\n", (LONG)n->ln_Name, (LONG)atr.atr_inuse,
	   (LONG)((atr.atr_inuse == 1) ? "y" : "ies"));
    for (i = 0; i < (int)atr.atr_inuse && i < MAXENT; i++)
      show_entry(&tab[i]);
    shown++;
  }
  ng_releaseiflist(l);

  if (!shown && !quiet)
    Printf((STRPTR)PROG ": no interface has an ARP table yet\n");
  return RETURN_OK;
}

int main(void)
{
  struct RDArgs *rda;
  LONG  a[6] = { 0, 0, 0, 0, 0, 0 };
  int   quiet, rc = RETURN_OK;
  long  s;
  ULONG addr = 0;
  struct ng_arpreq ar;

  rda = ReadArgs((STRPTR)"HOST,DELETE=DEL/S,SET/K,PUB=PUBLISH/S,TEMP/S,QUIET/S",
		 a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }
  quiet = a[5] != 0;

  if (a[0] && !parse_ip((char *)a[0], &addr)) {
    if (!quiet)
      Printf((STRPTR)PROG ": '%s' is not a dotted-decimal address\n", a[0]);
    FreeArgs(rda); return RETURN_ERROR;
  }
  if (!a[0] && (a[1] || a[2])) {
    if (!quiet) Printf((STRPTR)PROG ": DELETE and SET need a host address\n");
    FreeArgs(rda); return RETURN_ERROR;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4L);
  if (!SocketBase) {
    if (!quiet) Printf((STRPTR)PROG ": cannot open bsdsocket.library\n");
    FreeArgs(rda); return RETURN_FAIL;
  }
  /* The ioctls are issued against a socket; any datagram socket will do. */
  if ((s = ng_socket(NG_AF_INET, NG_SOCK_DGRAM, 0)) < 0) {
    if (!quiet)
      Printf((STRPTR)PROG ": cannot create a socket, errno %ld\n", ng_errno());
    CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL;
  }

  if (!a[0]) {
    rc = list_all(s, quiet);

  } else if (a[1]) {				/* DELETE */
    set_pa(&ar, addr);
    if (ng_ioctl(s, NG_SIOCDARP, &ar) != 0) {
      if (!quiet)
	Printf((STRPTR)PROG ": cannot delete that entry, errno %ld\n", ng_errno());
      rc = RETURN_ERROR;
    } else if (!quiet) {
      Printf((STRPTR)"deleted\n");
    }

  } else if (a[2]) {				/* SET <hardware address> */
    UBYTE hw[NG_MAXADDRARP];
    int hl = parse_hw((char *)a[2], hw, NG_MAXADDRARP), i;

    if (hl <= 0) {
      if (!quiet)
	Printf((STRPTR)PROG ": '%s' is not a hardware address "
	       "(expected aa:bb:cc:dd:ee:ff)\n", a[2]);
      rc = RETURN_ERROR;
    } else {
      set_pa(&ar, addr);
      ar.arp_ha.sa_len = (UBYTE)(hl + 2);
      for (i = 0; i < hl; i++) ar.arp_ha.sa_data[i] = (char)hw[i];
      /* Permanent by default: a hand-set entry that quietly ages out is rarely
       * what was meant. TEMP asks for the ordinary aging behaviour instead. */
      ar.arp_flags = (a[4] ? 0 : NG_ATF_PERM) | (a[3] ? NG_ATF_PUBL : 0);
      if (ng_ioctl(s, NG_SIOCSARP, &ar) != 0) {
	if (!quiet)
	  Printf((STRPTR)PROG ": cannot set that entry, errno %ld\n", ng_errno());
	rc = RETURN_ERROR;
      } else if (!quiet) {
	set_pa(&ar, addr);
	ar.arp_ha.sa_len = (UBYTE)(NG_MAXADDRARP + 2);
	if (ng_ioctl(s, NG_SIOCGARP, &ar) == 0) show_entry(&ar);
      }
    }

  } else {					/* show one */
    set_pa(&ar, addr);
    ar.arp_ha.sa_len = (UBYTE)(NG_MAXADDRARP + 2);
    if (ng_ioctl(s, NG_SIOCGARP, &ar) != 0) {
      if (!quiet) Printf((STRPTR)"%s: no entry\n", a[0]);
      rc = RETURN_WARN;
    } else if (!quiet) {
      show_entry(&ar);
    }
  }

  ng_closesocket(s);
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
