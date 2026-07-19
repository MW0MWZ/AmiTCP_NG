/*
 * charread.h
 * 
 * Author: Tomi Ollila <too@cs.hut.fi>
 * 
 * This module is FREEWARE. Standard "NO WARRANTY" disclaimer applies.
 *
 * Created: Wed Aug  5 18:57:25 1992 too
 * Last modified: Wed Jun 16 19:16:02 1993 too
 * 
 * $Id: charread.h,v 1.1 1993/06/16 16:41:47 too Exp $
 *
 * HISTORY
 * $Log: charread.h,v $
 * Revision 1.1  1993/06/16  16:41:47  too
 * Initial revision
 *
 *
 */

#ifndef CHARREAD_H
#define CHARREAD_H

#ifndef RC_BUFSIZE
#define RC_BUFSIZE 1024
#endif

struct CharRead {
  int	rc_Fd;
  int	rc_Size;
  int	rc_Curr;
  char	rc_Buffer[RC_BUFSIZE];
};

#define RC_DO_SELECT	-3
#define RC_EOF		-2
#define RC_ERROR	-1

#define initCharRead(rc, fd) do { \
				  (rc)->rc_Fd = fd;  \
			          (rc)->rc_Size = 0; \
				  (rc)->rc_Curr = 1; \
			     } while(0)

#ifdef AMIGA
#define RC_R_E_A_D(a, b, c) recv(a, b, c, 0)
#else
#define RC_R_E_A_D(a, b, c) read(a, b, c)
#endif

#define charRead(rc) \
	((rc)->rc_Curr >= (rc)->rc_Size ? \
		(rc)->rc_Curr++ == (rc)->rc_Size ? \
			RC_DO_SELECT: \
			((rc)->rc_Size = RC_R_E_A_D((rc)->rc_Fd, \
						    (rc)->rc_Buffer, \
						    RC_BUFSIZE)) <= 0 ? \
				(rc)->rc_Size == 0 ? \
					RC_EOF:	\
					RC_ERROR: \
				(rc)->rc_Buffer[((rc)->rc_Curr = 1) - 1]: \
	        (rc)->rc_Buffer[(rc)->rc_Curr++])

#endif /* _CHARREAD_H_ */
