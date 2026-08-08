/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ftp -- File Transfer Protocol client (RFC 959), passive mode.
 *
 *   ftp GET <host> <remotefile> [<localfile>]
 *   ftp PUT <host> <remotefile> [<localfile>]
 *   ftp DIR <host> [<path>]
 *   options: USER <name>  PASS <password>  PORT <n>  ASCII  QUIET
 *
 * As in tftp, the file argument is ALWAYS the name on the server and the
 * optional one is always the local file, in both directions.
 *
 * WHY THIS EXISTS. On a fresh Amiga carrying only this stack there is otherwise
 * no way to get any other software onto the machine. TFTP works but needs a TFTP
 * server, which almost nobody runs; FTP is what actually exists on the other end.
 * That makes this the difference between "the stack installs" and "the stack is
 * usable on its own". AmiTCP 3.0b2 shipped ncftp for the same reason.
 *
 * PASSIVE MODE ONLY, and that is deliberate rather than a shortcut. Active mode
 * asks the SERVER to open a connection back to us, which fails through any NAT,
 * any router doing the sane thing, and notably through the SLIRP setup this
 * project tests under. Passive mode has us open both connections, so it works
 * from behind anything. A client that defaults to active in 2026 is a client
 * that mostly does not work.
 *
 * NON-INTERACTIVE, one transfer per invocation. That is the useful shape on this
 * platform: it composes with scripts, needs no terminal handling, and avoids a
 * command loop that would be most of the code. If you want a session, run it
 * twice.
 *
 * THE TWO-CONNECTION DANCE, which is where FTP clients get subtly wrong:
 * the data connection must be opened BEFORE the transfer command is sent, or the
 * server's response and the data can race. So: PASV, parse the address, connect
 * the data socket, THEN send RETR/STOR/LIST, then read the data to EOF, then read
 * the final reply on the control connection. Closing the data socket is what
 * tells the server a STOR is complete.
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
static long ng_connect(long s, void *n, long l) {		/* -54  (d0,a0,d1) */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l;
  register void *_a0 __asm("a0")=n; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-54)":"+r"(_d0),"+r"(_a0),"+r"(_d1):"r"(_a6):"d2","a1","memory");
  return _d0;
}
static long ng_send(long s, void *b, long l, long f) {		/* -66  (d0,a0,d1,d2) */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f;
  register void *_a0 __asm("a0")=b; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-66)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a1","memory");
  return _d0;
}
static long ng_recv(long s, void *b, long l, long f) {		/* -78  (d0,a0,d1,d2) */
  register long _d0 __asm("d0")=s, _d1 __asm("d1")=l, _d2 __asm("d2")=f;
  register void *_a0 __asm("a0")=b; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-78)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a1","memory");
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
static ULONG ng_inet_addr(const char *s) {			/* -180 (a0) */
  register ULONG _d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-180)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}
static void *ng_gethostbyname(const char *s) {			/* -210 (a0) */
  register void *_d0 __asm("d0"); register const char *_a0 __asm("a0")=s;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-210)":"=r"(_d0),"+r"(_a0):"r"(_a6):"d1","a1","memory"); return _d0;
}

/* ---- structures ------------------------------------------------------------- */
struct ng_sin { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE zero[8]; };
typedef char ng_sin_must_be_16[(sizeof(struct ng_sin) >= 16) ? 1 : -1];
struct ng_hostent { char *h_name; char **h_aliases; long h_addrtype, h_length; char **h_addr_list; };
struct ng_tv { long tv_secs, tv_micro; };

#define NG_AF_INET	2
#define NG_SOCK_STREAM	1
#define FTP_PORT	21
#define REPLYMAX	512		/* one control-connection line */
#define XFERBUF		4096
#define TIMEOUT_SECS	30	/* control replies; servers can be slow to think */
#define MAX_FD		512
#define REPLY_DEADLINE	120	/* seconds for a COMPLETE reply, however many
				 * lines it runs to -- not per line. A per-line
				 * budget bounds the wrong quantity: a multi-line
				 * banner re-arms it on every line, so a server
				 * that keeps talking holds the tool forever, and
				 * one that merely paces itself just under the
				 * limit holds it for line-count x limit. This
				 * tool is meant to run unattended from scripts,
				 * where there is nobody to press Ctrl-C. */

