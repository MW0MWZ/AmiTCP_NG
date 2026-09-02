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

# fpu_model is NOT the same knob as cpu_model, and it does not take a CPU number.
# Amiberry accepts 0 / 68881 / 68882 / 68040 / 68060 -- so "fpu_model=68020" is not a
# valid FPU and the emulator never reaches the guest (empty phase.log, reads exactly
# like the stack hanging). The 040/060 have the FPU on-chip and name themselves; a
# bare 020/030 has no FPU unless one is fitted, which is the honest default for a
# test rig. This only ever worked before because CPU was always 68040.
CPUARG=()
if [ "$CPU" != "0" ]; then
  case "$CPU" in
    68040|68060) FPUMODEL="$CPU" ;;   # on-chip FPU, named after the CPU
    *)           FPUMODEL=0 ;;        # 68020/68030: no coprocessor fitted
  esac
  CPUARG=(-s "cpu_model=$CPU" -s "fpu_model=$FPUMODEL")
fi

docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  export HOME=/tmp/abhome; mkdir -p /tmp/abhome
  sleep 2
  echo '>>> launching amiberry (model $MODEL, CPU=$CPU, NET=$NET, RAM=${RAM}MB Z3, timeout ${TIMEOUT}s)'
  cd /opt/amiberry
  # STOP AS SOON AS THE GUEST IS DONE.
  #
  # This used to be a bare timeout+amiberry, so every run burned its whole
  # timeout however quickly the guest finished -- the test scripts write
  # done.marker as their last act and then the host just sat there. Over a
  # three-tier smoke run that was minutes of pure waiting.
  #
  # The guest disk is a DIRECTORY mount, not an HDF, so guest writes land on this
  # filesystem as they happen and the marker shows up here immediately. Poll for
  # it and stop. TIMEOUT goes back to being the backstop it was meant to be, for a
  # guest that hangs or never reaches the end.
  rm -f '$HDD/done.marker'
  timeout ${TIMEOUT} ./build/amiberry --model $MODEL \
     -r '$ROM' \
     -s filesystem2=rw,DH0:System:'$HDD',0 \
     ${NETARG[*]} ${RAMARG[*]} ${CPUARG[*]} \
     -G >/tmp/ami.log 2>&1 &
  amipid=\$!
  waited=0
  while [ \$waited -lt ${TIMEOUT} ]; do
    if [ -s '$HDD/done.marker' ]; then
      sleep 3                       # let the guest quiesce after its last write
      kill \$amipid 2>/dev/null
      echo \">>> guest finished after \${waited}s (timeout ${TIMEOUT}s)\"
      break
    fi
    if ! kill -0 \$amipid 2>/dev/null; then
      # The emulator stopped by itself. That is NOT the same as the guest
      # hanging, and reporting it as one sends the reader hunting a stack bug
      # that is not there. Say which happened and show the emulator's own output.
      early=1; break
    fi
    sleep 1; waited=\$((waited+1))
  done
  wait \$amipid 2>/dev/null
  if [ -s '$HDD/done.marker' ]; then
    :
  elif [ \"\${early:-0}\" = 1 ]; then
    echo \">>> EMULATOR EXITED on its own after \${waited}s with no done.marker --\"
    echo \">>> this is an emulator/host failure, NOT necessarily a guest hang:\"
    tail -20 /tmp/ami.log
  else
    echo \">>> TIMEOUT after ${TIMEOUT}s with no done.marker -- the guest really did hang\"
  fi
  grep -viE '^[[:space:]]*\$' /tmp/ami.log | tail -30
  echo '>>> amiberry exited'
"
