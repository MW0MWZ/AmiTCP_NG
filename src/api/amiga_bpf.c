/*
 * AmiTCP_NG: the bpf_* library vectors -- the Roadshow-compatible Berkeley
 * Packet Filter entry points (LVOs -366 .. -408). Copyright 2026 Andy Taylor
 * (MW0MWZ). GPL v2 (see COPYING).
 *
 * These are thin shims over the channel core in net/bpf.c. Each unpacks the
 * vector's register arguments (A6 always holds the SocketBase, per amiga_raf.h),
 * calls the matching ng_bpf_* routine, and translates its return -- >= 0 on
 * success (a channel number for open, a byte/record count for read / write /
 * data_waiting, else 0), or a negative errno -- into Roadshow's convention: the
 * value on success, or -1 with errno set on the caller's own SocketBase.
 *
 * The register assignments come straight from the published SFD
 * (roadshow-ref bsdsocket_lib.sfd); note the deliberately different orders for
 * bpf_set_notify_mask (d1,d0) and bpf_set_interrupt_mask (d0,d1). The channel
 * semantics live entirely in net/bpf.c; nothing here keeps state.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/socket.h>

#include <kern/amiga_includes.h>

#include <api/amiga_api.h>
#include <api/amiga_libcallentry.h>
#include <api/amiga_raf.h>

#include <net/bpf.h>

/* Translate an ng_bpf_* result (>= 0 ok, < 0 == -errno) into a Roadshow return. */
#define BPF_RETURN(libPtr, r)						\
	do {								\
		int _r = (r);						\
		if (_r < 0) {						\
			writeErrnoValue((libPtr), -_r);			\
			return (-1);					\
		}							\
		return (_r);						\
	} while (0)

LONG SAVEDS RAF2(_bpf_open,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d0)
#if 0
{
#endif
  BPF_RETURN(libPtr, ng_bpf_open((int)channel));
}

LONG SAVEDS RAF2(_bpf_close,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d0)
#if 0
{
#endif
  BPF_RETURN(libPtr, ng_bpf_close((int)channel));
}

LONG SAVEDS RAF4(_bpf_read,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d0,
		 APTR,			buffer,		a0,
		 LONG,			len,		d1)
#if 0
{
#endif
  BPF_RETURN(libPtr, ng_bpf_read((int)channel, (caddr_t)buffer, (int)len, libPtr));
}

LONG SAVEDS RAF4(_bpf_write,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d0,
		 APTR,			buffer,		a0,
		 LONG,			len,		d1)
#if 0
{
#endif
  BPF_RETURN(libPtr, ng_bpf_write((int)channel, (caddr_t)buffer, (int)len, libPtr));
}

LONG SAVEDS RAF3(_bpf_set_notify_mask,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d1,
		 ULONG,			signal_mask,	d0)
#if 0
{
#endif
  BPF_RETURN(libPtr, ng_bpf_set_notify_mask((int)channel, (u_long)signal_mask));
}

LONG SAVEDS RAF3(_bpf_set_interrupt_mask,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d0,
		 ULONG,			signal_mask,	d1)
#if 0
{
#endif
  int r = ng_bpf_set_interrupt_mask((int)channel, (u_long)signal_mask);
  /*
   * Make a blocking bpf_read() actually break on these signals. bpf_read()
   * blocks in tsleep(), which already returns EINTR on any signal in this task's
   * SocketBase sigIntrMask -- and that mask defaults to SIGBREAKF_CTRL_C, so
   * Ctrl-C already interrupts a read. Fold the caller's requested mask in too,
   * so a custom break signal (which Roadshow's bpf_set_interrupt_mask lets an app
   * choose) works as well.
   *
   * DELIBERATE TRADEOFF: we OR the bits in and never remove them (not even at
   * bpf_close). A correct removal would need reference counting -- the same bit
   * may still be wanted by another open channel, or by the app itself, on this
   * shared SocketBase -- so a blind `&= ~mask` could wrongly clear a live break
   * signal. The bit is one the app allocated and set on purpose, so the app owns
   * not reusing it for an unrelated purpose (and then being surprised that a
   * blocking socket call treats it as a break) after closing the channel. The
   * only default-relevant case, Ctrl-C, needs no folding at all.
   */
  if (r >= 0)
    libPtr->sigIntrMask |= (ULONG)signal_mask;
  BPF_RETURN(libPtr, r);
}

LONG SAVEDS RAF4(_bpf_ioctl,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d0,
		 ULONG,			command,	d1,
		 APTR,			buffer,		a0)
#if 0
{
#endif
  BPF_RETURN(libPtr, ng_bpf_ioctl((int)channel, (u_long)command, (caddr_t)buffer));
}

LONG SAVEDS RAF2(_bpf_data_waiting,
		 struct SocketBase *,	libPtr,		a6,
		 LONG,			channel,	d0)
#if 0
{
#endif
  BPF_RETURN(libPtr, ng_bpf_data_waiting((int)channel));
}
