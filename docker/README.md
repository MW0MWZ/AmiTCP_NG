# AmiTCP_NG — Docker build & test harness

Everything needed to **build** AmiTCP_NG and **run it on a real emulated Amiga**
lives here, in disposable Docker containers. You need Docker and nothing else on
the host — no cross-compiler, no emulator, no Amiga libraries installed locally.

There are three images, each with its own step-by-step guide:

| Image | Purpose | Guide |
|-------|---------|-------|
| `amigadev/crosstools:m68k-amigaos` | Cross-compile the stack (bebbo GCC 6.5, 68000) | [README.toolchain.md](README.toolchain.md) |
| `amitcp-ng-fsuae` | Boot AmigaOS 3.2 and run the **loopback / API** tests (no network hardware) | [README.fsuae.md](README.fsuae.md) |
| `amitcp-ng-amiberry` | Boot with an **emulated Ethernet NIC + SLIRP** and run the **real-network** tests | [README.amiberry.md](README.amiberry.md) |

This file covers the harness (the three images and how results come back). For
**building** — the stack, the drop-in library, the command-line tools, and rolling
your own `.lha`/`.adf` — and for **testing your own code**, see
**[../docs/BUILDING.md](../docs/BUILDING.md)**.

## TL;DR

```bash
# Build: the drop-in library, the full tool set, or a complete release
./docker/build-lib.sh          # -> build/bsdsocket.library
./docker/build-tools.sh        # -> build/Online, build/ping, ...
./docker/build-release.sh      # -> build/release/AmiTCP_NG-v<version>.lha  and  .adf
./docker/build.sh              # -> build/amitcp (the standalone stack program)

# Test: fast loopback / API tests on emulated WB3.2
TIMEOUT=95 ./docker/run-fsuae.sh

# Test: real-network (emulated A2065 over SLIRP, with DHCP server)
NET=1 TIMEOUT=150 ./docker/run-amiberry.sh
```

## What you must supply (licensed, never in this repo)

The emulators need Amiga system software that we cannot redistribute. Put your
own copies under `emu/` (the whole directory is git-ignored):

| Path | What | Where to get it |
|------|------|-----------------|
| `emu/rom/*.rom` | A Kickstart 3.1-class ROM (A600/A500 works) | Amiga Forever, or a dump of your own machine |
| `emu/hdd/System/Workbench3.2/` | An installed AmigaOS 3.2 Workbench (a directory the emulators mount as `SYS:`) | Your AmigaOS 3.2 install media |
| `emu/hdd/System/Workbench3.2/Devs/Networks/a2065.device` | The A2065 SANA-II driver (only for the Amiberry network harness) | Aminet `comm/net`, or the AmiTCP 3.0b2 distribution |

`emu/`, `ref/`, and every ROM/OS file are git-ignored on purpose. **Never commit
them.**

## How results come back (both emulators)

The emulators mount `emu/hdd/System/Workbench3.2` as the Amiga's boot volume
`SYS:` using a **directory hard drive** — so files the Amiga writes to `SYS:`
appear directly on your host. The boot `S/Startup-sequence` runs the stack and
the test programs, which write `*.log` files you read on the host:

```bash
tr -d '\r' < emu/hdd/System/Workbench3.2/exttest.log
```

(Amiga text is CR-terminated; `tr -d '\r'` makes it readable. Each guest-written
file gets a harmless `.uaem` metadata sidecar.)

## The two harnesses, and why there are two

`fs-uae` (the Debian package) is fast and perfect for the loopback / library-vector
tests — but it has **no emulated network card**, so it cannot exercise the SANA-II
driver, ARP, or DHCP. The `amiberry` image exists precisely to fill that gap: it
presents a real emulated **Commodore A2065** Ethernet card backed by **SLIRP**
user-mode NAT (which includes a DHCP server), so the whole lower half of the
stack runs for real. Keep using FS-UAE for the quick loop; reach for Amiberry
when you need packets on the wire.

See each image's guide for prerequisites, exact commands, expected output, and
troubleshooting.
