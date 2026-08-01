/* bcastbench -- same-host UDP broadcast discovery repro for AmiTCP_NG.
 *
 * Reproduces the Fitz "fitz serve" / "fitz query" round trip in ONE process:
 *   server socket A: bind(INADDR_ANY:17710), waits for a datagram, replies unicast
 *                    to the sender (which is THIS host's own IP).
 *   client socket B: SO_BROADCAST, sendto("PING") to 255.255.255.255:17710, then
 *                    WaitSelect()s for the "PONG" reply.
 *
 * Both legs travel the software loopback (broadcast self-delivery + reply-to-own-IP).
 * On the CURRENT code with a static IP / no default route the broadcast sendto()
 * has no route and fails -> discovery fails; that is exactly the bug. Logs each
 * step to SYS:bcast.log and prints PASS/FAIL. No arguments.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase;

struct sockaddr_in {
  UBYTE  sin_len, sin_family;
  UWORD  sin_port;            /* big-endian host == network order */
  ULONG  sin_addr;
  UBYTE  sin_zero[8];
};
struct bsd_tv { long tv_secs; long tv_micro; };

static long ng_socket(long d,long t,long p){register long _d0 __asm("d0")=d,_d1 __asm("d1")=t,_d2 __asm("d2")=p;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a0","a1","memory");return _d0;}
static long ng_bind(long s,void*n,long l){register long _d0 __asm("d0")=s,_d1 __asm("d1")=l;
  register void*_a0 __asm("a0")=n; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-36)":"+r"(_d0),"+r"(_a0),"+r"(_d1):"r"(_a6):"d2","a1","memory");return _d0;}
static long ng_sendto(long s,void*m,long len,long fl,void*to,long tl){register long _d0 __asm("d0")=s;
  register void*_a0 __asm("a0")=m; register long _d1 __asm("d1")=len,_d2 __asm("d2")=fl;
  register void*_a1 __asm("a1")=to; register long _d3 __asm("d3")=tl;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-60)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_d3):"r"(_a6):"memory");return _d0;}
static long ng_recvfrom(long s,void*b,long len,long fl,void*fr,long*frl){register long _d0 __asm("d0")=s;
  register void*_a0 __asm("a0")=b; register long _d1 __asm("d1")=len,_d2 __asm("d2")=fl;
  register void*_a1 __asm("a1")=fr; register long*_a2 __asm("a2")=frl;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-72)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_a2):"r"(_a6):"memory");return _d0;}
static long ng_setsockopt(long s,long lvl,long name,void*val,long len){register long _d0 __asm("d0")=s;
  register long _d1 __asm("d1")=lvl,_d2 __asm("d2")=name; register void*_a0 __asm("a0")=val;
  register long _d3 __asm("d3")=len; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-90)":"+r"(_d0),"+r"(_d1),"+r"(_d2),"+r"(_a0),"+r"(_d3):"r"(_a6):"a1","memory");return _d0;}
static long ng_waitselect(long n,void*r,void*w,void*e,void*tv,ULONG*sig){register long _d0 __asm("d0")=n;
  register void*_a0 __asm("a0")=r,*_a1 __asm("a1")=w,*_a2 __asm("a2")=e,*_a3 __asm("a3")=tv;
  register ULONG*_d1 __asm("d1")=sig; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-126)":"+r"(_d0),"+r"(_a0),"+r"(_a1),"+r"(_a2),"+r"(_a3),"+r"(_d1):"r"(_a6):"memory");return _d0;}