/* 1/50 s ticks. RESETS AT MIDNIGHT (ds_Minute goes back to 0), which is why the
 * reply budget below tracks ELAPSED time from a start stamp and re-bases when
 * the counter goes backwards, rather than storing an absolute target -- a
 * target computed just before midnight would not be reached again for most of
 * a day. Same rollover idiom as sntp's round-trip measurement. */
static unsigned long now_ticks(void)
{
  struct DateStamp ds;
  DateStamp(&ds);
  return (unsigned long)ds.ds_Minute * 3000UL + (unsigned long)ds.ds_Tick;
}

static UBYTE xfer[XFERBUF];
static char  reply[REPLYMAX];
static int   quiet = 0;

/* ---- helpers ---------------------------------------------------------------- */

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

static int wait_readable(long s, long secs)
{
  ULONG fds[16];
  struct ng_tv tv;
  ULONG sigs = SIGBREAKF_CTRL_C;
  long r;
  int i;

  if (s < 0 || s >= MAX_FD) return -1;
  for (i = 0; i < 16; i++) fds[i] = 0;
  fds[s / 32] |= (1UL << (s % 32));
  tv.tv_secs = secs; tv.tv_micro = 0;
  r = ng_waitselect(s + 1, fds, (void *)0, (void *)0, &tv, &sigs);
  if (sigs & SIGBREAKF_CTRL_C) return -1;
  if (r < 0) return -1;
  return (r > 0) ? 1 : 0;
}

/*
 * Read one CRLF-terminated line from the control connection into `reply`.
 * Returns the length, or -1. Reads a byte at a time: control traffic is tiny and
 * line-oriented, and buffering it would mean carrying leftovers across calls --
 * the classic place an FTP client loses the first bytes of a reply.
 */
static long ctrl_line(long s, unsigned long *started)
{
  long n = 0;

  for (;;) {
    char c;
    unsigned long now;
    int w;

    /* A DEADLINE FOR THE WHOLE REPLY, not per line and not per wait.
     * wait_readable's timeout restarts on every byte, so a server trickling one
     * byte every few seconds -- broken, or hostile -- would keep this loop
     * alive forever; and a budget that restarted on each LINE would let a
     * multi-line banner do the same thing one line at a time. `started` is
     * owned by ctrl_reply() and spans every line of the reply.
     *
     * now_ticks() resets at midnight, so a backwards step means the day rolled
     * over, not that time passed: re-base rather than compare against a stale
     * start. Costs at most one extra full budget, once, on the run that
     * straddles midnight. */
    now = now_ticks();
    if (now < *started)
      *started = now;
    if (now - *started > (unsigned long)(REPLY_DEADLINE * 50)) {
      Printf((STRPTR)"ftp: server is not sending a complete reply -- giving up\n");
      return -1;
    }

    w = wait_readable(s, TIMEOUT_SECS);
    if (w < 0) return -1;
    if (w == 0) { Printf((STRPTR)"ftp: timed out waiting for a reply\n"); return -1; }
    if (ng_recv(s, &c, 1, 0) != 1) return -1;		/* closed or error */
    if (c == '\n') break;
    if (c != '\r' && n < REPLYMAX - 1) reply[n++] = c;
  }
  reply[n] = 0;
  return n;
}

/*
 * Read a complete reply and return its numeric code.
 *
 * A reply may be several lines: "220-first" ... "220 last". The continuation
 * rule is that the FIRST line's code followed by '-' opens it, and it ends at a
 * line beginning with the SAME code followed by a space. Matching on "any line
 * with a space" is the common bug -- server banners routinely contain lines that
 * look like codes.
 */
static int ctrl_reply(long s)
{
  char code[4];
  int i, multi = 0;
  /* One budget for the whole reply, however many continuation lines it runs
   * to. Stamped once here and threaded into every ctrl_line() call below --
   * see the note in ctrl_line() for why this is elapsed-from-start rather than
   * an absolute target. */
  unsigned long started = now_ticks();

  if (ctrl_line(s, &started) < 0) return -1;
  if (!quiet) Printf((STRPTR)"%s\n", (LONG)reply);

  for (i = 0; i < 3; i++) {
    if (reply[i] < '0' || reply[i] > '9') return -1;	/* not a reply at all */
    code[i] = reply[i];
  }
  code[3] = 0;
  multi = (reply[3] == '-');

  while (multi) {
    if (ctrl_line(s, &started) < 0) return -1;
    if (!quiet) Printf((STRPTR)"%s\n", (LONG)reply);
    if (reply[0] == code[0] && reply[1] == code[1] &&
        reply[2] == code[2] && reply[3] == ' ')
      break;
  }
  return (code[0]-'0')*100 + (code[1]-'0')*10 + (code[2]-'0');
}

