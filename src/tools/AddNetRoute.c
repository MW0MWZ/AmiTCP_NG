/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 * AddNetRoute -- Roadshow-compatible: add an entry to the routing table.
 * Template QUIET/S,DST=DESTINATION/K,HOSTDST=HOSTDESTINATION/K,NETDST=NETDESTINATION/K,
 *          VIA=GATEWAY/K,DEFAULT=DEFAULTGATEWAY/K  (addresses are dotted-decimal). */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
struct Library *SocketBase;
#include "ng_lvo.h"
int main(void){
  struct RDArgs *rda; LONG a[6]={0,0,0,0,0,0}; int rc=RETURN_OK, n=0, quiet;
  struct TagItem t[6];
  rda=ReadArgs((STRPTR)"QUIET/S,DST=DESTINATION/K,HOSTDST=HOSTDESTINATION/K,"
                       "NETDST=NETDESTINATION/K,VIA=GATEWAY/K,DEFAULT=DEFAULTGATEWAY/K",a,NULL);
  if(!rda){PrintFault(IoErr(),(STRPTR)"AddNetRoute");return RETURN_ERROR;}
  quiet=a[0]!=0;
  if(a[1]){t[n].ti_Tag=RTA_Destination;     t[n].ti_Data=a[1];n++;}
  if(a[2]){t[n].ti_Tag=RTA_DestinationHost; t[n].ti_Data=a[2];n++;}
  if(a[3]){t[n].ti_Tag=RTA_DestinationNet;  t[n].ti_Data=a[3];n++;}
  if(a[4]){t[n].ti_Tag=RTA_Gateway;         t[n].ti_Data=a[4];n++;}
  if(a[5]){t[n].ti_Tag=RTA_DefaultGateway;  t[n].ti_Data=a[5];n++;}
  t[n].ti_Tag=TAG_END; t[n].ti_Data=0;
  if(n==0){if(!quiet)Printf((STRPTR)"AddNetRoute: nothing to do (give a DESTINATION/GATEWAY/etc.)\n");FreeArgs(rda);return RETURN_WARN;}
  SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4L);
  if(!SocketBase){if(!quiet)Printf((STRPTR)"AddNetRoute: cannot open bsdsocket.library\n");FreeArgs(rda);return RETURN_FAIL;}
  if(ng_addroute(t)!=0){if(!quiet)Printf((STRPTR)"AddNetRoute: failed, errno %ld\n",ng_errno());rc=RETURN_ERROR;}
  CloseLibrary(SocketBase); FreeArgs(rda); return rc;
}
