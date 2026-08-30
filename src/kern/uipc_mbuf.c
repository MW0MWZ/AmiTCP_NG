/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C = "$Id: uipc_mbuf.c,v 1.18 1993/06/04 11:16:15 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * Last modified: Fri Jun  4 00:36:45 1993 jraja
 *
 * HISTORY
 * $Log: uipc_mbuf.c,v $
 * Revision 1.18  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.17  1993/05/29  20:57:23  jraja
 * Added function mb_read_stats() to return mbuf type specific statistics.
 *
 * Revision 1.16  1993/05/16  15:20:25  ppessi
 * Fixed bug with cluster allocation.
 *
 * Revision 1.16  1993/05/16  15:20:25  ppessi
 * Fixed bug with cluster allocation.
 *
 * Revision 1.15  93/05/04  12:52:25  12:52:25  jraja (Jarno Tapio Rajahalme)
 * Fixed default values of the configuration variables.
 * 
 * Revision 1.14  93/04/25  02:59:37  02:59:37  jraja (Jarno Tapio Rajahalme)
 * Added some comments.
 * 
 * Revision 1.13  93/04/24  22:19:41  22:19:41  jraja (Jarno Tapio Rajahalme)
 * Removed MBTYPES, moved configurable variables to a structure (mbconf),
 * removed nmbufs and nmbclusters (already in mbstat), moved mbufmemsize to
 * mbstat (as m_memused), added configuration notify function 
 * mb_check_conf() to validate configurable variables,
 * added checks for maximum memory usage,
 * removed m_retryhdr(), since m_retry() is already called by MGETHDR,
 * removed all USECLUSTERS (now using clusters always.
 * 
 * Revision 1.12  93/04/23  02:26:28  02:26:28  ppessi (Pekka Pessi)
 * Added some configureable parameters
 * 
 * Revision 1.11  93/04/13  22:31:51  22:31:51  jraja (Jarno Tapio Rajahalme)
 * Added #ifdef USECLUSTERS ... #endif to compile without.
 * 
 * Revision 1.10  93/04/06  15:16:04  15:16:04  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.9  93/04/02  01:08:17  01:08:17  jraja (Jarno Tapio Rajahalme)
 * Implemented clusters.
 * Updated memory allocation.
 * Added memHeader structure to keep account of allocated memory.
 * 
 * Revision 1.8  93/03/05  03:26:21  03:26:21  ppessi (Pekka Pessi)
 * Compiles with SASC. Initial test version.
 * 
 * Revision 1.7  93/03/04  09:55:46  09:55:46  jraja (Jarno Tapio Rajahalme)
 * Fixed includes.
 * 
 * Revision 1.6  93/03/03  19:59:29  19:59:29  jraja (Jarno Tapio Rajahalme)
 * Added static initializers to globals.
 * 
 * Revision 1.5  93/03/03  19:20:43  19:20:43  jraja (Jarno Tapio Rajahalme)
 * Moved some definitions from sys/mbuf.h to here.
 * 
 * Revision 1.4  93/02/24  12:55:20  12:55:20  jraja (Jarno Tapio Rajahalme)
 * Changed init to remember if initialized.
 * 
 * Revision 1.3  93/01/06  19:24:53  19:24:53  jraja (Jarno Tapio Rajahalme)
 * Ported this for AmigaOS. Added function mbdeinit(), which is used to free
 * memory allocated by mbuf subsystem.
 * Alse commented all memory cluster related stuff with #ifdef USECLUSTERS.
 * 
 * Revision 1.2  92/11/20  15:14:25  15:14:25  jraja (Jarno Tapio Rajahalme)
 * Added #ifndef AMITCP's to make this compile.
 * 
 * Revision 1.1  92/11/19  12:07:15  12:07:15  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 */

/* 
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon 
 * the rights to redistribute these changes.
 */
/*
 * HISTORY
 * Log:	uipc_mbuf.c,v
 * Revision 2.2  92/06/25  17:25:22  mrt
 * 	Preallocate mbufs in a chunk.
 * 	[92/06/24            rwd]
 * 
 * Revision 2.1  92/04/21  17:12:59  rwd
 * BSDSS
 * 
 *
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1991 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)uipc_mbuf.c	7.19 (Berkeley) 4/20/91
 */

/*
 * uipc_mbuf.c --- the mbuf allocator (the stack's packet buffer memory).
 *
 * Derived from 4.4BSD (via Mach); see the copyrights above. This is the ONE place
 * to learn what an mbuf is, so read this header carefully -- every packet in the
 * stack is made of these. docs/ARCHITECTURE.md section 7 (data path) and 8.
 *
 * WHAT AN MBUF IS. An `struct mbuf` is a small fixed-size buffer, MSIZE = 128
 * bytes: a little header (m_next, m_nextpkt, m_data, m_len, m_type, ...) plus
 * ~100 bytes of payload room. Networking never uses one flat buffer per packet;
 * instead a packet is a CHAIN of mbufs linked by `m_next`, and separate packets
 * waiting on a queue are linked by `m_nextpkt`. Because each mbuf's payload is
 * addressed by a (m_data, m_len) pointer/length pair, you prepend a protocol
 * header by allocating a fresh mbuf and pointing it at the chain, and you strip a
 * header by advancing m_data -- all WITHOUT copying the payload. That zero-copy
 * property is why the whole stack speaks mbufs. Larger payloads use CLUSTERS
 * (mbconf.mclbytes bytes, default 2048) that an mbuf can point at instead of
 * carrying data inline.
 *
 * WHY A PRE-ALLOCATED POOL (the Amiga-specific part). In BSD the mbuf allocator
 * lives in the kernel and can grab pages on demand. Here we cannot: inbound
 * packets are copied into mbufs by SANA-II device drivers at (emulated) INTERRUPT
 * time, and you must never call Exec's AllocMem() from an interrupt. So mbinit()
 * reserves a pool up front (AllocMem with MEMF_PUBLIC -- "public" precisely
 * because interrupts touch it) and hands mbufs out from a simple free list `mfree`
 * (clusters from `mclfree`). Every allocation is tracked through the `memHeader`
 * chain so mbdeinit() can hand the memory back when the stack shuts down -- a real
 * BSD kernel never frees mbuf memory, but a library that comes and goes must.
 *
 * LOCKING. The free lists are shared between the main task and interrupt-time
 * driver code, so they are protected by splimp() (see kern/ spl emulation): raise
 * the interrupt priority level, touch the list, splx() back down.
 *
 * Read first: mbinit() (builds the pool), then m_alloc()/m_get()/m_free().
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/synch.h>

#include <kern/amiga_includes.h>

#include <dos/rdargs.h>

/*
 * Configuration information.
 */
struct mbconf mbconf = {
  2,		                /* # of mbuf chunks to allocate initially */
  64,				/* # of mbufs to allocate at a time */
  4,				/* # of clusters to allocate at a time */
  256,				/* maximum memory to use (in kilobytes) */
  2048				/* size of the mbuf cluster */
};

/*
 * List of free mbufs. Access to this list is protected by splimp()
 */
struct mbuf *mfree = NULL;

struct	mbstat mbstat = { 0 };

struct	mcluster *mclfree = NULL;

int	max_linkhdr = 0;		/* largest link-level header */
int	max_protohdr = 0;		/* largest protocol header */
int	max_hdr = 0;			/* largest link+protocol header */
int	max_datalen = 0;		/* MHLEN - max_hdr */

/*
 * Header structure that is placed at the start of every allocated memory 
 * region to be freed on deinit. All memory alloctions are thus 
 * sizeof(memHeader) larger and the data pointer is set past this header
 * before used. These headers are linked together and the mbufmem pointer 
 * holds the pointer to the start of the list.
 */
struct memHeader {
  struct memHeader *next;
  ULONG             size;
  /*
   * PORT (AmiTCP_NG): which kind of chunk this is. BOTH m_alloc() (an array of
   * MSIZE-aligned mbufs) and m_clalloc() (mcluster headers each followed by
   * mclbytes of raw payload) prepend themselves to the single mbufmem list, and
   * the two layouts are nothing alike. m_valid() must not apply mbuf geometry
   * to a cluster chunk -- a 2048-byte cluster body contains sixteen
   * MSIZE-aligned addresses, none of which are mbufs.
   */
  ULONG             ismbuf;	/* nonzero: chunk holds mbufs, not clusters */
};

static struct memHeader *mbufmem = NULL;

static BOOL initialized = FALSE;

/*
 * PORT (AmiTCP_NG) security fix: is `m` plausibly a real mbuf from our pool?
 *
 * The mbuf_* library vectors hand raw struct mbuf * pointers out to
 * applications and take them back again, so a caller controls a pointer that
 * the stack then dereferences and, in MFREE's case, links straight into the
 * free list -- `(m)->m_next = mfree; mfree = (m);` runs unconditionally. A
 * forged pointer therefore becomes the next mbuf handed to ANY task, including
 * the stack's own receive path, which then writes packet data through it. With
 * no MMU that is a write-what-where primitive available to any local task.
 *
 * Every mbuf is MSIZE-aligned by construction (dtom() depends on it) and lives
 * inside one of the chunks on mbufmem, so those two properties are cheap to
 * check and exclude arbitrary addresses -- including the low memory the
 * exception vectors occupy. This CANNOT detect a pointer to an mbuf that was
 * genuinely ours but has since been freed: catching that would need per-mbuf
 * generation tags, which this pool does not have. It closes the "point
 * anywhere" class, not the stale-pointer class.
 *
 * Only for the API boundary -- the stack's own paths already hold valid mbufs
 * and must not pay for this walk.
 */
int
m_valid(struct mbuf *m)
{
  struct memHeader *mh;
  ULONG a = (ULONG)m;
  spl_t ms;
  int ok = 0;

  if (m == NULL || (a & (MSIZE - 1)) != 0)
    return 0;

  ms = splimp();
  for (mh = mbufmem; mh != NULL; mh = mh->next) {
    ULONG base = (ULONG)mh;

    /*
     * Only mbuf chunks have the MSIZE slot geometry -- skip cluster chunks
     * entirely, or a pointer into live cluster payload would validate.
     * The lower bound is the first byte AFTER the memHeader: combined with
     * the alignment test that is exactly the first real mbuf, since no
     * MSIZE-aligned address can lie between the two. Without it, a chunk
     * whose AllocMem() base happened to be MSIZE-aligned would accept the
     * memHeader itself, and freeing "an mbuf" there would overwrite the
     * mh->next that links the chunk list.
     */
    if (!mh->ismbuf)
      continue;
    if (a >= base + sizeof(struct memHeader) &&
	a + MSIZE <= base + mh->size) {
      ok = 1;
      break;
    }
  }
  splx(ms);
  return ok;
}

/*
 * PORT (AmiTCP_NG): mbuf free-list poisoning -- MBUFCHECK= in AmiTCP.config.
 *
 * This pool had NO integrity checking of any kind. MFREE linked an mbuf onto
 * the free list unconditionally, so freeing one twice put it on the list TWICE:
 * two later allocations hand out the same 128 bytes, two owners write through
 * it, and with no MMU nothing traps. The damage surfaces later, somewhere else,
 * as corrupted packets or a severed free list -- which from outside looks like
 * throughput sagging and then transmit dying, with nothing logged.
 *
 * When enabled, a freed mbuf is marked MT_FREE and its data area stamped with a
 * pattern. That converts three silent failures into named ones, AT THE MOMENT
 * THEY HAPPEN rather than hours later:
 *
 *   double free       m_type is already MT_FREE when it is freed again. The
 *                     free is REFUSED -- relinking is what corrupts the list,
 *                     so declining to do it keeps the pool consistent and lets
 *                     the machine stay up long enough to read the log.
 *   use after free    the poison has been altered by the time the mbuf is
 *                     handed out again; the offset says where it was written.
 *   list corruption   an mbuf comes off the free list NOT marked MT_FREE, so
 *                     something reached it by a route other than MFREE.
 *
 * It also closes the gap m_valid() documents above: that check "CANNOT detect a
 * pointer to an mbuf that was genuinely ours but has since been freed ...
 * catching that would need per-mbuf generation tags". MT_FREE is exactly that
 * tag, so with MBUFCHECK on, a freed mbuf handed back through the mbuf_* API
 * vectors is caught rather than recycled under the caller.
 *
 * Cost is why it is a runtime switch and OFF by default: poisoning writes MLEN
 * bytes per free and verifying reads them per allocation, on the hot path. But
 * it is a CONFIG switch, not a build flag -- a diagnostic that needs a special
 * binary cannot diagnose a machine you cannot reach (the same reasoning that
 * put the API tracer in every build; see netinclude/sys/syslog.h).
 */
LONG  ng_mbufcheck        = 0;	/* MBUFCHECK=ON in AmiTCP.config           */
LONG  ng_mbufcheck_active = 0;	/* latched from it by mbinit() -- see below */
ULONG ng_mbuf_doublefree  = 0;	/* freed while already on the free list    */
ULONG ng_mbuf_useafterfree= 0;	/* written while on the free list          */
ULONG ng_mbuf_listcorrupt = 0;	/* left the free list not marked MT_FREE   */

#define NG_MBUF_POISON	0xDFDFDFDFUL
#define NG_MBUF_REPORTS	5	/* log this many of each, then only count.
				 * Corruption tends to repeat, and a log line
				 * per event on a 68k writing to disk would
				 * itself take the machine down. */

/*
 * Called from MFREE before anything is touched. Non-zero means "do not free
 * this": the caller must leave the free list alone.
 */
int
ng_mbuf_bad_free(struct mbuf *m)
{
  if (m->m_type != MT_FREE)
    return 0;
  if (++ng_mbuf_doublefree <= NG_MBUF_REPORTS)
    log(LOG_ERR, "mbuf: DOUBLE FREE of 0x%lx -- refused, not relinked",
	(ULONG)m);
  return 1;
}

/*
 * Called from MFREE with splimp held, AFTER the caller has saved m_next and
 * released any cluster, and BEFORE the mbuf is linked onto the free list.
 * m_next is deliberately left alone -- the caller overwrites it immediately as
 * the free-list link.
 */
void
ng_mbuf_poison(struct mbuf *m)
{
  register ULONG *p = (ULONG *)(void *)m->m_dat;
  register int n = MLEN / sizeof(ULONG);	/* 108/4 -- exact */

  m->m_type = MT_FREE;
  while (n--)
    *p++ = NG_MBUF_POISON;
}

/*
 * Called from MGET with splimp held, after the mbuf is unlinked from the free
 * list and before its type is set. Reports only -- the mbuf is handed over
 * either way, because refusing an allocation here would turn a detected fault
 * into an outage, and the point of this is to NAME the fault, not to police it.
 */
void
ng_mbuf_alloc_check(struct mbuf *m)
{
  register ULONG *p = (ULONG *)(void *)m->m_dat;
  register int i, n = MLEN / sizeof(ULONG);

  if (m->m_type != MT_FREE) {
    if (++ng_mbuf_listcorrupt <= NG_MBUF_REPORTS)
      log(LOG_ERR, "mbuf: 0x%lx left the free list as type %ld, not MT_FREE "
	  "-- free-list corruption", (ULONG)m, (long)m->m_type);
    return;			/* header already wrong; poison check adds noise */
  }
  for (i = 0; i < n; i++) {
    if (p[i] != NG_MBUF_POISON) {
      if (++ng_mbuf_useafterfree <= NG_MBUF_REPORTS)
	log(LOG_ERR, "mbuf: 0x%lx written at +%ld while free (0x%lx) "
	    "-- use after free", (ULONG)m, (long)(i * sizeof(ULONG)), p[i]);
      return;			/* one report per mbuf is enough to locate it */
    }
  }
}

LONG mb_read_stats(struct CSource *args, UBYTE **errstrp, struct CSource *res)
{
  int i, total = 0;

  /*
   * PORT (AmiTCP_NG) fix: the original wrote MTCOUNT+1 numbers into
   * res->CS_Buffer with raw sprintf(), never checking res->CS_Length. Safe only
   * by coincidence today (MTCOUNT small, counters are u_short), but a stack
   * overflow the moment a type is added or a counter widened. csprintf() goes
   * through the bounded cs_putchar(), matching every sibling status function.
   */
  for(i = 0; i < MTCOUNT; i++) {
    csprintf(res, "%ld ", (long)mbstat.m_mtypes[i]);
    total += mbstat.m_mtypes[i];
  }
  csprintf(res, "%ld", (long)total);

  return RETURN_OK;
}

/*
 * PORT (AmiTCP_NG) security fix: every tunable below is settable from the
 * config file (the MBUF CONFIG stanza), and the values flow straight into the
 * size arithmetic of m_alloc()/m_clalloc(). The original only enforced LOWER
 * bounds, so a large config value such as `mbufchunk 20000000` overflowed
 * `MSIZE * (howmany + 1)` past 2^32, wrapped to a tiny AllocMem(), and the
 * fill loop then wrote thousands of mbuf headers off the end of the block --
 * a config-driven heap smash. Clamp each tunable to a sane ceiling. These
 * caps are generous (far above any real tuning) yet leave the products in
 * m_alloc/m_clalloc comfortably inside 32 bits; those functions keep their own
 * overflow guard as a second line of defence.
 */
int
mb_check_conf(void *dp, LONG newvalue)
{
  if ((u_long *)dp == &mbconf.initial_mbuf_chunks) {
    if (newvalue > 0 && newvalue <= 4096)
      return TRUE;
  }
  else
  if (dp == &mbconf.mbufchunk) {
    if (newvalue >= 32 && newvalue <= 65536)
      return TRUE;
  }
  else
  if (dp == &mbconf.clusterchunk) {
    if (newvalue > 0 && newvalue <= 65536)
      return TRUE;
  }
  else
  if (dp == &mbconf.maxmem) {
    if (newvalue > 32 && newvalue <= 128 * 1024)	/* kilobytes, <=128 MB */
      return TRUE;
  }
  else
  if (dp == &mbconf.mclbytes) {
    /*
     * The cluster size doubles as the stride between clusters in the pool, so
     * an unaligned value yields unaligned cluster bases. Every protocol header
     * is then read through an mtod() cast off that base, which is an Address
     * Error trap on a 68000/68010 the moment the first packet arrives -- and
     * the range check alone is parity-blind, so a plausible typo such as
     * CLUSTERSIZE=1501 gets through. Require 4-byte alignment.
     *
     * Strictly, the 68000/68010 traps only on an ODD address, and the cluster
     * stride is 4 + mclbytes, so an even value alone would keep every base
     * even forever -- `& 1` would be the minimum correct bound. `& 3` is
     * chosen deliberately: it costs nothing real (no shipped or documented
     * CLUSTERSIZE value is excluded), it gives every 32-bit header field
     * long-alignment rather than merely legal alignment, and it avoids
     * resting on the AllocMem() base-alignment assumption holding forever.
     */
    if (newvalue >= MINCLSIZE && newvalue <= 65536 && (newvalue & 3) == 0)
      return TRUE;
  }

  return FALSE;
}

/*
 * mbinit() must be called before any other mbuf related function (exept the
 * mb_check_conf() which is called at configuration time). This
 * allocates memory from the system in one big chunk. This memory will not be
 * freed until AMITCP/IP is shut down.
 */

BOOL
mbinit(void)
{
  spl_t s;

  /*
   * Return success if already initialized
   */
  if (initialized)
    return TRUE;

  s = splimp();
  /*
   * PORT (AmiTCP_NG): latch MBUFCHECK now, and use ONLY the latch from here on.
   *
   * VF_RCONF is weaker than it looks: setvalue() rejects a write only once the
   * global `initialized` is TRUE, and that is not set until the whole stack is
   * up -- long after this point, and after log_init() has already made the
   * ARexx port public. So an ARexx `SET MBUFCHECK ON` can land mid-bring-up.
   * If the macros tested the config variable directly, mbufs freed before that
   * moment (unpoisoned, because checking was off) would be reported as
   * free-list corruption the first time they were reallocated -- a false
   * positive in the one tool that has to be trustworthy. Latching makes the
   * documented promise ("read only at startup") actually true.
   */
  ng_mbufcheck_active = ng_mbufcheck;

  /*
   * Clear the report quotas with the latch. Each counter is capped at
   * NG_MBUF_REPORTS so a storm of faults cannot flood the log, but the cap is
   * lifetime-of-the-binary unless it is reset here -- so a stack that had
   * already used its five reports would come back from a NetShutdown/Online
   * restart silently, and the operator restarting it to reproduce a fault
   * would see nothing at all. The counters are diagnostics, not state: a new
   * stack session gets a new quota.
   */
  ng_mbuf_doublefree   = 0;
  ng_mbuf_useafterfree = 0;
  ng_mbuf_listcorrupt  = 0;

  /*
   * Initialize the list headers to NULL
   */
  mfree = NULL;
  mclfree = NULL;

  /*
   * Preallocate some mbufs and mbuf clusters.
   */
  initialized = 
    (m_alloc(mbconf.initial_mbuf_chunks * mbconf.mbufchunk, M_WAIT)
     && m_clalloc(mbconf.clusterchunk, M_WAIT));

  splx(s);

  if (!initialized) {
    log(LOG_ERR, "mbinit: Failed to allocate memory.");
    mbdeinit();
  }
  return (initialized);
}

/*
 * Free all memory allocated by mbuf subsystem. This must be the last mbuf
 * related function called. (Implying that NO mbuf allocations should be done
 * concurrently with this!)
 *
 * This is new function to AMITCP/IP.
 */
void
mbdeinit(void)
{
  struct memHeader *next;

  /*
   * free all memory chunks
   */
  while (mbufmem) {
    next = mbufmem->next;
    mbstat.m_memused -= mbufmem->size;
    FreeMem(mbufmem, mbufmem->size);
    mbufmem = next;
  }
  initialized = FALSE;
}

/*
 * Allocate memory for mbufs.
 * and place on the mbuf free list.
 *
 * canwait (M_WAIT/M_DONTWAIT) is DELIBERATELY ignored: AllocMem() here never
 * blocks, and this codebase runs the whole stack at task level, so growing the
 * pool on demand -- even for an M_DONTWAIT caller whose free list is empty --
 * is both safe and load-bearing. Refusing to grow on M_DONTWAIT (the strict
 * BSD semantics) would make the hot tcp_output()/ip_output() path (all
 * M_DONTWAIT) return ENOBUFS under bursts, driving tcp_quench() to collapse
 * cwnd -- the exact upstream throughput bug the on-demand growth exists to
 * avoid. The one hard rule this trades on: mbufs MUST NOT be allocated from a
 * real interrupt handler (AllocMem() is illegal there); the SANA RX path
 * upholds this by pre-allocating at task level and only filling at interrupt.
 *
 * MUST be called at splimp!
 */
BOOL
m_alloc(int howmany, int canwait)
{
 /*
  * Note that mbufs must be aligned on MSIZE boundary
  * for dtom to work correctly. This is archieved by allocating size for one 
  * additional mbuf per chunk so that given memory can be aligned properly.
  */ 
  struct mbuf *m;
  struct memHeader *mh;
  ULONG  size;

  /*
   * PORT (AmiTCP_NG) security fix: `howmany` reaches here as the product
   * initial_mbuf_chunks * mbufchunk (see mbinit) or from an internal grow, and
   * the size below is MSIZE * (howmany + 1) + header. Reject any count that
   * would overflow a 32-bit ULONG before we compute -- a wrapped `size` would
   * AllocMem() a small block that the fill loop then overruns. mb_check_conf
   * already bounds the config inputs; this guards internal callers too.
   */
  if (howmany <= 0 ||
      (ULONG)howmany + 1 > (0xFFFFFFFFUL - sizeof(struct memHeader)) / MSIZE) {
    log(LOG_ERR, "m_alloc: refusing bogus mbuf count %ld.", (long)howmany);
    return FALSE;
  }

  /* Compute in ULONG throughout: MSIZE and howmany are int, so the product
   * MSIZE*(howmany+1) is signed-int arithmetic and is technically UB once it
   * exceeds INT_MAX -- the guard above keeps the true value under ULONG_MAX,
   * but relying on twos-complement wraparound is not portable. */
  size = (ULONG)MSIZE * ((ULONG)howmany + 1) + sizeof(struct memHeader);

  /*
   * check if allowed to allocate more
   */
  if (mbstat.m_memused + size > mbconf.maxmem * 1024) {
    log(LOG_ERR, "m_alloc: max amount of memory already used (%ld bytes).",
	mbstat.m_memused);
    return FALSE;
  }

  mh = AllocMem(size, MEMF_PUBLIC);	/* public since used from interrupts */
  if (mh == NULL) {
    log(LOG_ERR, "m_alloc: Cannot allocate memory for mbufs.");
    return FALSE;
  }

  /*
   * initialize the memHeader and link it to the chain of allocated memory 
   * blocks
   */
  mbstat.m_memused += size;		/* add to the total */
  mh->size = size;
  mh->ismbuf = 1;		/* mbuf chunk -- see m_valid() */
  mh->next = mbufmem;
  mbufmem = mh;
  mh++;				/* pass by the memHeader */

  /*
   * update the statistics
   */
  mbstat.m_mbufs += howmany;

  /*
   * Link the new mbufs into the free list.
   *
   * mbufs MUST start on an MSIZE-byte boundary. The reason is dtom(): given any
   * pointer *into* an mbuf's payload, dtom() recovers the mbuf's head by simply
   * rounding the address down to the nearest MSIZE boundary (a mask). That trick
   * -- used all over the stack to get from a data pointer back to its mbuf -- only
   * works if every mbuf is MSIZE-aligned. We allocated one extra mbuf's worth of
   * space (howmany + 1 in m_alloc's size) precisely so we can round the raw
   * AllocMem() block UP to the next boundary here without running off the end.
   */
  m = dtom(((caddr_t)mh) + MSIZE - 1); /* first correctly aligned mbuf */
  while(howmany--) {
    /*
     * PORT (AmiTCP_NG): stamp every new mbuf, ALWAYS -- deliberately not gated
     * on ng_mbufcheck. AllocMem() does not clear, so an mbuf entering the pool
     * unmarked would be reported as free-list corruption the first time it was
     * allocated with checking on. This runs once per pool growth, so the cost
     * does not matter.
     */
    ng_mbuf_poison(m);
    m->m_next = mfree;
    mfree = m++;
  }
  return TRUE;
}  

/*
 * Allocate some number of mbuf clusters
 * and place on cluster free list.
 * canwait is DELIBERATELY ignored -- see m_alloc() above for the rationale
 * (task-level-only, AllocMem() never blocks, on-demand growth is load-bearing
 * for throughput; never allocate mbufs from a real interrupt handler).
 * MUST be called at splimp.
 */
BOOL
m_clalloc(int ncl, int canwait)
{
  struct memHeader *mh;
  struct mcluster *p;
  ULONG  size;
  int    i;		/* must hold ncl (up to clusterchunk == 65536); a short
			 * wraps negative above 32767 and the link loop never ends */

  /*
   * struct mcluster has variable length buffer so its size is not calculated
   * in sizeof(struct mcluster). The size of the buffer is mbconf.mclbytes.
   * Each memory block allocated is prepended by the memHeader, so size
   * must be allocted for it, too.
   */
  /*
   * PORT (AmiTCP_NG) security fix: same overflow reasoning as m_alloc -- a large
   * `ncl` (clusterchunk) times the per-cluster stride must not wrap the 32-bit
   * size. mb_check_conf bounds clusterchunk/mclbytes; this guards internal
   * callers and pins the arithmetic regardless of how we got here.
   */
  if (ncl <= 0 ||
      (ULONG)ncl > (0xFFFFFFFFUL - sizeof(struct memHeader)) /
			(sizeof(struct mcluster) + mbconf.mclbytes)) {
    log(LOG_ERR, "m_clalloc: refusing bogus cluster count %ld.", (long)ncl);
    return FALSE;
  }

  size = ncl * (sizeof(struct mcluster) + mbconf.mclbytes)
    + sizeof(struct memHeader);

  /*
   * check if allowed to allocate more
   */
  if (mbstat.m_memused + size > mbconf.maxmem * 1024) {
    log(LOG_ERR, "m_clalloc: max amount of memory already used (%ld bytes).",
	mbstat.m_memused);
    return FALSE;
  }

  mh = AllocMem(size, MEMF_PUBLIC); /* public since used from interrupts */
  if (mh == NULL) {
    log(LOG_ERR, "m_clalloc: Cannot allocate memory for mbuf clusters");
    return FALSE;
  }
  /*
   * initialize the memHeader and link it to the chain of allocated memory 
   * blocks
   */
  mbstat.m_memused += size;
  mh->size = size;
  mh->ismbuf = 0;		/* cluster chunk, NOT mbuf slots -- see m_valid() */
  mh->next = mbufmem;
  mbufmem = mh;
  mh++;				/* pass by the memHeader */
  /*
   * link clusters to the free list
   */
  for (i = 0, p = (struct mcluster *)mh; 
       i < ncl; 
       i++, p = (struct mcluster*)((char *)(p + 1) + mbconf.mclbytes)) {
    p->mcl.mcl_next = mclfree;
    mclfree = p;
    mbstat.m_clfree++;
  }
  mbstat.m_clusters += ncl;
  
  return TRUE;
}

/*
 * When MGET failes, ask protocols to free space when short of memory,
 * then re-attempt to allocate an mbuf.
 *
 * Allocate more memory for mbufs if there still are no mbufs left 
 *
 * MUST be called at splimp.
 */
struct mbuf *
m_retry(int canwait, int type)
{
  register struct mbuf *m;

  m_reclaim();

  /*
   * Try to allocate more memory if still no free mbufs
   */
  if (!mfree)
    m_alloc(mbconf.mbufchunk, canwait);
  
#define m_retry(i, t)	/*mbstat.m_drops++,*/NULL
  MGET(m, canwait, type);
#undef m_retry
  return (m);
}

void
m_reclaim()
{
	register struct domain *dp;
	register struct protosw *pr;
	spl_t s = splimp();

	for (dp = domains; dp; dp = dp->dom_next)
		for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW; pr++)
			if (pr->pr_drain)
				(*pr->pr_drain)();
	splx(s);
	mbstat.m_drain++;
}

