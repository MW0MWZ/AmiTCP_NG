# AmiTCP_NG — an open, unlimited TCP/IP stack for 68k AmigaOS

> **This is the original planning document, kept for the record.** For the current
> state of the project see [README.md](README.md); for how to build and test see
> [docs/BUILDING.md](docs/BUILDING.md). The build/run/DHCP/Roadshow-compat phases
> below are **done** — the stack builds and runs on emulated AmigaOS 3.2 (and on
> real 68k hardware) as a self-starting drop-in `bsdsocket.library`, with a working
> DHCP client, the Roadshow-compatible extension API, a complete Roadshow-compatible
> command set, an Amiga Installer, Berkeley Packet Filter (`bpf_*`) packet capture,
> and real-network validation (DHCP, DNS, `ping`). The IP-filter `ipf_*` firewall
> vectors remain deliberately deferred (see
> [docs/DEFERRED-VECTORS.md](docs/DEFERRED-VECTORS.md)).

## Goal
A `bsdsocket.library`-compatible TCP/IP stack for stock 68k AmigaOS (2.04+), with **no time limit**, **open source**, that:
1. runs every existing Amiga network app unchanged (standard socket API), and
2. is **driven by Roadshow's own config tools** (`AddNetInterface`, `Online`, `ShowNetStatus`, …) by implementing Roadshow's config-management extension API.

Built by **forking AmiTCP 3.0b2** (the original, source-available BSD-derived stack that *defined* the `bsdsocket.library` API) and adding a Roadshow-extensions shim. **No Roadshow binaries are touched or cracked** — we replace the stack with our own; only published SDK *specifications* and *reference tool source* are used.

