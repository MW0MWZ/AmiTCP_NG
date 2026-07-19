#ifndef PWD_H
#define PWD_H
/*
**      $Filename: pwd.h $
**	$Release$
**      $Revision: 3.3 $
**      $Date: 1994/04/07 20:23:52 $
**
**	Definitions of uid_t and passwd structure for 32 bit C compilers
**
**	Copyright © 1993,1994 AmiTCP/IP Group, <AmiTCP-Group@hut.fi>
**                  Helsinki University of Technology, Finland.
**                  All rights reserved.
*/

#ifndef SYS_TYPES_H
#include <sys/types.h>
#endif

/* The passwd structure */
struct passwd
{
  char  *pw_name;               /* Username */
  char  *pw_passwd;             /* Encrypted password */
  uid_t  pw_uid;                /* User ID */
  gid_t  pw_gid;                /* Group ID */
  char  *pw_gecos;		/* Real name etc */
  char  *pw_dir;                /* Home directory */
  char  *pw_shell;              /* Shell */
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

void setpwent(void);
struct passwd *getpwent(void);
void endpwent(void);

#endif /* PWD_H */