/*
 * Space allocation routines.
 * These are also available as macros
 * for critical paths.
 */
struct mbuf *
m_get(canwait, type)
	int canwait, type;
{
	register struct mbuf *m;

	MGET(m, canwait, type);
	return (m);
}

struct mbuf *
m_gethdr(canwait, type)
	int canwait, type;
{
	register struct mbuf *m;

	MGETHDR(m, canwait, type);
	return (m);
}

struct mbuf *
m_getclr(canwait, type)
	int canwait, type;
{
	register struct mbuf *m;

	MGET(m, canwait, type);
	if (m == 0)
		return (0);
	aligned_bzero_const(mtod(m, caddr_t), MLEN);
	return (m);
}

struct mbuf *
m_free(m)
	struct mbuf *m;
{
	register struct mbuf *n;

	MFREE(m, n);
	return (n);
}

void
m_freem(m)
	register struct mbuf *m;
{
	register struct mbuf *n;

	if (m == NULL)
		return;
	do {
		MFREE(m, n);
	} while ((m = n));
}

/*
 * Mbuffer utility routines.
 */

/*
 * Lesser-used path for M_PREPEND:
 * allocate new mbuf to prepend to chain,
 * copy junk along.
 */
struct mbuf *
m_prepend(m, len, canwait)
	register struct mbuf *m;
	int len, canwait;
{
	struct mbuf *mn;

	/*
	 * PORT (AmiTCP_NG) security fix: bound len. Below, `if (len < MHLEN)
	 * MH_ALIGN(m, len);` then `m->m_len = len;` -- for len >= MHLEN the
	 * alignment step is skipped and m_len is set to the caller's value with
	 * no check against the ~MHLEN bytes the mbuf actually holds, and for a
	 * negative len MH_ALIGN drives m_data far past the end of the 128-byte
	 * mbuf. Either leaves an mbuf whose m_len lies about its own storage,
	 * which m_cat() and m_copydata() then use as a copy length -- an
	 * out-of-bounds read, and a write into the destination for m_cat.
	 * Stock BSD relied on only ever prepending small constant protocol
	 * headers; the mbuf_prepend vector (LVO) makes len caller-controlled.
	 * m_pullup() imposes exactly this ceiling on itself a few functions
	 * below. Failure frees the chain, matching this function's existing
	 * MGET-failure contract.
	 */
	if (len < 0 || len > MHLEN) {
		m_freem(m);
		return (NULL);
	}

	MGET(mn, canwait, m->m_type);
	if (mn == NULL) {
		m_freem(m);
		return (NULL);
	}
	if (m->m_flags & M_PKTHDR) {
		M_COPY_PKTHDR(mn, m);
		m->m_flags &= ~M_PKTHDR;
	}
	mn->m_next = m;
	m = mn;
	if (len < MHLEN)
		MH_ALIGN(m, len);
	m->m_len = len;
	return (m);
}

