#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# run-ipprofile.sh -- the ITERATION harness: change the stack, measure, repeat.
#
# Runs docker/bench/ipbench.c (UDP over loopback) against the CURRENT source, on the
# plain A600/68000 model, and prints us/packet at four sizes.
#
# WHY THIS ONE IS TRUSTWORTHY WHEN run-bench.sh IS NOT. run-bench measures throughput
# over SLIRP and an emulated A2065, so it measures the emulated NIC as much as the
# stack, and its run-to-run spread (~5%) swamps the single-digit changes we are now
# chasing. This uses NO network at all: every packet goes
#   udp_output -> ip_output -> looutput -> ip_input -> udp_input -> socket
# so it is pure stack CPU cost. And it runs on the A600 QuickStart, which amiberry
# emulates cycle-by-cycle at 7 MHz -- NOT the accelerated models, which use
# cpu_speed=max and a JIT and whose timings really are meaningless.
#
# WHAT THE COLUMNS MEAN. 64 and 256 bytes are dominated by PER-PACKET cost (header
# handling, route lookup, socket delivery); 1472 is dominated by PER-BYTE cost
# (checksum, copies). A change that helps only the big size is a per-byte win; one
# that helps the small sizes is per-packet. Knowing which is which is the point.
#
#   ./docker/run-ipprofile.sh            # one run against the current source
#   N=3 ./docker/run-ipprofile.sh        # three runs, so you can see the spread
#   NOSTAGE=1 ./docker/run-ipprofile.sh  # reuse the staged library (skip the build)
#
# CAREFUL WITH NOSTAGE: other harnesses (run-bench.sh) stage their OWN library, so
# "reuse what is staged" can silently mean "whatever the last different experiment
# left there". That produced a confident wrong conclusion once. If in doubt, restage.
#
# ALWAYS check the spread before believing a difference. If two builds differ by
# less than the spread of repeated runs of the SAME build, you have measured noise.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
G="$ROOT/emu/hdd/System/Workbench3.2"
# Container-relative form. Paths inside the container are /work/<relative>, so an
# absolute HOST path used there silently resolves to something that does not exist,
# and every read comes back empty -- which looks like the run failing, not the path
# being wrong. Cost an hour once; keep the two forms separate.
GREL="emu/hdd/System/Workbench3.2"
AB="${AB:-amitcp-ng-amiberry:latest}"
N="${N:-1}"

if [ "${NOSTAGE:-0}" != "1" ]; then
  echo ">>> building library from current source (-m68000) ..."
  NG_ARCH=-m68000 bash "$ROOT/docker/build-lib.sh" >/dev/null 2>&1 || {
    echo "library build FAILED"; exit 1; }
  cp "$ROOT/build/bsdsocket.library" "$G/Libs/bsdsocket.library"
fi
if [ ! -x "$G/C/ipbench" ] || [ "$ROOT/docker/bench/ipbench.c" -nt "$G/C/ipbench" ]; then
  echo ">>> building ipbench (-m68000) ..."
  "$ROOT/docker/cc.sh" m68k-amigaos-gcc -noixemul -O2 -m68000 -Wall -Werror \
    -o build/ipbench docker/bench/ipbench.c || { echo "ipbench build FAILED"; exit 1; }
  "$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" /work/build/ipbench >/dev/null 2>&1 || true
  cp "$ROOT/build/ipbench" "$G/C/ipbench"
fi

cat > "$G/S/Startup-sequence" <<'BOOT'
C:SetPatch QUIET
MakeDir RAM:T >NIL:
Assign T: RAM:T
Assign AmiTCP: SYS:AmiTCP
FailAt 21
ipbench >SYS:ipbench.log
Echo >>SYS:ipbench.log "ipbench-done"
Wait 300
BOOT

echo ">>> library: $(wc -c < "$G/Libs/bsdsocket.library") bytes"
for i in $(seq 1 "$N"); do
  docker run --rm -v "$ROOT":/work -w /work "$AB" \
    bash -c "cd '/work/$GREL' && rm -f ipbench.log" >/dev/null 2>&1
  # A600 QuickStart = a real emulated 68000; NET=0 so no NIC is needed (loopback only).
  TIMEOUT=150 bash "$ROOT/docker/run-amiberry.sh" >/dev/null 2>&1
  out=$(docker run --rm -v "$ROOT":/work -w /work "$AB" \
        bash -c "cat '/work/$GREL/ipbench.log' 2>/dev/null")
  if ! printf '%s' "$out" | LC_ALL=C grep -aq "ipbench-done"; then
    echo "run $i: DID NOT COMPLETE"; printf '%s\n' "$out" | head -8; continue
  fi
  echo "--- run $i ---"
  printf '%s\n' "$out" | LC_ALL=C grep -av "ipbench-done"
done
