# AmiTCP_NG — Roadshow extension vectors: deferred and since-implemented

Status of the Roadshow `bsdsocket.library` extension vectors that AmiTCP_NG does
**not** implement, plus the ones that were once on this list and are now live. The
deferrals are deliberate, documented decisions — not oversights. Every
unimplemented vector is wired to a clean stub that returns the standard "not
supported" signal (`-1` + `errno = ENOSYS`, or `NULL` for pointer-returning
vectors), and where Roadshow gates a family behind a capability probe
(`SocketBaseTagList` `SBTC_HAVE_*_API`), AmiTCP_NG answers that probe with **0**, so
well-behaved tools detect the absence and never call the vector. This is exactly how
Roadshow itself signals optional features it was built without.

For the authoritative, current live-vs-stubbed vector table see the LVO table itself
(`src/api/amiga_libtables.c`); the implemented surface is summarised in
[README.md](../README.md).

## Deferred by design

### IP filter / firewall — `ipf_*` (7 vectors, LVO -762…-798)
`ipf_open`, `ipf_close`, `ipf_ioctl`, `ipf_log_read`, `ipf_log_data_waiting`,
`ipf_set_notify_mask`, `ipf_set_interrupt_mask`.

The packet-filtering (firewall) control interface. Like BPF, this is a whole
subsystem — a rule engine plus hooks in `ip_input`/`ip_output` plus a logging
device — with no bearing on baseline connectivity or on the config-tool
compatibility that is this project's goal. Deferred. Callers get `ENOSYS`.

### Network-monitor hooks — `AddNetMonitorHookTagList` / `RemoveNetMonitorHook` (2, LVO -498/-504)
Install/remove a callback hook invoked per packet for monitoring tools. Capability
`SBTC_HAVE_MONITORING_API` is answered **0**. Overlaps heavily with BPF in purpose;
deferred with the same reasoning. Callers get `ENOSYS`.

### Server API — `ProcessIsServer` / `ObtainServerSocket` (2, LVO -690/-696)
Support for `inetd`-style servers that inherit a listening socket from a launching
super-server. Capability `SBTC_HAVE_SERVER_API` is answered **0**. Niche; not needed
by clients or by the config tools. Deferred. Callers get the "not supported" signal.

## Intentionally never implemented

### `ChangeRouteTagList` (1, LVO -426)
A private, undocumented route-mutation entry that **Roadshow itself leaves
unimplemented**. There is no published contract to match and no tool that calls it.
Left as a permanent `ENOSYS` stub to hold the SFD offset — matching Roadshow's own
behaviour.

## Reserved SFD slots (not vectors)

The SFD reserves 18 slots for future expansion (`==reserve 10` after
`GetSocketEvents`, `==reserve 2` after `gethostbyaddr_r`, `==reserve 6` after
`getnameinfo`). These are **not** functions — they are offset placeholders with no
Roadshow implementation behind them. AmiTCP_NG fills them with the clean stub purely
to keep every real vector at its exact SFD offset.

## Implemented (were on this list, now live)

These extension surfaces were once deferred and are now fully implemented; they are
kept here so the record stays complete.

### Berkeley Packet Filter — `bpf_*` (8 vectors, LVO -366…-408)
`bpf_open`, `bpf_close`, `bpf_read`, `bpf_write`, `bpf_set_notify_mask`,
`bpf_set_interrupt_mask`, `bpf_ioctl`, `bpf_data_waiting` are all live. The full
subsystem is implemented: a 4.4BSD/libpcap BPF filter virtual machine, a per-channel
double-buffered capture ring, "cooked" Ethernet taps on the SANA-II receive **and**
transmit paths (reconstructing the link-layer header SANA-II delivers separately),
packet injection via `bpf_write`, the `BIOC*` ioctls, and Ctrl-C-interruptible
reads. The `SBTC_NUM_PACKET_FILTER_CHANNELS` capability is answered with the channel
count, so a pcap/tcpdump port can discover BPF is available. Validated end-to-end
over an emulated A2065 NIC: live capture of real ARP/IP frames in both directions,
and injection proven by capturing the injected frame back through the transmit tap.

### DHCP client — `BeginInterfaceConfig` / `AbortInterfaceConfig` (2, LVO -486/-492)
The async BOOTP/DHCP client spawns a helper process that brings the interface up
unnumbered, transmits DHCPDISCOVER as a proper Ethernet broadcast, completes the
full DORA handshake (DISCOVER→OFFER→REQUEST→ACK), applies the leased
address/mask/router/DNS, and `ReplyMsg()`s the result. Proven end to end in the
Amiberry+SLIRP harness (obtains `10.0.2.15/24`, gateway/server `10.0.2.2` over the
emulated A2065) and on real hardware (PiStorm + `wifipi.device`).

A supporting change was needed in `netinet/ip_input.c`: an **unnumbered interface**
(address `0.0.0.0`, i.e. mid-DHCP) accepts L2-unicast UDP frames delivered to its
own MAC. This is the standard DHCP-bootstrap reception case — some servers
(including SLIRP's) ignore the BOOTP broadcast flag and unicast the reply to the
address being offered, which is not yet ours. The rule is tightly scoped (receiving
interface only, unicast UDP only) and inert once the interface has an address.

## DHCP config ownership (route / DNS)

After a successful lease the DHCP client applies the whole configuration itself —
address, netmask, **default route**, and **DNS** — *and* also returns the router and
DNS servers to the caller in `aam_RouterTable` / `aam_DNSTable`. A Roadshow-model
caller (e.g. Roadshow's own `AddNetInterface`) that reads those tables and installs
the route/DNS *again* does not double up, because both paths are self-healing:

- **Routes are idempotent and self-cleaning.** `AddRouteTagList` treats re-adding a
  genuinely identical route (same destination key, host/net nature, and gateway) as
  success rather than `EEXIST`. An interface's default route and the DHCP
  `255.255.255.255` broadcast host route are removed when it goes offline or is
  removed, so a re-lease starts from a clean table with no orphaned routes.
- **DNS is replaced, not appended.** Each lease flushes the previously
  DHCP/runtime-added DNS servers before installing this lease's, and removing an
  interface flushes them too; statically configured (config-file) servers are kept.
  Repeated Online/Offline cycles therefore do not accumulate duplicates.

A more faithful Roadshow split — the library *returns* the parameters and leaves the
caller to install the route and DNS — is a possible future refinement, but is not
required for correctness given the above.
