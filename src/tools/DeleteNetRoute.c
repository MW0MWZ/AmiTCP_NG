/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 * DeleteNetRoute -- Roadshow-compatible: delete a routing-table entry.
 * Template QUIET/S,DST=DESTINATION/K,DEFAULT=DEFAULTGATEWAY/K */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
struct Library *SocketBase;
#include "ng_lvo.h"
int main(void){
  struct RDArgs *rda; LONG a[3]={0,0,0}; int rc=RETURN_OK,n=0,quiet; struct TagItem t[3];
  rda=ReadArgs((STRPTR)"QUIET/S,DST=DESTINATION/K,DEFAULT=DEFAULTGATEWAY/K",a,NULL);
  if(!rda){PrintFault(IoErr(),(STRPTR)"DeleteNetRoute");return RETURN_ERROR;}
  quiet=a[0]!=0;
  if(a[1]){t[n].ti_Tag=RTA_Destination;    t[n].ti_Data=a[1];n++;}
  if(a[2]){t[n].ti_Tag=RTA_DefaultGateway; t[n].ti_Data=a[2];n++;}
  t[n].ti_Tag=TAG_END; t[n].ti_Data=0;
  if(n==0){if(!quiet)Printf((STRPTR)"DeleteNetRoute: nothing to do\n");FreeArgs(rda);return RETURN_WARN;}
  SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4L);
  if(!SocketBase){if(!quiet)Printf((STRPTR)"DeleteNetRoute: cannot open bsdsocket.library\n");FreeArgs(rda);return RETURN_FAIL;}
  if(ng_delroute(t)!=0){if(!quiet)Printf((STRPTR)"DeleteNetRoute: failed, errno %ld\n",ng_errno());rc=RETURN_ERROR;}
  CloseLibrary(SocketBase); FreeArgs(rda); return rc;
}
