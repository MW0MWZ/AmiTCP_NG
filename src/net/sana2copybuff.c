/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: sana2copybuff.c,v 1.15 1993/12/20 18:06:41 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * sana2copybuff.c - Buffer Management Routines for Sana-II Interfaces
 *
 * Last modified: Sun Nov  7 01:37:49 1993 ppessi
 *
 * HISTORY
 * $Log: sana2copybuff.c,v $
 * Revision 1.15  1993/12/20  18:06:41  jraja
 * Added more robust version of ioip_alloc_mbuf().
 * Added more detail to "mbuf chain short" message.
 *
 * Revision 1.13  1993/11/06  23:39:15  ppessi
 * Added more information to "mbuf chain short" error message.
 *
 * Revision 1.12  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.11  1993/05/16  21:09:43  ppessi
 * RCS version changed.
 *
 * Revision 1.10  1993/05/05  16:10:38  puhuri
 * Fixed cluster allocation code.
 *
 * Revision 1.9  93/04/26  11:53:11  11:53:11  too (Tomi Ollila)
 * Changed include paths of amiga_api.h, amiga_libcallentry.h and amiga_raf.h
 * from kern to api
 * 
 * Revision 1.8  93/04/24  22:46:13  22:46:13  jraja (Jarno Tapio Rajahalme)
 * Removed Define for USECLUSTERS
 * 
 * Revision 1.7  93/04/13  22:22:56  22:22:56  jraja (Jarno Tapio Rajahalme)
 * Changed return from buffer allocation function back to recent form.
 * 
 * Revision 1.6  93/04/12  09:20:40  09:20:40  jraja (Jarno Tapio Rajahalme)
 * Changed reserved mbuf chain so that all mbufs after the header have 
 * clusters.
 * 
 * Revision 1.5  93/04/05  17:46:26  17:46:26  jraja (Jarno Tapio Rajahalme)
 * Changed spl storage variables to spl_t.
 * Changed every .c file to use conf.h.
 * 
 * Revision 1.4  93/03/20  07:11:55  07:11:55  ppessi (Pekka Pessi)
 * Fixed mbuf allocating for headers
 * 
 * Revision 1.3  93/03/05  19:51:16  19:51:16  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.2  93/02/28  22:21:57  22:21:57  ppessi (Pekka Pessi)
 * Made to compile; used RAFn macros.
 * 
 * Revision 1.1  93/02/25  14:34:29  14:34:29  ppessi (Pekka Pessi)
 * Initial revision
 */

/*
 * sana2copybuff.c --- the mbuf<->driver payload bridge (SANA-II callbacks).
 *
 * A SANA-II driver knows nothing about mbufs. When it needs to move packet
 * payload, it calls back into US through two hook functions we register on each
 * IORequest -- this file provides them. They are the exact seam where "the Amiga
 * driver's buffer" meets "the BSD stack's mbuf chain". docs/ARCHITECTURE.md sect 8.
 *
 *   m_copy_to_mbuf   (the S2_CopyToBuff hook)  -- RECEIVE. The driver calls this
 *                    with a just-arrived frame; we copy it INTO a fresh mbuf chain
 *                    (allocated by ioip_alloc_mbuf from the interrupt-safe pool).
 *                    Runs at DEVICE INTERRUPT time: no AllocMem, hence the pool.
 *   m_copy_from_mbuf (the S2_CopyFromBuff hook) -- TRANSMIT. The driver calls this
 *                    to pull payload OUT of our mbuf chain into its transmit buffer.
 *
 * Both are declared with register-argument calling conventions (RAF3/SAVEDS) and
 * the __saveds attribute because the DRIVER calls them from its own context, not
 * ours -- see api/apicalls_gnuc.h for the same pattern. Read ioip_alloc_mbuf()
 * first (it sizes and grabs the receive mbuf chain), then the two hooks.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>
#include <sys/synch.h>

#include <net/if.h>

#if INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#include <net/if_sana.h>
#include <api/amiga_raf.h>
/* Payload copies use ng_bcopy_dev(), which is now just ng_bcopy(). The two were
 * separate only to keep MOVE16 away from driver-owned buffers, whose caching and bus
 * behaviour we do not know; ng_bcopy() no longer issues MOVE16 or any other burst or
 * cache-line operation -- plain move.b/move.w/movem.l -- so that objection is spent. */

