/* timecmd <bytes> <command...> : run an AmigaDOS command, time it, print KB/s. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
static unsigned long ticks(void){ struct DateStamp d; DateStamp(&d);
  return (unsigned long)d.ds_Minute*3000UL + (unsigned long)d.ds_Tick; }
int main(int argc,char**argv){
  char cmd[600]; unsigned long bytes=0,t0,el,kbps; int i; char*p;
  if(argc<3){ Printf((STRPTR)"usage: timecmd bytes command...\n"); return 20; }
  for(p=argv[1]; *p>='0'&&*p<='9'; p++) bytes=bytes*10UL+(*p-'0');
  cmd[0]=0;
  for(i=2;i<argc;i++){ if(i>2) strcat(cmd," "); strcat(cmd,argv[i]); }
  t0=ticks();
  Execute((STRPTR)cmd, 0, Output());
  el=ticks()-t0; if(!el) el=1;
  kbps=(bytes/1024UL)*50UL/el;
  Printf((STRPTR)"TIMECMD: %ld bytes in %ld ticks (1/50s) = %ld KB/s\n",
         (LONG)bytes,(LONG)el,(LONG)kbps);
  return 0;
}
