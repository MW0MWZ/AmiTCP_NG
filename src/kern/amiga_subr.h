/*
 * $Id: amiga_subr.h,v 1.13 1994/01/23 22:06:26 jraja Exp $
 * 
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 */

#ifndef AMIGA_SUBR_H
#define AMIGA_SUBR_H

/*
 * Memory functions are a complete mess in this project. Naming conventions and
 * half-baked optimization ideas with various compilers were genuinely b0rked
 * already in 1994. This should be cleaned up thoroughly before more attempts
 * at optimization are undertaken. SAS/C cruft should be removed completely,
 * as well as useless information about check-ins more than 30 yrs ago, filling
 * endless pages and rendering this stuff unmaintainable (TSM260903)
 */

#define RARG(reg,arg) arg __asm(reg)
void ng_bcopy( RARG("a0", void *), RARG("a1", void *), RARG("d0", LONG) );
void ng_bzero( RARG("a0", void *), RARG("d0", LONG) );

#define ovbcopy(src,dst,len) ng_bcopy((void *)(src),dst,len)
#define bcopy ovbcopy
#define ng_bcopy_dev bcopy
#define aligned_bcopy bcopy
#define aligned_bcopy_const bcopy

#define bzero ng_bzero
#define aligned_bzero bzero
#define aligned_bzero_const bzero


#if __SASC
/*
 * Using builtin functions (string.h included in kern/amiga_includes.h)
 */
#ifndef AMIGA_INCLUDES_H
#include <kern/amiga_includes.h>
#endif

#define imin(a,b) min(a,b)
#define MIN(a,b) min(a,b)
#define lmin(a,b) min(a,b)
#define ulmin(a,b) min(a,b)

#define imax(a,b) max(a,b)
#define MAX(a,b) max(a,b)
#define lmax(a,b) max(a,b)
#define ulmax(a,b) max(a,b)

/*
 * bcopy(), bcmp() and bzero() are defined in string.h
 *
 * NOTE: bcopy is infact ovbcopy(). Optimize this when all other works!
 */

#undef bcopy
#define bcopy(a,b,c) CopyMem((APTR)(a),b,c)
#define ovbcopy(a,b,c) memmove(b,a,c)

#else

#ifndef SYS_CDEFS_H
#include <sys/cdefs.h>
#endif

static inline int 
imin(int a, int b)
{
  return (a < b ? a : b);
}

#define MIN(a,b) imin(a,b)

static inline int 
imax(int a, int b)
{
  return (a > b ? a : b);
}

/*
 * WARNING: min()/max() take UNSIGNED parameters, unlike stock BSD where they
 * are type-preserving macros. A negative argument converts to a huge unsigned
 * value, so `max(x, LOWER_BOUND)` is a SILENT NO-OP when x is negative, and
 * `min(x, y)` returns y for a negative x. Use imin()/imax() (or lmin()/lmax())
 * whenever an operand can be negative -- a signed sequence-number difference, a
 * length derived from a signed 16-bit protocol field, an sbspace() result, and
 * so on. Two real bugs came from this (tcp_input.c's rcv_wnd and tcp_mss's
 * sanity floor); the signatures are left as-is because most call sites are
 * genuinely unsigned and changing them would be the riskier edit.
 */
static inline unsigned int
min(unsigned int a, unsigned int b)
{
  return (a < b ? a : b);
}

static inline unsigned int
max(unsigned int a, unsigned int b)
{
  return (a > b ? a : b);
}

static inline long
lmin(long a, long b)
{
  return (a < b ? a : b);
}

static inline long
lmax(long a, long b)
{
  return (a > b ? a : b);
}

static inline unsigned long
ulmin(unsigned long a, unsigned long b)
{
  return (a < b ? a : b);
}

static inline unsigned long
ulmax(unsigned long a, unsigned long b)
{
  return (a > b ? a : b);
}

static inline int 
bcmp(const void *v1, const void *v2, register unsigned len)
{
  const register u_char *s1 = v1, *s2 = v2;
  
  while (len--)
    if (*s1++ != *s2++)
      return (1);
  return (0);
}

#if 0
static inline void
bzero(void *buf, register unsigned len)
{
  register char *s = buf;

  while(len--)
    *s++ = '\0';
}

static inline void
ovbcopy(const void *v1, void *v2, register unsigned len)
{
  ng_bcopy(v1, v2, (long)len);
}

static inline void
bcopy(const void *v1, void *v2, register unsigned len)
{
  const register u_char *s1 = v1;
  register u_char *s2 = v2;
  
  while (len--)
    *s2++ = *s1++;
}
#endif

static inline int
strlen(register const char *s1)
{
  register int len;
  
  for (len = 0; *s1++ != '\0'; len++)
    ;
  return (len);
}

static inline char *
strcpy(register char *s1, register const char *s2)
{
  register char *s = s1;
  while((*s++ = *s2++))
    ;
  return (s1);
}