/*
 * allocate mbufs for the size MTU at free_chain for read request
 */
#if 0 /* old one */
BOOL
ioip_alloc_mbuf(struct IOIPReq *s2rp, ULONG MTU)
{
  register struct mbuf *m, *n;
  register int len = 0;

  n = s2rp->ioip_reserved;

  /* Check for packet header */
  if (n && (n->m_flags & M_PKTHDR)) {
    /* There is already a full packet  */
    return TRUE;
  }

  /* Prepend by a packet header */
  MGETHDR(m, M_NOWAIT, MT_HEADER);
  if (m) { 
    m->m_len = len = MHLEN;
    s2rp->ioip_reserved = m;
    m->m_next = n;
    
    /* Find the end of the free chain */ /* ASSUME THAT THESE HAVE CLUSTERS */
    while (n = m->m_next) {
      len += n->m_len; m = n;
    } 
    
    /*
     * add new (cluster)mbufs to get the desired size
     */
    while (len < MTU) {
      MGET(n, M_NOWAIT, MT_DATA);
      if (n != NULL) {
	MCLGET(n, M_NOWAIT);
	if (n->m_ext.ext_buf != NULL) {
	  n->m_len = n->m_ext.ext_size;
	  len += n->m_ext.ext_size;
	}
	else {
	  m_free(n);
	  break;
	}
	m = m->m_next = n;
      }
      else
	break;
    }
    
    s2rp->ioip_reserved->m_pkthdr.len = len;
  }
  if (len < MTU) { 
    m_freem(s2rp->ioip_reserved);
    s2rp->ioip_reserved = NULL;
    return FALSE;
  }

  return TRUE;
}
#else
BOOL
ioip_alloc_mbuf(struct IOIPReq *s2rp, ULONG MTU)
{
  register struct mbuf *m, *n;
  register int len;

  /*
   * s2rp->ioip_reserved is either NULL, or an mbuf chain, possibly having
   * packet header in the first one.
   */
  n = s2rp->ioip_reserved;

  /* Check for packet header */
  if (n && (n->m_flags & M_PKTHDR)) {
    /*
     * chain already has the packet header
     */
    m = n;
    len = m->m_len;
  }
  else {
    /* Prepend by a packet header */
    MGETHDR(m, M_NOWAIT, MT_HEADER);
    if (m) { 
      m->m_len = len = MHLEN;
      s2rp->ioip_reserved = m;
      m->m_next = n;
    }
    else
      goto fail;
  }
  /*
   * Now m points to the start of the mbuf chain. The first mbuf has
   * a packet header. 
   */

  /* Find the end of the free chain */ /* ASSUME THAT THESE HAVE CLUSTERS */
  while ((n = m->m_next)) {
    len += n->m_len; m = n;
  }
    
  /*
   * Now len has the total length of the mbuf chain, add new 
   * (cluster)mbufs to get the desired size (MTU).
   */
  while (len < MTU) {
    MGET(n, M_NOWAIT, MT_DATA);
    if (n != NULL) {
      MCLGET(n, M_NOWAIT);
      if (n->m_ext.ext_buf != NULL) {
	len += n->m_len = n->m_ext.ext_size;
      }
      else {
	m_free(n);
        goto fail;
      }
      m = m->m_next = n;
    }
    else
      goto fail;
  }
  s2rp->ioip_reserved->m_pkthdr.len = len;
  return TRUE;

 fail:
  m_freem(s2rp->ioip_reserved);
  s2rp->ioip_reserved = NULL;
  return FALSE;
}
#endif

/*
 * Copy data from an mbuf chain starting from the beginning,
 * continuing for "n" bytes, into the indicated continuous buffer.
 *
 * NOTE: this WILL be called from INTERRUPTS, so compile with stack checking
 *       disabled and use __saveds if near data is needed.
 */
/*
 * Shared implementation. The register-argument hooks below are thin wrappers
 * around these, because the gcc RAF3 macro declares a hook as taking NO C
 * arguments (it reads a0/a1/d0 directly), so one hook cannot simply call
 * another. Extracting the body is what lets the 32-bit-aligned variants exist
 * without a second copy of the loop -- and a second copy of a loop that writes
 * wire data into mbufs at interrupt time is exactly what we do not want.
 */
