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

- **BSD sockets** — the full `bsdsocket.library` API (TCP, UDP, raw) on a drop-in
  Roadshow ABI, so existing Amiga software works unchanged, with no time limit.
- **Protocols** — TCP, UDP, ICMP, IP with routing and broadcast, ARP.
- **Interfaces** — any SANA-II device, plus software loopback (`lo0`, always on).
- **Addressing** — DHCP client, static, or **RFC 3927** link-local when no DHCP
  server answers.
- **Names** — DNS resolver (`gethostbyname`, `getaddrinfo`, the reentrant
  `gethostby*_r`), search domain, RAM-tiered cache.
- **Modern TCP** — **SACK** recovery (RFC 2018/6675, with D-SACK, PRR, NewReno),
  **RFC 1323** window scaling and timestamps, **RFC 6928** initial window, and
  header prediction that works while *sending*, not only receiving.
- **Asynchronous socket events** — `SO_EVENTMASK`, `SBTC_SIGEVENTMASK` and
  `GetSocketEvents()`: the AmiTCP V4 mechanism Roadshow-era clients expect.
- **Security-hardened** — randomised ISNs (**RFC 6528**), **RFC 5961**
  challenge-ACK limiting, broadcast and fragment DoS guards, and a
  whole-codebase memory-safety review.
- **Packet capture (BPF)** — filter, read or inject raw frames, and `PacketCapture`
  writes a **pcap file you can open in Wireshark**, so a failing network can be
  handed to someone who isn't sat at the Amiga.
- **Diagnostics that ship** — every build can log, off-screen by default;
  `LOGLEVEL=7` names any library call that fails and the errno it failed with.
  No debug build to obtain.
- **Roadshow-compatible tooling** — the extension API and capability flags, the
  full 25-command set (`Online`, `AddNetInterface`, `ShowNetStatus`, `ping`, …,
  plus `RoadshowControl` under the name Roadshow scripts expect), shared library
  bases, and an Amiga Installer.
- **Usable on its own** — `netstat`, `nslookup`, `ftp`, `tftp`, `sntp`, `arp`,
  `traceroute`, `PacketCapture`, `SampleNetSpeed` (live per-interface throughput),
  `NetLogViewer`, `CheckAmiTCPNGConfig` and `ManageNetInterfaces` ship with it, so
  a freshly installed Amiga can fetch everything else itself.
- **`usergroup.library`** — the user/group/account library Roadshow ships, for
  software that expects it. Reads Roadshow's own `DEVS:Internet/users` and
  `groups`; independent of the stack.
- **Self-tuning** — socket buffers, SANA-II rings, the mbuf pool and the DNS
  cache size themselves to installed RAM and link speed; per-interface and
  stack-wide overrides are honoured.

## Status

- Runs on **emulated AmigaOS 3.2** and **real 68k hardware** as a self-starting
  `LIBS:bsdsocket.library`.
- **Emulator-validated** (A2065 over SLIRP): DNS, a full DHCP lease, `ping`,
  same-host broadcast and loopback, the socket-event mechanism end to end, and
  BPF capture whose pcap files were checked by reading them back with `tcpdump`
  on the host.
- **Hardware-validated** (PiStorm + `wifipi.device`): bring-up, DHCP, routing,
  DNS and connectivity over 100 Mbit WiFi — roughly **56 Mbit down / 52 up**
  once the SANA-II transmit queue landed. **Amiga Explorer** works.
- **Roadshow-compatible** — name-, argument- and output-compatible, so Roadie,
  NetMon and existing scripts drive it unchanged.
- **Measured, not assumed** — the TCP work came from counters read off real
  hardware: header prediction was covering 98% of segments downloading and 9%
  uploading; the cause was the peer's moving window, and the fix took uploads
  to 57%.
- **Tunable at run time** — `AmiTCPControl` reads and changes the stack's
  internal options (`AmiTCPControl` on its own lists them all), the same command
  shape and option names Roadshow uses, so existing scripts work unchanged.
  `SAVE` keeps a setting across reboots. Options the stack sizes for itself from
  your machine's RAM, CPU and link speed accept a setting but keep the tuned
  value, saying so in the log — a script that sets them carries on instead of
  failing, and nobody is left wondering why their number is not the one in use.
