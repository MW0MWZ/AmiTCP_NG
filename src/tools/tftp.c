/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * tftp -- Trivial File Transfer Protocol client (RFC 1350, with RFC 2348 blksize).
 *
 *   tftp GET <host> <remotefile> [<localfile>]
 *   tftp PUT <host> <remotefile> [<localfile>]
 *   options: PORT <n>  BLKSIZE <n>  ASCII
 *
 * The file argument is ALWAYS the name on the server and the optional one is
 * always the local file, for GET and PUT alike -- so PUT reads <localfile> and
 * stores it as <remotefile>. Defaulting the local name to the remote one means
 * the common case needs no second argument in either direction.
 *
 * TFTP is lockstep: one data block, one acknowledgement, repeat. With the RFC
 * 1350 block size of 512 bytes that costs a full round trip per 512 bytes, which
 * on anything faster than a modem is the entire bottleneck -- so this client asks
 * for 1428-byte blocks by default (the most that still fits an Ethernet MTU
 * without IP fragmentation) and falls back cleanly when the server does not
 * support the option. A server that ignores the option answers with DATA block 1
 * instead of an OACK, which is the RFC 2348 way of declining, and we then use 512.
 *
 * TWO THINGS THAT BITE EVERY TFTP IMPLEMENTATION, handled explicitly below:
 *
 *  - The TRANSFER IDENTIFIER. The request goes to port 69, but the server replies
 *    from a NEW, ephemeral port and the whole rest of the transfer uses that. We
 *    adopt the source port of the first valid reply and then ignore anything from
 *    a different port -- which is also the only thing standing between this and a
 *    trivially spoofed transfer, since UDP offers nothing else.
 *
 *  - The SORCERER'S APPRENTICE bug. If an acknowledgement is delayed rather than
 *    lost, a naive client re-sends the ACK, the server re-sends the block, and the
 *    two sides amplify each other for the rest of the transfer. We only ever
 *    acknowledge the block we expected; a duplicate block is acknowledged once and
 *    never re-triggers our own retransmit timer.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase = 0;

/* ---- bsdsocket vectors (LVO = -30 - 6*index, from the SFD vector order) ----- */
static long ng_socket(long d, long t, long p) {			/* -30  (d0,d1,d2) */
  register long _d0 __asm("d0")=d, _d1 __asm("d1")=t, _d2 __asm("d2")=p;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a0","a1","memory");
  return _d0;
}
static long ng_sendto(long s, void *b, long l, long f, void *to, long tl) { /* -60 */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f, _d3 __asm("d3")=tl;
  register void *_a0 __asm("a0")=b, *_a1 __asm("a1")=to;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-60)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_d3)
                       :"r"(_a6):"memory");
  return _d0;
}
static long ng_recvfrom(long s, void *b, long l, long f, void *from, long *fl) { /* -72 */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f;
  register void *_a0 __asm("a0")=b, *_a1 __asm("a1")=from;
  register long *_a2 __asm("a2")=fl;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-72)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_a2)
                       :"r"(_a6):"memory");
  return _d0;
}
static void ng_close(long s) {					/* -120 (d0) */
  register long _d0 __asm("d0")=s; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_a6):"d1","a0","a1","memory");
}
static long ng_waitselect(long n, void *r, void *w, void *e, void *tv, ULONG *sig) { /* -126 */
  register long _d0 __asm("d0")=n; register void *_a0 __asm("a0")=r, *_a1 __asm("a1")=w;
  register void *_a2 __asm("a2")=e, *_a3 __asm("a3")=tv; register ULONG *_d1 __asm("d1")=sig;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-126)":"+r"(_d0),"+r"(_a0),"+r"(_a1),"+r"(_a2),"+r"(_a3),"+r"(_d1)
                       :"r"(_a6):"memory");
  return _d0;
}
static long ng_errno(void) {					/* -162 */
  register long _d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0;
}
static ULONG ng_inet_addr(const char *s) {			/* -180 */
  register ULONG _d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-180)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}
static void *ng_gethostbyname(const char *s) {			/* -210 */
  register void *_d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-210)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}

