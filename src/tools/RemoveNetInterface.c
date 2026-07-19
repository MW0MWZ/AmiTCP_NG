/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 * RemoveNetInterface -- Roadshow-compatible: tear down a network interface.
 * Template INTERFACE/A,FORCE/S,QUIET/S */
#include <exec/types.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
struct Library *SocketBase;
#include "ng_lvo.h"
int main(void){
  struct RDArgs *rda; LONG a[3]={0,0,0}; int rc=RETURN_OK,quiet;
  rda=ReadArgs((STRPTR)"INTERFACE/A,FORCE/S,QUIET/S",a,NULL);
  if(!rda){PrintFault(IoErr(),(STRPTR)"RemoveNetInterface");return RETURN_ERROR;}
  quiet=a[2]!=0;
  SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4L);
  if(!SocketBase){if(!quiet)Printf((STRPTR)"RemoveNetInterface: cannot open bsdsocket.library\n");FreeArgs(rda);return RETURN_FAIL;}
  /* Take the interface down first, so removing one that is still up (e.g. configured
   * via DHCP) works without the caller having to add FORCE: RemoveInterface() returns
   * EBUSY for an interface that is still up unless forced. Best-effort -- if it is
   * already down, or the name is unknown, the removal below reports the real error. */
  { struct TagItem tg[2];
    tg[0].ti_Tag=IFC_State; tg[0].ti_Data=NG_SM_Offline;
    tg[1].ti_Tag=TAG_END;   tg[1].ti_Data=0;
    (void)ng_configif((void*)a[0], tg); }
  if(!ng_removeif((void*)a[0], a[1]?1:0)){if(!quiet)Printf((STRPTR)"RemoveNetInterface: '%s' failed, errno %ld\n",a[0],ng_errno());rc=RETURN_ERROR;}
  CloseLibrary(SocketBase); FreeArgs(rda); return rc;
}