/*
 * Make a copy of an mbuf chain starting "off0" bytes from the beginning,
 * continuing for "len" bytes.  If len is M_COPYALL, copy to end of mbuf.
 * The wait parameter is a choice of M_WAIT/M_DONTWAIT from caller.
 */
int MCFail;

struct mbuf *
m_copym(m, off0, len, wait)
	register struct mbuf *m;
	int off0, wait;
	register int len;
{
	register struct mbuf *n, **np;
	register int off = off0;
	struct mbuf *top = NULL;
	int copyhdr = 0;

	if (off < 0 || len < 0) {
	  log(LOG_ERR, "m_copym: Bad arguments");
	  goto nospace;
	}
	/*
	 * PORT (AmiTCP_NG): `m` may legitimately be NULL -- an empty socket send
	 * buffer (so_snd.sb_mb == 0) is the ordinary state of an idle connection,
	 * and tcp_output()'s SACK hole-retransmit path takes its length from the
	 * scoreboard rather than clamping to sb_cc, so it can reach here with a
	 * NULL chain. The m_flags test below dereferenced it BEFORE any of the
	 * loops' own `m == 0` checks. With no MMU that read does not fault -- it
	 * quietly returns whatever sits at address 0x12, the exception vectors --
	 * so the bug was invisible rather than absent. Check first; the callers
	 * already handle a NULL return (tcp_output degrades to a header-only
	 * segment).
	 */
	if (m == NULL)
		goto nospace;
	if (off == 0 && m->m_flags & M_PKTHDR)
		copyhdr = 1;
	/*
	 * find first mbuf to copy data from
	 */
	while (off > 0) {
		if (m == 0) {
		  log(LOG_ERR, "m_copym: short mbuf chain");
		  goto nospace;
		}
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	np = &top;
	while (len > 0) {
		if (m == 0) {
			if (len != M_COPYALL) {
			  log(LOG_ERR, "m_copym: short mbuf chain");
			  goto nospace;
			}
			break;
		}
		MGET(n, wait, m->m_type);
		*np = n;
		if (n == 0)
			goto nospace;
		if (copyhdr) {
			M_COPY_PKTHDR(n, m);
			if (len == M_COPYALL)
				n->m_pkthdr.len -= off0;
			else
				n->m_pkthdr.len = len;
			copyhdr = 0;
		}
		n->m_len = MIN(len, m->m_len - off);

		if (m->m_flags & M_EXT) {
			spl_t s;
			n->m_data = m->m_data + off;
			/* Bumping the shared cluster's refcount must be atomic vs
			 * MCLFREE's decrement-and-free (which is splimp-protected);
			 * this is reachable unlocked via the mbuf_copym() library
			 * vector, so protect it the same way. */
			s = splimp();
			m->m_ext.ext_buf->mcl.mcl_refcnt++;
			splx(s);
			n->m_ext = m->m_ext;
			n->m_flags |= M_EXT;
		} else
			bcopy(mtod(m, caddr_t)+off, mtod(n, caddr_t),
			    (unsigned)n->m_len);
		if (len != M_COPYALL)
			len -= n->m_len;
		off = 0;
		m = m->m_next;
		np = &n->m_next;
	}
	if (top == 0)
		MCFail++;
	return (top);
nospace:
	m_freem(top);
	MCFail++;
	return NULL;
}

/*
 * Copy data from an mbuf chain starting "off" bytes from the beginning,
 * continuing for "len" bytes, into the indicated buffer.
 */
void
m_copydata(m, off, len, cp)
	register struct mbuf *m;
	register int off;
	register int len;
	caddr_t cp;
{
	register unsigned count;

	if (off < 0 || len < 0) {
	  log(LOG_ERR, "m_copydata: bad arguments");
	  return;
	}
	while (off > 0) {
		if (m == 0) {
		  log(LOG_ERR, "m_copydata: short mbuf chain to copy from");
		  return;
		}
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0) {
		if (m == 0) {
		  log(LOG_ERR, "m_copydata: short mbuf chain to copy from");
		  return;
		}
		count = MIN(m->m_len - off, len);
		bcopy(mtod(m, caddr_t) + off, cp, count);
		len -= count;
		cp += count;
		off = 0;
		m = m->m_next;
	}
}

/*
 * Concatenate mbuf chain n to m.
 * Both chains must be of the same type (e.g. MT_DATA).
 * Any m_pkthdr is not updated.
 */
void
m_cat(m, n)
	register struct mbuf *m, *n;
{
	while (m->m_next)
		m = m->m_next;
	while (n) {
		if (m->m_flags & M_EXT ||
		    m->m_data + m->m_len + n->m_len >= &m->m_dat[MLEN]) {
			/* just join the two chains */
			m->m_next = n;
			return;
		}
		/* splat the data from one into the other */
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t) + m->m_len,
		    (u_int)n->m_len);
		m->m_len += n->m_len;
		n = m_free(n);
	}
}

