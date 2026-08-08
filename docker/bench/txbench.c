/* txbench -- raw TCP SEND-throughput benchmark for AmiTCP_NG.
 *
 * The transmit-direction twin of rxbench: connects to the transfer host's
 * throughput SINK (172.20.0.10:9001, which reads and discards), sends as fast as
 * it can for a fixed wall-clock window, and reports KB/s. Logs SYS:txbench.log.
 *
 * WHY THIS EXISTS: rxbench only downloads, so nothing in the emulated suite ever
 * exercised the SENDER side of TCP -- and the sender side is where the
 * header-prediction pure-ACK branch lives. A change to that branch validated
 * only against rxbench would have been tested exclusively on the path it does
 * not touch. Upload is also the direction where the peer is a receiver whose
 * advertised window keeps moving, which is the whole reason that branch was
 * missing 91% of ACKs on real hardware.
 *
 * NOTE ON EXPECTED NUMBERS: SLIRP caps the emulated NIC path at roughly 200 KB/s
 * regardless of CPU, so this is a CORRECTNESS and fast-path-accounting check,
 * not a speed measurement. Read the rxprofile fast pure-ACK count alongside it;
 * the throughput figure only has to be non-zero and stable.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

struct Library *SocketBase;

struct sockaddr_in {
  UBYTE  sin_len, sin_family;
  UWORD  sin_port;         /* big-endian on this big-endian host == network order */
  ULONG  sin_addr;
  UBYTE  sin_zero[8];
};

static long v_socket(long d,long t,long p){register long _d0 __asm("d0")=d,_d1 __asm("d1")=t,_d2 __asm("d2")=p;
  register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-30)":"+r"(_d0):"r"(_d0),"r"(_d1),"r"(_d2),"r"(_a6):"a0","a1","memory");return _d0;}
static long v_connect(long s,void*name,long len){register long _d0 __asm("d0")=s,_d1 __asm("d1")=len;
  register void*_a0 __asm("a0")=name; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-54)":"+r"(_d0):"r"(_d0),"r"(_a0),"r"(_d1),"r"(_a6):"d2","a1","memory");return _d0;}
static long v_send(long s,void*buf,long len,long fl){register long _d0 __asm("d0")=s,_d1 __asm("d1")=len,_d2 __asm("d2")=fl;
  register void*_a0 __asm("a0")=buf; register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-66)":"+r"(_d0):"r"(_d0),"r"(_a0),"r"(_d1),"r"(_d2),"r"(_a6):"a1","memory");return _d0;}
static void v_close(long s){register long _d0 __asm("d0")=s;register struct Library *_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-120)":"+r"(_d0):"r"(_d0),"r"(_a6):"d1","a0","a1","memory");}
static long v_errno(void){register long _d0 __asm("d0");register struct Library*_a6 __asm("a6")=SocketBase;
  __asm__ __volatile__("jsr a6@(-162)":"=r"(_d0):"r"(_a6):"d1","a0","a1","memory");return _d0;}

static void logs(BPTR f,const char*s){long n=0;const char*p=s;while(*p++)n++;if(f)Write(f,(APTR)s,n);}
static void lognum(BPTR f,long v){char b[12];int i=11;unsigned long u=v<0?-(unsigned long)v:v;b[i--]=0;
  do{b[i--]='0'+(u%10);u/=10;}while(u); if(v<0)b[i--]='-'; logs(f,b+i+1);}

/* 1/50 s ticks; monotonic within an hour (fine for a short bench). */
static unsigned long now_ticks(void){ struct DateStamp ds; DateStamp(&ds);
  return (unsigned long)ds.ds_Minute*3000UL + (unsigned long)ds.ds_Tick; }

int main(void){
  static UBYTE buf[32768];
  struct sockaddr_in sa;
  long s, n, secs = 15;
  unsigned long total = 0, t0, tnow, elapsed, kbps;
  BPTR f = Open((STRPTR)"SYS:txbench.log", MODE_NEWFILE);

  SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
  if (!SocketBase){ logs(f,"OpenLibrary FAILED\n"); if(f)Close(f); return 20; }

  { int i; for(i=0;i<(int)sizeof(buf);i++) buf[i] = (UBYTE)i; }

  s = v_socket(2,1,0);                        /* AF_INET, SOCK_STREAM */
  if (s < 0){ logs(f,"socket() failed errno="); lognum(f,v_errno()); logs(f,"\n"); goto out; }

  sa.sin_len=sizeof(sa); sa.sin_family=2; sa.sin_port=9001;
  sa.sin_addr=0xAC14000AUL;                   /* 172.20.0.10 */
  { int i; for(i=0;i<8;i++) sa.sin_zero[i]=0; }

  logs(f,"connecting to 172.20.0.10:9001 (throughput sink) ...\n");
  if (v_connect(s,&sa,sizeof(sa)) < 0){ logs(f,"connect() failed errno="); lognum(f,v_errno()); logs(f,"\n"); v_close(s); goto out; }
  logs(f,"connected -- sending for "); lognum(f,secs); logs(f," s ...\n");

  t0 = now_ticks();
  for (;;) {
    tnow = now_ticks();
    elapsed = tnow - t0;
    if (elapsed >= (unsigned long)secs*50UL) break;
    n = v_send(s, buf, sizeof(buf), 0);
    /* A blocking send returns only when it has queued everything, so a SHORT
     * count is not normal here -- report it rather than silently under-counting. */
    if (n <= 0){ logs(f,"send ended (n="); lognum(f,n); logs(f," errno="); lognum(f,v_errno()); logs(f,")\n"); break; }
    total += (unsigned long)n;
  }
  elapsed = now_ticks() - t0;
  if (elapsed == 0) elapsed = 1;
  kbps = (total / 1024UL) * 50UL / elapsed;

  logs(f,"RESULT: sent "); lognum(f,(long)total); logs(f," bytes in ");
  lognum(f,(long)elapsed); logs(f," ticks (1/50s) = ");
  lognum(f,(long)kbps); logs(f," KB/s\n");
  v_close(s);

out:
  CloseLibrary(SocketBase);
  if(f)Close(f);
  return 0;
}