static BOOL copy_from_mbuf_body(to, from, n)
     BYTE            *to;
     struct IOIPReq  *from;
     ULONG            n;
{
  register struct mbuf *m = from->ioip_packet;
  register unsigned count;

  /*
   * ioip_if NULL means sana_unrun() gave up on a driver that would not return
   * this request on teardown, neutralised it, and sana_remove_interface() then
   * FREED that softc. The driver still owns the request and can still call this
   * hook on it -- that is precisely the case the neutralising exists for -- so
   * touching ioip_if here writes through freed memory. On a no-MMU 68k that is
   * silent: no trap at the fault site, corruption surfacing later somewhere
   * unrelated. Refuse the transfer instead; the request is abandoned anyway.
   */
  if (from->ioip_if == NULL)
    return FALSE;

  from->ioip_if->ss_copyout++;		/* SANA2CopyStats: byte CopyFromBuff (TX) */

  while (n > 0) {
    /*
     * PORT (AmiTCP_NG) security fix: this guard MUST be unconditional. `n` is
     * the length the driver asked us to copy; if it exceeds the mbuf chain we
     * hold, m walks off the end to NULL and the bcopy below dereferences it --
     * on this MMU-less 68000 that reads through address 0, i.e. the Exec vector
     * table. The original compiled the only check out unless DIAGNOSTIC was set.
     * Keep the detailed log under DIAGNOSTIC, but always bail.
     */
    if (m == 0) {
#if DIAGNOSTIC
      log(LOG_ERR, "m_copy_from_buff: mbuf chain short");
#endif
      return FALSE;
    }
    count = MIN(m->m_len, n);
    ng_bcopy_dev(mtod(m, caddr_t), to, count);
    n -= count;
    to += count;
    m = m->m_next;
  }
  return TRUE;
}


/*
 * Copy data from an continuous buffer 'from' to preallocated mbuf chain
 * starting from the beginning, continuing for "n" bytes.
 * Mbufs in the preallocated chain must have their m_len field set to maximum
 * amount of data that they can have.
 * 
 * NOTE: this WILL be called from INTERRUPTS, so compile with stack checking
 *       disabled and use __saveds if near data is needed.
 */
static BOOL copy_to_mbuf_body(to, from, n)
     struct IOIPReq  *to;
     BYTE            *from;
     ULONG            n;
{
  register struct mbuf *f, *m = to->ioip_reserved;
  unsigned totlen = n;

  /* Same abandoned-request hazard as copy_from_mbuf_body -- see the note there. */
  if (to->ioip_if == NULL)
    return FALSE;

  to->ioip_if->ss_copyin++;		/* SANA2CopyStats: byte CopyToBuff (RX) */

  /* The driver may take a DMA pointer from us and then copy anyway -- the spec
   * lets it decide late. Only the copy hook knows a copy really happened, so it
   * is the one that must withdraw the promise. */
  to->ioip_dmaed = 0;

  /*
   * Reject a NULL preallocated chain or a zero-length frame up front. Without
   * this, a NULL m (no reserved chain) would be dereferenced by the DIAGNOSTIC
   * check just below and by the post-loop `f = m->m_next` -- a wild read/write
   * near address 0 on this no-MMU 68k; and a zero-length frame (n == 0) skips the
   * copy loop entirely and would then be promoted upstream as a bogus 0-length
   * packet. Runs at device-interrupt time, so keep it branch-cheap.
   */
  if (m == NULL || n == 0)
    return FALSE;

#if DIAGNOSTIC
  if (!(m->m_flags & M_PKTHDR)) {
    log(LOG_ERR, "m_copy_to_buff: mbuf chain has no header");
    return FALSE;
  }
#endif


  while (n > 0) {
    /*
     * PORT (AmiTCP_NG) security fix: as in m_copy_from_mbuf, this guard MUST be
     * unconditional. A driver over-reporting the frame length walks m off the
     * end of the preallocated chain to NULL; the bcopy below would then write
     * wire data through address 0 (the Exec vectors on this no-MMU 68000). Keep
     * the diagnostic detail under DIAGNOSTIC, but always refuse to deref NULL.
     */
    if (m == 0) {
#if DIAGNOSTIC
      log(LOG_ERR, "m_copy_to_buff: mbuf chain short, "
	  "packet len =%lu, reserved =%lu, "
	  "wiretype =%lu, mtu =%lu",
	  totlen, to->ioip_reserved->m_pkthdr.len,
	  to->ioip_s2.ios2_PacketType,
	  (ULONG)to->ioip_if->ss_if.if_mtu);
#endif
      return FALSE;
    }
    /* Truncate to what really exists: a stale full-capacity m_len would read past
     * the end of the driver's buffer. */
    if (n < m->m_len)
      m->m_len = n;
    ng_bcopy_dev(from, mtod(m, caddr_t), m->m_len);
    from += m->m_len;
    n -= m->m_len;
    /*
     * DELIBERATELY CONDITIONAL. `m` must be left pointing at the LAST MBUF ACTUALLY
     * WRITTEN, because the chain is split at `m->m_next` immediately below. Advancing
     * unconditionally here would hand the packet one extra, never-written mbuf whose
     * m_len is still at full capacity -- an inconsistent chain, where anything walking
     * m_next to NULL reads uninitialised cluster memory.
     */
    if (n > 0)
      m = m->m_next;
  }

  /*
   * move the packet to the field 'ioip_packet',
   * set total length of the packet and terminate it.
   */
  f = m->m_next;		/* first free mbuf */
  m->m_next = NULL;		/* terminate the chain */

  to->ioip_packet = to->ioip_reserved;
  to->ioip_packet->m_pkthdr.len = totlen; /* set packet length */
  to->ioip_reserved = f;		/* leftover mbufs */


  /*
   * More mbuf flags and interface pointer must be set later
   */
  return TRUE;
}