- **Paced ARP** — the 4.3BSD base broadcast a fresh ARP request for *every*
  packet to an unresolved address, with no cap, and told the caller it had been
  sent. Now: one request a second, a bounded burst, then a hold-down that
  reports the host unreachable instead of transmitting into silence. A resolved
  entry is re-checked by **unicast** probe rather than trusted for twenty
  minutes, and ordinary inbound traffic confirms a peer is alive, so an active
  connection is never probed at all.
- **One build for every Amiga** — a single 68000 archive that picks its copy
  routine at run time from the CPU it finds itself on.
- **Deferred** — IP filter (`ipf_*`), monitor hooks, server API
  ([docs/DEFERRED-VECTORS.md](docs/DEFERRED-VECTORS.md)); IP multicast *receive*
  is not implemented.

## Installing

Grab the release `.lha` (or the `.adf` floppy image) and run its
`Install-AmiTCP_NG` Installer script on your Amiga.

**There is one archive, and it runs on every 68k Amiga.** There used to be
separate `-68020` and `-68040` builds; they were dropped because compiling for
those processors was proven in real-world testing not to make the stack any
faster, so the only thing the extra archives reliably did was let someone
install a build their machine could not run. The decisions that genuinely do
depend on the processor — which memory-copy routine to use — are made at run
time, inside this one binary. **How much it asks depends on
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

## Configuring your network from cold

AmiTCP_NG is configured the same way as Roadshow, so existing configurations work
unchanged. There are two pieces: a **per-interface** file that says which hardware
to use and how to get an address, and an optional **stack** file for global
settings. In most cases the interface file is all you need.

### 1. Describe your interface — `DEVS:NetInterfaces/<name>`

Create one text file per network interface. **The file's name is the interface
name** (so `DEVS:NetInterfaces/eth0` defines interface `eth0`). The simplest
possible file — get everything (address, netmask, router, DNS) from DHCP:

```
device=a2065.device
configure=dhcp
```

Or a fixed (static) address instead:

```
device=a2065.device
address=192.168.0.10
netmask=255.255.255.0
gateway=192.168.0.1
nameserver=192.168.0.1
```

Settings AmiTCP_NG acts on:

| Key                  | Meaning |
|----------------------|---------|
| `device=`            | **Required.** SANA-II driver. A bare name resolves to `DEVS:Networks/<name>`; a resident driver name (e.g. `wifipi.device`) is used directly. |
| `unit=`              | Device unit number (default `0`). |
| `configure=dhcp`     | Lease the address / netmask / router / DNS via DHCP. Omit for a static setup. |
| `address=`           | Static IPv4 address. |
| `netmask=`           | Static subnet mask. |
| `gateway=`           | Default-route gateway. |
| `nameserver=`        | A DNS server. Repeat the line for more than one. |
| `domain=`            | Default domain name. |
| `requiresinitdelay=yes` | Pause briefly after opening the device (some hardware needs a warm-up before it will configure). |

Roadshow keys that AmiTCP_NG does not act on (`iprequests`, `writerequests`,
`filter`, `configure=auto/fastauto`, `debug`) are accepted and ignored, so a
Roadshow interface file drops in without edits.

### 2. Bring it up

```
AddNetInterface eth0
```

`AddNetInterface` reads `DEVS:NetInterfaces/eth0` and brings the interface up —
running the DHCP handshake or applying the static address as the file dictates.
After that, `Online`/`Offline` toggle it, and `ShowNetStatus` reports the current
state. A full install also drops in a boot-time `S:Network-Startup` script that
does this for you at startup.

### 3. Stack-wide settings — `AmiTCP:db/AmiTCP.config`

Optional. One `NAME=VALUE` per line; `#` starts a comment. Read once when the stack
starts. The knobs worth knowing:

