#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Headless Amiberry run (Mesa/llvmpipe under Xvfb), killed after $TIMEOUT sec.
# Unlike run-fsuae.sh this image can emulate a real SANA-II NIC: set NET=1 to
# attach a Commodore A2065 backed by SLIRP user-mode NAT (built-in DHCP server,
# guest 10.0.2.15 / gw 10.0.2.2 / dns 10.0.2.3; TCP/UDP + ICMP to the gateway and
# loopback, but no ICMP routed to the wider internet).
#
# Capture, exactly like FS-UAE, is via files the Amiga writes to the directory
# hard drive (host-visible); Amiberry uses the same .uaem metadata sidecars.
#
#   TIMEOUT=95 ./docker/run-amiberry.sh          # loopback/LVO tests (no NIC)
#   NET=1 TIMEOUT=120 ./docker/run-amiberry.sh   # with the A2065+SLIRP NIC
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMEOUT="${TIMEOUT:-95}"
NET="${NET:-0}"
ROM="${ROM:-/work/emu/rom/kickCDTVa1000a500a2000a600.rom}"
HDD="${HDD:-/work/emu/hdd/System/Workbench3.2}"

# A600-class machine via QuickStart (--model handles the fiddly chipset/CPU/mem
# keys); we override only ROM, the boot directory-HD, and optionally the NIC.
NETARG=()
if [ "$NET" = "1" ]; then
  NETARG=(-s "a2065=slirp")     # "slirp" = SLIRP User Mode NAT driver (ethernet.cpp)
fi

docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  export HOME=/tmp/abhome; mkdir -p /tmp/abhome
  sleep 2
  echo '>>> launching amiberry (NET=$NET, timeout ${TIMEOUT}s)'
  cd /opt/amiberry
  timeout ${TIMEOUT} ./build/amiberry --model A600 \
     -r '$ROM' \
     -s filesystem2=rw,DH0:System:'$HDD',0 \
     ${NETARG[*]} \
     -G 2>&1 | grep -viE '^\s*$' | tail -30
  echo '>>> amiberry exited'
"
