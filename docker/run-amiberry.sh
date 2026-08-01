#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Headless Amiberry run (Mesa/llvmpipe under Xvfb), killed after $TIMEOUT sec.
# Unlike run-fsuae.sh this image can emulate a real SANA-II NIC: set NET=1 to
# attach a Commodore A2065 backed by SLIRP user-mode NAT (built-in DHCP server,
# guest 10.0.2.15 / gw 10.0.2.2 / dns 10.0.2.3; TCP/UDP + ICMP to the gateway and
# loopback, but no ICMP routed to the wider internet).
#
# IMPORTANT: the A2065 is a Zorro II card, so NET=1 needs a Zorro-slot machine --
# an A600/A1200 has NO Zorro bus, so a2065.device never autoconfigures and
# OpenDevice() fails with "Device or unit failed to open". NET=1 therefore
# defaults to an A4000 (Zorro III) + its Kickstart; the loopback-only NET=0 path
# stays on the lighter A600. Both MODEL and ROM can be overridden explicitly.
# (Bonus: the A4000 QuickStart reports ~9 MB, which lands on the 8-16 MB RAM
# tier -- so NET=1 also exercises the window-scaling / big-RAM buffer defaults.)
#
# Capture, exactly like FS-UAE, is via files the Amiga writes to the directory
# hard drive (host-visible); Amiberry uses the same .uaem metadata sidecars.
#
#   TIMEOUT=95 ./docker/run-amiberry.sh          # loopback/LVO tests (no NIC, A600)
#   NET=1 TIMEOUT=120 ./docker/run-amiberry.sh   # with the A2065+SLIRP NIC (A4000)
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMEOUT="${TIMEOUT:-95}"
NET="${NET:-0}"
HDD="${HDD:-/work/emu/hdd/System/Workbench3.2}"

# Model + ROM default by mode: NET=1 needs Zorro (A4000), NET=0 is fine on A600.
NETARG=()
if [ "$NET" = "1" ]; then
  MODEL="${MODEL:-A4000}"                          # Zorro III -- hosts the A2065
  ROM="${ROM:-/work/emu/rom/kicka4000.rom}"
  NETARG=(-s "a2065=slirp")     # "slirp" = SLIRP User Mode NAT driver (ethernet.cpp)
else
  MODEL="${MODEL:-A600}"                            # light loopback-only machine
  ROM="${ROM:-/work/emu/rom/kickCDTVa1000a500a2000a600.rom}"
fi

docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  export HOME=/tmp/abhome; mkdir -p /tmp/abhome
  sleep 2
  echo '>>> launching amiberry (model $MODEL, NET=$NET, timeout ${TIMEOUT}s)'
  cd /opt/amiberry
  timeout ${TIMEOUT} ./build/amiberry --model $MODEL \
     -r '$ROM' \
     -s filesystem2=rw,DH0:System:'$HDD',0 \
     ${NETARG[*]} \
     -G 2>&1 | grep -viE '^\s*$' | tail -30
  echo '>>> amiberry exited'
"