/* ---- structures we touch ---------------------------------------------------- */
struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE zero[8]; };
/* recvfrom writes a whole sockaddr_in here; anything smaller is an overrun. */
typedef char ng_sin_must_be_16[(sizeof(struct ng_sin) >= 16) ? 1 : -1];
struct ng_hostent { char *h_name; char **h_aliases; long h_addrtype, h_length; char **h_addr_list; };
struct ng_tv { long tv_secs, tv_micro; };

#define NG_AF_INET	2
#define NG_SOCK_DGRAM	2

/* TFTP opcodes */
#define OP_RRQ	1
#define OP_WRQ	2
#define OP_DATA	3
#define OP_ACK	4
#define OP_ERROR 5
#define OP_OACK	6

#define TFTP_PORT	69
#define BLK_MIN		8
#define BLK_MAX		1428		/* fits a 1500-byte MTU: 1428+4+8+20 */
#define BLK_DEFAULT	512		/* RFC 1350, and the fallback */
#define RETRIES		5
#define TIMEOUT_SECS	3
/* A packet that is not ours must never count as progress NOR as a retry -- but it
 * must not let us spin either. Bound how many we will ignore per block before
 * giving up, so a peer that keeps babbling cannot wedge the transfer. */
#define MAX_STRAY	200
#define MAX_FD		512		/* what the fd_set below can address */

static UBYTE pkt[BLK_MAX + 4];		/* receive buffer: opcode+block+data */
static UBYTE out[BLK_MAX + 4];

/* ---- small helpers (no sprintf: -noixemul) ---------------------------------- */

static int ci_eq(const char *a, const char *b)
{
  while (*a && *b) {
    char ca = *a++, cb = *b++;
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
    if (ca != cb) return 0;
  }
  return *a == *b;
}

static int str_put(UBYTE *dst, int at, const char *s, int cap)
{
  while (*s && at < cap - 1) dst[at++] = (UBYTE)*s++;
  if (at < cap) dst[at++] = 0;			/* the NUL is part of the field */
  return at;
}

static int num_put(UBYTE *dst, int at, long v, int cap)
{
  char b[12]; int i = 11;
  b[i--] = 0;
  if (v == 0) b[i--] = '0';
  while (v > 0 && i >= 0) { b[i--] = (char)('0' + (v % 10)); v /= 10; }
  return str_put(dst, at, b + i + 1, cap);
}

/* Parse at most 6 digits. A hostile OACK can send hundreds, and letting a signed
 * long overflow is undefined behaviour -- harmless here only by accident, because
 * the caller re-checks the range. Do not rely on that accident. */
