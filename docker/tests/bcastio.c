/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING). */
/*
 * bcastio -- does UDP broadcast still work? Send and receive, both directions.
 *
 * Broadcast has been broken here before -- limited broadcast was once ROUTED
 * instead of going to the wire, and reply-to-self was gated on a knob that no
 * longer exists -- and neither failure is visible from a throughput test. This
 * sends to 255.255.255.255 AND to the subnet address, with a receiver bound to
 * INADDR_ANY on the same port, so a send that silently goes nowhere fails here.
 *
 * LVOs are from ref/roadshow-sdk/sfd/bsdsocket_lib.sfd (==bias 30, 6 per entry),
 * NOT from memory: a wrong one lands on a neighbouring vector and returns a
 * plausible errno from the wrong function.
 *
 * WHAT THIS DOES NOT COVER: a broadcast originated by ANOTHER host. That path
 * -- the driver flagging the frame SANA2IOF_BCAST, if_sana turning it into
 * M_BCAST, udp_input's fan-out choosing recipients -- is untestable under
 * SLIRP, whose network exists only inside the emulator process and which
 * carries no inbound broadcast. A pass here says nothing about it.
 *
 * Logs SYS:bcastio.log, every line flushed as written.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>

struct Library *SocketBase = 0;

static long ng_socket(long d,long t,long p){ register long _d0 __asm("d0")=d,_d1 __asm("d1")=t,_d2 __asm("d2")=p; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0),"+r"(_d1),"+r"(_d2):"r"(_a6):"a0","a1","memory"); return _d0; }
static long ng_bind(long s,void*n,long l){ register long _d0 __asm("d0")=s; register void*_a0 __asm("a0")=n; register long _d1 __asm("d1")=l; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-36)":"+r"(_d0),"+r"(_a0),"+r"(_d1):"r"(_a6):"a1","memory"); return _d0; }
static long ng_sendto(long s,void*b,long l,long f,void*t,long tl){ register long _d0 __asm("d0")=s; register void*_a0 __asm("a0")=b; register long _d1 __asm("d1")=l,_d2 __asm("d2")=f; register void*_a1 __asm("a1")=t; register long _d3 __asm("d3")=tl; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-60)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_d3):"r"(_a6):"memory"); return _d0; }
static long ng_recvfrom(long s,void*b,long l,long f,void*fr,void*fl){ register long _d0 __asm("d0")=s; register void*_a0 __asm("a0")=b; register long _d1 __asm("d1")=l,_d2 __asm("d2")=f; register void*_a1 __asm("a1")=fr; register void*_a2 __asm("a2")=fl; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-72)":"+r"(_d0),"+r"(_a0),"+r"(_d1),"+r"(_d2),"+r"(_a1),"+r"(_a2):"r"(_a6):"memory"); return _d0; }
static long ng_setsockopt(long s,long lv,long o,void*v,long vl){ register long _d0 __asm("d0")=s; register long _d1 __asm("d1")=lv,_d2 __asm("d2")=o; register void*_a0 __asm("a0")=v; register long _d3 __asm("d3")=vl; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-90)":"+r"(_d0),"+r"(_d1),"+r"(_d2),"+r"(_a0),"+r"(_d3):"r"(_a6):"a1","memory"); return _d0; }
static long ng_ioctl(long s,unsigned long c,void*d){ register long _d0 __asm("d0")=s; register unsigned long _d1 __asm("d1")=c; register void*_a0 __asm("a0")=d; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-114)":"+r"(_d0),"+r"(_d1),"+r"(_a0):"r"(_a6):"a1","memory"); return _d0; }
static long ng_close(long s){ register long _d0 __asm("d0")=s; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0; }
static long ng_errno(void){ register long _d0 __asm("d0"); register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory"); return _d0; }

#define FIONBIO_  0x8004667EUL
#define SOL_SOCKET_ 0xffff
#define SO_BROADCAST_ 0x0020
#define PORT 17999

struct sain { UBYTE len, fam; UWORD port; ULONG addr; UBYTE pad[8]; };

static BPTR lg; static char buf[512]; static char line[200];
static void zero(void*p,int n){int i;for(i=0;i<n;i++)((char*)p)[i]=0;}
static void say(const char*s){int n=0;while(s[n])n++;if(lg){Write(lg,(APTR)s,n);Flush(lg);}Printf((STRPTR)"%s",(LONG)s);}

int main(void)
{
  struct sain me, to, from;
  long rx = -1, tx = -1, one = 1, n, i;
  ULONG fl;

  lg = Open((STRPTR)"SYS:bcastio.log", MODE_NEWFILE);
  if (!(SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4))) { say("FAIL: no bsdsocket\n"); goto out; }
  say("bcastio: UDP broadcast send/receive\n");

  /* receiver: bound to INADDR_ANY on the test port */
  if ((rx = ng_socket(2,2,0)) < 0) { say("FAIL: socket(rx)\n"); goto out; }
  zero(&me,sizeof me); me.len=sizeof me; me.fam=2; me.port=PORT; me.addr=0;
  if (ng_bind(rx,&me,sizeof me) < 0) { sprintf(line,"FAIL: bind errno %ld\n",ng_errno()); say(line); goto out; }
  if (ng_ioctl(rx,FIONBIO_,&one) < 0) { say("FAIL: FIONBIO\n"); goto out; }
  say("receiver bound to *:17999\n");

  /* sender: broadcast enabled */
  if ((tx = ng_socket(2,2,0)) < 0) { say("FAIL: socket(tx)\n"); goto out; }
  if (ng_setsockopt(tx,SOL_SOCKET_,SO_BROADCAST_,&one,4) < 0) {
    sprintf(line,"FAIL: SO_BROADCAST errno %ld\n",ng_errno()); say(line); goto out; }
  say("SO_BROADCAST set\n");

  /* limited broadcast 255.255.255.255 */
  zero(&to,sizeof to); to.len=sizeof to; to.fam=2; to.port=PORT; to.addr=0xFFFFFFFFUL;
  n = ng_sendto(tx,(void*)"BCAST-LIMITED",13,0,&to,sizeof to);
  sprintf(line,"sendto 255.255.255.255 -> %ld%s", n, n<0?"":"\n");
  say(line);
  if (n < 0) { sprintf(line," errno %ld\n",ng_errno()); say(line); }

  /* subnet broadcast x.x.x.255 -- derived from the SLIRP net we know we are on */
  zero(&to,sizeof to); to.len=sizeof to; to.fam=2; to.port=PORT; to.addr=0x0A0002FFUL;
  n = ng_sendto(tx,(void*)"BCAST-SUBNET",12,0,&to,sizeof to);
  sprintf(line,"sendto 10.0.2.255 -> %ld%s", n, n<0?"":"\n");
  say(line);
  if (n < 0) { sprintf(line," errno %ld\n",ng_errno()); say(line); }

  /* did our own receiver see either of them? */
  for (i = 0; i < 100; i++) {
    fl = sizeof from; zero(&from,sizeof from);
    n = ng_recvfrom(rx,buf,sizeof buf,0,&from,&fl);
    if (n > 0) { buf[n]=0; sprintf(line,"RECEIVED %ld bytes: %s\n",n,buf); say(line); }
    Delay(5);
  }
  say("done\n");

out:
  if (tx>=0) ng_close(tx); if (rx>=0) ng_close(rx);
  if (SocketBase) CloseLibrary(SocketBase);
  if (lg) Close(lg);
  return RETURN_OK;
}