/* Send "<cmd> <arg>\r\n". arg may be NULL. */
static int ctrl_cmd(long s, const char *cmd, const char *arg)
{
  /* Static, like xfer[] and reply[] above. This is a single-run, single-threaded
   * CLI tool with no reentrancy and no interrupt-time caller, and 512 bytes was
   * the largest stack local in the whole tool set -- out of step with this
   * file's own pattern. ping.c carries a comment about a REAL crash from exactly
   * this: two 1500-byte stack arrays overran a Shell's default stack and
   * corrupted the return address. */
  static char buf[REPLYMAX];
  int n = 0;

  while (*cmd && n < REPLYMAX - 3) buf[n++] = *cmd++;
  if (arg) {
    if (n < REPLYMAX - 3) buf[n++] = ' ';
    while (*arg && n < REPLYMAX - 3) buf[n++] = *arg++;
  }
  /* Refuse rather than truncate. A silently shortened RETR would fetch the
   * wrong file and a silently shortened PASS would send the wrong password --
   * both fail in confusing ways rather than obvious ones. */
  if (*cmd || (arg && *arg)) {
    Printf((STRPTR)"ftp: command or argument too long\n");
    return -1;
  }
  buf[n++] = '\r'; buf[n++] = '\n';
  if (ng_send(s, buf, n, 0) != n) {
    Printf((STRPTR)"ftp: send failed (errno %ld)\n", ng_errno());
    return -1;
  }
  return 0;
}

/*
 * PASV: ask for a data port and parse "227 ... (h1,h2,h3,h4,p1,p2)".
 * Returns a connected data socket, or -1.
 *
 * The address the server reports is used for the PORT ONLY; we connect back to
 * the address we are already talking to. A server behind NAT commonly advertises
 * its private address here, and honouring that sends us somewhere unreachable --
 * or, worse, somewhere else entirely on the local network.
 *
 * The known cost: a deliberately multi-homed server that really does serve data
 * from a different address than it accepts control on will not work. That is rare,
 * and the alternative fails for everyone behind NAT -- which is nearly everyone.
 */
static long open_data(long ctrl, struct ng_sin *srv)
{
  struct ng_sin dst;
  long d, v[6];
  int i, at, got = 0;

  if (ctrl_cmd(ctrl, "PASV", (char *)0) < 0) return -1;
  if (ctrl_reply(ctrl) != 227) {
    Printf((STRPTR)"ftp: server refused passive mode\n");
    return -1;
  }

  /* Find the parenthesised list, then six comma-separated numbers. */
  at = 0;
  while (reply[at] && reply[at] != '(') at++;
  if (!reply[at]) { Printf((STRPTR)"ftp: cannot parse the PASV reply\n"); return -1; }
  at++;
  for (i = 0; i < 6; i++) {
    long acc = 0; int digits = 0;
    while (reply[at] == ' ') at++;
    while (reply[at] >= '0' && reply[at] <= '9' && digits < 5) {
      acc = acc * 10 + (reply[at] - '0'); at++; digits++;
    }
    if (!digits || acc > 255) break;
    v[i] = acc; got++;
    if (i < 5) { if (reply[at] != ',') break; at++; }
  }
  if (got != 6) { Printf((STRPTR)"ftp: cannot parse the PASV reply\n"); return -1; }

  for (i = 0; i < (int)sizeof(dst); i++) ((char *)&dst)[i] = 0;
  dst.sin_len = sizeof(dst); dst.sin_family = NG_AF_INET;
  dst.sin_port = (UWORD)((v[4] << 8) | v[5]);
  dst.sin_addr = srv->sin_addr;			/* NOT the advertised address */

  d = ng_socket(NG_AF_INET, NG_SOCK_STREAM, 0);
  if (d < 0) { Printf((STRPTR)"ftp: data socket failed (errno %ld)\n", ng_errno()); return -1; }
  if (ng_connect(d, &dst, sizeof(dst)) < 0) {
    Printf((STRPTR)"ftp: data connect failed (errno %ld)\n", ng_errno());
    ng_close(d); return -1;
  }
  return d;
}