void
m_adj(struct mbuf *mp, int req_len)
{
	register int len = req_len;
	register struct mbuf *m;
	register int count;

	if ((m = mp) == NULL)
		return;
	if (len >= 0) {
		/*
		 * Trim from head.
		 */
		while (m != NULL && len > 0) {
			if (m->m_len <= len) {
				len -= m->m_len;
				m->m_len = 0;
				m = m->m_next;
			} else {
				m->m_len -= len;
				m->m_data += len;
				len = 0;
			}
		}
		m = mp;
		if (mp->m_flags & M_PKTHDR) {
			m->m_pkthdr.len -= (req_len - len);
			/*
			 * PORT (AmiTCP_NG) fix: floor it. req_len is caller
			 * controlled through the mbuf_adj vector and is not
			 * bounded by the chain's real length, so trimming more
			 * than the chain holds drove pkthdr.len negative. The
			 * per-mbuf m_len fields above are already clamped to 0
			 * the same way; a negative total is a corrupted
			 * invariant that later code reading it as a size would
			 * see as enormous.
			 */
			if (m->m_pkthdr.len < 0)
				m->m_pkthdr.len = 0;
		}
	} else {
		/*
		 * Trim from tail.  Scan the mbuf chain,
		 * calculating its length and finding the last mbuf.
		 * If the adjustment only affects this mbuf, then just
		 * adjust and return.  Otherwise, rescan and truncate
		 * after the remaining size.
		 */
		len = -len;
		count = 0;
		for (;;) {
			count += m->m_len;
			if (m->m_next == (struct mbuf *)0)
				break;
			m = m->m_next;
		}
		if (m->m_len >= len) {
			m->m_len -= len;
			if ((mp = m)->m_flags & M_PKTHDR)
				m->m_pkthdr.len -= len;
			return;
		}
		count -= len;
		if (count < 0)
			count = 0;
		/*
		 * Correct length for chain is "count".
		 * Find the mbuf with last data, adjust its length,
		 * and toss data from remaining mbufs on chain.
		 */
		m = mp;
		if (m->m_flags & M_PKTHDR)
			m->m_pkthdr.len = count;
		for (; m; m = m->m_next) {
			if (m->m_len >= count) {
				m->m_len = count;
				break;
			}
			count -= m->m_len;
		}
		while ((m = m->m_next))
			m->m_len = 0;
	}
}

