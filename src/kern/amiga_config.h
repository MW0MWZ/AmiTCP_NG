#ifndef AMIGA_CONFIG_H
#define AMIGA_CONFIG_H
/* $Id: amiga_config.h,v 1.17 1994/01/18 02:27:22 jraja Exp $
 *
 * kern/amiga_config.h --- configuration declarations
 *
 * Author: ppessi <Pekka.Pessi@hut.fi>
 *
 * Copyright (c) 1993 OHT-AmiTCP/IP Group,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Created      : Thu Apr 22 12:05:05 1993 ppessi
 * Last modified: Mon Dec 20 10:20:26 1993 jraja
 *
 * $Log: amiga_config.h,v $
 * Revision 1.17  1994/01/18  02:27:22  jraja
 * Added SPACE(X) macro.
 *
 * Revision 1.15  1993/12/20  08:24:16  jraja
 * Added new rexx keyword: "KILL" + prototype for the sendbreak() function.
 *
 * Revision 1.14  1993/11/06  23:51:22  ppessi
 * Changed error message format.
 *
 * Revision 1.13  1993/06/03  16:39:39  jraja
 * Added VAR_ENUM variable type.
 *
 * Revision 1.12  1993/05/31  23:31:05  jraja
 * C=CONNECTIONS keyword deleted, getsockets() prototype deleted.
 *
 * Revision 1.11  1993/05/29  20:51:15  jraja
 * Removed config file path, added error value ERR_NOWRITE,
 * Changed readconfig to return BOOL.
 *
 * Revision 1.10  1993/05/17  01:02:04  ppessi
 * Changed RCS version
 *
 * Revision 1.9  1993/05/16  00:12:11  jraja
 * Minor cosmetic changes.
 *
 * Revision 1.8  1993/05/15  20:14:08  jraja
 * Added RESET to the rexx keywords template, also changed order to agree with
 * enum keyword.
 *
 * Revision 1.7  93/05/14  11:36:39  11:36:39  ppessi (Pekka Pessi)
 * Error messages now global constants.
 * 
 * Revision 1.6  93/05/04  12:20:10  12:20:10  jraja (Jarno Tapio Rajahalme)
 * fix.
 * 
 * Revision 1.5  93/04/28  21:55:03  21:55:03  ppessi (Pekka Pessi)
 * Added some prototypes.
 * 
 * Revision 1.4  93/04/28  13:36:27  13:36:27  too (Tomi Ollila)
 * Fixed one #defien to #define
 * this revision includes also some ppessi work
 * 
 * Revision 1.3  93/04/25  21:00:17  21:00:17  too (Tomi Ollila)
 * Changed some structure field names for consistency
 * 
 * Revision 1.2  93/04/23  21:01:28  21:01:28  puhuri (Markus Peuhkuri)
 * Add prototype for getsockets.
 * 
 * Revision 1.1  93/04/23  00:26:57  00:26:57  ppessi (Pekka Pessi)
 * Initial revision
 * 
 * Revision 1.1  93/04/22  12:26:23  ppessi
 * Initial revision
 *
 */

extern BOOL initialized;	/* are we up? */

#define CURRENT(X) ((X)->CS_Buffer+(X)->CS_CurChr)
#define SPACE(X)   ((X)->CS_Length-(X)->CS_CurChr)

#define KEYWORDLEN 24		/* buffer len for keywords */
#define CONFIGLINELEN 1024	/* buffer len for configuration line */
#define REPLYBUFLEN 255
#define MAXRVALLEN 10

/* Parsing error messages */
extern UBYTE ERR_UNKNOWN[];
extern UBYTE ERR_ILLEGAL_VAR[];
extern UBYTE ERR_ILLEGAL_IND[];
extern UBYTE ERR_SYNTAX[];
extern UBYTE ERR_TOO_LONG[];
extern UBYTE ERR_MEMORY[];
extern UBYTE ERR_NONETDB[];
extern UBYTE ERR_VALUE[];
extern UBYTE ERR_NOWRITE[];

/* The command keywords and their tokens */
/* Note: ROUTE is currently not implemented */
#define REXXKEYWORDS "Q=QUERY,S=SET,READ,ROUTE,ADD,RESET,KILL"
enum keyword
{ KEY_QUERY, KEY_SET, KEY_READ, KEY_ROUTE, KEY_ADD, KEY_RESET, KEY_KILL };

/* Variable types */
/* Note: Query calls value, Set calls notify functions */
enum var_type
{
 VAR_FUNC = 1,		/* value is function pointer */
 VAR_LONG,		/* value is pointer to LONG */
 VAR_STRP,		/* value is pointer to string */
 VAR_FLAG,		/* LONG value is set once */
 VAR_INET,		/* struct sockaddr_in */
 VAR_ENUM		/* value is pointer to long, whose value is set
                           according to a enumeration string in notify */
};

typedef LONG 
  (*var_f)(struct CSource *args, UBYTE **errstrp, struct CSource *res);
typedef int (*notify_f)(void *pt, LONG new);

/* Configurable variable structure */
struct cfg_variable {
  enum var_type type;		/* type of value */
  WORD  flags;			/* see below */
  const char *index;		/* optional index keyword list (KW_* string literals);
				 * char* so the generated config_var.c string
				 * initializers match -- use sites cast to UBYTE*. */
  void  *value;			/* pointer to value... */
  notify_f notify;		/* notification function */
};

#define boolean_enum (notify_f)"NO=FALSE=OFF=0,YES=TRUE=ON=1"

/* Variable flags */
#define VF_TABLE    (1<<0) /* with an index... */
#define VF_READ     (1<<1) /* readable */
#define VF_WRITE    (1<<2) /* writeable */
#define VF_CONF     (1<<3) /* writeable only during configuration */
#define VF_RW       (VF_WRITE|VF_READ)
#define VF_RCONF    (VF_CONF|VF_READ)
#define VF_FREE     (1<<8) /* free when replaced? */

BOOL readconfig(void);

LONG parsefile(UBYTE const *fname, UBYTE **errstrp, struct CSource *res);
LONG parseline(struct CSource *args, UBYTE **errstrp, struct CSource *res);
LONG readfile(struct CSource *args, UBYTE **errstrp, struct CSource *res);
LONG getvalue(struct CSource *args, UBYTE **errstrp, struct CSource *res);
LONG setvalue(struct CSource *args, UBYTE **errstrp, struct CSource *res);
LONG sendbreak(struct CSource *args, UBYTE **errstrp, struct CSource *res);

/*LONG read_gets(struct CSource *args, UBYTE **errstrp, struct CSource *res);*/
LONG read_sets(struct CSource *args, UBYTE **errstrp, struct CSource *res);

LONG parseroute(struct CSource *args, UBYTE **errstrp, struct CSource *res);

#endif /* !AMIGA_CONFIG_H */

