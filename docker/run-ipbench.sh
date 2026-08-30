#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Build the per-packet loopback benchmark and a kit for running it on REAL hardware,
# then prove the binary actually works by running it in the emulator first.
#
# THE QUESTION IT EXISTS TO ANSWER: is the -m68020/-m68040 archive actually faster
# than the plain -m68000 one on a real machine? On a real A3000 the 68000 build
# appeared to WIN, which -- if true -- means the tuned archives are not worth
# shipping. Reading the disassembly can show that 020+ emits BFINS on the transmit
# path (see docker/check-codegen.sh); only a measurement says whether that costs
# anything.
#
# WHY THE EMULATOR RUN MATTERS EVEN THOUGH IT PROVES NOTHING ABOUT SPEED: emulated
# timings are worthless here (the accelerated models run with cpu_speed=max and a
# JIT). It is run ONLY to prove the binary starts, binds, and completes -- so that
# nobody carries a broken program to the Amiga and burns an evening on it.
#
#   usage:  docker/run-ipbench.sh          # build kit + verify in the emulator
#           SKIPEMU=1 docker/run-ipbench.sh    # just build the kit
#
# Output: build/ipbench-kit/  (three libraries, the bench, and a README)
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
G="$ROOT/emu/hdd/System/Workbench3.2"
AB="${AB:-amitcp-ng-amiberry:latest}"
KIT="$ROOT/build/ipbench-kit"

if ! docker info >/dev/null 2>&1; then
  echo "NOT READY: docker is not usable." >&2; exit 2
fi
for img in amigadev/crosstools:m68k-amigaos; do
  docker image inspect "$img" >/dev/null 2>&1 || {
    echo "NOT READY: docker image '$img' is missing -- nothing was built." >&2; exit 2; }
done

# 1. The bench itself. Built for 68000 ON PURPOSE: it must run on every machine
#    unchanged, because the thing under test is the LIBRARY, not the client. A
#    client compiled per-arch would vary two things at once and measure neither.
echo ">>> building ipbench (-m68000, runs on any 68k) ..."
"$ROOT/docker/cc.sh" m68k-amigaos-gcc -noixemul -O2 -m68000 -Wall -Werror \
  -o build/ipbench docker/bench/ipbench.c || { echo "ipbench build failed"; exit 1; }

# 2. One library per CPU target.
rm -rf "$KIT"; mkdir -p "$KIT"
cp "$ROOT/build/ipbench" "$KIT/ipbench"
for cpu in 68000 68020 68040; do
  echo ">>> building bsdsocket.library (-m$cpu) ..."
  NG_ARCH="-m$cpu" "$ROOT/docker/build-lib.sh" >/dev/null 2>&1 || {
    echo "library build failed for $cpu"; exit 1; }
  cp "$ROOT/build/bsdsocket.library" "$KIT/bsdsocket.library.$cpu"
done
"$ROOT/docker/cc.sh" chown -R "$(id -u):$(id -g)" /work/build >/dev/null 2>&1 || true

cat > "$KIT/README" <<'EOF'
ipbench -- which bsdsocket.library build is actually fastest on THIS machine?

Three libraries are included, one per CPU target. The benchmark itself is a single
68000 binary and never changes; only the library does. That is the point -- vary
one thing.

  1. Copy ipbench somewhere on your path (e.g. C:).
  2. For each library in turn:

       Copy bsdsocket.library.68000 LIBS:bsdsocket.library
       (reboot, or stop and restart the stack so the new library is loaded)
       ipbench

     ...then repeat for .68020 and .68040. Only run a build your CPU supports:
     the 020 and 040 archives emit instructions a lesser CPU cannot execute.

  3. Compare the packets/sec column.

WHAT TO LOOK AT. Small sizes (64, 256) are dominated by PER-PACKET cost, which is
where IP header handling lives -- that is where the builds are expected to differ.
Large sizes (1472) are dominated by per-BYTE cost (checksum, copying), which is the
same hand-written assembly in all three, so those numbers should be close.

FAIRNESS. Run them back to back on an otherwise idle machine, same session if you
can. The benchmark already takes the best of five rounds, because interference only
ever makes a round slower.

It uses the loopback interface, so no network, cable or card is involved -- this
measures the stack, not the hardware.
EOF

echo ">>> kit: $KIT"
ls -la "$KIT"

[ "${SKIPEMU:-0}" = "1" ] && exit 0

# 3. Prove it RUNS. Timings here are meaningless; completion is not.
docker image inspect "$AB" >/dev/null 2>&1 || {
  echo "NOTE: '$AB' missing -- skipping the emulator check. The kit is built, but"
  echo "      the binary has NOT been shown to run. Build the image or use SKIPEMU=1."
  exit 2; }
if [ ! -d "$G" ]; then
  echo "NOTE: guest image '$G' missing -- skipping the emulator check." >&2; exit 2
fi

echo ">>> verifying ipbench runs (emulator; timings there mean nothing) ..."
cp "$ROOT/build/ipbench" "$G/C/ipbench"
cp "$KIT/bsdsocket.library.68000" "$G/Libs/bsdsocket.library"
cat > "$G/S/Startup-sequence" <<'BOOT'
C:SetPatch QUIET
MakeDir RAM:T >NIL:
Assign T: RAM:T
Assign AmiTCP: SYS:AmiTCP
FailAt 21
Echo >SYS:ipbench.log "start"
C:ipbench >>SYS:ipbench.log
Echo >>SYS:ipbench.log "done"
Echo >SYS:done.marker "x"
BOOT
rm -f "$G/ipbench.log" "$G/done.marker"
# Use the harness that is known to boot rather than hand-rolling an amiberry
# command line: model, ROM and filesystem args all have to agree and getting any
# of them wrong produces exactly the "no output at all" this check then reports as
# a broken binary. NET=0 -- loopback needs no card.
NET=0 TIMEOUT=150 "$ROOT/docker/run-amiberry.sh" >/dev/null 2>&1 || true

echo
if [ -f "$G/ipbench.log" ]; then
  echo ">>> emulator output:"; LC_ALL=C sed 's/^/      /' "$G/ipbench.log"
else
  echo "!!! ipbench produced NO output in the emulator -- do not take this to the"
  echo "    Amiga until that is understood."
  exit 1
fi
[ -f "$G/done.marker" ] || { echo "!!! it did not run to completion (no marker)"; exit 1; }
echo ">>> ipbench runs. Timings above are emulator noise; use the kit on real hardware."
