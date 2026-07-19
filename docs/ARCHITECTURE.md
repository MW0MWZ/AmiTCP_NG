# AmiTCP_NG — Architecture (start here)

This document is the map. Read it before diving into the source: it explains what
the pieces are, how they fit, and *in what order to read the code*. The source
files themselves carry the detailed, teaching-oriented comments; this ties them
together.

AmiTCP_NG is a modern-toolchain revival of **AmiTCP/IP 3.0b2** — the original
source-available TCP/IP stack for the Amiga, which *defined* the
`bsdsocket.library` API that every Amiga network program still uses. The
networking core is 4.4BSD-Lite; the interesting, Amiga-specific work is the glue
that makes a Unix kernel's network stack live inside AmigaOS as a shared library.

---

## 1. The one thing that surprises everyone

**In the classic AmiTCP design, `bsdsocket.library` is not a file on disk** — there
is no `LIBS:bsdsocket.library`; the library is built in RAM by a running program.
(AmiTCP_NG also ships a self-starting on-disk drop-in — see the note at the end of
this section — but the classic model is the clearest way in.)

That classic stack is an ordinary AmigaOS **program** (`amitcp`) that you *run*.
When it starts, it calls Exec's `MakeLibrary()` to *build a library in RAM* and
`AddLibrary()` to publish it, then it settles into a loop and *becomes* the TCP/IP
stack — a background process servicing sockets and network hardware. When it
quits, it removes the library again.

So "installing the stack" = running a program; "the library" = a data structure
that program created and manages. Keep this in mind and everything else falls into
place. (Roadshow, the later commercial stack, ships `bsdsocket.library` as a real
LIBS: file instead — a different packaging of the same idea.) AmiTCP_NG is also a
drop-in replacement for Roadshow: it matches Roadshow's ABI, configuration format,
and command set, and its Roadshow-specific extensions are a clean-room
re-implementation written against Olaf Barthel's Roadshow SDK as an ABI reference
only — none of Roadshow's own code is used.

**AmiTCP_NG's primary, recommended deliverable is that on-disk drop-in: a
self-starting `LIBS:bsdsocket.library` (built by `docker/build-lib.sh`, entry point
`src/lib/bsdsocket_lib.c`) that lazily starts the whole stack on the first library
call.** So for an *installed* AmiTCP_NG there **is** a `LIBS:bsdsocket.library`; the
`amitcp` program described above is the same stack in its original run-a-program
form and is mainly a build intermediate. Everything below applies to both forms
— they share all the code; only the way the library gets published (a running
program vs. a RomTag auto-init on first open) differs.

Entry point: `kern/amiga_main.c` → `main()`.

---

## 2. Background you need

### AmigaOS concepts (Exec, the kernel)
- **Task / Process** — AmigaOS's unit of execution. A *Process* is a Task with
  extra fields for DOS (current directory, file handles). AmiTCP_NG runs as a
  Process and spawns helper Processes (`CreateNewProc`).