static inline char *
strncpy(register char *s1, register const char *s2, register unsigned int len)
{
  register char *s = s1;
  while(len-- && (*s++ = *s2++))
    ;
  return (s1);
}
#endif /* __SASC */

/* 
 * These are for both environments
 */

#ifdef USE_ALIGNED_COPIES

/*
 * clear an aligned memory area of constant length to zero
 */ 
static inline void
aligned_bzero_const(void *buf, long size) 
{
  /* PORT (AmiTCP_NG): long, not short. A 16-bit signed longword count wraps for
   * sizes at or above ~128 KB, which mbuf clusters and socket buffers can reach.
   * This whole family is inert in the current build (USE_ALIGNED_COPIES is only
   * set by the legacy SAS/C Smakefile), so this is a latent trap being closed,
   * not a live bug. */
  long lcount;
  long *lbuf = (long *)buf;
  short *sbuf;

  lcount = (size >> 2);
  if (lcount--) {
    /*
     * unroll the loop if short enough
     */
    if (lcount < 6) {
      *lbuf++ = 0;
      if (--lcount >= 0)
	*lbuf++ = 0;
      if (--lcount >= 0)
	*lbuf++ = 0;
      if (--lcount >= 0)
	*lbuf++ = 0;
      if (--lcount >= 0)
	*lbuf++ = 0;
      if (--lcount >= 0)
	*lbuf++ = 0;
    }
    else {
      do {
	*lbuf++ = 0;
      } while (--lcount >= 0);
    }
  }

  sbuf = (short *)lbuf;
  if (size & 0x2)
    *sbuf++ = 0;

  if (size & 0x1)
    *(char *)sbuf = 0;
}

static inline void
aligned_bzero(void *buf, long size) 
{
  /* PORT (AmiTCP_NG): long, not short. A 16-bit signed longword count wraps for
   * sizes at or above ~128 KB, which mbuf clusters and socket buffers can reach.
   * This whole family is inert in the current build (USE_ALIGNED_COPIES is only
   * set by the legacy SAS/C Smakefile), so this is a latent trap being closed,
   * not a live bug. */
  long lcount;
  long *lbuf = (long *)buf;
  short *sbuf;

  lcount = (size >> 2);
  if (lcount--) {
    do {
      *lbuf++ = 0;
    } while (--lcount >= 0);
  }

  sbuf = (short *)lbuf;
  if (size & 0x2)
    *sbuf++ = 0;

  if (size & 0x1)
    *(char *)sbuf = 0;
}

static inline void
aligned_bcopy_const(const void *src, void *dst, long size) 
{
  /* PORT (AmiTCP_NG): long, not short. A 16-bit signed longword count wraps for
   * sizes at or above ~128 KB, which mbuf clusters and socket buffers can reach.
   * This whole family is inert in the current build (USE_ALIGNED_COPIES is only
   * set by the legacy SAS/C Smakefile), so this is a latent trap being closed,
   * not a live bug. */
  long lcount;
  long *ldst = (long *)dst;
  short *sdst;
  long *lsrc = (long *)src;
  short *ssrc;

  lcount = (size >> 2);
  if (lcount--) {
    /*
     * unroll the loop if short enough
     */
    if (lcount < 6) {
      *ldst++ = *lsrc++;
      if (--lcount >= 0)
	*ldst++ = *lsrc++;
      if (--lcount >= 0)
	*ldst++ = *lsrc++;
      if (--lcount >= 0)
	*ldst++ = *lsrc++;
      if (--lcount >= 0)
	*ldst++ = *lsrc++;
      if (--lcount >= 0)
	*ldst++ = *lsrc++;
    }
    else {
      do {
	*ldst++ = *lsrc++;
      } while (--lcount >= 0);
    }
  }

  sdst = (short *)ldst;
  ssrc = (short *)lsrc;
  if (size & 0x2)
    *sdst++ = *ssrc++;

  if (size & 0x1)
    *(char *)sdst = *(char *)ssrc;
}

static inline void
aligned_bcopy(const void *src, void *dst, long size) 
{
  /* PORT (AmiTCP_NG): long, not short. A 16-bit signed longword count wraps for
   * sizes at or above ~128 KB, which mbuf clusters and socket buffers can reach.
   * This whole family is inert in the current build (USE_ALIGNED_COPIES is only
   * set by the legacy SAS/C Smakefile), so this is a latent trap being closed,
   * not a live bug. */
  long lcount;
  long *ldst = (long *)dst;
  short *sdst;
  long *lsrc = (long *)src;
  short *ssrc;

  lcount = (size >> 2);
  if (lcount--) {
    do {
      *ldst++ = *lsrc++;
    } while (--lcount >= 0);
  }

  sdst = (short *)ldst;
  ssrc = (short *)lsrc;
  if (size & 0x2)
    *sdst++ = *ssrc++;

  if (size & 0x1)
    *(char *)sdst = *(char *)ssrc;
}
#endif /* USE_ALIGNED_COPIES */
#endif /* AMIGA_SUBR_H */
