#ifndef UNISTD_H
#define	UNISTD_H
/*
**      $Filename: unistd.h $
**	$Release$
**      $Revision: 3.5 $
**      $Date: 1994/04/07 20:23:52 $
**
**	Usergroup.library and bsdsocket.library function prototypes
**	    for 32 bit C compilers
**
**	Copyright © 1993 AmiTCP/IP Group, <AmiTCP-Group@hut.fi>
**                  Helsinki University of Technology, Finland.
**                  All rights reserved.
*/

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif
#ifndef SYS_TIME_H
#include <sys/time.h>
#endif

gid_t getegid(void);
uid_t geteuid(void);
gid_t getgid(void);
int   getgroups(int, gid_t []);
char *getlogin(void);
uid_t getuid(void);
int   isatty(int);
int   setgid(gid_t);
int   setuid(uid_t);
char *ttyname(int);
pid_t getpid(void);
pid_t setsid(void);
void  sleep(unsigned int);
mode_t umask(mode_t);

int __dup(int);
int __dup2(int, int);

#define dup   __dup
#define dup2  __dup2 

#ifndef	_POSIX_SOURCE
pid_t getsid(void);
mode_t getumask(void);

#if 0
int   getdtablesize(void);
long  gethostid(void);
int   gethostname(char *, int);
#endif
int   select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
void  sethostid(long);
int   sethostname(const char *, int);
int   rcmd(char **, int, const char *, const char *, const char *, int *);
int   ruserok(const char *, int, const char *, const char *);
int   rresvport(int *alport);

char *crypt(const char *, const char *);
char *getpass(const char *);
char *getusershell(void);

int   initgroups(const char *, gid_t);
int   setgroups(int, const gid_t []);
int   setlogin(const char *);
int   setregid(gid_t, gid_t);
int   setreuid(uid_t, uid_t);
int   setrgid(gid_t);
int   setruid(uid_t);
int   setegid(gid_t);
int   seteuid(uid_t);
void  setusershell(void);
int   ttyslot(void);
void  usleep(unsigned int);
#endif /* !_POSIX_SOURCE */

#endif /* !_UNISTD_H_ */