- **Library** — a table of function pointers (the *vector table*) preceded by a
  `struct Library` node. You call a function by jumping to a negative offset from
  the library base (`jsr a6@(-30)` = "call the function 30 bytes before the
  base"). Those offsets are the **LVOs** (Library Vector Offsets). See §5.
- **Message / MsgPort** — asynchronous IPC. A Task allocates a signal bit, waits
  on it, and other Tasks `PutMsg()` to its port to wake it. This is how the stack
  waits for work and how helper tasks hand back results.
- **Device** — like a library but for I/O: you send it `IORequest`s. **SANA-II**
  (see §8) is the standard for *network* devices (Ethernet cards, SLIP, etc.).
- **Signals** — 32 per Task. `Wait(mask)` sleeps until one of the signalled bits
  fires. The whole stack is driven by one `Wait()` (see §6).
- No memory protection on the 68000 (no MMU): a bad pointer corrupts the machine.
  This is why bugs here are found by tracing, not by a fault handler.

### BSD networking concepts (the portable core)
- **socket** — the application's handle on a network connection. Backed by a
  `struct socket` with a send buffer and a receive buffer (`sockbuf`s).
- **mbuf** — the BSD "memory buffer": a small fixed-size chunk (128 bytes) that
  holds a piece of a packet. Packets are *chains* of mbufs linked by `m_next`;
  separate packets on a queue are linked by `m_nextpkt`. Headers are prepended and
  data trimmed by adjusting pointers, so no copying. See §7.
- **protosw / domain** — the protocol switch. A table of `{input, output, usrreq}`
  function pointers per protocol (TCP, UDP, ICMP, raw IP), grouped into a "domain"
  (here, the Internet domain). Dispatch through these tables is how the generic
  socket code stays protocol-agnostic.
- **pcb** (protocol control block, `inpcb`) — per-connection state: local/remote
  address and port, pointer to the socket, route cache.
- **ifnet** — a network interface. `lo0` (loopback) is built in; hardware
  interfaces are created over SANA-II devices (see §8).

If these are new, the canonical reference is Stevens & Wright,
*TCP/IP Illustrated, Volume 2* — it annotates the very 4.4BSD-Lite code this stack
is derived from, almost function for function.

---

## 3. The layers (10,000-foot view)

```
   Application (e.g. ping, a web browser)
        │  OpenLibrary("bsdsocket.library"); socket(); send(); recv()
        │  — via the client inline stubs (jsr a6@(-LVO))
        ▼
 ┌───────────────────────────────────────────────────────────────┐
 │  bsdsocket.library  (the public API — one vector table)        │  api/
 │  socket/bind/connect/send/recv/... GetSocketEvents, ...        │
 └───────────────────────────────────────────────────────────────┘
        │  each call runs inside the stack's context
        ▼
 ┌───────────────────────────────────────────────────────────────┐
 │  Socket layer   sosend/soreceive, socket buffers               │  kern/
 └───────────────────────────────────────────────────────────────┘
        │  protosw dispatch (pr_usrreq / pr_output / pr_input)
        ▼
 ┌───────────────┬───────────────┬───────────────────────────────┐
 │  TCP          │  UDP          │  raw IP / ICMP                 │  netinet/
 └───────────────┴───────────────┴───────────────────────────────┘
        │  ip_output / ip_input, routing (netinet/, net/route.c)
        ▼
 ┌───────────────────────────────────────────────────────────────┐
 │  Interface layer   ifnet: lo0 (if_loop.c), SANA-II (if_sana.c) │  net/
 └───────────────────────────────────────────────────────────────┘
        │  SANA-II IORequests (CMD_WRITE / CMD_READ)
        ▼
 ┌───────────────────────────────────────────────────────────────┐
 │  SANA-II device   e.g. a2065.device, ppp-serial.device         │  (external)
 └───────────────────────────────────────────────────────────────┘
        │
        ▼   physical network
```

The left/right split is the classic one: everything above the interface layer is
essentially portable 4.4BSD; the interface layer and the library packaging are the
Amiga-specific parts that make this project interesting.

---

## 4. Lifecycle — starting, running, stopping

`kern/amiga_main.c`:
- `main()` grabs `SysBase`, opens `utility.library` (see §10 — this is load-bearing
  for integer math), then calls **`init_all()`**.
- `init_all()` brings the subsystems up *in order*, each depending on the last:
  1. `malloc_init`, `spl_init`, `sleep_init` — kernel-emulation primitives.
  2. `readconfig()` — parse `AmiTCP:db/AmiTCP.config` (`kern/amiga_config.c`).
  3. `log_init()` — spawn the **NETTRACE** logging/ARexx Process (`kern/amiga_log.c`).
  4. `mbinit()` — the mbuf pools (`kern/uipc_mbuf.c`).
  5. `timer_init()` — open `timer.device` for protocol timers (`kern/amiga_time.c`).
  6. **`api_init()`** — `MakeLibrary()` builds `bsdsocket.library` (`api/amiga_api.c`).
  7. `sana_init()` — prepare the SANA-II interface machinery (`net/if_sana.c`).
  8. `domaininit()` — initialise the protocol switch (TCP/UDP/ICMP/raw).
  9. `init_netdb()` — load the host/network database (`kern/amiga_netdb.c`).
  10. `api_show()` — `AddLibrary()` publishes the library so apps can open it.
- Then `main()` enters the **service loop** (§6) until CTRL-C, and `deinit_all()`
  tears everything down in reverse.

A student's reading order: `amiga_main.c` → `amiga_api.c` (how the library is
built) → then follow a packet (§7).