static long num_get(const UBYTE *s, int max)
{
  long v = 0; int i = 0;
  while (i < max && i < 6 && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
  return v;
}

/* Wait for readability. 1 = ready, 0 = timed out, -1 = error/Ctrl-C. */
static int wait_readable(long s, long secs)
{
  ULONG fds[16];
  struct ng_tv tv;
  ULONG sigs = SIGBREAKF_CTRL_C;
  long r;
  int i;

  /* fds[16] addresses fd 0..511. A larger fd would write past it -- on a machine
   * with no MMU that is silent stack corruption, not a crash, so refuse instead. */
  if (s < 0 || s >= MAX_FD) return -1;
  for (i = 0; i < 16; i++) fds[i] = 0;
  fds[s / 32] |= (1UL << (s % 32));
  tv.tv_secs = secs; tv.tv_micro = 0;

  r = ng_waitselect(s + 1, fds, (void *)0, (void *)0, &tv, &sigs);
  if (sigs & SIGBREAKF_CTRL_C) return -1;
  if (r < 0) return -1;
  return (r > 0) ? 1 : 0;
}

/* The transfer identifier check. UDP offers nothing else, so compare BOTH the
 * address and the port -- matching on the port alone lets any host that can guess
 * or observe it inject into the transfer. */
static int from_peer(const struct ng_sin *from, const struct ng_sin *srv)
{
  return from->sin_port == srv->sin_port && from->sin_addr == srv->sin_addr;
}

static void show_server_error(const UBYTE *p, long n)
{
  char msg[128];
  int i = 0;
  long code = ((long)p[2] << 8) | p[3];
  while (i < (int)sizeof(msg) - 1 && 4 + i < n && p[4 + i]) { msg[i] = (char)p[4 + i]; i++; }
  msg[i] = 0;
  Printf((STRPTR)"tftp: server error %ld: %s\n", code, (LONG)msg);
}

/* ---- the transfer ----------------------------------------------------------- */

/*
 * Send `req` (already built, `reqlen` bytes) to the server and wait for the first
 * reply, adopting its source port as the transfer identifier. Returns the reply
 * length, or -1. On return `blksize` has been reduced to 512 if the server
 * declined the option by answering with DATA rather than OACK.
 */
static long start_transfer(long s, struct ng_sin *srv, UBYTE *req, int reqlen,
                           long *blksize, int wrq)
{
  struct ng_sin from;
  long fromlen, n;
  int try;

  {
    int stray = 0, need_send = 1;

    try = 0;
    for (;;) {
      int w;

      if (need_send) {
        if (ng_sendto(s, req, reqlen, 0, srv, sizeof(*srv)) < 0) {
          Printf((STRPTR)"tftp: sendto failed (errno %ld)\n", ng_errno());
          return -1;
        }
        need_send = 0;
      }

      w = wait_readable(s, TIMEOUT_SECS);
      if (w < 0) { Printf((STRPTR)"tftp: aborted\n"); return -1; }
      if (w == 0) {				/* genuine timeout: resend */
        if (++try > RETRIES) {
          Printf((STRPTR)"tftp: no response from server\n");
          return -1;
        }
        need_send = 1;
        continue;
      }

      fromlen = sizeof(from);
      n = ng_recvfrom(s, pkt, sizeof(pkt), 0, &from, &fromlen);

      /*
       * THE ADDRESS MUST MATCH, even though the port cannot be checked yet.
       * This reply is what establishes the transfer identifier -- the server
       * answers from a NEW ephemeral port, which we are about to adopt. Every
       * LATER packet is checked with from_peer() on address AND port, so
       * without this check the one packet that decides who we are talking to
       * was the only unchecked one. Anyone who guessed the local port could
       * seize it: our sends still went to the real server, but its replies
       * then came from a port that no longer matched and were discarded, which
       * stalls the transfer without any address spoofing at all.
       *
       * Rejected here WITHOUT resending and WITHOUT costing a retry, bounded
       * separately -- the same split do_get and do_put already use, so a peer
       * that keeps babbling cannot burn the retry budget or make us retransmit
       * on every stray packet.
       */
      if (n < 4 || fromlen < (long)sizeof(from) ||
          from.sin_addr != srv->sin_addr) {
        if (++stray > MAX_STRAY) {
          Printf((STRPTR)"tftp: too many stray packets -- giving up\n");
          return -1;
        }
        continue;
      }

      /* Only now is this packet known to be from the server, so only now may it
       * abort the transfer with an error. */
      if (pkt[1] == OP_ERROR) { show_server_error(pkt, n); return -1; }

      srv->sin_port = from.sin_port;		/* adopt the transfer identifier */

    if (pkt[1] == OP_OACK) {
      /* Parse the options we asked for. Only blksize is negotiated here; a
       * server may return a SMALLER value than requested and we must honour it. */
      int i = 2;
      while (i < n) {
        int keyat = i;
        while (i < n && pkt[i]) i++;
        if (i >= n) break;
        i++;					/* past the key's NUL */
        if (ci_eq((const char *)&pkt[keyat], "blksize")) {
          long v = num_get(&pkt[i], (int)(n - i));
          if (v >= BLK_MIN && v <= *blksize) *blksize = v;
        }
        while (i < n && pkt[i]) i++;
        i++;					/* past the value's NUL */
      }
      return n;
    }

    /* No OACK: the server declined the options (RFC 2348), so the transfer runs
     * at the default block size. For a read that first reply is already DATA
     * block 1 and the caller must not discard it. */
      *blksize = BLK_DEFAULT;
      return n;
    }
  }
  /* Not reached: every path inside the loop returns. */
}

static int do_get(long s, struct ng_sin *srv, UBYTE *req, int reqlen,
                  const char *localfile, long blksize)
{
  struct ng_sin from;
  long fromlen, n;
  BPTR fh;
  UWORD expect = 1;
  ULONG total = 0;
  int have_pkt, rc = RETURN_OK;

  n = start_transfer(s, srv, req, reqlen, &blksize, 0);
  if (n < 0) return RETURN_ERROR;
  have_pkt = (pkt[1] == OP_DATA);		/* OACK consumed; DATA is the first block */

  fh = Open((STRPTR)localfile, MODE_NEWFILE);
  if (!fh) { Printf((STRPTR)"tftp: cannot create %s\n", (LONG)localfile); return RETURN_FAIL; }

  if (!have_pkt) {
    /* We got an OACK: acknowledge block 0 to start the data flowing. */
    out[0] = 0; out[1] = OP_ACK; out[2] = 0; out[3] = 0;
    ng_sendto(s, out, 4, 0, srv, sizeof(*srv));
  }

  for (;;) {
    UWORD blk;
    int try;

    if (!have_pkt) {
      /* Wait for a packet that is genuinely ours. A stray one is ignored WITHOUT
       * resending and WITHOUT touching the retry budget -- otherwise any peer
       * that keeps sending packets refills `try` forever and the transfer can
       * never time out. Only an actual timeout resends and costs a retry. */
      int stray = 0;
      try = 0;
      for (;;) {
        int w = wait_readable(s, TIMEOUT_SECS);
        if (w < 0) { Printf((STRPTR)"tftp: aborted\n"); rc = RETURN_ERROR; goto done; }
        if (w == 0) {
          if (++try > RETRIES) {
            Printf((STRPTR)"tftp: timed out after block %ld\n", (LONG)(expect - 1));
            rc = RETURN_ERROR; goto done;
          }
          /* Re-acknowledge the last block we accepted; that asks the server to
           * resend, without ever acking a block we have not seen. */
          out[0] = 0; out[1] = OP_ACK;
          out[2] = (UBYTE)((expect - 1) >> 8); out[3] = (UBYTE)((expect - 1) & 0xFF);
          ng_sendto(s, out, 4, 0, srv, sizeof(*srv));
          continue;
        }
        fromlen = sizeof(from);
        n = ng_recvfrom(s, pkt, sizeof(pkt), 0, &from, &fromlen);
        if (n >= 4 && fromlen >= (long)sizeof(from) && from_peer(&from, srv)) break;
        if (++stray > MAX_STRAY) {
          Printf((STRPTR)"tftp: too many stray packets -- giving up\n");
          rc = RETURN_ERROR; goto done;
        }
      }
    }
    have_pkt = 0;

    if (pkt[1] == OP_ERROR) { show_server_error(pkt, n); rc = RETURN_ERROR; goto done; }
    if (pkt[1] != OP_DATA) continue;

    blk = (UWORD)(((UWORD)pkt[2] << 8) | pkt[3]);
    if (blk != expect) {
      /* A duplicate (the server did not see our ack). Acknowledge it once so it
       * moves on, but do NOT write it again -- and do not treat it as progress. */
      out[0] = 0; out[1] = OP_ACK; out[2] = pkt[2]; out[3] = pkt[3];
      ng_sendto(s, out, 4, 0, srv, sizeof(*srv));
      continue;
    }

    /* The server agreed a block size; a longer block breaks that contract. It
     * cannot overrun pkt[] (recvfrom bounded it), but writing it would silently
     * accept more per block than was negotiated. */
    if (n - 4 > blksize) {
      Printf((STRPTR)"tftp: server sent %ld bytes for a %ld-byte block\n",
             (LONG)(n - 4), blksize);
      rc = RETURN_ERROR; goto done;
    }

    if (n > 4) {
      if (Write(fh, (APTR)&pkt[4], n - 4) != n - 4) {
        Printf((STRPTR)"tftp: write to %s failed (disk full?)\n", (LONG)localfile);
        rc = RETURN_FAIL; goto done;
      }
      total += (ULONG)(n - 4);
    }

    out[0] = 0; out[1] = OP_ACK; out[2] = pkt[2]; out[3] = pkt[3];
    ng_sendto(s, out, 4, 0, srv, sizeof(*srv));

    if (n - 4 < blksize) break;			/* short block ends the transfer */
    expect++;
  }

  Printf((STRPTR)"tftp: received %ld bytes into %s (block size %ld)\n",
         (LONG)total, (LONG)localfile, blksize);
done:
  Close(fh);
  return rc;
}

static int do_put(long s, struct ng_sin *srv, UBYTE *req, int reqlen,
                  const char *localfile, long blksize)
{
  struct ng_sin from;
  long fromlen, n;
  BPTR fh;
  UWORD blk = 0;
  ULONG total = 0;
  int rc = RETURN_OK, last = 0;

  fh = Open((STRPTR)localfile, MODE_OLDFILE);
  if (!fh) { Printf((STRPTR)"tftp: cannot open %s\n", (LONG)localfile); return RETURN_FAIL; }

  n = start_transfer(s, srv, req, reqlen, &blksize, 1);
  if (n < 0) { Close(fh); return RETURN_ERROR; }
  /*
   * A WRQ has exactly three legal first replies (RFC 1350 / RFC 2347): ACK
   * block 0, an OACK if the server took our options, or ERROR. Anything else
   * must not be read as "go ahead and send". ERROR is not re-checked here --
   * start_transfer already returns -1 for it, so reaching this point means the
   * reply was not an error, and a second check would be dead code.
   */
  if (pkt[1] != OP_OACK &&
      !(pkt[1] == OP_ACK && ((UWORD)(((UWORD)pkt[2] << 8) | pkt[3])) == 0)) {
    Printf((STRPTR)"tftp: server did not acknowledge the write request\n");
    Close(fh);
    return RETURN_ERROR;
  }

  while (!last) {
    long got;
    int try;

    got = Read(fh, (APTR)&out[4], blksize);
    if (got < 0) { Printf((STRPTR)"tftp: read of %s failed\n", (LONG)localfile);
                   rc = RETURN_FAIL; goto done; }
    if (got < blksize) last = 1;		/* short block signals the end */

    blk++;
    out[0] = 0; out[1] = OP_DATA;
    out[2] = (UBYTE)(blk >> 8); out[3] = (UBYTE)(blk & 0xFF);

    /* Send, then wait for THIS block's ack. As in do_get: only a timeout resends
     * and costs a retry; a stray or stale packet is ignored without doing either,
     * so a chatty peer can neither refill the retry budget nor make us retransmit
     * on every packet it happens to send. */
    { int stray = 0;
      int sent = 0;
      try = 0;
      for (;;) {
        int w;
        if (!sent) {
          if (ng_sendto(s, out, got + 4, 0, srv, sizeof(*srv)) < 0) {
            Printf((STRPTR)"tftp: sendto failed (errno %ld)\n", ng_errno());
            rc = RETURN_ERROR; goto done;
          }
          sent = 1;
        }
        w = wait_readable(s, TIMEOUT_SECS);
        if (w < 0) { Printf((STRPTR)"tftp: aborted\n"); rc = RETURN_ERROR; goto done; }
        if (w == 0) {
          if (++try > RETRIES) {
            Printf((STRPTR)"tftp: timed out on block %ld\n", (LONG)blk);
            rc = RETURN_ERROR; goto done;
          }
          sent = 0;				/* resend this block */
          continue;
        }
        fromlen = sizeof(from);
        n = ng_recvfrom(s, pkt, sizeof(pkt), 0, &from, &fromlen);
        if (n >= 4 && fromlen >= (long)sizeof(from) && from_peer(&from, srv)) {
          if (pkt[1] == OP_ERROR) { show_server_error(pkt, n);
                                    rc = RETURN_ERROR; goto done; }
          if (pkt[1] == OP_ACK &&
              (UWORD)(((UWORD)pkt[2] << 8) | pkt[3]) == blk) break;
        }
        if (++stray > MAX_STRAY) {
          Printf((STRPTR)"tftp: too many stray packets -- giving up\n");
          rc = RETURN_ERROR; goto done;
        }
      }
    }
    total += (ULONG)got;
  }

  Printf((STRPTR)"tftp: sent %ld bytes from %s (block size %ld)\n",
         (LONG)total, (LONG)localfile, blksize);
done:
  Close(fh);
  return rc;
}

/* ------------------------------------------------------------------- main ---- */

int main(void)
{
  struct RDArgs *rda;
  LONG args[7];
  struct ng_sin srv;
  struct ng_hostent *hp;
  const char *cmd, *host, *file, *local;
  ULONG addr;
  long s, port = TFTP_PORT, blksize = BLK_MAX;
  int reqlen, get, i, rc;

  for (i = 0; i < 7; i++) args[i] = 0;
  rda = ReadArgs((STRPTR)"COMMAND/A,HOST/A,FILE/A,LOCAL,PORT/K/N,BLKSIZE/K/N,ASCII/S",
                 args, NULL);
  if (!rda) {
    Printf((STRPTR)"usage: tftp GET|PUT <host> <remotefile> [<localfile>]\n"
                   "       [PORT <n>] [BLKSIZE <n>] [ASCII]\n");
    return RETURN_FAIL;
  }
  cmd  = (const char *)args[0];
  host = (const char *)args[1];
  file = (const char *)args[2];
  local = args[3] ? (const char *)args[3] : file;
  if (args[4]) port = *(LONG *)args[4];
  if (args[5]) blksize = *(LONG *)args[5];

  get = ci_eq(cmd, "GET");
  if (!get && !ci_eq(cmd, "PUT")) {
    Printf((STRPTR)"tftp: first argument must be GET or PUT\n");
    FreeArgs(rda); return RETURN_FAIL;
  }
  if (blksize < BLK_MIN || blksize > BLK_MAX) {
    Printf((STRPTR)"tftp: BLKSIZE must be %ld..%ld\n", (LONG)BLK_MIN, (LONG)BLK_MAX);
    FreeArgs(rda); return RETURN_FAIL;
  }
  if (port < 1 || port > 65535) {
    Printf((STRPTR)"tftp: PORT must be 1..65535\n");
    FreeArgs(rda); return RETURN_FAIL;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    Printf((STRPTR)"tftp: bsdsocket.library v4+ not available.\n");
    FreeArgs(rda); return RETURN_FAIL;
  }

  addr = ng_inet_addr(host);
  if (addr == 0xFFFFFFFFUL) {
    hp = (struct ng_hostent *)ng_gethostbyname(host);
    if (!hp || !hp->h_addr_list || !hp->h_addr_list[0]) {
      Printf((STRPTR)"tftp: unknown host %s\n", (LONG)host);
      CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_ERROR;
    }
    { UBYTE *p = (UBYTE *)hp->h_addr_list[0];
      addr = ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3]; }
  }

  for (i = 0; i < (int)sizeof(srv); i++) ((char *)&srv)[i] = 0;
  srv.sin_len = sizeof(srv); srv.sin_family = NG_AF_INET;
  srv.sin_port = (UWORD)port; srv.sin_addr = addr;

  s = ng_socket(NG_AF_INET, NG_SOCK_DGRAM, 0);
  if (s < 0) {
    Printf((STRPTR)"tftp: socket failed (errno %ld)\n", ng_errno());
    CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL;
  }

  /* An over-long name would be silently truncated by str_put into a malformed
   * request; say so instead. 400 leaves ample room for the mode and options. */
  { int fl = 0; while (file[fl]) fl++;
    if (fl > 400) {
      Printf((STRPTR)"tftp: remote filename too long\n");
      ng_close(s); CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL;
    } }

  /* Build the request: opcode, filename, mode, then the blksize option. */
  out[0] = 0; out[1] = (UBYTE)(get ? OP_RRQ : OP_WRQ);
  reqlen = 2;
  reqlen = str_put(out, reqlen, file, (int)sizeof(out));
  reqlen = str_put(out, reqlen, args[6] ? "netascii" : "octet", (int)sizeof(out));
  if (blksize != BLK_DEFAULT) {
    reqlen = str_put(out, reqlen, "blksize", (int)sizeof(out));
    reqlen = num_put(out, reqlen, blksize, (int)sizeof(out));
  }

  rc = get ? do_get(s, &srv, out, reqlen, local, blksize)
           : do_put(s, &srv, out, reqlen, local, blksize);

  ng_close(s);
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
