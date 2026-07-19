/*
 * lineread.h
 * 
 * Author: Tomi Ollila <too@cs.hut.fi>
 * 
 * This module is FREEWARE. Standard "NO WARRANTY" disclaimer applies.
 *
 * Created: Fri Nov 29 03:23:03 1991 too
 * Last modified: Wed Jul  7 20:42:54 1993 too
 * 
 * $Id: lineread.h,v 1.2 1993/07/07 17:45:36 too Exp $
 *
 * HISTORY
 * $Log: lineread.h,v $
 * Revision 1.2  1993/07/07  17:45:36  too
 * Made lineread compile without using sys/cdefs.h (only w/ ANSI compilers)
 *
 * Revision 1.1  1993/06/16  16:41:04  too
 * Initial revision
 *
 *
 */

#ifndef LINEREAD_H
#define LINEREAD_H

/* #include <sys/cdefs.h> */

#ifndef RL_BUFSIZE
#define RL_BUFSIZE 1024
#endif

struct rl_private {
/*  struct Library * rlp_SocketBase; */
  int        rlp_Startp;
  int        rlp_Bufpointer;
  int        rlp_Howlong;
  int	     rlp_Buffersize;
#define bool int
  bool	     rlp_Line_completed;
  bool	     rlp_Selected;
#undef bool  
  char       rlp_Saved;
  char       rlp_Buffer[RL_BUFSIZE + 1];
};

struct LineRead {
  char *							rl_Line;
  enum {RL_LFNOTREQ = 0, RL_LFREQLF = 1, RL_LFREQNUL = 2}	rl_Lftype;
  int								rl_Fd;
  struct rl_private						rl_Private;
};

/*__BEGIN_DECLS
  
int	lineRead __P((struct LineRead * rl));
void	initLineRead __P((struct LineRead * rl, int fd,
			  int lftype, int buffersize));

__END_DECLS */

int	lineRead (struct LineRead * rl);
void	initLineRead (struct LineRead * rl, int fd,
			  int lftype, int buffersize);

#endif /* LINEREAD_H */
