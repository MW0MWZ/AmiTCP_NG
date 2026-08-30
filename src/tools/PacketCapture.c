/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * PacketCapture -- capture packets off an interface, print a summary, and
 * optionally write a pcap file.
 *
 * We implemented the whole Berkeley Packet Filter vector set and shipped nothing
 * that used it. This is that something.
 *
 * THE PCAP FILE IS THE POINT. A one-line summary is useful while you are stood
 * at the machine; a pcap file is what lets someone hand over a capture of the
 * network that is actually failing and have it opened in Wireshark by somebody
 * who is not in the room. Every remote diagnosis on this project so far has had
 * to work from a description of the symptom.
 *
 * FOUR THINGS THAT WOULD SILENTLY PRODUCE A WRONG FILE OR A SPINNING LOOP, each
 * checked against the implementation in src/net/bpf.c rather than assumed:
 *
 *   - Timestamps are Amiga system time, whose epoch is 1 Jan 1978. pcap readers
 *     assume Unix time, so without the offset below every capture opens dated
 *     eight years early and cannot be correlated with anything else.
 *
 *   - ng_bpf_read() REFUSES a buffer smaller than the channel's ring rather than
 *     part-filling it (bpf.c: `if (d->bd_hlen > len) return -EINVAL`), because a
 *     partial copy would split a bpf_hdr record. It does not consume the data,
 *     so reading with a guessed-too-small buffer fails forever. The buffer is
 *     therefore sized from BIOCGBLEN, and a failure to ask is fatal.
 *
 *   - Ctrl-C arrives as EINTR out of the blocking read, not as a signal we get
 *     to poll -- tsleep() returns EINTR for anything in the SocketBase interrupt
 *     mask, which defaults to SIGBREAKF_CTRL_C. Treating that as an error would
 *     make every normal "stop now" print a failure and exit non-zero.
 *
 *   - The ioctl request numbers encode sizeof() of their argument, so the struct
 *     mirrors here must match the kernel's exactly. A mismatch does not misbehave
 *     subtly: the request code itself changes and the call fails outright. They
 *     are asserted at compile time rather than trusted.
 *
 * Build: m68k-amigaos-gcc -noixemul -O2 -m68000 -Isrc/tools \
 *          src/tools/PacketCapture.c -o PacketCapture
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase = 0;

#include "ng_lvo.h"

#define PROG		"PacketCapture"

/* ---- mirrored structures, asserted against the kernel's -------------------- */

struct ng_timeval { ULONG tv_secs, tv_micro; };
struct ng_ifreq   { char ifr_name[16]; char ifr_pad[16]; };	/* IFNAMSIZ + union tail */
struct ng_bpfstat { ULONG bs_recv, bs_drop; };
struct ng_bpfhdr  { struct ng_timeval bh_tstamp; ULONG bh_caplen, bh_datalen;
                    UWORD bh_hdrlen; };
struct ng_bpfinsn { UWORD code; UBYTE jt, jf; LONG k; };
struct ng_bpfprog { ULONG bf_len; struct ng_bpfinsn *bf_insns; };

/* Checked against net/bpf.h and net/if.h: ifreq 32, timeval 8, bpf_stat 8,
 * bpf_hdr 18, bpf_insn 8, bpf_program 8. See the header comment for why a
 * mismatch is not survivable. */
typedef char ng_bpf_layout_check[(sizeof(struct ng_ifreq)   == 32 &&
                                  sizeof(struct ng_timeval) ==  8 &&
                                  sizeof(struct ng_bpfstat) ==  8 &&
                                  sizeof(struct ng_bpfhdr)  == 18 &&
                                  sizeof(struct ng_bpfinsn) ==  8 &&
                                  sizeof(struct ng_bpfprog) ==  8) ? 1 : -1];

/* _IO/_IOR/_IOW, mirrored from sys/ioctl.h. */
#define NG_IOCPARM(x)	(((ULONG)sizeof(x) & 0x1fff) << 16)
#define NG_IO(g,n)	(0x20000000UL | ((ULONG)(g) << 8) | (ULONG)(n))
#define NG_IOR(g,n,t)	(0x40000000UL | NG_IOCPARM(t) | ((ULONG)(g) << 8) | (ULONG)(n))
#define NG_IOW(g,n,t)	(0x80000000UL | NG_IOCPARM(t) | ((ULONG)(g) << 8) | (ULONG)(n))

