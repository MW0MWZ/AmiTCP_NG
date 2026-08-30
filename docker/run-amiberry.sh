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
#   RAM=256 NET=1 TIMEOUT=280 ./docker/run-amiberry.sh
#                                                # A4000/040 + 256 MB: matches Andy's
#                                                # PiStorm (68040, 128 MB+ RAM tier)
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMEOUT="${TIMEOUT:-95}"
NET="${NET:-0}"
HDD="${HDD:-/work/emu/hdd/System/Workbench3.2}"

# RAM (Z3 fast, MB). THIS SELECTS THE STACK'S RAM TIER, so it decides which set of
# socket-buffer/mbuf defaults ng_ram_tier() installs -- and therefore which code the
# test actually exercises. The A4000 QuickStart is ~9 MB, i.e. the 8-16 MB tier, and
# that is ALL this rig ever ran until 2026-08-14. A fault that only appears on the
# 128 MB+ tier (a real PiStorm has 350 MB: udp_recvspace 262144, tcp ~1 MB, sb_max
# 2 MB) could not be reproduced here no matter how many times it was run. Set
# RAM=256 to match a big PiStorm; RAM=0 keeps the QuickStart default.
#   RAM=256 NET=1 TIMEOUT=280 ./docker/run-amiberry.sh
RAM="${RAM:-0}"
RAMARG=()
[ "$RAM" != "0" ] && RAMARG=(-s "z3mem_size=$RAM")

# CPU. --model A4000 is a QuickStart, and QuickStart picks the CPU for you -- so the
# processor the test ran on was never actually stated anywhere. Andy's machine is a
# PiStorm/Emu68 presenting as a 68040, and ng_cpu_tune() turns the RFC 1323 options on
# by AFF_68020|...|AFF_68060, so the CPU decides which code path runs. State it
# explicitly rather than inheriting whatever QuickStart chose. CPU=0 keeps the
# QuickStart default -- which is what the NET=0 A600 wants, since it IS a 68000.
# Default is therefore set per-mode in the MODEL block below.
CPU="${CPU:-}"

# Model + ROM default by mode: NET=1 needs Zorro (A4000), NET=0 is fine on A600.
NETARG=()
if [ "$NET" = "1" ]; then
  MODEL="${MODEL:-A4000}"                          # Zorro III -- hosts the A2065
  ROM="${ROM:-/work/emu/rom/kicka4000.rom}"
  CPU="${CPU:-68040}"                               # match Andy's PiStorm/Emu68 040
  NETARG=(-s "a2065=slirp")     # "slirp" = SLIRP User Mode NAT driver (ethernet.cpp)
else
  MODEL="${MODEL:-A600}"                            # light loopback-only machine
  ROM="${ROM:-/work/emu/rom/kickCDTVa1000a500a2000a600.rom}"
  CPU="${CPU:-0}"                                   # A600 is a 68000 -- leave it alone
fi

CPUARG=()
[ "$CPU" != "0" ] && CPUARG=(-s "cpu_model=$CPU" -s "fpu_model=$CPU")

docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  export HOME=/tmp/abhome; mkdir -p /tmp/abhome
  sleep 2
  echo '>>> launching amiberry (model $MODEL, CPU=$CPU, NET=$NET, RAM=${RAM}MB Z3, timeout ${TIMEOUT}s)'
  cd /opt/amiberry
  timeout ${TIMEOUT} ./build/amiberry --model $MODEL \
     -r '$ROM' \
     -s filesystem2=rw,DH0:System:'$HDD',0 \
     ${NETARG[*]} ${RAMARG[*]} ${CPUARG[*]} \
     -G 2>&1 | grep -viE '^\s*$' | tail -30
  echo '>>> amiberry exited'
"
