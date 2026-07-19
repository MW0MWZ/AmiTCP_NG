# Amiberry network harness — `amitcp-ng-amiberry`

Boots AmigaOS 3.2 with an **emulated Commodore A2065 Ethernet card backed by
SLIRP** user-mode NAT, so AmiTCP_NG's SANA-II driver binds a real NIC and the
whole lower half of the stack — device I/O, ARP, IP routing, UDP/TCP, DHCP —
runs for real. This is what FS-UAE cannot do (it has no network card).

SLIRP is user-mode NAT: **TCP/UDP, plus ICMP to the SLIRP gateway and loopback**
(`ping 10.0.2.2` and `ping 127.0.0.1` both reply; ICMP is not routed to the wider
internet), with a built-in DHCP server and fixed addressing — guest `10.0.2.15`,
gateway `10.0.2.2`, DNS `10.0.2.3`. No host admin rights, no bridging, no pcap.

## Prerequisites

- Docker, and outbound internet from the container (SLIRP forwards DNS/UDP/TCP
  to the host network; the default `docker run` network is fine).
- Licensed files under `emu/` (git-ignored):
  - `emu/rom/*.rom` — a Kickstart 3.1-class ROM.
  - `emu/hdd/System/Workbench3.2/` — an installed AmigaOS 3.2 Workbench.
  - `emu/hdd/System/Workbench3.2/Devs/Networks/a2065.device` — the A2065 SANA-II
    driver. Get it from Aminet (`comm/net`) or the AmiTCP 3.0b2 distribution and
    drop it there. **Full path matters** (see Troubleshooting).

## Build the image

```bash
docker build -f docker/Dockerfile.amiberry -t amitcp-ng-amiberry:latest .
```

This builds **Amiberry v7.1.1** from source (CMake). Why that exact version:

- Amiberry is native SDL2/Linux, so it runs headless under Xvfb exactly like the
  FS-UAE image (WinUAE, the most mature option, is Windows-only).
- **v7.1.1 is the last SDL2 release** — v8.x needs SDL3, which Debian bookworm
  does not ship — and it already carries the in-tree SLIRP stack (including its
  BOOTP/DHCP server), the A2065 emulation, and `uaenet.device`.

The first build compiles the whole Amiberry C++ tree (several minutes) and
produces a ~1.5 GB image. The Debian build-deps that are easy to miss are in
`docker/Dockerfile.amiberry` — notably `libenet-dev` (CMake configure hard-fails
without it) and the SDL2 satellite `-dev` packages.

## Run — loopback tests (no NIC)

Same tests as FS-UAE, to confirm parity:

```bash
TIMEOUT=95 ./docker/run-amiberry.sh
tr -d '\r' < emu/hdd/System/Workbench3.2/exttest.log   # -> RESULT: EXTENSION VECTORS OK
```

## Run — with the emulated NIC

```bash
NET=1 TIMEOUT=150 ./docker/run-amiberry.sh
```

`NET=1` attaches the A2065 on SLIRP. Amiberry's own log then shows the card in
the Zorro-II autoconfig chain:

```
Autoconfig board list:  'A2065'
7990: 'slirp' 00:80:10:32:33:34         # AMD Am7990 LANCE bound to SLIRP, real Commodore OUI
Card 4: Z2 0x00e90000   64K IO  7990 Ethernet
```

### The real-network test (`nictest`)

`tmp/nictest` brings up `eth0` on `a2065.device` with a static `10.0.2.15/24` and
a default route to `10.0.2.2`, then sends a live DNS query to SLIRP's resolver at
`10.0.2.3:53` and reads the answer back:

```bash
tr -d '\r' < emu/hdd/System/Workbench3.2/nictest.log
```

```
AddInterface(eth0,DEVS:Networks/a2065.device,0) r=0
  interface up: 10.0.2.15/24
sendto(10.0.2.3:53, DNS query) r=29
recvfrom() r=61
  resp id=0x00001234   flags=0x00008180   ancount=2
RESULT: REAL PACKET ROUND-TRIP OK (DNS answered over the NIC)
```

That single run exercises `a2065.device` open, SANA-II transmit **and** receive,
ARP, IP output + routing, IP input, and UDP delivery — a real DNS server answered
through SLIRP's NAT.

## Environment knobs (`run-amiberry.sh`)

| Var | Default | Meaning |
|-----|---------|---------|
| `NET` | `0` | `1` attaches the A2065 + SLIRP NIC. |
| `TIMEOUT` | `95` | Seconds before the Amiga is killed. Use `150` for network runs (boot + tests + NIC bring-up). |
| `ROM` | the bundled path | Kickstart ROM path (container path under `/work`). |
| `HDD` | `…/Workbench3.2` | Boot directory-HD (container path). |

The script uses `--model A600` for the base machine and overrides only the ROM,
the boot directory-HD, and (with `NET=1`) `-s a2065=slirp`, then `-G` to boot
straight in without the GUI.

## How results come back

Identical to FS-UAE: the guest writes `*.log` files to `SYS:`, which is your
`emu/hdd/System/Workbench3.2` directory. Amiberry uses the same `.uaem` metadata
sidecars, so nothing about capture changes between the two emulators.

## DHCP

`BeginInterfaceConfig` (the DHCP client) works end to end in this harness. The
boot `dhcptest` creates `eth0` with no address, then DHCPs one: it runs the full
DISCOVER→OFFER→REQUEST→ACK handshake against SLIRP's DHCP server and applies the
lease.

```bash
tr -d '\r' < emu/hdd/System/Workbench3.2/dhcptest.log
```

```
BeginInterfaceConfig: DHCP DISCOVER...
  aam_Result=0x00000000        # AAMR_Success
  aam_Address=0x0a00020f       # 10.0.2.15
  aam_SubnetMask=0xffffff00    # 255.255.255.0
  aam_Server=0x0a000202        # 10.0.2.2
RESULT: DHCP OK (address leased over the NIC)
```

## Troubleshooting

- **`AddInterface … errno=6` (ENXIO).** The SANA-II device could not be opened.
  `OpenDevice()` does **not** search `DEVS:Networks/`, so you must name the driver
  by **full path**: `DEVS:Networks/a2065.device`, not bare `a2065.device`.
- **`OpenLibrary FAILED` in a test program.** This stack reports `bsdsocket.library`
  version **4** (`4.1`), so request version 4 or lower when you open it — never a
  higher version than the library provides.
- **No OFFER during a DHCP run.** The DHCP server lives in SLIRP, so the run needs
  `NET=1` — without the NIC there is nothing to answer the DISCOVER. Confirm the
  A2065 appears in Amiberry's autoconfig list (see the NIC run above).
- **Ping the gateway or loopback, not the internet.** `ping 10.0.2.2` (the SLIRP
  gateway) and `ping 127.0.0.1` reply; SLIRP does not route ICMP to external hosts,
  so use a UDP/TCP exchange (as `nictest` does) to test off-box reachability.
- **Build fails at CMake configure.** Check `libenet-dev` and the SDL2 `-dev`
  packages are present (they are in the Dockerfile); and confirm the pinned tag
  is `v7.1.1` (v8.x needs SDL3).