#define BIOCGBLEN	NG_IOR('B', 102, ULONG)
#define BIOCSETF	NG_IOW('B', 103, struct ng_bpfprog)
#define BIOCPROMISC	NG_IO ('B', 105)
#define BIOCGDLT	NG_IOR('B', 106, ULONG)
#define BIOCSETIF	NG_IOW('B', 108, struct ng_ifreq)
#define BIOCSRTIMEOUT	NG_IOW('B', 109, struct ng_timeval)
#define BIOCGSTATS	NG_IOR('B', 111, struct ng_bpfstat)
#define BIOCIMMEDIATE	NG_IOW('B', 112, ULONG)

#define BPF_WORDALIGN(x)	(((x) + 3) & ~3UL)

/* Filter opcode for the one-instruction snaplen program: "return K bytes".
 * BPF_RET (0x06) | BPF_K (0x00). See net/bpf.h. */
#define BPF_RET_K	0x0006

#define NG_EINTR	4		/* sys/errno.h */

/* No truncation. A capture channel with no filter installed captures whole
 * frames, so this is what the default run genuinely does. */
#define SNAP_FULL	65535UL

/*
 * Amiga system time counts from 1 Jan 1978; pcap counts from 1 Jan 1970. Eight
 * years, two of them leap: 2922 days.
 */
#define AMIGA_TO_UNIX_EPOCH	252460800UL

/* Data-link types, as BIOCGDLT reports them (bpf_dlt_of(): loopback has no link
 * header, everything else here is SANA-II Ethernet framing). */
#define DLT_NULL	0
#define DLT_EN10MB	1

static int quiet = 0;

/*
 * Sticky: cleared the moment any Write() to the pcap file comes up short.
 * Without it the tool would print "Wrote <file>" over a truncated capture --
 * and a capture you cannot trust is worse than no capture, because it gets
 * analysed anyway. A full RAM: disk mid-run is the ordinary way this happens.
 */
static int write_ok = 1;

/* ------------------------------------------------------------------ */

static void put_bytes(BPTR fh, const void *p, LONG len)
{
  if (write_ok && Write(fh, (APTR)p, len) != len)
    write_ok = 0;
}

static void put32(BPTR fh, ULONG v)
{
  UBYTE b[4];
  b[0] = (UBYTE)(v >> 24); b[1] = (UBYTE)(v >> 16);
  b[2] = (UBYTE)(v >> 8);  b[3] = (UBYTE)v;
  put_bytes(fh, b, 4);
}

static void put16(BPTR fh, UWORD v)
{
  UBYTE b[2];
  b[0] = (UBYTE)(v >> 8); b[1] = (UBYTE)v;
  put_bytes(fh, b, 2);
}

/*
 * The libpcap file header. Written big-endian because that is what this machine
 * is -- the magic number is precisely what tells a reader which way round the
 * file is, so a big-endian file is as valid as a little-endian one and needs no
 * byte swapping at either end.
 */
static void pcap_header(BPTR fh, ULONG snaplen, ULONG dlt)
{
  put32(fh, 0xa1b2c3d4UL);	/* magic */
  put16(fh, 2); put16(fh, 4);	/* version 2.4 */
  put32(fh, 0);			/* thiszone: timestamps are UTC */
  put32(fh, 0);			/* sigfigs */
  put32(fh, snaplen);
  put32(fh, dlt);
}

static void pcap_packet(BPTR fh, const struct ng_bpfhdr *h, const UBYTE *data)
{
  put32(fh, h->bh_tstamp.tv_secs + AMIGA_TO_UNIX_EPOCH);
  put32(fh, h->bh_tstamp.tv_micro);
  put32(fh, h->bh_caplen);
  put32(fh, h->bh_datalen);
  if (h->bh_caplen > 0)
    put_bytes(fh, data, (LONG)h->bh_caplen);
}

/* ------------------------------------------------------------------ */

static void print_ip(const UBYTE *a)
{
  Printf((STRPTR)"%ld.%ld.%ld.%ld", (LONG)a[0], (LONG)a[1], (LONG)a[2], (LONG)a[3]);
}

/*
 * One line per packet. Deliberately shallow -- enough to see what is flowing and
 * whether it is the traffic you expected, not a protocol decoder. Anything
 * deeper belongs in the pcap file, where a real analyser can do it properly.
 */
