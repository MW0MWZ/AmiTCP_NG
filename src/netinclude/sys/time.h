#ifndef SYS_TIME_H
#define SYS_TIME_H
/*
 * $Id: time.h,v 1.14 1993/10/18 05:48:28 jraja Exp $
 *
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 */

#ifndef DEVICES_TIMER_H
#include <devices/timer.h>
#endif

/* 
 * struct timeval is defined in <devices/timer.h>.  It is similar to the
 * struct timeval defined normally in sys/time.h, but the names of the
 * fields are different and the values are unsigned long instead of
 * long. Following code rename the fields so that the UNIX code
 * works correctly. (tv_sec and tv_usec are signed!!)
 */

struct compatible_timeval {
  union {
    long s_sec;
    ULONG u_secs;
  } mtv_sec;
  union {
    long s_usec;
    ULONG u_micro;
  } mtv_usec;
};

#define timeval compatible_timeval
#define tv_sec mtv_sec.s_sec
#define tv_usec mtv_usec.s_usec
#define tv_secs mtv_sec.u_secs
#define tv_micro mtv_usec.u_micro

/*
 * We must define the timerequest, because compatible_timeval is not 
 * compatible with old timeval...
 */
struct compatible_timerequest {
    struct IORequest tr_node;
    struct timeval tr_time;
};
#define timerequest compatible_timerequest

#if __SASC
#ifndef PROTO_TIMER_H
#include <proto/timer.h>
#endif
#elif __GNUC__
#ifndef _INLINE_TIMER_H
/*
 * predefine TimerBase to Library to follow SASC convention.
 */
#define BASE_EXT_DECL extern struct Library * TimerBase;
#define BASE_NAME (struct Device *)TimerBase
#include <inline/timer.h>
#endif
#else
#include <clib/timer_protos.h>
#endif

#ifdef KERNEL
/*
 * The functionality and interface of get_time() and microtime() is similar
 * to the amiga timer.device's GetSysTime(), so they are just defined as
 * follows:
 */
#define get_time GetSysTime
#define microtime GetSysTime
/* 
 * There are no timezones in V36 timer device, nor tries the KERNEL to 
 * use them.
 */
#define gettimeofday(x,y) GetSysTime(x)	
#else
/*
 * These are not used by AmiTCP/IP itself
 */

/*
 * bacause of a name conflict with SAS/C time.h definition 'timezone' and
 * BSD sys/time.h struct timezone, time.h must always be included first.
 * (the struct timezone becomes actually struct __timezone, but this does
 * not raise a problem at the source level).
 */
#if __SASC
#include <time.h>
#endif

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};
#define	DST_NONE	0	/* not on dst */
#define	DST_USA		1	/* USA style dst */
#define	DST_AUST	2	/* Australian style dst */
#define	DST_WET		3	/* Western European dst */
#define	DST_MET		4	/* Middle European dst */
#define	DST_EET		5	/* Eastern European dst */
#define	DST_CAN		6	/* Canada */

/* defined in the net.lib */
extern int gettimeofday(struct timeval *tp, struct timezone *tzp);
#endif /* KERNEL */

/*
 * Operations on timevals.
 *
 * NB: timercmp does not work for >= or <=.
 */
#define	timerisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#define	timercmp(tvp, uvp, cmp)	\
	((tvp)->tv_sec cmp (uvp)->tv_sec || \
	 (tvp)->tv_sec == (uvp)->tv_sec && (tvp)->tv_usec cmp (uvp)->tv_usec)
#define	timerclear(tvp)		(tvp)->tv_sec = (tvp)->tv_usec = 0

#endif /* !SYS_TIME_H */