## Why this is tractable (not "write a stack")
- **`bsdsocket.library` IS the stack.** Replacing it = shipping our own; Roadshow is simply not present. Clean, legal.
- AmiTCP 3.0b2 (forked into `src/`) is a **complete working stack** — 34K lines: the library (`api/`), BSD socket+mbuf core (`kern/`), link/route/**SANA-II** glue (`net/`, incl. `if_sana.c`, `sana2arp.c`, `sana2copybuff.c`), and full IPv4/TCP/UDP/ICMP (`netinet/`, incl. a hand-asm IP checksum `in_cksum.asm`). Every hard AmigaOS-integration problem (SANA-II shim, mbuf pools that dodge no-alloc-in-interrupt, stack-as-a-Task, `WaitSelect`→`Wait()`) is **already solved here**.
- **Compile-ready:** the '94 GNUmakefile already targets `gcc-amigados`; no `ixemul`; only 7 files carry `__asm`/register-arg decls (the LVO entry points). Porting to modern **bebbo m68k-amigaos-gcc** is a gcc-2.x→gcc-6/10 jump, largely mechanical.

## Licensing (the constraints we must honour)
- **AmiTCP core = GPL v2** (`COPYING`), the Berkeley `net`/`netinet` under **4-clause BSD** (`COPYRIGHTS`; the advertising acknowledgement must appear in docs — Berkeley later rescinded clause 3). => **our derivative stays GPL/open.** That rules out any *closed commercial* stack (which would have meant the lwIP path instead).
- **Roadshow SDK** (`roadshow-ref/`): SFD + `roadshow.h` + `bsdsocket.doc` are published specs (fair to implement against). The config-tool source README grants *"reuse parts... but not take the code as a whole and claim it as your own"* — we may study/adapt, and they're separate programs from the library anyway.
- **SANA-II** headers/drivers = Commodore, "freely redistributable with notices."

## The work, scoped by the API gap analysis (SFD split at the first `==reserve 10`)
**48 STANDARD functions** (`socket`…`GetSocketEvents`) — AmiTCP already provides; **apps work today.**
**83 ROADSHOW EXTENSION functions** = the shim. By effort:
- **Trivial / wrappers (~30):** `mbuf_*` (11 — thin exports over AmiTCP's existing `m_*` in `uipc_mbuf.c`), `inet_aton`/`inet_ntop`/`inet_pton`, `In_LocalAddr`/`In_CanForward`, the `get{net,proto,serv}ent` iterators, `gethostby*_r` (wrap non-r), default-domain get/set.
- **Moderate (~12):** `getaddrinfo`/`getnameinfo`/`freeaddrinfo`/`gai_strerror` (RFC 3493), DNS-server list mgmt (`AddDomainNameServer`, `ObtainDomainNameServerList`…).
- **The real work (~25): config-management shim** — `AddInterfaceTagList`, `ConfigureInterfaceTagList`, `ObtainInterfaceList`, `QueryInterfaceTagList`, `RemoveInterface`, `AddRouteTagList`/`DeleteRouteTagList`/`GetRouteInfo`, `ObtainRoadshowData`/`ChangeRoadshowData`, `GetNetworkStatistics`, `AddNetMonitorHook`. These translate Roadshow's *tag-based* config into AmiTCP's *internal* interface/route ops (AmiTCP already does the ops — via `ifconfig`/`route` ioctls/routing sockets; the shim just wraps them tag-style).
- **DHCP (~7): the one genuinely new subsystem** — `CreateAddrAllocMessage`/`BeginInterfaceConfig`/`AbortInterfaceConfig`. AmiTCP has only BOOTP; Roadshow's DHCP client is the headline feature to add (a few hundred lines of DHCP state machine over UDP).
- **DEFER for v1 (~16):** `bpf_*` (packet capture / tcpdump), `ipf_*` (firewall). Stub the vectors, implement later.

**=> v1 shim ≈ 40–50 functions, most trivial/moderate; the meat is the config-mgmt translation + a DHCP client.** No protocol code to write.

## Toolchain
Native AmigaOS → **m68k-amigaos-gcc** (bebbo) + NDK + `amiga.lib`. See `docker/` scaffold. Provision options: build bebbo `amiga-gcc` in a Docker image (`make all`, ~1h, self-contained), or source a current prebuilt image. The first step was a compile-only smoke test of one `api/` file.

## Test strategy (a stack needs a network)
Compile-first, then runtime on a **networked emulated AmigaOS**: FS-UAE (Linux) or WinUAE with a Workbench 3.x install + a SANA-II driver bridged to the host (TAP/bridge, or the emulator's virtual net). MAME's Amiga networking is too limited. This harness is its own build-out; keep it *behind* the compile milestone.

## Roadmap (all done unless noted)
- **Toolchain up** — bebbo image; compile one file. **[done]**
- **It builds** — whole AmiTCP core compiles+links to a `bsdsocket.library` under bebbo (fix gcc-2.x→6/10 issues, register-arg syntax, NDK header diffs). **[done]**
- **It runs** — loads on emulated WB3.x, opens a SANA-II driver, ARP+ping over UDP/ICMP. **[done — self-starting drop-in library; real-network validated]**
- **DHCP** — add the DHCP client (replaces AmiTCP's BOOTP). **[done — full DORA lease]**
- **Roadshow-compat shim** — implement the extension vectors at exact SFD offsets; drive it with Roadshow's `AddNetInterface`/`Online`/`ShowNetStatus`. **[done — plus our own clean-room Roadshow-compatible command set and an Installer]**
- **Modernize** — `getaddrinfo`, fixes; optional `bpf_*`/`ipf_*`. **[getaddrinfo and `bpf_*` packet capture done; the `ipf_*` firewall deliberately deferred]**

## Layout
```
AmiTCP_NG/
  src/              forked AmiTCP 3.0b2 core (api kern net netinet sys protos conf + GNUmakefile)
  roadshow-ref/     Roadshow SDK specs: bsdsocket_lib.sfd, roadshow.h, sample config-tool source
  docs/             design notes
  COPYING COPYRIGHTS  AmiTCP GPL v2 + BSD attribution (kept with the fork)
  PLAN.md           this file
```