/*
 * The register-argument hooks the driver actually calls.
 *
 * S2_CopyToBuff / S2_CopyFromBuff are MANDATORY -- every SANA-II driver has
 * them and the spec requires us to supply them, so they are the floor that
 * makes any driver work.
 *
 * The *32 variants are SANA-II R4. The "32" means the DRIVER guarantees its
 * buffer is 32-bit aligned; we still do the copy, so there is no correctness
 * difference for us and the same body serves both. Advertising them is safe by
 * construction: a driver that predates R4 ignores tags it does not recognise
 * and keeps calling the mandatory pair, so this cannot break existing hardware.
 *
 * We advertise them mainly to FIND OUT. Nothing in this stack has ever offered a
 * driver anything beyond the original two functions, so we genuinely do not know
 * whether any real driver would use more -- and that question gates whether the
 * DMA variants (which return a buffer address instead of copying, and would
 * remove the per-frame copy entirely) are worth building. Separate counters mean
 * every machine running this reports what its driver actually chose, rather than
 * us guessing from one person's hardware.
 */
static SAVEDS BOOL RAF3(m_copy_from_mbuf,
		 BYTE*,          to,   a0,
		 struct IOIPReq*,from, a1,
		 ULONG,          n,    d0)
#if 0
{
#endif
  return copy_from_mbuf_body(to, from, n);
}

static SAVEDS BOOL RAF3(m_copy_to_mbuf,
		 struct IOIPReq*,to,   a0,
		 BYTE*,          from, a1,
		 ULONG,          n,    d0)
#if 0
{
#endif
  return copy_to_mbuf_body(to, from, n);
}

static SAVEDS BOOL RAF3(m_copy_from_mbuf32,
		 BYTE*,          to,   a0,
		 struct IOIPReq*,from, a1,
		 ULONG,          n,    d0)
#if 0
{
#endif
  /* Guard BEFORE the counter: the body checks too, but this touches ioip_if
   * first, and on an abandoned request that pointer is into freed memory. */
  if (from->ioip_if == NULL)
    return FALSE;
  from->ioip_if->ss_copyout32++;	/* how often the driver chose the R4 variant */
  return copy_from_mbuf_body(to, from, n);
}

static SAVEDS BOOL RAF3(m_copy_to_mbuf32,
		 struct IOIPReq*,to,   a0,
		 BYTE*,          from, a1,
		 ULONG,          n,    d0)