/* Drain a data connection to a file handle (or to output if fh is 0). */
static int data_to(long d, BPTR fh, ULONG *total)
{
  for (;;) {
    long n;
    int w = wait_readable(d, TIMEOUT_SECS);
    if (w < 0) { Printf((STRPTR)"ftp: aborted\n"); return -1; }
    if (w == 0) { Printf((STRPTR)"ftp: data connection timed out\n"); return -1; }
    n = ng_recv(d, xfer, sizeof(xfer), 0);
    if (n < 0) { Printf((STRPTR)"ftp: data read failed (errno %ld)\n", ng_errno()); return -1; }
    if (n == 0) break;					/* server closed: complete */
    if (fh) {
      if (Write(fh, (APTR)xfer, n) != n) {
        Printf((STRPTR)"ftp: local write failed (disk full?)\n"); return -1;
      }
    } else {
      /* Printf() buffers and Write() does not, so without this the listing
       * overtakes the control-connection replies printed around it and the
       * output reads out of order. */
      Flush(Output());
      if (Write(Output(), (APTR)xfer, n) != n) {
        Printf((STRPTR)"ftp: output write failed\n"); return -1;
      }
    }
    *total += (ULONG)n;
  }
  return 0;
}

/* ---- main ------------------------------------------------------------------- */

static void usage(void)
{
  Printf((STRPTR)"usage: ftp GET|PUT <host> <remotefile> [<localfile>]\n"
                 "       ftp DIR <host> [<path>]\n"
                 "       [USER <name>] [PASS <password>] [PORT <n>] [ASCII] [QUIET]\n");
}

