/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 * NetShutdown -- Roadshow-compatible: shut the network down (remove all interfaces).
 * Template TIMEOUT/N,QUIET/S */
#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
struct Library *SocketBase;
#include "ng_lvo.h"
int main(void){
  struct RDArgs *rda; LONG a[2]={0,0}; int quiet, rc=RETURN_OK; struct List *l; struct Node *nd;
  /* TIMEOUT/N (a[0]) is accepted for Roadshow argument compatibility; removals here are
   * synchronous, so it is not used as a wait bound. */
  rda=ReadArgs((STRPTR)"TIMEOUT/N,QUIET/S",a,NULL);
  if(!rda){PrintFault(IoErr(),(STRPTR)"NetShutdown");return RETURN_ERROR;}
  quiet=a[1]!=0;
  SocketBase=OpenLibrary((STRPTR)"bsdsocket.library",4L);
  if(!SocketBase){if(!quiet)Printf((STRPTR)"NetShutdown: cannot open bsdsocket.library\n");FreeArgs(rda);return RETURN_FAIL;}
  /* Remove every real interface. The interface list includes the loopback (lo0), which
   * we must NOT remove -- it is not a configurable network interface and must stay up;
   * removing it (or looping forever on it if the stack refuses) would be wrong. So on
   * each pass take the first NON-loopback interface, remove it, and re-list (removal
   * invalidates the list); stop when only the loopback (or nothing) is left. */
  while((l=ng_obtainiflist())!=NULL){
    struct Node *victim=NULL;
    for(nd=(struct Node*)l->lh_Head; nd->ln_Succ; nd=nd->ln_Succ){
      const char *nm=nd->ln_Name;
      if(nm[0]=='l'&&nm[1]=='o'&&nm[2]=='0'&&nm[3]=='\0') continue;  /* skip lo0 */
      victim=nd; break;
    }
    if(!victim){ ng_releaseiflist(l); break; }            /* only loopback left -> done */
    if(!quiet)Printf((STRPTR)"NetShutdown: removing %s\n",(LONG)victim->ln_Name);
    if(!ng_removeif(victim->ln_Name, 1)){                 /* force */
      /* Removal failed: report and STOP rather than retry the same interface forever.
       * The scan order is deterministic, so an un-removable interface would otherwise
       * be picked every pass and loop endlessly. */
      if(!quiet)Printf((STRPTR)"NetShutdown: could not remove %s (errno %ld)\n",(LONG)victim->ln_Name,ng_errno());
      ng_releaseiflist(l); rc=RETURN_WARN; break;
    }
    ng_releaseiflist(l);
  }
  CloseLibrary(SocketBase); FreeArgs(rda); return rc;
}
