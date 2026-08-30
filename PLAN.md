# AmiTCP_NG — plan and direction

> **What this file is.** [README.md](README.md) describes what the stack does
> today and how to install and configure it. This file is the other half: why it
> is built the way it is, what is deliberately not built, and what is next. The
> project is a rolling release, so this document is kept current rather than
> preserved as a historical plan.
>
> Build and test instructions: [docs/BUILDING.md](docs/BUILDING.md).
> Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Goal

A `bsdsocket.library`-compatible TCP/IP stack for stock 68k AmigaOS (2.04+), with
**no time limit** and **open source**, that:

1. runs every existing Amiga network application unchanged (standard socket API), and
2. is **driven by Roadshow's own configuration tools** — `AddNetInterface`,
   `Online`, `ShowNetStatus` and friends — by implementing Roadshow's
   config-management extension API.

Built by forking **AmiTCP 3.0b2** — the original source-available BSD-derived
stack that defined the `bsdsocket.library` API — and adding a Roadshow-extensions
shim. **No Roadshow binaries are touched, patched or cracked.** The stack is
replaced with our own; only published SDK *specifications* and *reference tool
source* are consulted.

## Why this approach works

- **`bsdsocket.library` IS the stack.** Replacing that one file replaces the
  networking, with Roadshow simply not present. Clean, and legal.
- **AmiTCP 3.0b2 is a complete working stack**, not a skeleton — roughly 34K
  lines covering the library (`api/`), the BSD socket and mbuf core (`kern/`),
  link/route/SANA-II glue (`net/`) and full IPv4/TCP/UDP/ICMP (`netinet/`). Every
  hard AmigaOS integration problem was already solved there: the SANA-II shim,
  mbuf pools that avoid allocating at interrupt time, the stack as a Task, and
  `WaitSelect` mapped onto `Wait()`.
- **It was compile-ready.** The 1994 makefile already targeted a gcc-flavoured
  toolchain with no `ixemul`, and only seven files carried register-argument
  declarations. Porting to a modern cross-compiler was largely mechanical; the
  specifics are in [PORTING.md](PORTING.md).

So the work was never "write a TCP/IP stack". It was: port a good one, add the
Roadshow-compatible configuration surface, write a DHCP client, and then
modernise the protocol behaviour.

## Licensing constraints

These bind every change and are not negotiable:

- **AmiTCP core is GPL v2** (`COPYING`); the Berkeley `net`/`netinet` code is
  under the 4-clause BSD licence (`COPYRIGHTS` — the advertising acknowledgement
  must appear in documentation; Berkeley later rescinded clause 3). **The
  derivative stays GPL and open.**
- **The Roadshow SDK is reference only.** The SFD, `roadshow.h` and
  `bsdsocket.doc` are published specifications and fair to implement against. The
  sample tool source may be studied, not lifted wholesale. Our command set is a
  clean-room reimplementation that matches names, arguments and output.
- **SANA-II** headers and drivers are Commodore's, freely redistributable with
  their notices intact.
- The repository contains **no licensed Amiga assets** — no Kickstart ROMs, no
  Workbench images. The emulator directories that hold them are ignored by git.

## The extension API surface

The library exposes Roadshow's full vector table at the exact SFD offsets. Of
that surface:

- The **48 standard functions** (`socket` … `GetSocketEvents`) come from AmiTCP,
  so ordinary applications work as they always did.
- The **Roadshow extensions** are the shim. Address conversion, DNS server
  management, `getaddrinfo`/`getnameinfo`, the reentrant `gethostby*_r` family,
  the interface and routing configuration families, network statistics,
  `ObtainRoadshowData`, BPF packet capture and the DHCP client are all
  implemented and advertised through the `SBTC_HAVE_*` capability tags.
- Anything not implemented returns `ENOSYS` from a shared stub rather than
  occupying an empty vector, so a caller gets a clean failure instead of a jump
  into nothing.

**Deliberately not implemented**, with reasoning, in
[docs/DEFERRED-VECTORS.md](docs/DEFERRED-VECTORS.md): the `ipf_*` IP filter
(7 vectors), the network-monitor hooks (2), the server API (2), and
`ChangeRouteTagList`, which is private and unimplemented in Roadshow itself.

