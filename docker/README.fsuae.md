# FS-UAE harness — `amitcp-ng-fsuae`

Boots a real emulated AmigaOS 3.2, loads AmiTCP_NG, and runs the **loopback and
library-vector (API) tests**. This is the fast correctness loop. It needs **no
emulated network hardware** — for real packets on the wire use the
[Amiberry harness](README.amiberry.md) instead.

## Prerequisites

- Docker.
- The build image already used at least once (`./docker/build.sh`).
- Licensed files under `emu/` (git-ignored — see the
  [hub README](README.md#what-you-must-supply-licensed-never-in-this-repo)):
  - `emu/rom/*.rom` — a Kickstart 3.1-class ROM.
  - `emu/hdd/System/Workbench3.2/` — an installed AmigaOS 3.2 Workbench directory.

## Build the image

```bash
docker build -f docker/Dockerfile.fsuae -t amitcp-ng-fsuae:latest .
```

It is `debian:bookworm-slim` + the Debian `fs-uae` package + `xvfb` + Mesa
software GL (llvmpipe, so FS-UAE gets the OpenGL context it needs with no GPU) +
`amitools` (for assembling boot media). One build, then reuse.

## Run the tests

```bash
# build + deploy the freshly built stack, then boot
./docker/build.sh
cp build/amitcp emu/hdd/System/Workbench3.2/AmiTCP/amitcp
TIMEOUT=95 ./docker/run-fsuae.sh
```

`run-fsuae.sh` starts Xvfb, launches `fs-uae` under a `timeout`, and tails the
emulator's own output. The Amiga is killed after `$TIMEOUT` seconds (default 90).
The config it loads is `emu/amitcp-ng.fs-uae` (model A600, your ROM, 2 MB chip +
8 MB fast, the directory hard drive as `System:`).

## Read the results

The boot `S/Startup-sequence` runs `amitcp`, then the test programs, which write
logs to `SYS:` — i.e. straight back onto your host:

```bash
tr -d '\r' < emu/hdd/System/Workbench3.2/exttest.log   # extension-vector assertions
tr -d '\r' < emu/hdd/System/Workbench3.2/udptest.log   # UDP loopback round-trip
```

Success looks like:

```
...
RESULT: EXTENSION VECTORS OK
```

## What gets tested here

- **`exttest`** — asserts the Roadshow extension vectors (address conversion,
  DNS-server management, routing, interface config/query, statistics,
  RoadshowData tunables, mbuf kernel access, `get*ent` iterators, …) and the
  capability handshake.
- **`udptest`** — sends a datagram to `127.0.0.1` and receives it back through
  the real socket layer: the definitive "packets flow through the stack" proof
  that does not need a NIC.

## Rebuild-and-run cheat sheet

```bash
# stack changed:
./docker/build.sh && cp build/amitcp emu/hdd/System/Workbench3.2/AmiTCP/amitcp

# a test program changed (e.g. exttest):
docker run --rm -v "$PWD":/work -w /work amigadev/crosstools:m68k-amigaos bash -c \
  'm68k-amigaos-gcc -noixemul -O2 -m68000 tmp/exttest.c -o tmp/exttest'
cp tmp/exttest emu/hdd/System/Workbench3.2/AmiTCP/exttest

rm -f emu/hdd/System/Workbench3.2/exttest.log
TIMEOUT=95 ./docker/run-fsuae.sh
tr -d '\r' < emu/hdd/System/Workbench3.2/exttest.log
```

## Troubleshooting

- **A test log is missing / stale.** Delete it before the run (`rm -f …log`) so
  you can be sure the new boot wrote it.
- **`udptest` never ran.** A failing `exttest` returns exit code ≥ 10, which
  trips AmigaOS `FailAt 10` and **aborts the rest of the Startup-sequence** — so
  later tests are skipped. Fix the `exttest` failure first; its log shows which
  assertion broke.
- **The 2.3 MB "binary" appears in a log.** That is a test-harness bug, not a
  stack bug: a direct-LVO caller stub passed a callee-scratched register as a
  plain input instead of `"+r"`, so `-O2` reused a stale register. Mark the
  clobbered `d0/d1/a0/a1` as `"+r"` in the caller stub.
- **This package has no network device.** Confirmed: the Debian `fs-uae`
  `3.1.66-2` build links neither libslirp nor libpcap. That is by design here —
  the [Amiberry harness](README.amiberry.md) provides the NIC.