#if 0
{
#endif
  if (to->ioip_if == NULL)		/* see m_copy_from_mbuf32 */
    return FALSE;
  to->ioip_if->ss_copyin32++;
  return copy_to_mbuf_body(to, from, n);
}

/* ON by default (SANADMA=NO turns it off). Offering these tags is safe by
 * construction: a driver that does not implement DMA ignores tags it does not
 * recognise and keeps calling the copy hooks, which is what every driver did
 * before this existed. Gating it off by default would mean nobody ever got the
 * benefit, because nobody would know to ask for it.
 *
 * The setting remains so a machine that misbehaves has a way out without
 * reinstalling. */
LONG ng_sana_dma = 1;

/*
 * SANA-II DMA buffer management: S2_DMACopyToBuff32 / S2_DMACopyFromBuff32.
 *
 * Instead of calling us to copy the frame, a capable driver asks for an address
 * and moves the bytes itself. That removes a whole CPU pass over every received
 * packet -- the copy this file otherwise performs.
 *
 * THE CONTRACT (SANA-II R3). The driver calls us with the ios2_Data cookie in A0.
 * We return either NULL ("the driver may not use DMA for this buffer", and it
 * falls back to the copy hooks), or the address of a buffer that is in contiguous
 * memory, correctly readable/writable, aligned on a 32-bit boundary, and whose
 * size is a multiple of 32 bits and at least ios2_DataLength. Returning NULL is
 * always legal, so every check below simply declines rather than failing.
 *
 * WHAT WE HAND BACK, AND WHY IT IS THE SECOND MBUF. ioip_alloc_mbuf() builds the
 * receive chain as a small MGETHDR mbuf followed by 2048-byte clusters. That shape
 * is deliberate and is NOT changed here: it is what keeps the copy working on
 * aligned pointers. So DMA is offered the CLUSTER -- one
 * contiguous, 4-aligned, 2048-byte region -- and the little header mbuf is simply
 * unused for a DMA'd frame.
 *
 * A pleasant consequence: ARP reads never get a cluster (ARP_MTU fits inside
 * MHLEN), so m_next is NULL for them, we decline, and they keep copying exactly as
 * before. No extra cluster is consumed anywhere.
 *
 * ALIGNMENT. A normal (non-RAW) CMD_READ delivers "the Data Link Layer packet data
 * only" -- the driver strips the Ethernet header, which comes back separately in
 * ios2_SrcAddr/ios2_PacketType. So byte 0 of this buffer is the IP header, and a
 * 4-aligned buffer means a 4-aligned IP header. We never set SANA2IOB_RAW on a
 * read.
 */
static SAVEDS APTR RAF3(m_dma_to_mbuf32,
		 struct IOIPReq*, to,      a0,
		 BYTE*,           unused1, a1,
		 ULONG,           unused2, d0)
#if 0
{
#endif
  register struct mbuf *m;
  register ULONG len;

  (void)unused1; (void)unused2;

  if (!ng_sana_dma || to == NULL || to->ioip_if == NULL)
    return NULL;

  /* ioip_if NULL means sana_unrun() gave up waiting for a driver that would not
   * return this request and neutralised it -- AND FREED THAT SOFTC (see the
   * comment at if_sana.c's sana_poll skip). A driver still holding such a request
   * can still call us on it, which is exactly this case, so touching ioip_if here
   * would be a use-after-free on a no-MMU machine. Check before ANY counter. */
  to->ioip_if->ss_dmaask++;		/* the driver ASKED -- count before any test */

  m = to->ioip_reserved;
  if (m == NULL || (m = m->m_next) == NULL ||	/* the cluster, not the header mbuf */
      (m->m_flags & M_EXT) == 0) {		/* must be cluster-backed to be contiguous */
    to->ioip_if->ss_dmano_buf++;
    return NULL;
  }

  len = (ULONG)to->ioip_s2.ios2_DataLength;
  if (len == 0 || len > (ULONG)m->m_ext.ext_size) {
    /* Expected to dominate if a driver asks BEFORE the frame has arrived, when
     * it cannot yet know the length. That is a legitimate thing for it to do and
     * we simply decline, but it must be visible rather than looking like silence. */
    to->ioip_if->ss_dmano_len++;
    return NULL;
  }

  if (((ULONG)mtod(m, caddr_t) & 3UL) != 0) {	/* spec: 32-bit boundary */
    to->ioip_if->ss_dmano_align++;
    return NULL;
  }

  /*
   * Push any dirty lines we still hold for this range out to RAM NOW, before the
   * device writes it. The cluster is recycled memory and nothing flushes on free,
   * so the CPU can still hold dirty lines from its previous life. Doing it here is
   * what makes the post-DMA CacheClearE() in sana_read() safe: on a copyback cache
   * "clear" is push-THEN-invalidate, so a dirty line surviving into the completion
   * would be written out ON TOP of the frame the device just delivered. Flushing
   * first leaves nothing to push. No-op below a 68040.
   */
  CacheClearE((APTR)mtod(m, caddr_t), len, CACRF_ClearD);

  to->ioip_dmaed = 1;			/* completion must build the packet itself */
  to->ioip_if->ss_dmato32++;
  return (APTR)mtod(m, caddr_t);
}

