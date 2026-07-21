# AmiTCP_NG

A modernised, open TCP/IP stack for 68k AmigaOS — a fork of **AmiTCP/IP 3.0b2**
brought up to date with a current cross-toolchain, extensively documented, and
extended into a **drop-in replacement for Roadshow's `bsdsocket.library`**.

Three things it is, in one breath:

1. **A fork of AmiTCP/IP 3.0b2.** The BSD networking core and the AmigaOS
   integration descend directly from the GPL AmiTCP/IP 3.0b2 sources.
2. **A drop-in replacement for Roadshow.** It provides the same
   `bsdsocket.library` API and the same configuration model and command set, so
   existing Amiga network software — and Roadshow's own configuration tools —
   work against it unchanged, with **no time limit**.
3. **A clean-room re-implementation of the Roadshow extensions.** The
   Roadshow-specific `bsdsocket.library` extensions were re-implemented from
   scratch, using **Olaf Barthel's open-source Roadshow SDK purely as a reference**
   for the published ABI (function offsets, tag values, structure layouts,
   documented behaviour). **No Roadshow code is used, copied, disassembled, or
   redistributed** — see [Attribution](#attribution--the-roadshow-sdk-reference-only).

The stack ships as a self-starting **`LIBS:bsdsocket.library`**: drop it in, and
it brings the whole TCP/IP stack up by itself the first time any program opens it.
(A standalone `amitcp` program build exists too.)

> **Not affiliated with, and not derived from, Roadshow.** This project does not
> crack, patch, disassemble, or bypass Roadshow or any other commercial stack.
> Its goal is *interoperability* through an independent, open implementation of a
> published ABI.

## Features

- **BSD sockets** — the full `bsdsocket.library` API (TCP, UDP, raw), a drop-in
  Roadshow ABI so existing Amiga network software works unchanged.
- **Protocols** — TCP, UDP, ICMP (including `ping`), IP with routing, and ARP.
- **Interfaces** — any SANA-II network device, plus software loopback (`lo0`).
- **Address configuration** — DHCP client, static, or **RFC 3927 IPv4
  link-local** (ZeroConf) auto-assignment when no DHCP server answers.
- **Name resolution** — DNS resolver (`gethostbyname`, `getaddrinfo`, reentrant
  `gethostby*_r`, …) with a RAM-tiered **DNS response cache**.
- **Modern TCP** — **RFC 1323 window scaling + timestamps**; socket buffers, the
  mbuf pool and the DNS cache size to installed RAM, and timestamps gate on the
  CPU (68020+). Initial sequence numbers are randomised per connection
  (**RFC 6528**, a keyed HalfSipHash of the connection tuple), hardening TCP
  against off-path spoofing and blind connection injection.
- **Packet capture (BPF)** — a Berkeley Packet Filter subsystem (the `bpf_*`
  vectors): open a channel, bind it to an interface, set a filter, and read
  captured frames — or inject your own — the raw-packet engine a `tcpdump`/`pcap`
  port needs.
- **Roadshow-compatible tooling** — the extension API plus the full command set
  (`Online`, `Offline`, `AddNetInterface`, `ShowNetStatus`, `ping`, …) and an
  Amiga Installer.

## Status

- Builds, links, and **runs on emulated AmigaOS 3.2**, installing a working
  self-starting `LIBS:bsdsocket.library`.
- **Real-network validated** on an emulated Commodore A2065 NIC over SLIRP: a
  live DNS round-trip, a full **DHCP lease** (`DISCOVER→OFFER→REQUEST→ACK`), and
  **ICMP `ping`** (to the gateway and to loopback).
- **Validated on real 68k hardware** (PiStorm accelerator + `wifipi.device`):
  interface bring-up, DHCP lease, default-route install, DNS, `ping`, and
  end-to-end connectivity over a 100 Mbit WiFi link.
- **Roadshow-compatible extension API** implemented (address conversion,
  DNS-server management, interface configuration/query/enumeration, routing,
  network statistics + system status, RoadshowData tunables, kernel `mbuf`
  access, the `get*ent` database iterators, `getaddrinfo`/`getnameinfo`,
  reentrant `gethostby*`, and a working **DHCP client**), plus its capability
  flags.
- **Zero-configuration networking (RFC 3927).** When DHCP finds no server, the
  interface automatically self-assigns an IPv4 link-local address
  (`169.254.x.y`) — ARP-probed for uniqueness, announced, and defended against
  conflicts — so a cable between two Amigas, or any DHCP-less LAN, just works
  with no manual configuration. It keeps retrying DHCP in the background and
  upgrades to a real lease the moment a server appears — see
  [docs/BUILDING.md](docs/BUILDING.md#zero-configuration-rfc-3927-link-local).
- **A complete set of Roadshow-compatible command-line tools**, name-, argument-,
  and output-compatible so Roadie, NetMon and existing scripts drive the stack
  unchanged: `Online`, `Offline`, `AddNetInterface`, `ConfigureNetInterface`,
  `AddNetRoute`, `DeleteNetRoute`, `RemoveNetInterface`, `NetShutdown`,
  `GetNetStatus`, `ShowNetStatus`, and `ping`.
- **Machine-adaptive TCP performance.** Full **RFC 1323 window scaling and
  timestamps**, so a single connection can scale past the old 64 KB /
  round-trip wall. The stack sizes itself to the hardware at start-up: socket
  buffers and the mbuf pool tier to installed **RAM**, and the CPU-costly
  timestamp option is gated on the **processor** (on for 68020+, off for a bare
  68000/68010). Both are negotiated per connection and degrade cleanly against
  peers that don't offer them — see
  [docs/BUILDING.md](docs/BUILDING.md#throughput-and-memory).
- **Randomised TCP sequence numbers (RFC 6528).** Each connection's initial
  sequence number is a keyed HalfSipHash of its address/port 4-tuple plus a
  per-boot secret, instead of a predictable global counter — hardening TCP
  against off-path spoofing and blind connection injection. The keyed hash is
  light (add/rotate/xor, no multiplies) and suited to a 68000; the secret is a
  best-effort boot seed, as this class of machine has no hardware RNG.
- **DNS response caching.** A small, RAM-tiered cache in front of the resolver
  remembers successful lookups (honouring each record's TTL) and definitive
  "host not found" results, so repeated `gethostbyname` / `getaddrinfo` calls
  are served without a network round-trip. Sized to installed RAM (8–128
  entries), automatic, no configuration — see
  [docs/BUILDING.md](docs/BUILDING.md#dns-response-cache).
- **An Amiga Installer** with install / uninstall / preview modes, automatic
  upgrade-vs-full-install detection, and a chooseable install location.
- **Berkeley Packet Filter (`bpf_*`) is implemented** — open a channel, bind it
  to an interface, and capture or inject raw frames (the `tcpdump`/`pcap`
  engine), with the `SBTC_NUM_PACKET_FILTER_CHANNELS` capability so tools can
  discover it. Validated over an emulated A2065 NIC: live capture of ARP/IP in
  both directions, and injection confirmed by capturing the injected frame back.
- A few advanced surfaces remain deliberately deferred (IP filter `ipf_*`,
  monitor hooks, server API) — see
  [docs/DEFERRED-VECTORS.md](docs/DEFERRED-VECTORS.md).

## Installing

Grab the release `.lha` (or the `.adf` floppy image) and run its
`Install-AmiTCP_NG` Installer script on your Amiga. **How much it asks depends on
the user level you pick in the Installer's opening dialog** (the standard Amiga
Installer Novice / Intermediate / Expert choice):

- **Novice** — the installer asks **nothing** and takes every default:
  - It installs the **`AmiTCP` drawer** (its configuration + host database) to
    **`SYS:Programs/AmiTCP`** — or `SYS:AmiTCP` if you have no `Programs` drawer.
  - If it detects an **existing TCP/IP stack** (a `bsdsocket.library`, e.g.
    **Roadshow**), it **automatically upgrades in place**: it backs up and swaps in
    AmiTCP_NG's `bsdsocket.library`, and installs AmiTCP_NG's own command set over the
    existing tools (originals backed up to `C:<name>.orig`). Replacing the commands is
    required — Roadshow's own config tools drive AmiTCP_NG down an interface-setup path
    that hangs, so the command set must be AmiTCP_NG's too. Your interface
    configuration is left untouched, and it all runs with no time limit. On a clean
    machine it does a full install (library + the whole command set + a network
    startup + example configs).

- **Intermediate / Expert** — unlocks the extra choices:
  - **Choose where** the `AmiTCP` drawer goes (any volume or drawer).
  - **Preview** — show exactly what an install would do, changing nothing.
  - **Uninstall / roll back** — restore the library (and command) that were backed
    up by an upgrade, or remove what a full install added. **Uninstall is only
    offered above Novice level.**

Reboot when the installer finishes. End-user details are in
[install/ReadMe](install/ReadMe).

## Build & test

Everything runs in disposable Docker containers — you need only Docker on the
host. **[docs/BUILDING.md](docs/BUILDING.md)** is the full guide (compiling the
tools, rolling your own `.lha`/`.adf`, and testing your own code); the harness
internals are in **[docker/README.md](docker/README.md)**.

```bash
# The self-starting drop-in library  ->  build/bsdsocket.library
./docker/build-lib.sh

# The full Roadshow-compatible command set  ->  build/Online, build/ping, ...
./docker/build-tools.sh

# A complete installable release  ->  build/release/AmiTCP_NG.lha  and  .adf
./docker/build-release.sh

# Test: fast loopback / API tests, then real-network tests (A2065 + SLIRP + DHCP)
TIMEOUT=95 ./docker/run-fsuae.sh
NET=1 TIMEOUT=150 ./docker/run-amiberry.sh
```

To run the emulators you must supply your own licensed Amiga system files (a
Kickstart ROM and an AmigaOS 3.2 install). These live under `emu/`, which is
git-ignored and **never committed** — see the docker guide.

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | The TCP/IP stack: AmigaOS integration (`kern/`, `api/`), BSD networking core (`net/`, `netinet/`), the drop-in library (`lib/`), headers (`netinclude/`). |
| `src/tools/` | The Roadshow-compatible command-line tools (source), sharing `ng_lvo.h`. |
| `install/` | The Amiga Installer script, its `ReadMe`, the network database, `Network-Startup`, and example interface configs. |
| `docker/` | The build/test harness — Dockerfiles, scripts, and per-image how-to READMEs. |
| `docs/` | `BUILDING.md`, `ARCHITECTURE.md`, `COMMENTING.md`, `REVIEW_FINDINGS.md`, `DEFERRED-VECTORS.md`. |
| `COPYING` / `COPYRIGHTS` | GPL v2, and the retained original AmiTCP/IP copyright notices. |

## Buy me a coffee ☕

This is a hobby project, done for the love of the Amiga — not to make money, and
it will always be free. But if it's useful to you and you fancy buying me a
coffee, that's very kind: <https://paypal.me/AndyTaylorTweet>. Entirely optional,
and thank you either way.

## License

AmiTCP_NG is licensed under the **GNU General Public License, version 2** (see
[COPYING](COPYING)), consistent with its AmiTCP/IP heritage.

- **AmiTCP_NG modifications and new code:** Copyright © 2026 Andy Taylor
  (MW0MWZ). Modified and new source files carry this notice; original AmiTCP/IP
  and BSD copyright notices are retained alongside.
- **AmiTCP/IP 3.0b2:** Copyright © 1993, 1994 AmiTCP/IP Group,
  Helsinki University of Technology (see [COPYRIGHTS](COPYRIGHTS)).
- **BSD networking code:** Copyright © the Regents of the University of
  California, under the original BSD license (retained in the affected files).

## Attribution — the Roadshow SDK (reference only)

AmiTCP_NG's Roadshow-compatible `bsdsocket.library` extensions are a **clean-room
re-implementation**. They were written from scratch using **Olaf Barthel's
Roadshow SDK** as the authoritative *reference* for the ABI: the extension
function offsets, tag values, structure layouts, and documented behaviour all
come from the SDK's `autodoc`, SFD files, and headers. Reading that published
documentation (and the SDK's example command sources, and `strings` on published
binaries) is legitimate interoperability work — **no Roadshow code is reused,
copied, disassembled, or included here.** Our implementation is our own.

We are genuinely grateful that **Olaf Barthel** made the Roadshow SDK openly
available. This project simply could not match the ABI so precisely without that
documentation, and we thank him for it.

The Roadshow SDK and its sample sources are **Copyright © Olaf Barthel / APC&TCP,
All Rights Reserved**, and are **not** included in this repository. To build or
verify against the SDK reference, obtain it directly from its author:

- **Roadshow:** <http://roadshow.apc-tcp.de/>
- **Roadshow SDK:** <https://www.amigafuture.de/app.php/dlext/details?df_id=3658>

Roadshow is a commercial product. AmiTCP_NG is an independent, open
implementation of the same published ABI — neither derived from, nor affiliated
with, Roadshow — and does not include, modify, or bypass any Roadshow code.