/*
 * Rearrange an mbuf chain so that len bytes from the beginning are
 * contiguous and in the data area of an mbuf (so that mtod and dtom
 * will work for a structure of size len). Note that resulting
 * structure is assumed to get properly aligned. This will happen only if
 * there is no odd-length data before the structure. Fortunately all
 * headers are before any data in the packet and are of even length.
 * Returns the resulting mbuf chain on success, frees it and returns
 * null on failure. If there is room, it will add up to max_protohdr-len
 * extra bytes to the contiguous region in an attempt to avoid being
 * called next time.
 */
int MPFail;

struct mbuf *
m_pullup(n, len)
	register struct mbuf *n;
	int len;
{
	register struct mbuf *m;
	register int count;
	int space;

	/*
	 * If first mbuf has no cluster, and has room for len bytes
	 * without shifting current data, pullup into it,
	 * otherwise allocate a new mbuf to prepend to the chain.
	 */
	if ((n->m_flags & M_EXT) == 0 &&
	    n->m_data + len < &n->m_dat[MLEN] && n->m_next) {
		if (n->m_len >= len)
			return (n);
		m = n;			/* pullup to */
		n = n->m_next;		/* pullup from */
		len -= m->m_len; 	/* pullup length */
	} else {
		if (len > MHLEN)
			goto bad;
		MGET(m, M_DONTWAIT, n->m_type);
		if (m == 0)
			goto bad;
		m->m_len = 0;
		if (n->m_flags & M_PKTHDR) {
			M_COPY_PKTHDR(m, n);
			/*
			 * PORT (AmiTCP_NG): demote the old head, and strip the flags that
			 * only mean anything ON a head. M_CSUM_DONE says "this packet's
			 * pkthdr.csum is valid" -- on a mbuf that is no longer a head, that
			 * claim refers to a pkthdr nobody will read, so leaving the bit set
			 * is meaningless at best. Nothing today looks at flags on a non-head
			 * link, so this is inert; it is cleared because a bit that is only
			 * harmless by convention is exactly the kind that stops being
			 * harmless later.
			 */
			n->m_flags &= ~(M_PKTHDR | M_CSUM_DONE);
		}
	}
	space = &m->m_dat[MLEN] - (m->m_data + m->m_len);
	do {
		count = min(min(max(len, max_protohdr), space), n->m_len);
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t) + m->m_len,
		  (unsigned)count);
		len -= count;
		m->m_len += count;
		n->m_len -= count;
		space -= count;
		if (n->m_len)
			n->m_data += count;
		else
			n = m_free(n);
	} while (len > 0 && n);
	if (len > 0) {
		(void) m_free(m);
		goto bad;
	}
	m->m_next = n;
	return (m);
bad:
	m_freem(n);
	MPFail++;
	return (0);
}

#if 0				/* not needed (yet), DO NOT DELETE! */
/*
 * Allocate a "funny" mbuf, that is, one whose data is owned by someone else.
 */
struct mbuf *
mclgetx(fun, arg, addr, len, wait)
        void (*fun)();
        int arg, len, wait;
        caddr_t addr;
{
        register struct mbuf *m;

        MGETHDR(m, wait, MT_DATA);
        if (m == 0)
                return (0);
        m->m_data = addr ;
        m->m_len = len;
        m->m_ext.ext_free = fun;
        m->m_ext.ext_size = len;
        m->m_ext.ext_buf = (caddr_t)arg;
        m->m_flags |= M_EXT;

        return (m);
}

void mcl_free_routine(buf, size)
    char *buf;
    int size;
{
}
#endif /* 0 */
