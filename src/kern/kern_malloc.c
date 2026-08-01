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
 * PORT (AmiTCP_NG): a correct realloc on top of AllocVec, which DOES preserve the
 * old contents. AllocVec records the block size in the ULONG immediately before the
 * returned pointer (that is what FreeVec reads to free it), so the old payload size
 * is that recorded value minus the header ULONG. We copy the smaller of the old
 * payload and the new size into a fresh block, then free the old one. Callers:
 * setup_accesscontroltable() (kern/accesscontrol.h) shrinks the access-control table
 * with this, and RELIES on the contents surviving -- an earlier version that returned
 * a fresh uninitialised block silently corrupted that table.
 */
void *
bsd_realloc(void * mem, unsigned long size)
{
  void *nblk;

  if (mem == NULL)
    return AllocVec((ULONG)size, MEMF_PUBLIC);	/* realloc(NULL, n) == malloc(n) */
  if (size == 0) {
    FreeVec(mem);				/* realloc(p, 0) == free(p)      */
    return NULL;
  }
  nblk = AllocVec((ULONG)size, MEMF_PUBLIC);
  if (nblk != NULL) {
    ULONG oldpayload = ((ULONG *)mem)[-1] - (ULONG)sizeof(ULONG);
    CopyMem(mem, nblk, (oldpayload < size) ? oldpayload : (ULONG)size);
    FreeVec(mem);				/* on failure the old block is left intact */
  }
  return nblk;
}
