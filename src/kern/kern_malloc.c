RCS_ID_C="$Id: kern_malloc.c,v 1.9 1994/03/26 09:36:29 too Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log: kern_malloc.c,v $
 * Revision 1.9  1994/03/26  09:36:29  too
 * Moved bsd_malloc(), bsd_free() inlines here as normal functions.
 * Added bsd_realloc()
 *
 * Revision 1.8  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.7  1993/05/17  01:07:47  ppessi
 * Changed RCS version.
 *
 * Revision 1.6  1993/05/04  12:48:38  jraja
 * Added tuning for SASC malloc.
 *
 * Revision 1.5  93/04/06  15:15:50  15:15:50  jraja (Jarno Tapio Rajahalme)
 * Changed spl function return value storage to spl_t,
 * changed bcopys and bzeros to aligned and/or const when possible,
 * added inclusion of conf.h to every .c file.
 * 
 * Revision 1.4  93/03/11  19:41:03  19:41:03  jraja (Jarno Tapio Rajahalme)
 * Changed mallocSemaphore to malloc_semaphore.
 * 
 * Revision 1.3  93/03/05  21:11:15  21:11:15  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.2  93/02/24  12:54:36  12:54:36  jraja (Jarno Tapio Rajahalme)
 * Changed init to remember if initialized.
 * 
 * Revision 1.1  93/02/04  18:29:36  18:29:36  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 */

/*
 * kern_malloc.c --- the kernel memory allocator, on top of Exec AllocMem.
 *
 * BSD kernel code allocates with malloc(size, type, flags) / free(addr, type) --
 * NOT the C library malloc; a typed kernel allocator with per-type statistics.
 * This file provides that interface (bsd_malloc/bsd_free) for the stack, backed by
 * Exec's AllocMem/FreeMem, with a small header per block to remember its size and
 * type. The `type` argument (M_MBUF, M_PCB, M_RTABLE, ...) is what mb_read_stats
 * and netstat report on. Used everywhere except mbufs, which have their own
 * interrupt-safe pool (kern/uipc_mbuf.c) precisely because AllocMem is unavailable
 * at interrupt time.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/malloc.h>

#include <kern/amiga_includes.h>

#if __SASC
/* 
 * Change the minimum chuck size of the SASC memory allocator.
 * The default value is 16.384 bytes (SASC6.2), but smaller value
 * is more efficient, since it takes less time to search the free
 * memory pool when it is smaller.
 */
long _MSTEP = 4096;

/*
 * Set the memory type allocated by malloc() to public, since the memory
 * is used by many tasks (including all library users)
 */
unsigned long _MemType = MEMF_PUBLIC;

#endif /* __SASC */

struct SignalSemaphore malloc_semaphore = { 0 };
static BOOL initialized = FALSE;

BOOL
malloc_init(void)
{
  if (!initialized) {
    /*
     * Initialize malloc_semaphore for use.
     * Do not call bsd_malloc() or bsd_free() before this!
     */
    InitSemaphore(&malloc_semaphore);
    initialized = TRUE;
  }
  return TRUE;
}

/*
 * PORT (AmiTCP_NG): back bsd_malloc()/bsd_free() with Exec's AllocVec()/FreeVec()
 * instead of libnix malloc()/free(). This matches this file's own description
 * ("backed by Exec's AllocMem/FreeMem") and, crucially, removes the dependency on
 * libnix's C runtime -- which is set up by crt0 in the `amitcp` PROGRAM build but
 * NOT in the self-starting LIBS:bsdsocket.library build (no crt0). AllocVec()
 * records the block size itself (so FreeVec() needs no size) and is task-safe, so
 * the old malloc_semaphore is no longer required.
 */
/*
 * NB: sys/malloc.h macro-strips callers' bsd_malloc(size,type,flags) down to the
 * real 1-arg bsd_malloc(size) (and bsd_free(addr,type) -> bsd_free(addr),
 * bsd_realloc(mem,size,type,flags) -> bsd_realloc(mem,size)). We must #undef those
 * macros here so these definitions declare the real, stripped-signature functions.
 */
#undef bsd_malloc
#undef bsd_free
#undef bsd_realloc

void *
bsd_malloc(unsigned long size)
{
  return AllocVec((ULONG)size, MEMF_PUBLIC);
}

void
bsd_free(void *addr)
{
  if (addr)
    FreeVec(addr);
}

/*
 * bsd_realloc() cannot be used for reallocating
 * last freed block for obvious reasons
 */

/*
 * PORT (AmiTCP_NG): bsd_realloc() is not used anywhere in the stack. Provide an
 * AllocVec-based stub so no libnix malloc-family symbol is referenced (see the
 * note on bsd_malloc above). It does NOT preserve the old contents -- acceptable
 * only because it is never called; revisit if a caller ever appears.
 */
void *
bsd_realloc(void * mem, unsigned long size)
{
  (void)mem;
  return AllocVec((ULONG)size, MEMF_PUBLIC);
}