int main(void)
{
  struct RDArgs *rda;
  LONG args[9];
  struct ng_sin srv;
  struct ng_hostent *hp;
  const char *cmd, *host, *file, *local, *user, *pass;
  ULONG addr, total = 0;
  long ctrl = -1, data = -1, port = FTP_PORT;
  int i, rc = RETURN_OK, is_get, is_put, is_dir, greeted = 0;
  BPTR fh = 0;

  for (i = 0; i < 9; i++) args[i] = 0;
  rda = ReadArgs((STRPTR)"COMMAND/A,HOST/A,FILE,LOCAL,USER/K,PASS/K,PORT/K/N,ASCII/S,QUIET/S",
                 args, NULL);
  if (!rda) { usage(); return RETURN_FAIL; }

  cmd  = (const char *)args[0];
  host = (const char *)args[1];
  file = args[2] ? (const char *)args[2] : (const char *)0;
  local = args[3] ? (const char *)args[3] : file;
  user = args[4] ? (const char *)args[4] : "anonymous";
  pass = args[5] ? (const char *)args[5] : "anonymous@";
  if (args[6]) port = *(LONG *)args[6];
  quiet = args[8] ? 1 : 0;

  is_get = ci_eq(cmd, "GET"); is_put = ci_eq(cmd, "PUT"); is_dir = ci_eq(cmd, "DIR");
  if (!is_get && !is_put && !is_dir) {
    Printf((STRPTR)"ftp: first argument must be GET, PUT or DIR\n");
    usage(); FreeArgs(rda); return RETURN_FAIL;
  }
  if ((is_get || is_put) && !file) {
    Printf((STRPTR)"ftp: %s needs a remote file name\n", (LONG)cmd);
    FreeArgs(rda); return RETURN_FAIL;
  }
  if (port < 1 || port > 65535) {
    Printf((STRPTR)"ftp: PORT must be 1..65535\n"); FreeArgs(rda); return RETURN_FAIL;
  }

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    Printf((STRPTR)"ftp: bsdsocket.library v4+ not available.\n");
    FreeArgs(rda); return RETURN_FAIL;
  }

  addr = ng_inet_addr(host);
  if (addr == 0xFFFFFFFFUL) {
    hp = (struct ng_hostent *)ng_gethostbyname(host);
    if (!hp || !hp->h_addr_list || !hp->h_addr_list[0]) {
      Printf((STRPTR)"ftp: unknown host %s\n", (LONG)host);
      CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_ERROR;
    }
    { UBYTE *p = (UBYTE *)hp->h_addr_list[0];
      addr = ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3]; }
  }

  for (i = 0; i < (int)sizeof(srv); i++) ((char *)&srv)[i] = 0;
  srv.sin_len = sizeof(srv); srv.sin_family = NG_AF_INET;
  srv.sin_port = (UWORD)port; srv.sin_addr = addr;

  ctrl = ng_socket(NG_AF_INET, NG_SOCK_STREAM, 0);
  if (ctrl < 0) {
    Printf((STRPTR)"ftp: socket failed (errno %ld)\n", ng_errno());
    CloseLibrary(SocketBase); FreeArgs(rda); return RETURN_FAIL;
  }
  if (ng_connect(ctrl, &srv, sizeof(srv)) < 0) {
    Printf((STRPTR)"ftp: cannot connect to %s (errno %ld)\n", (LONG)host, ng_errno());
    rc = RETURN_ERROR; goto done;
  }

  if (ctrl_reply(ctrl) / 100 != 2) { rc = RETURN_ERROR; goto done; }   /* greeting */
  greeted = 1;

  if (ctrl_cmd(ctrl, "USER", user) < 0) { rc = RETURN_ERROR; goto done; }
  i = ctrl_reply(ctrl);
  if (i == 331) {						/* password wanted */
    if (ctrl_cmd(ctrl, "PASS", pass) < 0) { rc = RETURN_ERROR; goto done; }
    i = ctrl_reply(ctrl);
  }
  if (i / 100 != 2) { Printf((STRPTR)"ftp: login refused\n"); rc = RETURN_ERROR; goto done; }

  /* Binary unless asked otherwise. An Amiga fetching a .lha through a client
   * that silently translated line endings would get a corrupt archive, so the
   * safe mode is the default and ASCII is opt-in. */
  if (ctrl_cmd(ctrl, "TYPE", args[7] ? "A" : "I") < 0) { rc = RETURN_ERROR; goto done; }
  if (ctrl_reply(ctrl) / 100 != 2) { rc = RETURN_ERROR; goto done; }

  /* Data connection FIRST, then the transfer command -- see the header note. */
  data = open_data(ctrl, &srv);
  if (data < 0) { rc = RETURN_ERROR; goto done; }

  if (is_dir) {
    if (ctrl_cmd(ctrl, "LIST", file) < 0) { rc = RETURN_ERROR; goto done; }
  } else if (is_get) {
    if (ctrl_cmd(ctrl, "RETR", file) < 0) { rc = RETURN_ERROR; goto done; }
  } else {
    if (ctrl_cmd(ctrl, "STOR", file) < 0) { rc = RETURN_ERROR; goto done; }
  }

  i = ctrl_reply(ctrl);
  if (i / 100 != 1) {			/* 1xx = transfer starting */
    Printf((STRPTR)"ftp: server declined the transfer\n");
    rc = RETURN_ERROR; goto done;
  }

  if (is_put) {
    fh = Open((STRPTR)local, MODE_OLDFILE);
    if (!fh) { Printf((STRPTR)"ftp: cannot open %s\n", (LONG)local); rc = RETURN_FAIL; goto done; }
    for (;;) {
      long n = Read(fh, (APTR)xfer, sizeof(xfer));
      if (n < 0) { Printf((STRPTR)"ftp: local read failed\n"); rc = RETURN_FAIL; goto done; }
      if (n == 0) break;
      if (ng_send(data, xfer, n, 0) != n) {
        Printf((STRPTR)"ftp: data send failed (errno %ld)\n", ng_errno());
        rc = RETURN_ERROR; goto done;
      }
      total += (ULONG)n;
    }
    /* Closing the data connection is what signals end-of-file to the server. */
    ng_close(data); data = -1;
  } else {
    if (is_get) {
      fh = Open((STRPTR)local, MODE_NEWFILE);
      if (!fh) { Printf((STRPTR)"ftp: cannot create %s\n", (LONG)local); rc = RETURN_FAIL; goto done; }
    }
    if (data_to(data, fh, &total) < 0) { rc = RETURN_ERROR; goto done; }
    ng_close(data); data = -1;
  }

  if (ctrl_reply(ctrl) / 100 != 2) {	/* 226 = transfer complete */
    Printf((STRPTR)"ftp: transfer did not complete cleanly\n");
    rc = RETURN_ERROR; goto done;
  }

  if (is_get)      Printf((STRPTR)"ftp: received %ld bytes into %s\n", (LONG)total, (LONG)local);
  else if (is_put) Printf((STRPTR)"ftp: sent %ld bytes from %s\n",     (LONG)total, (LONG)local);

done:
  if (fh) Close(fh);
  if (data >= 0) ng_close(data);
  if (ctrl >= 0) {
    /* Only if the session ever started -- QUIT on a socket that never connected
     * just prints a second, misleading error on top of the real one. */
    if (greeted) ctrl_cmd(ctrl, "QUIT", (char *)0);
    ng_close(ctrl);
  }
  CloseLibrary(SocketBase);
  FreeArgs(rda);
  return rc;
}