---

## 5. The library and how apps call it

`api/amiga_api.c` and `api/amiga_libtables.c`:
- `LibVectors[]` (`amiga_libtables.c`) is the **vector table** — the ordered list of
  function pointers. Its order *is* the ABI: `socket` is first (LVO −30), `bind`
  −36, `sendto` −60, and so on (bias 30, −6 per entry; see the SFD in
  `roadshow-ref/`). Change the order and you break every existing binary.
- `api/amiga_api.c` builds a `struct Library` from `LibVectors[]` and a hand-written
  `Library_initTable`, and implements the mandatory `Open`/`Close`/`Expunge`.
- **Per-opener state:** each program that `OpenLibrary()`s gets its *own* extended
  library base (a `SocketBase`) holding *its* socket fd table, `errno` pointer, and
  signal masks. Two programs never see each other's sockets. `ELL_Open` in
  `amiga_api.c` builds these; the API functions take the caller's base in `a6`.
- **Client side:** an application doesn't hand-write `jsr a6@(-30)`. It includes
  `<proto/socket.h>`, whose inline stubs load `a6=SocketBase` and jump to the LVO.
  Our in-tree copies of those stubs are `api/apicalls_gnuc.h` (used internally when
  the stack calls its *own* API). `tmp/socktest.c` and `tmp/udptest.c` show the
  raw form for teaching purposes.

---

## 6. The task model — how work gets done

One `Wait()` drives the whole stack. In `main()`:

```c
sigmask = timermask | breakmask | sanamask;   /* three sources of work */
for (;;) {
    sig = Wait(sigmask);          /* sleep until something needs attention */
    if (sig & sanamask)  sana_poll();   /* a SANA-II device has a packet / finished a write */
    if (sig & timermask) timer_poll();  /* a protocol timer (TCP retransmit, etc.) fired */
    if (sig & breakmask) ... shutdown ...
}
```

Two extra Processes exist alongside the main one:
- **NETTRACE** (`log_task` in `amiga_log.c`) — owns logging and the `AMITCP` ARexx
  port (how `online`/`offline` and other tools control a running stack). It is
  spawned during `log_init` and hands back a handshake message when ready.
- **SANA-II reader tasks** — each interface queues asynchronous `CMD_READ`
  IORequests; completions signal the main task, which drains them in `sana_poll`.

There is no preemptive locking of the BSD core: the stack cooperatively processes
one event at a time, and `spl_init`/`splnet` (`kern/`) emulate BSD's interrupt
priority levels by using Exec `Forbid`/`Permit` where the original masked
interrupts. This is the single biggest conceptual difference from a real Unix
kernel and is worth understanding before reading the protocol code.

---

## 7. Follow a packet (the data path)

**Outbound** — application sends a UDP datagram:
1. App calls `sendto()` → client stub → library vector → `api/amiga_syscalls.c`.
2. Socket layer `sosend()` (`kern/uipc_socket.c`) copies user data into an **mbuf
   chain** and calls the protocol's `pr_usrreq(PRU_SEND)`.
3. `udp_usrreq` → `udp_output` (`netinet/udp_usrreq.c`) prepends the UDP header,
   computes the checksum, and calls `ip_output`.
4. `ip_output` (`netinet/ip_output.c`) fills the IP header, looks up a **route**
   (`net/route.c`, `net/radix.c`), and hands the mbuf chain to the chosen
   interface's `if_output`.