| Setting                | Meaning |
|------------------------|---------|
| `HOSTNAME=<name>`      | The host's own name — what `gethostname()` returns to applications. |
| `MBUFCHECK=ON`         | Detect double frees and use-after-free in the packet buffers. Off by default; a diagnostic for chasing memory corruption, not something to leave on. |
| `USENAMESERVER=SECOND` | DNS resolution order: `NO` (local hosts table only), `FIRST` (ask DNS first), `SECOND` (local table first, then DNS). |
| `GATEWAY=NO`           | Whether to forward IP between interfaces (act as a router). |
| `TCP_SENDSPACE=<bytes>`| TCP send-buffer size (overrides the auto-tuned default; see below). |
| `TCP_RECVSPACE=<bytes>`| TCP receive-buffer size (overrides the auto-tuned default). |
| `LOGGING=ON\|OFF`      | Keep a log at all. Default **ON** — a quiet file, nothing on screen. |
| `LOGLEVEL=0..7`        | How much. Default 5. **7** also logs every failing library call and its errno, which is how you find out what a program is unhappy with. |
| `LOGCONSOLE=ON\|OFF`   | Also throw the log at a console window. Default **OFF** — the window puts itself in front of whatever you are doing. |
| `LOGFILENAME=<path>`   | Default `RAM:AmiTCP.log`. Point it at a disk if you are chasing something that only clears with a reboot. |
| `CONSOLENAME=<path>`   | Where `LOGCONSOLE=ON` opens its window. Default `con:0/0/600/100/AmiTCPIP Log/AUTO/INACTIVE`. Point it at a file and you get a *second* log file, not a window — `CheckAmiTCPNGConfig` warns if you have. |

A minimal example:

```
HOSTNAME=my-amiga
```

`CheckAmiTCPNGConfig` reads all of this back and reports anything wrong, without
starting the stack or touching the hardware.

### 4. Host, service and protocol names

AmiTCP_NG reads its own `AmiTCP:db/netdb` **and** Roadshow's files if you have
them — `DEVS:Internet/hosts`, `networks`, `protocols` and `services` — so a
machine upgraded from Roadshow keeps everything it had. They add to what `netdb`
already defines rather than replacing it, and a missing file is perfectly normal.

### 5. It tunes itself to your machine and your link

You normally do **not** need to touch the buffer sizes. AmiTCP_NG sets the TCP window
automatically from two things, and uses the **smaller** of them:

- **Your RAM sets the ceiling.** The socket buffers are backed by an mbuf pool sized to
  installed RAM, so a small machine stays lean and a big one can afford a large window:

  | Installed RAM | Window ceiling |
  |---|---|
  | ≤ 1 MB (e.g. 512K A500) | ~16 KB (lean enough to still boot) |
  | 2–4 MB | ~61 KB |
  | 8–16 MB | ~128 KB |
  | 16–64 MB | ~256 KB |
  | 64–128 MB | ~512 KB |
  | 128 MB+ (PiStorm-class) | ~1 MB |

- **Your link speed sets the target.** The window that fills a link without overshooting
  is its bandwidth-delay product, so the stack reads each NIC's link speed and sizes the
  window to it — ~512 KB for a ~100 Mbit link, ~1 MB for gigabit — never above the RAM
  ceiling. A window *larger* than the link needs doesn't add throughput and hurts loss
  recovery, so on a big-RAM machine a 100 Mbit NIC is deliberately held to ~512 KB, not
  1 MB. (A driver that doesn't report its speed simply falls back to the RAM ceiling.)

RFC 1323 **window scaling** makes the >64 KB windows possible and is negotiated per
connection; **timestamps** are enabled on a 68020+ and left off on a bare 68000/68010
(where the per-segment cost is not worth it).

To override, set `tcp.sendspace=`/`tcp.recvspace=` in a `DEVS:NetInterfaces` interface
config (this is stack-wide — the last interface configured wins) or
`TCP_SENDSPACE=`/`TCP_RECVSPACE=` in `AmiTCP.config`. Run **`GetNetStatus DEBUG`** to see
the RAM the stack detected and the window it chose.

Two more stack-wide TCP tunables use the same mechanism (`tcp.<name>=` in an interface
config, or the upper-case `<NAME>=` in `AmiTCP.config`): **`tcp.mssdflt`** /
`TCP_MSSDFLT` — the off-subnet MSS cap in bytes (`0` = auto: interface MTU − 40); and
**`tcp.iw`** / `TCP_INITIALWINDOW` — the initial congestion window in segments (`10` =
RFC 6928 default, `4` = RFC 3390, `1` = legacy single-segment slow-start).

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

# LIBS:usergroup.library  ->  build/usergroup.library
./docker/build-usergroup.sh

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
| `src/usergroup/` | `LIBS:usergroup.library` — the user/group/account library Roadshow ships. Independent of the stack. |
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
