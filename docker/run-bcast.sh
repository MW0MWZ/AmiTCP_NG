#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Same-host UDP broadcast discovery repro (the Fitz serve/query round trip).
# Stages the CURRENT build/bsdsocket.library + bcastbench into the guest, brings
# up the A2065 with a STATIC address and NO default route (so 255.255.255.255 has
# no route -- the bug), runs bcastbench, prints PASS/FAIL. Test infra only.
#   ./docker/run-bcast.sh              # static, no gateway (reproduces the bug)
#   MODE=dhcp ./docker/run-bcast.sh    # control: DHCP (has a default route)
#   NOSTAGE=1 ./docker/run-bcast.sh    # use the lib already staged
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
G="$ROOT/emu/hdd/System/Workbench3.2"
CPU="${CPU:-68000}"
TIMEOUT="${TIMEOUT:-170}"
MODE="${MODE:-static}"
MODEL="${MODEL:-A4000}"
ROM="${ROM:-/work/emu/rom/kicka4000.rom}"
CPUARGS=()
if [ "$CPU" != "68000" ]; then
  CPUARGS=(-s cpu_model="$CPU" -s cpu_speed=max -s cpu_compatible=false -s cachesize=8192)
fi

# 1. stage the current library + build the repro client
if [ "${NOSTAGE:-0}" != "1" ]; then
  bash "$ROOT/docker/build-lib.sh" >/dev/null 2>&1 || { echo "build-lib failed"; exit 1; }
  cp "$ROOT/build/bsdsocket.library" "$G/Libs/bsdsocket.library"
fi
if [ ! -x "$G/C/bcastbench" ] || [ "$ROOT/docker/bench/bcastbench.c" -nt "$G/C/bcastbench" ]; then
  "$ROOT/docker/cc.sh" m68k-amigaos-gcc -noixemul -O2 -m68000 -Wall \
    -o build/bcastbench docker/bench/bcastbench.c || { echo "bcastbench build failed"; exit 1; }
  cp "$ROOT/build/bcastbench" "$G/C/bcastbench"
fi
VER="$(strings "$G/Libs/bsdsocket.library" | grep -m1 'VER: bsdsocket' | sed 's/.*(\(.*\)).*/\1/')"

# 1b. interface config
if [ "$MODE" = "dhcp" ]; then
  cat > "$G/Devs/NetInterfaces/bcast" <<'IFACE'
device=a2065.device
configure=dhcp
IFACE
else
  cat > "$G/Devs/NetInterfaces/bcast" <<'IFACE'
device=a2065.device
unit=0
address=10.0.2.15
netmask=255.255.255.0
IFACE
fi

cat > "$G/S/Startup-sequence" <<'BOOT'
C:SetPatch QUIET
MakeDir RAM:T >NIL:
Assign T: RAM:T
Assign AmiTCP: SYS:AmiTCP
FailAt 21
Echo >SYS:bcast.log "boot"
Wait 3
C:AddNetInterface bcast >>SYS:bcast.log
Wait 3
bcastbench
Wait 300
BOOT

# 2. run the guest (SLIRP; no external host needed -- the test is same-host)
rm -f "$G/bcast.log"
docker rm -f amiberry-bcast >/dev/null 2>&1 || true
docker run --rm --name amiberry-bcast \
  -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe HOME=/tmp/abhome
  mkdir -p /tmp/abhome; sleep 2
  cd /opt/amiberry
  timeout $((TIMEOUT-20)) ./build/amiberry --model $MODEL -r $ROM \
     -s filesystem2=rw,DH0:System:/work/emu/hdd/System/Workbench3.2,0 \
     ${CPUARGS[*]} -s a2065=slirp -G >/dev/null 2>&1" >/dev/null 2>&1 &
ABPID=$!
RES=""
for _ in $(seq 1 $((TIMEOUT/5))); do
  RES="$(grep -m1 'RESULT:' "$G/bcast.log" 2>/dev/null || true)"
  [ -n "$RES" ] && break
  sleep 5
done
docker rm -f amiberry-bcast >/dev/null 2>&1 || true
wait "$ABPID" 2>/dev/null || true

echo "-------------------------------------------------------------"
echo " AmiTCP_NG bcast repro   lib=[$VER]   MODE=$MODE   CPU=$CPU"
echo "  --- SYS:bcast.log ---"
cat -v "$G/bcast.log" 2>/dev/null | sed 's/^/  /'
echo "-------------------------------------------------------------"