5. For a hardware interface that's `sana_output` (`net/if_sana.c`), which turns the
   mbuf chain into a SANA-II `CMD_WRITE` IORequest and sends it to the device. When
   the device's write requests are all in flight it queues the frame on the
   interface send queue (`if_snd`) instead of dropping it, so a bulk upload doesn't
   tail-drop.
   For `lo0` it's `looutput` (`net/if_loop.c`), which loops the packet straight
   back into `ip_input` — this is exactly the path `tmp/udptest.c` exercises.

**Inbound** — a packet arrives on the wire:
1. The SANA-II device completes a queued `CMD_READ`; the main task notices in
   `sana_poll` and calls `sana_read` (`net/if_sana.c`), which copies the frame into
   an mbuf chain (via `sana2copybuff.c`) and hands it to the protocol input queue.
2. The software-interrupt emulation `netisr` (`net/netisr.*`) dispatches to
   `ipintr`/`ip_input` (`netinet/ip_input.c`), which verifies the header/checksum,
   reassembles fragments, and dispatches by protocol number.
3. `udp_input` (`netinet/udp_usrreq.c`) finds the matching `inpcb`, appends the
   datagram to the socket's receive buffer, and signals any waiting reader.
4. The app's `recvfrom()` unblocks in `soreceive()` and copies the data out.

ARP (address resolution) and ICMP ride alongside: `sana2arp.c` handles ARP for
SANA-II Ethernet, and `netinet/ip_icmp.c` handles ICMP (including the echo/reply
that a real `ping` uses).

---

## 8. mbufs and SANA-II (the two Amiga-flavoured abstractions)

**mbufs** (`kern/uipc_mbuf.c`, `sys/mbuf.h`): identical in spirit to BSD, but
allocated from **pre-reserved pools** rather than a general kernel allocator. Why:
inbound packets are copied in at (emulated) interrupt time, where you must not call
Exec's `AllocMem`. So the stack reserves a pool of mbufs up front and hands them
out with a lightweight free-list. Read `uipc_mbuf.c`'s header comment for the pool
mechanics, then `m_get`/`m_free`/`m_pullup`.

**SANA-II** (`net/if_sana.c`, `sana2*.c`, `netinclude/devices/sana2.h`): the Amiga's
standard for network *device drivers*. A driver is an Exec device you drive with
IORequests:
- `CMD_WRITE` — transmit a frame; you tag it with a *packet type* (e.g. 0x0800 =
  IP, 0x0806 = ARP).
- `CMD_READ` — receive a frame of a given packet type; you keep several queued so
  the driver always has somewhere to put an arriving packet.
- `S2_...` commands — configure the interface, get the hardware address, statistics.
- **Buffer-management callbacks** (`S2_CopyToBuff`/`S2_CopyFromBuff`,
  `sana2copybuff.c`): the driver doesn't know about mbufs, so it calls back into the
  stack to move payload between its buffers and our mbuf chains. This is the crux of
  the mbuf-pool requirement above.

`if_sana.c` is where a BSD `ifnet` is wired onto a SANA-II device — the single most
Amiga-specific file in the stack and the best thing to study if you want to learn
"how do you attach a Unix stack to a foreign driver model".

---

## 9. Source map (where to read what)