static void summarise(ULONG dlt, const UBYTE *p, ULONG caplen, ULONG datalen)
{
  ULONG type;

  if (dlt != DLT_EN10MB || caplen < 14) {
    Printf((STRPTR)"  %ld bytes\n", (LONG)datalen);
    return;
  }
  type = ((ULONG)p[12] << 8) | p[13];

  if (type == 0x0800 && caplen >= 34) {		/* IPv4 */
    const UBYTE *ip = p + 14;
    ULONG ihl = (ULONG)(ip[0] & 0x0F) * 4;
    const char *proto = "IP";
    if (ip[9] == 1)       proto = "ICMP";
    else if (ip[9] == 6)  proto = "TCP";
    else if (ip[9] == 17) proto = "UDP";
    Printf((STRPTR)"  %s ", (LONG)proto);
    print_ip(ip + 12);
    /* Ports only when the L4 header really is inside the captured bytes: a short
     * snaplen truncates mid-packet, and reading past caplen would be reading the
     * next record's header as if it were this packet's payload. */
    if ((ip[9] == 6 || ip[9] == 17) && ihl >= 20 && caplen >= 14 + ihl + 4) {
      const UBYTE *l4 = ip + ihl;
      Printf((STRPTR)":%ld > ", (LONG)(((ULONG)l4[0] << 8) | l4[1]));
      print_ip(ip + 16);
      Printf((STRPTR)":%ld", (LONG)(((ULONG)l4[2] << 8) | l4[3]));
    } else {
      Printf((STRPTR)" > ");
      print_ip(ip + 16);
    }
    Printf((STRPTR)"  %ld bytes\n", (LONG)datalen);
  } else if (type == 0x0806 && caplen >= 42) {	/* ARP */
    const UBYTE *ar = p + 14;
    if ((((ULONG)ar[6] << 8) | ar[7]) == 1) {
      Printf((STRPTR)"  ARP who-has ");
      print_ip(ar + 24);
      Printf((STRPTR)" tell ");
      print_ip(ar + 14);
    } else {
      Printf((STRPTR)"  ARP reply from ");
      print_ip(ar + 14);
    }
    Printf((STRPTR)"  %ld bytes\n", (LONG)datalen);
  } else {
    Printf((STRPTR)"  type 0x%04lx  %ld bytes\n", (LONG)type, (LONG)datalen);
  }
}

/* ------------------------------------------------------------------ */

