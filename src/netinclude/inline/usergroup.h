#ifndef _INLINE_USERGROUP_H
#define _INLINE_USERGROUP_H

/*
 * $Id: usergroup.h,v 3.2 1994/04/08 15:21:28 too Exp $
 * $Release$
 *
 * Gcc inlines for usergroup.library. 
 *
 * Copyright © 1994 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                  All rights reserved.
 *
 */
#include <sys/cdefs.h>
#include <inline/stubs.h>

__BEGIN_DECLS

#ifdef BASE_EXT_DECL
#error BASE_EXT_DECL etc declarations not allowed
#endif

#ifndef USERGROUPBASE_IN_CONTEXT
#define BASE_EXT_DECL extern struct Library * UserGroupBase;
#define BASE_PAR_DECL
#define BASE_PAR_DECL0 void
#else
#define BASE_EXT_DECL
#define BASE_PAR_DECL struct Library * UserGroupBase,
#define BASE_PAR_DECL0 struct Library * UserGroupBase
#endif
#define BASE_NAME UserGroupBase

static __inline char *
crypt (BASE_PAR_DECL const char *key,const char * salt)
{
  BASE_EXT_DECL
  register char * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const char *a0 __asm("a0") = key;
  register const char *a1 __asm("a1") =  salt;
  __asm __volatile ("jsr a6@(-0xae)"
  : "=r" (_res)
  : "r" (a6), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline void 
endgrent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xa8)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline void 
endpwent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x8a)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline void 
endutent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xf0)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline gid_t 
getegid (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register gid_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x4e)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline uid_t 
geteuid (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register uid_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x36)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline gid_t 
getgid (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register gid_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x48)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct group *
getgrent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct group * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xa2)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct group *
getgrgid (BASE_PAR_DECL gid_t gid)
{
  BASE_EXT_DECL
  register struct group * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register gid_t d0 __asm("d0") = gid;
  __asm __volatile ("jsr a6@(-0x96)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct group *
getgrnam (BASE_PAR_DECL const char * name)
{
  BASE_EXT_DECL
  register struct group * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const char * a1 __asm("a1") = name;
  __asm __volatile ("jsr a6@(-0x90)"
  : "=r" (_res)
  : "r" (a6), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
getgroups (BASE_PAR_DECL int ngroups,gid_t * groups)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register int d0 __asm("d0") = ngroups;
  register gid_t *a1 __asm("a1") =  groups;
  __asm __volatile ("jsr a6@(-0x60)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct lastlog *
getlastlog (BASE_PAR_DECL uid_t uid)
{
  BASE_EXT_DECL
  register struct lastlog * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register uid_t d0 __asm("d0") = uid;
  __asm __volatile ("jsr a6@(-0xf6)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline char *
getlogin (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register char * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xd8)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline char *
getpass (BASE_PAR_DECL const char *prompt)
{
  BASE_EXT_DECL
  register char * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const char *a1 __asm("a1") = prompt;
  __asm __volatile ("jsr a6@(-0xba)"
  : "=r" (_res)
  : "r" (a6), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline pid_t 
getpgrp (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register pid_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xd2)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct passwd *
getpwent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct passwd * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x84)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct passwd *
getpwnam (BASE_PAR_DECL const char *name)
{
  BASE_EXT_DECL
  register struct passwd * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const char *a1 __asm("a1") = name;
  __asm __volatile ("jsr a6@(-0x72)"
  : "=r" (_res)
  : "r" (a6), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct passwd *
getpwuid (BASE_PAR_DECL uid_t uid)
{
  BASE_EXT_DECL
  register struct passwd * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register uid_t d0 __asm("d0") = uid;
  __asm __volatile ("jsr a6@(-0x78)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline uid_t 
getuid (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register uid_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x30)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline mode_t 
getumask (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register mode_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xc6)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline struct utmp *
getutent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct utmp * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xea)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
initgroups (BASE_PAR_DECL const char *name,gid_t  basegroup)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const char *a1 __asm("a1") = name;
  register gid_t d0 __asm("d0") =  basegroup;
  __asm __volatile ("jsr a6@(-0x6c)"
  : "=r" (_res)
  : "r" (a6), "r" (a1), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
setgid (BASE_PAR_DECL gid_t id)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register gid_t d0 __asm("d0") = id;
  __asm __volatile ("jsr a6@(-0x5a)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline void 
setgrent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x9c)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline int 
setgroups (BASE_PAR_DECL int ngroups,const gid_t * groups)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register int d0 __asm("d0") = ngroups;
  register const gid_t *a1 __asm("a1") =  groups;
  __asm __volatile ("jsr a6@(-0x66)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
setlastlog (BASE_PAR_DECL uid_t uid,char * name,char * host)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register uid_t d0 __asm("d0") = uid;
  register char *a0 __asm("a0") =  name;
  register char *a1 __asm("a1") =  host;
  __asm __volatile ("jsr a6@(-0xfc)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
setlogin (BASE_PAR_DECL const char *buffer)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const char *a1 __asm("a1") = buffer;
  __asm __volatile ("jsr a6@(-0xde)"
  : "=r" (_res)
  : "r" (a6), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline void 
setpwent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x7e)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline int 
setregid (BASE_PAR_DECL gid_t real,gid_t  eff)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register gid_t d0 __asm("d0") = real;
  register gid_t d1 __asm("d1") =  eff;
  __asm __volatile ("jsr a6@(-0x54)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
setreuid (BASE_PAR_DECL uid_t real,uid_t  eff)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register uid_t d0 __asm("d0") = real;
  register uid_t d1 __asm("d1") =  eff;
  __asm __volatile ("jsr a6@(-0x3c)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline pid_t 
setsid (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register pid_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xcc)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
setuid (BASE_PAR_DECL uid_t id)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register uid_t d0 __asm("d0") = id;
  __asm __volatile ("jsr a6@(-0x42)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline void 
setutent (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0xe4)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline int 
ug_GetErr (BASE_PAR_DECL0)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  __asm __volatile ("jsr a6@(-0x24)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline char *
ug_GetSalt (BASE_PAR_DECL const struct passwd *user,char * buffer,ULONG  size)
{
  BASE_EXT_DECL
  register char * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const struct passwd *a0 __asm("a0") = user;
  register char *a1 __asm("a1") =  buffer;
  register ULONG d0 __asm("d0") =  size;
  __asm __volatile ("jsr a6@(-0xb4)"
  : "=r" (_res)
  : "r" (a6), "r" (a0), "r" (a1), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline int 
ug_SetupContextTagList (BASE_PAR_DECL const UBYTE* pname,struct TagItem * taglist)
{
  BASE_EXT_DECL
  register int  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register const UBYTE* a0 __asm("a0") = pname;
  register struct TagItem *a1 __asm("a1") =  taglist;
  __asm __volatile ("jsr a6@(-0x1e)"
  : "=r" (_res)
  : "r" (a6), "r" (a0), "r" (a1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
#ifndef NO_INLINE_STDARG
#define ug_SetupContextTags(const UBYTE * pname, tags...) \
    ({ ULONG _tags[] = { tags }; ug_SetupContextTagList(pname, _tags); })
#endif
static __inline const char *
ug_StrError (BASE_PAR_DECL LONG code)
{
  BASE_EXT_DECL
  register const char * _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register LONG d1 __asm("d1") = code;
  __asm __volatile ("jsr a6@(-0x2a)"
  : "=r" (_res)
  : "r" (a6), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline mode_t 
umask (BASE_PAR_DECL mode_t mask)
{
  BASE_EXT_DECL
  register mode_t  _res  __asm("d0");
  register struct Library * a6 __asm("a6") = BASE_NAME;
  register mode_t d0 __asm("d0") = mask;
  __asm __volatile ("jsr a6@(-0xc0)"
  : "=r" (_res)
  : "r" (a6), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}

#ifdef USERGROUPBASE_IN_CONTEXT

/* Setup functions */
#define ug_SetupContextTagList(r...) ug_SetupContextTagList(UserGroupBase, r)
#define		     ug_GetErr(r)    ug_GetErr(UserGroupBase)
#define		   ug_StrError(r...) ug_StrError(UserGroupBase, r)
/* User identification */
#define			getuid(r)    getuid(UserGroupBase)
#define		       geteuid(r)    geteuid(UserGroupBase)
#define		      setreuid(r...) setreuid(UserGroupBase, r)
#define			setuid(r...) setuid(UserGroupBase, r)
/* Group membership */
#define			getgid(r)    getgid(UserGroupBase)
#define		       getegid(r)    getegid(UserGroupBase)
#define		      setregid(r...) setregid(UserGroupBase, r)
#define			setgid(r...) setgid(UserGroupBase, r)
#define		     getgroups(r...) getgroups(UserGroupBase, r)
#define		     setgroups(r...) setgroups(UserGroupBase, r)
#define		    initgroups(r...) initgroups(UserGroupBase, r)
/* User database */
#define		      getpwnam(r...) getpwnam(UserGroupBase, r)
#define		      getpwuid(r...) getpwuid(UserGroupBase, r)
#define		      setpwent(r)    setpwent(UserGroupBase)
#define		      getpwent(r)    getpwent(UserGroupBase)
#define		      endpwent(r)    endpwent(UserGroupBase)
/* Group database */
#define		      getgrnam(r...) getgrnam(UserGroupBase, r)
#define		      getgrgid(r...) getgrgid(UserGroupBase, r)
#define		      setgrent(r)    setgrent(UserGroupBase)
#define		      getgrent(r)    getgrent(UserGroupBase)
#define		      endgrent(r)    endgrent(UserGroupBase)
/* Password handling */
#define			 crypt(r...) crypt(UserGroupBase, r)
#define		    ug_GetSalt(r...) ug_GetSalt(UserGroupBase, r)
#define		       getpass(r...) getpass(UserGroupBase, r)
/* Default protections */
#define			 umask(r...) umask(UserGroupBase, r)
#define		      getumask(r)    getumask(UserGroupBas)
/* Sessions */
#define			setsid(r)    setsid(UserGroupBase)
#define		       getpgrp(r)    getpgrp(UserGroupBase)
#define		      getlogin(r)    getlogin(UserGroupBase)
#define		      setlogin(r...) setlogin(UserGroupBase, r)
/* Sessions, user login database (utmp) */
#define		      setutent(r)    setutent(UserGroupBase)
#define		      getutent(r)    getutent(UserGroupBase)
#define		      endutent(r)    endutent(UserGroupBase)
#define		    getlastlog(r...) getlastlog(UserGroupBase, r)
#define		    setlastlog(r...) setlastlog(UserGroupBase, r)

#endif /* USERGROUPBASE_IN_CONTEXT */

#undef BASE_EXT_DECL
#undef BASE_PAR_DECL
#undef BASE_PAR_DECL0
#undef BASE_NAME

__END_DECLS

#endif /* _INLINE_USERGROUP_H */