/*
 * Transmit. The outgoing chain nearly always starts with a header mbuf, so a
 * single contiguous run covering the whole packet is the exception rather than the
 * rule and this normally declines. Implemented honestly with that test rather than
 * pretended.
 */
static SAVEDS APTR RAF3(m_dma_from_mbuf32,
		 struct IOIPReq*, from,    a0,
		 BYTE*,           unused1, a1,
		 ULONG,           unused2, d0)
#if 0
{
#endif
  register struct mbuf *m;
  register ULONG len;

  (void)unused1; (void)unused2;

  if (!ng_sana_dma || from == NULL || from->ioip_if == NULL)
    return NULL;			/* see the note in m_dma_to_mbuf32 */

  from->ioip_if->ss_dmaaskout++;	/* asked, whatever we answer */

  m = from->ioip_packet;
  if (m == NULL || m->m_next != NULL)		/* must be ONE mbuf: contiguous */
    return NULL;

  len = (ULONG)from->ioip_s2.ios2_DataLength;
  if (len == 0 || len > (ULONG)m->m_len)
    return NULL;
  if (((ULONG)mtod(m, caddr_t) & 3UL) != 0)
    return NULL;

  /*
   * The device is about to READ this buffer. Everything in it was written by the
   * CPU (ip_output, tcp_output, the checksum routines), so on a 68040/060 copyback
   * cache those writes may still be sitting in the cache with RAM holding whatever
   * the mbuf last contained. Without this the card transmits stale bytes -- no
   * exception, no failed test, just a wrong frame on the wire. Push them out.
   */
  CacheClearE((APTR)mtod(m, caddr_t), len, CACRF_ClearD);

  from->ioip_if->ss_dmafrom32++;
  return (APTR)mtod(m, caddr_t);
}

/* Two lists. The driver reads whichever it was handed at OpenDevice() time and
 * keeps it, so an interface already up can never have its hooks changed under it. */
struct TagItem buffermanagement[5] = {
    { S2_CopyToBuff,     (ULONG)m_copy_to_mbuf },		/* mandatory */
    { S2_CopyFromBuff,   (ULONG)m_copy_from_mbuf },		/* mandatory */
    { S2_CopyToBuff32,   (ULONG)m_copy_to_mbuf32 },		/* R4, optional */
    { S2_CopyFromBuff32, (ULONG)m_copy_from_mbuf32 },	/* R4, optional */
    { TAG_END, }
};

struct TagItem buffermanagement_dma[7] = {
    { S2_CopyToBuff,        (ULONG)m_copy_to_mbuf },		/* mandatory */
    { S2_CopyFromBuff,      (ULONG)m_copy_from_mbuf },		/* mandatory */
    { S2_CopyToBuff32,      (ULONG)m_copy_to_mbuf32 },		/* R4, optional */
    { S2_CopyFromBuff32,    (ULONG)m_copy_from_mbuf32 },	/* R4, optional */
    { S2_DMACopyToBuff32,   (ULONG)m_dma_to_mbuf32 },		/* R3 DMA, optional */
    { S2_DMACopyFromBuff32, (ULONG)m_dma_from_mbuf32 },		/* R3 DMA, optional */
    { TAG_END, }
};