int main(void)
{
  struct RDArgs *rda;
  LONG a[6];
  struct ng_ifreq ifr;
  struct ng_timeval tmo;
  struct ng_bpfstat st;
  const char *ifname, *fname;
  UBYTE *buf = NULL;
  BPTR  out = 0;
  ULONG blen = 0, dlt = 0, imm = 1, snaplen;
  LONG  chan = -1, want, got, i, n;
  LONG  captured = 0;
  int   rc = RETURN_OK, stop = 0;

  for (i = 0; i < 6; i++) a[i] = 0;

  rda = ReadArgs((STRPTR)"INTERFACE/A,FILE/K,COUNT/N,SNAPLEN/N,PROMISC/S,QUIET/S",
                 a, NULL);
  if (!rda) { PrintFault(IoErr(), (STRPTR)PROG); return RETURN_ERROR; }

  ifname  = (const char *)a[0];
  fname   = a[1] ? (const char *)a[1] : NULL;
  want    = a[2] ? *(LONG *)a[2] : 0;		/* 0 = until Ctrl-C */
  quiet   = a[5] != 0;
  if (want < 0) want = 0;

  /* No SNAPLEN means no truncation, and the pcap header must say so: an
   * unfiltered channel captures whole frames, so writing any smaller number
   * here would be the file claiming a truncation that never happened. */
  snaplen = a[3] ? (ULONG)*(LONG *)a[3] : SNAP_FULL;
  if (a[3]) {
    if ((LONG)snaplen < 64)   snaplen = 64;
    if (snaplen > SNAP_FULL)  snaplen = SNAP_FULL;
  }

  if ((SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4L)) == NULL) {
    Printf((STRPTR)PROG ": cannot open bsdsocket.library v4\n");
    FreeArgs(rda);
    return RETURN_FAIL;
  }

  /* -1 means "any free channel": the library picks and claims one atomically
   * under splimp() and returns its number. Scanning for a free channel from out
   * here would race a second capture doing the same thing. */
  if ((chan = ng_bpf_open(-1)) < 0) {
    Printf((STRPTR)PROG ": cannot open a capture channel (errno %ld)\n", ng_errno());
    rc = RETURN_FAIL; goto out;
  }

  /* Attach to the interface BEFORE anything that depends on the medium: the
   * data-link type is only meaningful once the channel is bound. */
  for (i = 0; i < 16; i++) { ifr.ifr_name[i] = 0; ifr.ifr_pad[i] = 0; }
  for (i = 0; ifname[i] && i < 15; i++) ifr.ifr_name[i] = ifname[i];
  if (ng_bpf_ioctl(chan, BIOCSETIF, &ifr) < 0) {
    Printf((STRPTR)PROG ": cannot capture on '%s' (errno %ld). Is that the "
                   "interface name? ShowNetStatus lists them.\n",
           (LONG)ifname, ng_errno());
    rc = RETURN_ERROR; goto out;
  }

  if (ng_bpf_ioctl(chan, BIOCIMMEDIATE, &imm) < 0 && !quiet)
    Printf((STRPTR)PROG ": warning: immediate mode refused; packets may appear "
                   "in batches\n");

  /* A read timeout so a silent network still returns to this loop periodically.
   * Ctrl-C does not depend on it (that arrives as EINTR from the blocking read),
   * but without it a capture of nothing sits inside the library indefinitely. */
  tmo.tv_secs = 1; tmo.tv_micro = 0;
  if (ng_bpf_ioctl(chan, BIOCSRTIMEOUT, &tmo) < 0 && !quiet)
    Printf((STRPTR)PROG ": warning: read timeout refused; this capture will "
                   "wait for a packet rather than ticking\n");

  /*
   * SNAPLEN. There is no "capture only N bytes" ioctl -- the number of bytes
   * kept is whatever the channel's FILTER returns, and a channel with no filter
   * keeps whole frames. So truncation is expressed as a one-instruction filter
   * program: "return snaplen", which bpf_catchpacket() then takes the smaller
   * of against the real frame length.
   *
   * Only installed when the user actually asked for it. The default is to keep
   * everything, and installing a no-op filter to say so would just add a way for
   * the default case to fail.
   */
  if (a[3]) {
    struct ng_bpfinsn prog[1];
    struct ng_bpfprog bp;

    prog[0].code = BPF_RET_K; prog[0].jt = 0; prog[0].jf = 0;
    prog[0].k    = (LONG)snaplen;
    bp.bf_len    = 1;
    bp.bf_insns  = prog;
    if (ng_bpf_ioctl(chan, BIOCSETF, &bp) < 0) {
      if (!quiet)
      /* Say what will actually be in the file, and make the file agree. A pcap
       * header claiming snaplen 64 over untruncated packets is a lie that
       * survives the run and misleads whoever opens it later. */
      Printf((STRPTR)PROG ": warning: SNAPLEN %ld refused (errno %ld); packets "
                     "will NOT be truncated\n", (LONG)snaplen, ng_errno());
      snaplen = SNAP_FULL;
    }
  }

  if (a[4] && ng_bpf_ioctl(chan, BIOCPROMISC, NULL) < 0 && !quiet)
    Printf((STRPTR)PROG ": warning: promiscuous mode refused by the driver\n");

  /* Say so if we had to guess. Every other optional ioctl here warns on
   * failure, and this one has the worst silent outcome of the lot: guessing
   * Ethernet for what is really a loopback capture writes DLT_EN10MB into the
   * file header, and Wireshark then misdecodes every packet in it -- reading
   * fourteen bytes of IP header as an Ethernet header. A wrong capture that
   * looks plausible is worse than one that fails. */
  if (ng_bpf_ioctl(chan, BIOCGDLT, &dlt) < 0) {
    dlt = DLT_EN10MB;
    if (!quiet)
    Printf((STRPTR)PROG ": warning: could not ask the interface what it is "
                   "(errno %ld); assuming Ethernet framing in the capture "
                   "file\n", ng_errno());
  }

  /* Not a guess: a buffer smaller than the ring makes every read fail with
   * EINVAL without consuming anything. If the library will not tell us the
   * size, there is no safe size to pick. */
  if (ng_bpf_ioctl(chan, BIOCGBLEN, &blen) < 0 || blen == 0) {
    Printf((STRPTR)PROG ": cannot determine the capture buffer size (errno %ld)\n",
           ng_errno());
    rc = RETURN_FAIL; goto out;
  }
  if ((buf = (UBYTE *)AllocVec(blen, MEMF_PUBLIC)) == NULL) {
    Printf((STRPTR)PROG ": out of memory (%ld byte buffer)\n", (LONG)blen);
    rc = RETURN_FAIL; goto out;
  }

  if (fname) {
    if ((out = Open((STRPTR)fname, MODE_NEWFILE)) == 0) {
      Printf((STRPTR)PROG ": cannot create '%s'\n", (LONG)fname);
      rc = RETURN_ERROR; goto out;
    }
    pcap_header(out, snaplen, dlt);
  }

  if (!quiet) {
    Printf((STRPTR)PROG ": capturing on %s (%s)", (LONG)ifname,
           (LONG)(dlt == DLT_EN10MB ? "Ethernet" : "no link layer"));
    if (fname) Printf((STRPTR)", writing %s", (LONG)fname);
    Printf((STRPTR)". Ctrl-C to stop.\n");
  }

  while (!stop && (want == 0 || captured < want)) {
    /* Catches a Ctrl-C pressed while we were busy printing the last batch; one
     * pressed during the read itself comes back as EINTR below. */
    if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) break;

    got = ng_bpf_read(chan, buf, (LONG)blen);
    if (got < 0) {
      LONG e = ng_errno();
      if (e == NG_EINTR) break;			/* Ctrl-C: a normal stop */
      Printf((STRPTR)PROG ": read failed (errno %ld)\n", e);
      rc = RETURN_ERROR;
      break;
    }
    if (got == 0)
      continue;					/* read timeout: nothing arrived */

    /*
     * One read returns SEVERAL packets: a run of bpf_hdr + data records, each
     * padded to the next long boundary. bh_hdrlen is taken from the record
     * rather than from sizeof -- the library decides the padding between header
     * and payload, and trusting our own struct size is how a walk ends up
     * starting mid-packet and decoding noise from there on.
     */
    n = 0;
    while (n + (LONG)sizeof(struct ng_bpfhdr) <= got) {
      struct ng_bpfhdr *h = (struct ng_bpfhdr *)(buf + n);
      ULONG reclen;

      /* Bound bh_hdrlen at BOTH ends. The library only ever writes the fixed
       * aligned header size, so the upper bound cannot trigger today -- but
       * every other field out of that buffer is range-checked before it is
       * used as an offset, and this one was trusted merely because of who
       * writes it. It is the offset the packet pointer is built from. */
      if (h->bh_hdrlen < sizeof(struct ng_bpfhdr) ||
          h->bh_hdrlen > sizeof(struct ng_bpfhdr) + 64 ||
          h->bh_caplen > blen)
        break;					/* malformed: stop, do not guess */
      reclen = BPF_WORDALIGN((ULONG)h->bh_hdrlen + h->bh_caplen);
      if (n + (LONG)((ULONG)h->bh_hdrlen + h->bh_caplen) > got)
        break;					/* record runs past what we were given */

      captured++;
      if (!quiet)
        summarise(dlt, buf + n + h->bh_hdrlen, h->bh_caplen, h->bh_datalen);
      if (out)
        pcap_packet(out, h, buf + n + h->bh_hdrlen);

      n += (LONG)reclen;
      if (want != 0 && captured >= want) { stop = 1; break; }
    }

    /* The file was the point. Once a write has failed the rest of the capture
     * cannot reach it, so an open-ended run (COUNT 0) would otherwise sit there
     * filling a full disk until Ctrl-C. Stop and report. */
    if (out && !write_ok)
      break;
  }

  if (!quiet) {
    Printf((STRPTR)"\n%ld packet(s) captured", captured);
    /* bs_recv/bs_drop are the library's own view: how many the filter matched,
     * and how many it had to throw away because this program was not reading
     * fast enough. A non-zero drop count means the capture has holes in it. */
    if (ng_bpf_ioctl(chan, BIOCGSTATS, &st) >= 0)
      Printf((STRPTR)", %ld matched the filter, %ld dropped",
             (LONG)st.bs_recv, (LONG)st.bs_drop);
    Printf((STRPTR)".\n");
    if (out && write_ok)
      Printf((STRPTR)"Wrote %s -- open it with Wireshark, or tcpdump -r.\n",
             (LONG)fname);
  }
  if (out && !write_ok) {
    Printf((STRPTR)PROG ": WRITE FAILED -- '%s' is incomplete and should not be "
                   "trusted (disk full?)\n", (LONG)fname);
    rc = RETURN_ERROR;
  }

out:
  if (out) Close(out);
  if (buf) FreeVec(buf);
  if (chan >= 0) ng_bpf_close(chan);
  if (SocketBase) CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
