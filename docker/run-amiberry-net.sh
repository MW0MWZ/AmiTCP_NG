#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# High-bandwidth networked amiberry for THROUGHPUT testing. Unlike run-amiberry.sh
# (SLIRP user-mode NAT, bandwidth-limited), this pcap-BRIDGES the emulated A2065
# onto the container's eth0 at L2, on the shared 'amitcp-net' Docker network, so the
# Amiga guest talks to the transfer host (docker/run-transferhost.sh) at near-native
# bandwidth -- making the Amiga TCP stack the bottleneck we actually want to measure.
#
# Requires NET_ADMIN + NET_RAW (pcap opens eth0 in promiscuous mode). The guest uses
# a STATIC address on the docker subnet (docker bridges have no DHCP): see
# emu/hdd/.../Devs/NetInterfaces/bench (172.20.0.30/24, gw .1). Test infra only.
#
#   ./docker/run-amiberry-net.sh                 # A600-class, 120s
#   MODEL=A4000 TIMEOUT=300 ./docker/run-amiberry-net.sh
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMEOUT="${TIMEOUT:-120}"
ROM="${ROM:-/work/emu/rom/kickCDTVa1000a500a2000a600.rom}"
HDD="${HDD:-/work/emu/hdd/System/Workbench3.2}"
NET="${NET:-amitcp-net}"
IP="${IP:-172.20.0.20}"                 # the amiberry CONTAINER's eth0 (guest A2065 = .30)
MODEL="${MODEL:-A600}"

docker rm -f amiberry-net >/dev/null 2>&1 || true
docker run --rm --name amiberry-net --network "$NET" --ip "$IP" \
  --cap-add NET_ADMIN --cap-add NET_RAW \
  -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  export HOME=/tmp/abhome; mkdir -p /tmp/abhome
  sleep 2
  echo '>>> container eth0:'; ip -4 addr show eth0 | grep inet
  echo '>>> launching amiberry (model $MODEL, a2065=eth0 pcap-bridge, timeout ${TIMEOUT}s)'
  cd /opt/amiberry
  timeout ${TIMEOUT} ./build/amiberry --model ${MODEL} \
     -r '$ROM' \
     -s filesystem2=rw,DH0:System:'$HDD',0 \
     -s a2065=eth0 \
     -G 2>&1 | grep -viE '^\s*\$' | tail -40
  echo '>>> amiberry exited'
"