| Area | Files | What it is |
|---|---|---|
| **Lifecycle** | `kern/amiga_main.c` | `main`, `init_all`, the service loop. **Start here.** |
| **Library** | `lib/bsdsocket_lib.c`, `api/amiga_api.c`, `api/amiga_libtables.c`, `api/amiga_libcallentry.h` | RomTag self-start skeleton for the on-disk drop-in; build/publish `bsdsocket.library`; the vector table (the ABI). |
| **API entry** | `api/amiga_syscalls.c`, `api/amiga_generic*.c`, `api/amiga_sendrecv.c`, `api/apicalls_gnuc.h` | The public socket calls and `SocketBaseTagList`. |
| **Socket layer** | `kern/uipc_socket.c`, `uipc_socket2.c`, `uipc_domain.c` | `sosend`/`soreceive`, socket buffers, protosw dispatch. |
| **mbufs** | `kern/uipc_mbuf.c`, `sys/mbuf.h` | The packet buffer abstraction + pools. |
| **Interfaces** | `net/if_sana.c`, `sana2arp.c`, `sana2copybuff.c`, `sana2config.c`, `if_loop.c`, `net/if.c` | ifnet over SANA-II; loopback. |
| **Routing** | `net/route.c`, `net/rtsock.c`, `net/radix.c` | The routing table (radix tree) and the routing socket. |
| **IP/ICMP** | `netinet/ip_input.c`, `ip_output.c`, `ip_icmp.c`, `in.c`, `in_pcb.c`, `in_cksum.c` | IPv4, ICMP, address/pcb management, the checksum. |
| **TCP** | `netinet/tcp_input.c`, `tcp_output.c`, `tcp_subr.c`, `tcp_timer.c`, `tcp_usrreq.c`, `tcp_debug.c` | The TCP state machine. |
| **UDP / raw** | `netinet/udp_usrreq.c`, `raw_ip.c`, `net/raw_cb.c`, `raw_usrreq.c` | UDP; raw IP sockets (used by ping/traceroute). |
| **Config / log** | `kern/amiga_config.c`, `config_var.c` (generated), `amiga_log.c`, `amiga_rexx.c`, `amiga_cstat.c` | Config parsing, NETTRACE logging, the ARexx control port. |
| **Database / resolver** | `kern/amiga_netdb.c`, `api/auto_*.c`, `api/res_*.c`, `gethostnamadr.c`, `getxbyy.c` | hosts/services/protocols DB; the DNS resolver. |
| **Kernel shims** | `kern/kern_malloc.c`, `kern_synch.c`, `subr_prf.c`, `accesscontrol.c`, `amiga_time.c` | BSD kernel primitives re-implemented on Exec. |
| **Port glue** | `amitcp_ng_glue.c`, `conf/amitcp_ng_bases.h` | The AmiTCP_NG modern-toolchain shims (see §10). |

---

## 10. The modern-gcc port (what "_NG" means)

The original built with SAS/C in 1994. AmiTCP_NG builds with **bebbo's
`m68k-amigaos-gcc` 6.5** (`docker/build.sh`). The port lessons — every one worth
teaching because they generalise to any "revive old Amiga C" project — are logged
in `PORTING.md`. The headline ones:

- **`utility.library` is load-bearing.** bebbo's libgcc implements the 68000's
  missing 32-bit multiply/divide (`__mulsi3`/`__divsi3`) by calling
  `utility.library`'s `SMult32`/`UMult32` through the global `UtilityBase`. AmiTCP
  opened that late, so the *first* 32-bit multiply crashed. Fix: open it first
  thing in `main()`. This is the single best debugging war-story in the codebase.
- **Runtime choice:** link `-noixemul` (libnix) not clib2 — a `CreateNewProc`-
  spawned task must not depend on a heavy per-process C runtime.
- **CPU target:** compile *and link* `-m68000` so the 68000 multilib libgcc is
  used (else `muls.l`, a 68020 instruction, faults on the A600/A500+).
- **Base declarations:** bebbo's inline headers need the library base in scope;
  `conf/amitcp_ng_bases.h` is force-included to declare `SysBase`/`DOSBase`/etc.
- Every source change carries a `PORT (AmiTCP_NG): ...` comment explaining *why*.

---

## 11. Further reading

- **Stevens & Wright, *TCP/IP Illustrated, Vol 2*** — annotates this exact BSD core.
- **The 4.4BSD-Lite sources** — the upstream of `netinet/`, `net/`, `kern/uipc_*`.
- **The SANA-II specification** (`roadshow-ref/`, `netinclude/devices/sana2.h`) —
  the network-device standard `if_sana.c` implements against.
- **AmiTCP/IP 3.0b2 docs** (`ref/amitcp-*`) — the original design notes.
- **`PORTING.md`** (this repo) — the blow-by-blow of the modern-gcc revival.
