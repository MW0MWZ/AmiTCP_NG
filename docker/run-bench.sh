#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# RX-throughput drag-race bench. Stages the CURRENT build/bsdsocket.library into the
# guest, boots amiberry (A2065 over SLIRP), runs the rxbench client against the
# transfer host's throughput source, and prints KB/s. Repeat after a stack change to
# chase the number up. Test infra only.
#
#   ./docker/run-bench.sh                 # stock A600 (68000)
#   CPU=68040 ./docker/run-bench.sh       # accelerated (68040, unthrottled, JIT)
#   CPU=68060 ./docker/run-bench.sh
#   NOSTAGE=1 ./docker/run-bench.sh       # use whatever lib is already staged
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
G="$ROOT/emu/hdd/System/Workbench3.2"
CPU="${CPU:-68000}"
TIMEOUT="${TIMEOUT:-170}"
# The A2065 is a Zorro II card, so the guest machine MUST have a Zorro bus -- an
# A600/A1200 QuickStart has none and a2065.device never autoconfigures (interface
# never comes up -> rxbench connect() gets EHOSTUNREACH). Default to an A4000; the
# CPU var still overrides the emulated CPU speed for throughput runs.
MODEL="${MODEL:-A4000}"
ROM="${ROM:-/work/emu/rom/kicka4000.rom}"

# CPU flags: 68000 = the plain model; anything else = unthrottled + JIT.
CPUARGS=()
if [ "$CPU" != "68000" ]; then
  CPUARGS=(-s cpu_model="$CPU" -s cpu_speed=max -s cpu_compatible=false -s cachesize=8192)
fi

# 1. stage the current library (unless told not to) + (re)build the bench client
if [ "${NOSTAGE:-0}" != "1" ]; then
  bash "$ROOT/docker/build-lib.sh" >/dev/null 2>&1 || { echo "build-lib failed"; exit 1; }
  cp "$ROOT/build/bsdsocket.library" "$G/Libs/bsdsocket.library"
fi
if [ ! -x "$G/C/rxbench" ] || [ "$ROOT/docker/bench/rxbench.c" -nt "$G/C/rxbench" ]; then
  "$ROOT/docker/cc.sh" m68k-amigaos-gcc -noixemul -O2 -m68000 -Wall \
    -o build/rxbench docker/bench/rxbench.c >/dev/null 2>&1 || { echo "rxbench build failed"; exit 1; }
  cp "$ROOT/build/rxbench" "$G/C/rxbench"
fi
VER="$(strings "$G/Libs/bsdsocket.library" | grep -m1 'VER: bsdsocket' | sed 's/.*(\(.*\)).*/\1/')"

# 1b. write our OWN interface + startup so the run never depends on leftover guest
#     state (the guest HD is a shared scratch area).
cat > "$G/Devs/NetInterfaces/bench" <<'IFACE'
device=a2065.device
configure=dhcp
IFACE
cat > "$G/S/Startup-sequence" <<'BOOT'
C:SetPatch QUIET
MakeDir RAM:T >NIL:
Assign T: RAM:T
Assign AmiTCP: SYS:AmiTCP
FailAt 21
Echo >SYS:bench.log "boot"
Wait 3
C:AddNetInterface bench >>SYS:bench.log
Wait 2
rxbench
Wait 300
BOOT

# 2. transfer host up on 172.20.0.10
docker network inspect amitcp-net >/dev/null 2>&1 || \
  docker network create --subnet 172.20.0.0/24 --gateway 172.20.0.1 amitcp-net >/dev/null
if ! docker ps --format '{{.Names}}' | grep -q '^transferhost$'; then
  docker rm -f transferhost >/dev/null 2>&1 || true
  docker run -d --rm --name transferhost --network amitcp-net --ip 172.20.0.10 \
    -v amitcp-ng-share:/srv/share -e TESTFILE_SIZES="5 50" amitcp-ng-transferhost >/dev/null
  sleep 5
fi

# 3. run the guest
rm -f "$G/bench.log" "$G/rxbench.log"
docker rm -f amiberry-net >/dev/null 2>&1 || true
docker run --rm --name amiberry-net --network amitcp-net \
  -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe HOME=/tmp/abhome
  mkdir -p /tmp/abhome; sleep 2
  cd /opt/amiberry
  timeout $((TIMEOUT-20)) ./build/amiberry --model $MODEL -r $ROM \
     -s filesystem2=rw,DH0:System:/work/emu/hdd/System/Workbench3.2,0 \
     ${CPUARGS[*]} -s a2065=slirp -G >/dev/null 2>&1" >/dev/null 2>&1 &
ABPID=$!
# 4. wait for the result
RES=""
for _ in $(seq 1 $((TIMEOUT/5))); do
  RES="$(grep -m1 'RESULT:' "$G/rxbench.log" 2>/dev/null || true)"
  [ -n "$RES" ] && break
  sleep 5
done
docker rm -f amiberry-net >/dev/null 2>&1 || true
wait "$ABPID" 2>/dev/null || true

echo "-------------------------------------------------------------"
echo " AmiTCP_NG RX bench   lib=[$VER]   CPU=$CPU"
if [ -n "$RES" ]; then echo "  $RES"; else echo "  NO RESULT -- see $G/bench.log"; cat -v "$G/bench.log" 2>/dev/null | tail -6; fi
echo "-------------------------------------------------------------"
