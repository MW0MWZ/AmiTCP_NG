# Building and testing AmiTCP_NG

Everything builds and runs inside disposable Docker containers. You need **Docker
on the host and nothing else** — no cross-compiler, no emulator, no Amiga
libraries installed locally. Every script here lives in `docker/` and can be run
from the repository root.

- [The build scripts at a glance](#the-build-scripts-at-a-glance)
- [Building the stack, library, and tools](#building-the-stack-library-and-tools)
- [Compiling one tool (or your own program)](#compiling-one-tool-or-your-own-program)
- [Making your own release: `.lha` and `.adf`](#making-your-own-release-lha-and-adf)
- [Testing your code on a real emulated Amiga](#testing-your-code-on-a-real-emulated-amiga)

All build output lands under `build/`, which is **not committed** — the repo is
source-only. Compiled binaries are release artifacts, never checked in.

---

## The build scripts at a glance

| Script | Builds | Output |
|--------|--------|--------|
| `docker/build.sh` | The stack as a **program** (compiles every object file, links `amitcp`) | `build/amitcp`, `build/obj/*.o` |
| `docker/build-lib.sh` | The self-starting **drop-in library** | `build/bsdsocket.library` |
| `docker/build-tools.sh` | All 11 **command-line tools** (Online, Offline, AddNetInterface, …, ping) | `build/<ToolName>` |
| `docker/build-release.sh` | A full **installable release** (library + tools + Installer + configs), packed | `build/release/AmiTCP_NG-v<version>.lha` and `build/release/AmiTCP_NG-v<version>.adf` |
| `docker/cc.sh <cmd>` | Generic **toolchain wrapper** — runs any command in the cross-compiler container | (whatever you tell it) |

`build-lib.sh` calls `build.sh` first (it reuses the object files), and
`build-release.sh` calls both `build-lib.sh` and `build-tools.sh`. So to produce
a complete release from a clean tree, `build-release.sh` alone is enough.

The toolchain is **bebbo's `m68k-amigaos-gcc` 6.5** (the `amigadev/crosstools`
image). Everything is a plain **MC68000** build, so it runs on every Amiga from a
stock A500 up.

---

## Building the stack, library, and tools

```bash
# The stack program (installs bsdsocket.library at runtime when run on the Amiga)
./docker/build.sh                 #  -> build/amitcp

# The self-starting drop-in LIBS:bsdsocket.library (the recommended form)
./docker/build-lib.sh             #  -> build/bsdsocket.library

# The full Roadshow-compatible command set
./docker/build-tools.sh           #  -> build/Online build/Offline build/AddNetInterface ...

# LIBS:usergroup.library — the user/group/account library (independent of the stack)
./docker/build-usergroup.sh       #  -> build/usergroup.library
```

### `usergroup.library`

Roadshow ships `LIBS:usergroup.library`; AmiTCP_NG historically shipped only its
*headers*, so installing our stack over Roadshow took that library away from any
software using it. `docker/build-usergroup.sh` builds our own.

It shares no code with the stack (bar `src/api/amiga_errlists.c`, the errno
strings `ug_StrError()` returns) and does not need the stack running — it is a
standalone library about accounts, not networking.

The ABI is not guesswork: our `fd/usergroup_lib.fd` and Roadshow's SFD list the
same 39 functions in the same order from bias 30, and three independent sources
agree on every offset (the FD, the gcc inline stubs in
`netinclude/inline/usergroup.h`, and Roadshow's pragmas).

The account data comes from `DEVS:Internet/users` and `DEVS:Internet/groups` —
Roadshow's own files, in AmigaDOS `ReadArgs` format, not Unix colon-separated
ones:

```
users    NAME/A,PASSWORD/K,UID/A/N,GID/A/N,GECOS,DIR,SHELL      e.g.  NAME=root UID=0 GID=0
groups   NAME/A,ID/A/N,USERS/M                                  e.g.  NAME=wheel ID=0 USERS=root
```

Either file may be missing, in which case the built-in `root`/`nobody` and
`wheel`/`nogroup` entries are used — the same ones Roadshow ships.

**Six vectors, in two groups, are deliberately not implemented**, and both groups match what Roadshow's
own 4.31 binary appears to do:

- `crypt()` — the 4.3BSD DES password hash. Reproducing DES from memory risks a
  function that computes confident *wrong* hashes and rejects every correct
  password, so it is not attempted. It returns `"*"` (the traditional "cannot log
  in" marker) and sets `ENOSYS`: a valid string that can never match a stored
  hash, so password checks fail closed rather than crash on a `NULL`.
- `utmp` / `lastlog` — the login-session databases. They need a login program
  maintaining them; nothing here writes one, so `getutent()` reports an empty
  database and `setlastlog()` returns `ENOSYS` rather than pretending to record.

## The network databases

The stack loads `AmiTCP:db/netdb` — AmiTCP's single tagged file, carrying `H`ost,
`N`et, `S`ervice and `P`rotocol records — and then the Roadshow-style files in
`DEVS:Internet`:

```
DEVS:Internet/hosts       address name [aliases...]
DEVS:Internet/networks    name address [aliases...]
DEVS:Internet/protocols   name number [aliases...]
DEVS:Internet/services    name port/proto [aliases...]
```

Reading these closes a compatibility gap that lost real configuration: install
over Roadshow and every services, protocols and networks entry you had ever
customised silently stopped existing. It also fixes something worse and closer to
home — we *installed* a `DEVS:Internet/hosts` file, and `CheckAmiTCPNGConfig`
validated it, while nothing whatsoever read it. Adding a host there did nothing
at all, and our own checker said the file was fine.

No new parser was needed. `read_netdb()` could already parse a file whose lines
are one record type with the leading letter omitted (that is what
`WITH file PREFIX SERVICE` does), and the per-record formats turned out to be the
ones Roadshow already writes. The one difference was the separator: Roadshow
accepts `80,tcp` as well as `80/tcp`, so `addservent()` now takes both.

**Order is precedence, and it is deliberate.** Lookups return the first match, so
these files are read *after* `netdb`, making them purely additive: no name that
resolves today starts resolving differently, and the only change is that names
which previously did not resolve at all now do. On a stack people already run,
"adds entries" is a much safer promise than "may quietly change which port a
service means". A missing file is the normal case and is silent; a file that *is*
read logs one line saying how many entries came out of it, which is the thing
that answers "did it pick up my file?".

One thing worth knowing: network numbers go through `inet_aton()`, whose one-part
rule is that the whole value *is* the address — so `127` yields `0.0.0.127`, not
the `127.0.0.0` someone thinking in network numbers would expect. That predates
this change and applies equally to `netdb`'s own `N loopback 127`; write the
address in full if it matters to you.

`build-tools.sh` builds, and lists with sizes:

```
Online, Offline, AddNetInterface, ConfigureNetInterface, RemoveNetInterface,
NetShutdown, AddNetRoute, DeleteNetRoute, GetNetStatus, ShowNetStatus,
AmiTCPControl, arp, SampleNetSpeed, traceroute, CheckAmiTCPNGConfig,
NetLogViewer, ManageNetInterfaces, PacketCapture, ping, netstat, tftp,
nslookup, ftp, sntp
```

(`rxprofile` is built too, but ships only in `-beta` releases — see
`build-release.sh`.)

`PacketCapture` captures frames off an interface through the library's Berkeley
Packet Filter vectors, prints a line per packet, and — the part that matters —
writes a **libpcap file** that opens in Wireshark or `tcpdump -r`.

```
PacketCapture INTERFACE [FILE pcap] [COUNT n] [SNAPLEN n]
              [PROMISC] [QUIET]
```

The file is the point. Diagnosing a network at a distance has so far meant
working from a description of the symptom; a capture is the network itself. It
is written big-endian, which needs no byte swapping on a 68k and none at the far
end either — pcap's magic number is exactly what tells a reader which way round
the file is.

Timestamps are converted from Amiga system time (epoch 1 Jan 1978) to Unix time
on the way out. Without that every capture would open dated eight years early
and could not be lined up against a capture taken anywhere else.

Ethernet interfaces capture as `DLT_EN10MB`; `lo0` captures as `DLT_NULL`, whose
"link layer" is a four-byte address family. That loopback tap did not exist
until this tool went looking for it — `bpf_dlt_of()` already reported `DLT_NULL`
for `IFT_LOOP`, but nothing in `if_loop.c` ever called the tap, so a capture on
`lo0` was silently always empty. It is now taken once in `looutput()`, before the
packet is enqueued and while it is still ours to read.

`SNAPLEN` keeps only the first N bytes of each packet, which is how you capture
a long run of traffic onto an Amiga-sized disk when the headers are all you
need. There is no "capture N bytes" ioctl to call: the number of bytes kept is
whatever the channel's *filter* returns, and a channel with no filter keeps
whole frames — so `SNAPLEN` is expressed as a one-instruction filter program,
`BPF_RET | BPF_K`. Omitting it installs no filter at all and captures
everything. If the library refuses the filter, the tool says so and records the
real (untruncated) length in the file header rather than letting the file claim
a truncation that never happened.

Stop with Ctrl-C. That arrives as `EINTR` out of the blocking read rather than
as a signal the tool gets to poll, and is treated as a normal stop, not a
failure. The closing line reports how many packets the filter matched and how
many the library had to drop — a non-zero drop count means the capture has
holes in it. A failed write is reported too, and exits non-zero: a truncated
pcap that is announced as written gets analysed anyway.

`ManageNetInterfaces` keeps `DEVS:NetInterfaces` down to the interfaces this
machine can actually bring up, parking the rest in `SYS:Storage/NetInterfaces`.

```
ManageNetInterfaces [INSTALL|CLEANUP|SYNC] [CHECK] [COMMIT]
                    [QUIET] [VERBOSE] [IGNOREDEVICES "dev,dev"]
```

The test is a real `OpenDevice()`, immediately closed with no command sent — not
"does the driver file exist". The case it exists for is a driver that is present
but unusable: `a2065.device` in `DEVS:Networks` on a machine with no Zorro bus
opens never and exists always, and a config left active for it makes every boot
try and fail. Nothing is put online and no packet moves.

**It changes nothing unless you say `COMMIT`.** With neither switch it reports
what it would do and stops, which is the right default for a tool whose job is
moving your configuration files. `CHECK` is the explicit spelling of that.
`IGNOREDEVICES` protects named drivers from being parked — useful for hardware
that is genuinely absent right now but will be back (a PCMCIA card, a docked
adaptor).

`SYNC` runs CLEANUP before INSTALL, so parking a dead interface frees its name
before a parked config that wants it is promoted.

`NetLogViewer` watches the stack's log as it happens, with scrollback.

```
NetLogViewer [FILE name] [LINES n] [FILTER text] [LEVEL n] [LIVE]
             [LEFT n] [TOP n] [WIDTH n] [HEIGHT n]
```

**Finding the log.** With no `FILE`, it works out where the stack is actually
writing rather than assuming: `LOGFILENAME=` from `AmiTCP:db/AmiTCP.config`, then
each of the destinations the stack falls back through (`ram:`, `t:`, `AmiTCP:`,
`sys:`), taking the first that exists. `LIVE` instead asks the running stack
(`SBTC_LOG_FILE_NAME`), which is the only fully authoritative answer because it
reports the destination *in use* including any fallback — at the cost of opening
`bsdsocket.library`, which starts the whole stack if it is not already up. That
is why it is opt-in and not the default.

If there is nothing to show it says why in the pane — logging switched off, no
file yet, or an empty file — rather than presenting a blank window that looks
identical to a broken one.

`LOGCONSOLE=ON` shows log messages live and `LOGFILENAME=` writes a file any
editor opens. **`LOGCONSOLE` is not superseded and is not going away**: it needs
nothing running, opens itself on the first message, and is therefore what you
want on a machine that is mid-failure — which is exactly why the console and the
file were split into separate switches in the first place. NetLogViewer is the
better tool when you are *looking* at a problem rather than being surprised by
one. What neither of the old destinations gives you is going *back*: the console shows what
just happened, and the file must be reopened to see what has been added. This
keeps a scrollback ring (`LINES`, default 500 — fixed capacity on purpose, so a
viewer cannot be the thing that finally exhausts a 2 MB machine), follows the
file as it grows, and filters it.

Keys: `0`-`7` set the severity floor using the same numbering as `LOGLEVEL`, `8`
shows everything, `F` freezes following, `C` clears, `ESC` clears both filters,
`Q` quits. The box at the bottom searches. Level filtering reads the `[warn ]`
field the logger writes, so a line with no level (the "log opened" banner) is
always kept — hiding it would make the log look like it had gaps.

The text pane is a BOOPSI gadget class of its own (`src/tools/nlv_class.c`) so
the scroller can drive it through `ICA_TARGET` and redraw with the application
still asleep in `Wait()`. Row height and truncation come from the screen's font
via `DrawInfo`, so it follows a big-font screen.

`CheckAmiTCPNGConfig` reads every configuration file the stack uses and reports
what is wrong with it — our equivalent of Roadshow's `CheckRoadshowConfig`.

```
CheckAmiTCPNGConfig [QUIET] [VERBOSE]
```

It exists because almost nothing here is rejected at boot. An unknown keyword in
a `DEVS:NetInterfaces/` file is ignored *on purpose* so files written for a newer
version still work — which also means `adress=192.168.0.10` configures nothing
and says nothing. The checker names those. It catches a missing `device=`, a
malformed address, an interface with neither an address nor DHCP, a
`configure=auto` copied from a Roadshow config (that value is **not**
implemented here — only `dhcp` is, with link-local as its no-server fallback), a
`hosts` file with no `localhost`, and saved settings that are not numbers.

**It does not touch the machine.** No device is probed and
`bsdsocket.library` is deliberately not opened — our library starts the whole
stack on the first `OpenLibrary()`, and "check my configuration" must not bring
the network up as a side effect. Exit code is 0 clean, 5 warnings, 10 errors, so
it can gate a script.

`traceroute` walks the path with ICMP echo probes of increasing TTL, reading the
ICMP "time exceeded" replies each router sends back.

```
traceroute HOST [MAXHOPS n] [PROBES n] [TIMEOUT n] [LOOKUP] [QUIET]
```

Reverse DNS is **off** by default (`LOOKUP` turns it on), unlike Unix
traceroute: a hop with no PTR record costs a full resolver timeout, and thirty
of those makes the tool unusable on exactly the networks where you reached for
it. Addresses print as they arrive; names are opt-in.

Needs `setsockopt(IPPROTO_IP, IP_TTL)` on a raw socket, which this stack did not
support until it was written — see the note in `src/netinet/raw_ip.c` for why the
option is handled there rather than delegated to `ip_ctloutput()`.

`SampleNetSpeed` differences the per-interface byte counters
(`IFQ_GetBytesIn`/`Out`, the same ones `ShowNetStatus` reports) between samples —
there is no rate anywhere in the stack to read, because a rate is a difference
over time.

```
SampleNetSpeed [interface] [LEFT n] [TOP n] [WIDTH n] [HEIGHT n] [SCREEN name]
               [CLI] [INTERVAL n] [COUNT n]
```

Default is an Intuition window: received drawn above the centre line, sent
below, both on one shared scale (that shared scale is the point — it is what
makes the two comparable by eye), newest sample at the right. The live figures
are in the title bar, because a shape with no scale on it is not a measurement.
`CLI` gives the same sampling as text, which is what you want over a serial
console or in a script. With no interface named, the graph sums all interfaces
and the text mode lists them one per row.

`COUNT` applies to both displays, so the window can be told to take *n* samples
and exit — which is the only way the graphical mode can be tested without a
human to click the close gadget.

> **A note on silent errors.** `build-lib.sh` captures the compile step and only
> succeeds if every object built, so a hidden compile error can't let a *stale*
> library ship. If a build fails it prints the compiler output and stops; trust a
> green run.

---

## Compiling one tool (or your own program)

Use `docker/cc.sh` to run the cross-compiler directly. It executes inside the
container with the **repository root as the working directory**, so every path
you pass must be **repo-relative** (`src/tools/foo.c`, `build/foo`) — *not* a
bare filename from inside a subdirectory.

```bash
# One of our tools (the route/interface/status tools share src/tools/ng_lvo.h,
# so add -Isrc/tools):
./docker/cc.sh m68k-amigaos-gcc -noixemul -O2 -m68000 -Wall -Isrc/tools \
    src/tools/ShowNetStatus.c -o build/ShowNetStatus

# Online and Offline are one source selected by a -D:
./docker/cc.sh m68k-amigaos-gcc -noixemul -O2 -m68000 -DDO_ONLINE \
    src/tools/netonoff.c -o build/Online

# Your own bsdsocket.library client — the exact command used for the guest test
# programs under tmp/:
./docker/cc.sh m68k-amigaos-gcc -noixemul -O2 -m68000 \
    tmp/mytest.c -o build/mytest
```

`-noixemul` links against the Amiga-native C runtime (no `ixemul.library`
dependency); `-m68000` targets the base CPU. The tools reach `bsdsocket.library`
through inline register-argument LVO calls (see `src/tools/ng_lvo.h`) rather than
`net.lib`, so they stay self-contained.

---

## Making your own release: `.lha` and `.adf`

One command builds everything and packs it two ways:

```bash
./docker/build-release.sh
```

It builds the library and all tools, stages the Installer tree, generates
Workbench `.info` icons, and produces:

| Output | What it is |
|--------|------------|
| `build/release/AmiTCP_NG-v<version>.lha` | The Amiga-standard archive — unpack anywhere and run the Installer inside. |
| `build/release/AmiTCP_NG-v<version>.adf` | An 880 K DD floppy image of the same tree (FFS, volume `AmiTCP_NG`) — for real floppies or a Gotek. It is a plain data disk, not bootable. |
| `build/release/AmiTCP_NG/` | The staged tree itself, if you want to inspect or repackage it. |

Both carry the `Install-AmiTCP_NG` Installer script (whose icon's Default Tool is
`Installer`, so a double-click runs it), a `ReadMe`, the `COPYING`/`COPYRIGHTS`
licences, and a `data/` payload (the library, every command, the `S:Network-Startup`
script, example interface configs, and the network database).

The `.lha` is packed with the toolchain image's `lha` (the host's is often
extract-only); the `.adf` is written with `xdftool` from the fs-uae image. You
need both images pulled (they are pulled automatically the first time you build
or run a harness).

---

## Testing your code on a real emulated Amiga

There are two harnesses. Both mount `emu/hdd/System/Workbench3.2/` as the Amiga's
boot volume `SYS:` using a **directory hard drive**, so files the guest writes to
`SYS:` appear directly on your host.

| Harness | Network | Use it for |
|---------|---------|------------|
| `docker/run-fsuae.sh` | none | Fast loopback / library-vector tests |
| `docker/run-amiberry.sh` | emulated **A2065 + SLIRP** (with DHCP) when `NET=1` | Real packets: DHCP, DNS, TCP/UDP, ICMP to the gateway |

```bash
TIMEOUT=95  ./docker/run-fsuae.sh              # loopback / API only
NET=1 TIMEOUT=150 ./docker/run-amiberry.sh     # with the A2065 NIC + SLIRP
```

`TIMEOUT` (seconds) is how long the guest runs before it is killed; `NET=1`
attaches the NIC (Amiberry only). You must supply your own licensed Kickstart ROM
and AmigaOS 3.2 install under `emu/` (git-ignored, never committed) — see
[docker/README.md](../docker/README.md).

**SLIRP addressing** (Amiberry `NET=1`): the guest leases **10.0.2.15** by DHCP;
gateway **10.0.2.2**, DNS **10.0.2.3**. TCP and UDP work; ICMP echo works to the
gateway (10.0.2.2) and to loopback (127.0.0.1); ICMP is not routed to the wider
internet.

### The loop: run your own program

The guest runs its `S/Startup-sequence`; put your program there and read back the
log it writes. The whole cycle:

```bash
G=emu/hdd/System/Workbench3.2

# 1. Build your program and drop it in the guest's C: (which is SYS:C).
./docker/cc.sh m68k-amigaos-gcc -noixemul -O2 -m68000 tmp/mytest.c -o build/mytest
cp build/mytest "$G/C/mytest"

# 2. Make sure the drop-in library is the one you want to test.
./docker/build-lib.sh
cp build/bsdsocket.library "$G/Libs/bsdsocket.library"

# 3. Write a Startup-sequence: bring an interface up, run your test, log the output.
cat > "$G/S/Startup-sequence" <<'EOF'
C:SetPatch QUIET
C:AddNetInterface DEVS:NetInterfaces/eth0
Wait 3
C:mytest >SYS:mytest.log
Echo rc=$RC >>SYS:mytest.log
EOF

# 4. Run the networked harness.
NET=1 TIMEOUT=120 ./docker/run-amiberry.sh

# 5. Read the result (Amiga text is CR-terminated; strip the CRs).
tr -d '\r' < "$G/mytest.log"
```

For a networked test you need an interface config in the guest's
`DEVS:NetInterfaces/` — the simplest is DHCP over the A2065:

```bash
printf 'device=a2065.device\nconfigure=dhcp\nrequiresinitdelay=no\n' \
    > emu/hdd/System/Workbench3.2/Devs/NetInterfaces/eth0
```

The stack **self-starts on the first library call**, so a program that just opens
`bsdsocket.library` and makes a socket brings the whole stack up on its own — you
do not need to run `amitcp` first. `AddNetInterface` above is only needed to bring
a *hardware* interface up; loopback (127.0.0.1) works with no interface configured
at all.

### Tips

- Guest-written files get a harmless `.uaem` metadata sidecar — ignore it.
- If a run finishes with an empty log, the program was probably still running
  (or its buffered output was not flushed) when the guest was killed. Raise
  `TIMEOUT`, and for long-running programs flush as you go — e.g. call
  `Flush(Output())` after each line (this is exactly what `ping` does).
- Keep example test programs under `tmp/*.c` (their compiled binaries are not
  committed). Existing ones — `netprobe.c` (a stack-agnostic DNS probe),
  `qnet.c` (interface status query), `udptest.c` (loopback UDP) — are good
  starting templates.

## Throughput and memory

A single TCP connection can move at most about one send/receive buffer per
round-trip time, so the socket-buffer sizes set the throughput ceiling. With
**RFC 1323 window scaling** (`tcp_do_rfc1323`, on by default) a window can exceed
the old 16-bit 64 KB limit, so a single connection can scale past the 64 KB /
round-trip-time wall on machines with the RAM to back it. Its companion,
**RFC 1323 timestamps**, is documented under *[RFC 1323 timestamps](#rfc-1323-timestamps-cpu-adaptive)*
below; both are negotiated per connection and degrade cleanly against peers that
don't offer them.

### RAM sets the ceiling, link speed sets the target (automatic)

At start-up the stack reads the machine's installed RAM — by summing the exec
memory list directly, which is portable across every Kickstart (unlike
`AvailMem(MEMF_TOTAL)`, whose flag is missing on pre-2.0 Kickstarts) and agrees
with what `avail` reports — and picks a
tier, so it does not have to ship one compromise that either starves a 512 KB A500
or throttles a big-RAM PiStorm. The big-RAM tiers raise both the socket buffers and
the internal `sb_max` window ceiling above 64 KB, which is what makes window scaling
compute a nonzero shift:

| Installed RAM | mbuf pool (`MAXMEM`) | `sb_max` | RAM window ceiling | window scale |
|---|---|---|---|---|
| ≤ 1 MB    | 128 KB  | 64 KB  | 16060 bytes       | 0 (≤ 64 KB) |
| 2–4 MB    | 256 KB  | 64 KB  | 61320 bytes       | 0 (≤ 64 KB) |
| 8–16 MB   | 1 MB    | 256 KB | 131400 (~128 KB)  | 2 |
| 16–64 MB  | 4 MB    | 512 KB | 262800 (~256 KB)  | 3 |
| 64–128 MB | 8 MB    | 1 MB   | 524140 (~512 KB)  | 3 |
| 128 MB+   | 16 MB   | 2 MB   | 1048280 (~1 MB)   | 4 |

Buffers are MSS-aligned (`n × 1460`) and stay under the socket-buffer reservation
cap, so `soreserve()` never fails. The two smaller tiers still *negotiate* scaling
but with a shift of 0 — fully interoperable, and they can still **send** into a large
scaled window a peer advertises; they just don't advertise a >64 KB receive window.

That RAM figure is only the **ceiling**. The window that actually fills a link
without overshooting is its bandwidth-delay product, so when an interface comes up
the stack reads its link speed (`if_baudrate`, from the SANA-II `S2_DEVICEQUERY`)
and sizes the default window to the link — clamped down to the RAM ceiling above:

| Link speed | window target |
|---|---|
| ≤ ~10 Mbit (A2065, PLIP) | 64 KB |
| ~100 Mbit (wifipi, X-Surf-100) | 512 KB |
| gigabit (genet) | 1 MB |

So a 100 Mbit NIC on a 349 MB PiStorm gets a **512 KB** window, not the tier's 1 MB:
a window larger than the link's BDP adds no throughput and worsens loss recovery. A
driver that reports baud 0 falls back to the RAM ceiling. This is one global default
(last interface configured wins); a per-interface `tcp.sendspace`/`tcp.recvspace`
override always beats it, and `GetNetStatus DEBUG` prints the detected RAM, the tier
ceiling, and the live window.

### Overriding the tier

The tier only sets **defaults**; explicit configuration always wins, because the
detection runs before the config file is read. Set `tcp.sendspace` /
`tcp.recvspace` (RoadshowData nodes, e.g. via `AmiTCPControl`) or `MBUF_CONF`'s
`MAXMEM` to override — and `AmiTCPControl GET tcp.sendspace` also lets you *read*
back which value is active, to confirm the tier.

### Why the pool ceiling matters

The buffers are only a ceiling — the bytes that fill them come from the shared
mbuf pool (`MAXMEM`). A bulk connection with both buffers full uses roughly twice
its buffer size of pool: ~32 KB per connection on the ≤ 1 MB tier (16060-byte
buffers), ~120 KB on the 2–4 MB tier (61320), ~260 KB on the 8–16 MB tier
(131400), ~520 KB on the 16–64 MB tier (262800), ~1 MB on the 64–128 MB tier
(524140), and ~2 MB on the 128 MB+ tier (1048280). So a single high-speed stream fits
each tier's pool comfortably, but **several concurrent bulk streams** can exhaust
it and stall — which is why the big-RAM tiers raise `MAXMEM` in step with their
larger buffers. The pool allocates on demand, so a larger ceiling costs a
lightly-loaded machine nothing until the traffic needs it; raise `MAXMEM` further
if you push heavy multi-stream traffic. Each tier keeps proportionally similar
headroom, so a small machine is not squeezed harder than a big one.

### RFC 1323 timestamps (CPU-adaptive)

The second half of RFC 1323, **timestamps**, puts a small option on every data
segment (and the SYN handshake) carrying the sender's clock and an echo of the
peer's. That buys two things that matter precisely when window scaling opens a
big window:

- **Better round-trip timing (RTTM).** Every ack yields a fresh RTT sample
  instead of one-per-window, so the retransmit timer tracks a changing path
  much more tightly — fewer spurious or late retransmits on a busy link.
- **PAWS (Protect Against Wrapped Sequence numbers).** On a fast, long-lived
  connection the 32-bit sequence space can wrap while an old duplicate is still
  in flight; the timestamp lets the receiver reject the stale segment that
  sequence numbers alone would accept.

The cost is real, though: the 12-byte option is built, byte-swapped and
checked on every data segment (RSTs and keepalive probes, which carry no TCP
option at all, are the one exception), and a background clock ticks twice a
second. On a
7 MHz 68000 that per-packet tax is not worth paying, so timestamps are **gated
on the CPU**: the stack reads `SysBase->AttnFlags` at start-up (`ng_cpu_tune()`)
and turns them **on for a 68020/68030/68040/68060 and off for a bare
68000/68010**. Window scaling, by contrast, stays on everywhere — it costs one
shift at connection setup and nothing per segment — so a plain A500/A600 still
gets scaling, just not the timestamp overhead.

Like the RAM tier, the CPU decision is made **before the config file is read**,
and it is not one of the RoadshowData tunables a config tool can toggle at run
time (the `tcp.do_rfc1323` node Roadshow documents is deliberately absent, the
same as for window scaling). Timestamps are wire-compatible in both directions:
we offer the option in our SYN, but if the peer doesn't echo it back the stack
stops sending it and the connection runs exactly as before; a low-end box that
keeps them off never offers the option at all, and still interoperates fully with
a peer that uses them.

## Zero-configuration (RFC 3927 link-local)

When a `configure=dhcp` interface finds no DHCP server, it falls back to an
**IPv4 link-local address** in `169.254.0.0/16` (the same "APIPA" behaviour as
Windows and macOS), so two Amigas on a crossover cable — or any switch with no
DHCP — get on the network with zero manual configuration.

**How it acquires an address.** It picks a pseudo-random candidate in
`169.254.1.0`–`169.254.254.255`, seeded by the interface's MAC so the choice is
stable across reboots. It then **ARP-probes** the candidate (three probes with a
`0.0.0.0` sender, exactly as RFC 3927 requires, so nobody caches a mapping for an
address it does not yet own); if another host answers, it picks a different
address and tries again. Once a candidate probes clean it **announces** the
address with two gratuitous ARPs and assigns it as a `/16`. Thereafter it
**defends** the address: if another host is seen using it, the stack re-asserts
its claim with a gratuitous ARP, rate-limited to once every 10 s.

**It keeps trying DHCP.** Link-local is a fallback, not a surrender. In the
background the stack retries DHCP with exponential backoff (starting at one
minute, capped at thirty), and the instant a DHCP server appears it takes the
real lease and drops the link-local address. Because a DHCP `DISCOVER` must be
sent from a `0.0.0.0` source, each retry briefly unnumbers the interface — so a
link-local connection can see a few seconds' interruption at each (increasingly
rare) retry until a lease is won. If you reconfigure the interface yourself
(`Offline`, or a manual `ConfigureNetInterface`), the background retry notices
and steps aside rather than fighting you.

**No configuration needed.** This is automatic for any `configure=dhcp`
interface — there is no separate keyword to enable it, and nothing to tune. It
interoperates with any RFC 3927 / RFC 5227 host (Windows, macOS, `avahi`), and
never sends a link-local address to a router (it installs no default route and
carries no DNS).

## DNS response cache

Name lookups sit in front of a small cache, so resolving the same host twice
does not repeat the DNS query. Every `gethostbyname` / `getaddrinfo` /
`getnameinfo` (and the reentrant `_r` variants) benefits — they all funnel
through one place — and a cached answer is materialised through the *same*
parser a fresh one takes, so it is byte-for-byte the answer the network would
have given.

**What is cached.** Successful forward lookups (the raw DNS response, replayed
on a hit) and definitive **NXDOMAIN** "host not found" results — the latter so a
mistyped or dead name doesn't hammer the server on every retry. A transient
failure (timeout / server error) is never cached, so an outage self-heals.
Reverse lookups (`gethostbyaddr` / PTR) are deliberately **not** cached: they
are rarely used, and skipping them keeps the cache lean on a low-RAM machine.
Numeric addresses and local `hosts`-file entries bypass the cache entirely — it
only ever holds real DNS answers.

**How long.** A positive entry lives for the answer's own minimum record TTL,
clamped to **30 s – 1 h** (so a near-zero TTL is still worth caching and a huge
one can't pin stale data); a negative entry lives **30 s**.

**How big.** The capacity is tiered to installed RAM, the same way the socket
buffers and mbuf pool are — small on a lean machine, larger when there is RAM to
spare — and each entry's storage is allocated on demand, so an idle cache costs
only its tiny slot table:

| Installed RAM | Cache entries |
|---|---|
| ≤ 1 MB   | 8   |
| 2–4 MB   | 24  |
| 8–16 MB  | 64  |
| 32 MB+   | 128 |

**Automatic and internal.** There is no config knob and no flush — Roadshow
exposes no DNS-cache control either, and all the tunables (sizes, TTL clamp)
live in one header (`src/api/dns_cache.h`) for easy adjustment. The cache is
shared safely across tasks behind its own semaphore.