static long ng_errno(void){register long _d0 __asm("d0");register struct Library*_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory");return _d0;}

static void logs(BPTR f,const char*s){long n=0;const char*p=s;while(*p++)n++;if(f)Write(f,(APTR)s,n);}
static void lognum(BPTR f,long v){char b[12];int i=11;unsigned long u=v<0?-(unsigned long)v:v;b[i--]=0;
  do{b[i--]='0'+(u%10);u/=10;}while(u); if(v<0)b[i--]='-'; logs(f,b+i+1);}

/* WaitSelect a single socket for readability, up to `secs`. Returns >0 if ready. */
static long wait_read(long s, long secs, BPTR f) {
  ULONG rset = 1UL << s;
  struct bsd_tv tv; tv.tv_secs = secs; tv.tv_micro = 0;
  return ng_waitselect(s + 1, &rset, 0, 0, &tv, 0);
}

int main(void){
  struct sockaddr_in sa, dst, from;
  long srv, cli, n, frl, rc = 20;
  char buf[64];
  BPTR f = Open((STRPTR)"SYS:bcast.log", MODE_NEWFILE);
  const long one = 1;

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase){ logs(f,"OpenLibrary FAILED\n"); if(f)Close(f); return 20; }

  /* ---- server socket A: bind INADDR_ANY:17710 ---- */
  srv = ng_socket(2,2,0);                          /* AF_INET, SOCK_DGRAM */
  if (srv < 0){ logs(f,"server socket() errno="); lognum(f,ng_errno()); logs(f,"\n"); goto out; }
  { int i; sa.sin_len=16; sa.sin_family=2; sa.sin_port=17710; sa.sin_addr=0;
    for(i=0;i<8;i++) sa.sin_zero[i]=0; }
  if (ng_bind(srv,&sa,sizeof sa) < 0){ logs(f,"server bind(17710) errno="); lognum(f,ng_errno()); logs(f,"\n"); goto out; }
  logs(f,"server: bound INADDR_ANY:17710\n");

  /* ---- client socket B: SO_BROADCAST ---- */
  cli = ng_socket(2,2,0);
  if (cli < 0){ logs(f,"client socket() errno="); lognum(f,ng_errno()); logs(f,"\n"); goto out; }
  if (ng_setsockopt(cli,0xffff,0x20,(void*)&one,sizeof one) < 0){    /* SOL_SOCKET, SO_BROADCAST */
    logs(f,"client setsockopt(SO_BROADCAST) errno="); lognum(f,ng_errno()); logs(f,"\n"); goto out; }

  /* ---- client -> broadcast "PING" to 255.255.255.255:17710 ---- */
  { int i; dst.sin_len=16; dst.sin_family=2; dst.sin_port=17710; dst.sin_addr=0xFFFFFFFFUL;
    for(i=0;i<8;i++) dst.sin_zero[i]=0; }
  n = ng_sendto(cli,(void*)"PING",4,0,&dst,sizeof dst);
  if (n < 0){ logs(f,"client sendto(255.255.255.255:17710) FAILED errno="); lognum(f,ng_errno()); logs(f,"\n"); rc=10; goto out; }
  logs(f,"client: broadcast PING sent\n");

  /* ---- server: receive PING, reply PONG unicast to sender (our own IP) ---- */
  if (wait_read(srv, 3, f) <= 0){ logs(f,"server: NO broadcast received (loopback delivery failed)\n"); rc=11; goto out; }
  frl = sizeof from;
  n = ng_recvfrom(srv,buf,sizeof buf,0,&from,&frl);
  if (n <= 0){ logs(f,"server recvfrom errno="); lognum(f,ng_errno()); logs(f,"\n"); rc=12; goto out; }
  logs(f,"server: got broadcast, replying to own IP\n");
  n = ng_sendto(srv,(void*)"PONG",4,0,&from,sizeof from);
  if (n < 0){ logs(f,"server sendto(reply) FAILED errno="); lognum(f,ng_errno()); logs(f,"\n"); rc=13; goto out; }

  /* ---- client: receive PONG ---- */
  if (wait_read(cli, 3, f) <= 0){ logs(f,"client: NO reply received (to-self loopback failed)\n"); rc=14; goto out; }
  frl = sizeof from;
  n = ng_recvfrom(cli,buf,sizeof buf,0,&from,&frl);
  if (n <= 0){ logs(f,"client recvfrom errno="); lognum(f,ng_errno()); logs(f,"\n"); rc=15; goto out; }

  logs(f,"RESULT: PASS -- same-host UDP broadcast discovery round trip OK\n");
  rc = 0;

out:
  if (rc) logs(f,"RESULT: FAIL\n");
  if (SocketBase) CloseLibrary(SocketBase);
  if (f) Close(f);
  return rc;
}