**Known gap:** IP multicast *receive* is not implemented — there is no
`IP_ADD_MEMBERSHIP`, no IGMP and no `S2_ADDMULTICASTADDRESS`. Transmit works
incidentally. This is a tracked gap rather than a deferral.

## Toolchain

**bebbo's `m68k-amigaos-gcc`, via the prebuilt `amigadev/crosstools` Docker
image.** Everything builds in a container; nothing is installed on the host.

Do not plan on building the cross-compiler from source: the original
`bebbo/amiga-gcc` repository no longer resolves and the community Docker images
that wrapped it have gone. The prebuilt image is the supported route. All build
entry points live in `docker/` and are documented in
[docs/BUILDING.md](docs/BUILDING.md).

Three CPU targets ship: portable **68000**, plus **68020** and **68040**.

## Testing

A stack needs a network, so testing is done on an emulated AmigaOS with a real
SANA-II driver attached.

**Amiberry with its A2065 bridged to SLIRP** is the working harness. Plain FS-UAE
has no network card at all, so it can only exercise loopback and the library
vectors; it is still used for the checksum benchmark. Under SLIRP the guest gets
a genuine DHCP lease, DNS, and ICMP to the gateway, which is enough to validate
bring-up end to end.

`docker/run-smoke.sh` runs the suite in **tiers**, and the order is the point:

1. **Gate — the 68000 build on an A600**, loopback only: library vectors,
   first-touch start-up, UDP round trip, configuration checking. Cheap and broad.
   If this fails, nothing else runs, because a stack that cannot complete a
   loopback round trip has nothing useful to say about DHCP.
2. **Then the 68020 and 68040 builds on an A4000**, with DHCP, link-speed
   auto-tuning, `ping` and interface teardown. RAM is set mid-ladder so the
   window auto-tune has to clamp against the RAM ceiling rather than one side
   trivially winning, and the interface declares a 100 Mbit link so the tuning
   path real hardware takes is the one under test.

**Every CPU target is tested on every run.** This is not thoroughness for its own
sake: a fault once existed only in the 68040 build while the harness ran the
68000 one, and it survived for weeks precisely because "the emulator cannot
reproduce it" was true and misleading at the same time.

Two limits worth knowing. The emulator proxies TCP through host sockets, so the
guest's own SYN options cannot be observed on the wire — verifying those is a
real-hardware job. And anything about *having an address* passes trivially on a
loopback-only machine, because "no address" is also what a stack that never came
up reports; those tests belong on the networked tier.

## What is next

- **IP multicast receive**, if there is demand for it.
- **PPP.** Roadshow ships serial and Ethernet PPP devices; whether AmiTCP_NG
  grows an equivalent is an open decision, not a commitment.
- **The install / uninstall / reinstall cycle** has been reasoned about carefully
  and checked mechanically, but never driven end to end by a person on real
  hardware. It should be.

## Layout

```
AmiTCP_NG/
  src/            the stack: api/ kern/ net/ netinet/ sys/ lib/ netinclude/
  src/tools/      the Roadshow-compatible command set (25 commands)
  src/usergroup/  LIBS:usergroup.library, independent of the stack
  install/        Amiga Installer script, ReadMe, network database, examples
  docker/         build and test harness: Dockerfiles, build scripts, smoketest
  docs/           BUILDING, ARCHITECTURE, COMMENTING, DEFERRED-VECTORS, REVIEW_FINDINGS
  PLAN.md         this file — direction and rationale
  PORTING.md      what the 1994 → modern gcc port actually required
  README.md       what it does, and how to install and configure it
  COPYING         GPL v2
  COPYRIGHTS      the retained original AmiTCP/IP and Berkeley notices
```

Not in the repository, and ignored by git: `emu/` (emulator system files,
including licensed ROMs and Workbench images), `ref/` and `roadshow-ref/`
(vendor SDKs kept locally for reference), `build/` and `tmp/`.
