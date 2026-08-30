#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# High-bandwidth networked amiberry for THROUGHPUT testing. Unlike run-amiberry.sh
# (SLIRP user-mode NAT, which proxies TCP through host sockets and is bandwidth
# limited), this pcap-BRIDGES the emulated A2065 onto the container's eth0 at
# layer 2, on the shared 'amitcp-net' Docker network, so the Amiga guest talks to
# the transfer host (docker/run-transferhost.sh) at near-native speed -- making
# the Amiga TCP stack the bottleneck we actually want to measure.
#
# THE A2065 IS A ZORRO II CARD, so this needs a machine with a Zorro bus. It used
# to default to an A600, which has none: the card could never autoconfigure, so
# the guest had no interface at all. That is worth stating plainly because the
# resulting symptom -- zero packets received -- reads exactly like a broken
# receive path, and was recorded as one. Of amiberry's QuickStart models only the
# A4000 attaches the a2065 turnkey, which is why run-amiberry.sh defaults NET=1
# to an A4000 as well.
#
# Docker bridges have no DHCP, so the guest takes a STATIC address. This script
# WRITES ITS OWN interface config (DEVS:NetInterfaces/bridge) and its own
# Startup-sequence, rather than depending on a file some other script owns:
# run-bench.sh regenerates DEVS:NetInterfaces/bench on every run, so pointing
# both at that name meant whichever ran last decided what the other tested.
#
# Requires NET_ADMIN + NET_RAW (pcap opens eth0 in promiscuous mode).
#
#   ./docker/run-amiberry-net.sh            # A4000, 180s, ping the bridge gateway
#   TIMEOUT=300 ./docker/run-amiberry-net.sh
#   MODEL=A4000 IP=172.20.0.20 ./docker/run-amiberry-net.sh
#
# Test infrastructure only -- nothing here ships.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
TIMEOUT="${TIMEOUT:-180}"
HDD="${HDD:-/work/emu/hdd/System/Workbench3.2}"
G="emu/hdd/System/Workbench3.2"
NET="${NET:-amitcp-net}"
IP="${IP:-172.20.0.20}"                 # the amiberry CONTAINER's eth0
GUEST="${GUEST:-172.20.0.30}"           # the Amiga guest's A2065
GW="${GW:-172.20.0.1}"                  # the docker bridge itself
MODEL="${MODEL:-A4000}"                 # MUST have Zorro for the A2065
ROM="${ROM:-/work/emu/rom/kicka4000.rom}"

# ---------------------------------------------------------------------------
# CAPABILITY GATE -- read this before "fixing" the script.
#
# Amiberry v7.1.1 as built here CANNOT bridge the A2065 onto a host interface.
# libpcap is linked and a UAENET pcap path exists in the binary, but the binary
# also carries the string:  "uaenet.device" is currently disabled.
#
# Given -s a2065=eth0 it does NOT fail. It silently falls back to SLIRP user-mode
# NAT and starts a slirp-receive thread. That fallback is the whole reason this
# harness was once recorded as having a broken receive path: the guest was
# addressed on the docker subnet (172.20.0.x) while SLIRP only ever answers on
# 10.0.2.x, so nothing could route, in either direction, on any machine model.
# Verified 2026-08-15 by capturing on the docker bridge from the host during a
# run: ZERO frames from the guest, while amiberry's own log said 'slirp'.
#
# A silent fallback is worse than an error here, because the numbers still look
# like numbers -- they would be SLIRP NAT throughput reported as near-native
# bridge throughput. So refuse, loudly, rather than produce a plausible lie.
#
# (The FS-UAE image cannot do it either -- that binary is not linked against
# libpcap at all. So there is currently no bridged option in this harness; use
# run-bench.sh, which measures over SLIRP and is honest about what it is.)
#
# If a future amiberry enables uaenet, delete this gate and the script should
# work: everything below it is correct, including the Zorro-capable default.
# ---------------------------------------------------------------------------
if [ "${FORCE:-0}" != "1" ]; then
  if docker run --rm amitcp-ng-amiberry:latest \
       bash -c 'strings /opt/amiberry/build/amiberry | grep -q "\"uaenet.device\" is currently disabled"' 2>/dev/null; then
    cat >&2 <<'NOPE'
run-amiberry-net.sh: REFUSING TO RUN.

This emulator build cannot bridge the A2065 to a host interface -- uaenet is
disabled in it. Asking for -s a2065=eth0 does not fail; it quietly falls back to
SLIRP, so any throughput this produced would be SLIRP NAT measured under the
name of a near-native bridge.

Use ./docker/run-bench.sh instead: it measures over SLIRP and says so.
Set FORCE=1 to run anyway (you will be measuring SLIRP).
NOPE
    exit 2
  fi
fi

# The docker network has to exist before a container can join it.
if ! docker network inspect "$NET" >/dev/null 2>&1; then
  echo ">>> creating docker network $NET (172.20.0.0/24)"
  docker network create --subnet 172.20.0.0/24 "$NET" >/dev/null || exit 1
fi

# Our own interface config, under our own name. LF endings only -- AmigaDOS will
# not read a script or config with CR line endings.
cat > "$G/Devs/NetInterfaces/bridge" <<EOF
device=a2065.device
unit=0
address=$GUEST
netmask=255.255.255.0
gateway=$GW
EOF

# Our own boot sequence. Counters are read BEFORE and AFTER the ping so the
# question "did anything arrive" is answered by a delta, not by a single number
# that could have come from anywhere. FailAt 21 so a tool returning 10/20 does
# not abandon the rest of the script and leave a missing marker looking like a hang.
backup="$G/S/Startup-sequence.netbridge-backup"
[ -f "$backup" ] || cp "$G/S/Startup-sequence" "$backup" 2>/dev/null
restore() { [ -f "$backup" ] && mv "$backup" "$G/S/Startup-sequence"; }
trap restore EXIT

cat > "$G/S/Startup-sequence" <<EOF
C:SetPatch >NIL: QUIET
FailAt 21
Assign >NIL: AmiTCP: SYS:AmiTCP
MakeDir >NIL: RAM:ENV
Assign >NIL: ENV: RAM:ENV
Copy >NIL: ENVARC: RAM:ENV ALL QUIET
Echo >SYS:phase.log "1-boot"
C:AddNetInterface DEVS:NetInterfaces/bridge >SYS:bridge.log
Echo >>SYS:phase.log "2-addif"
C:ShowNetStatus >SYS:before.log
Echo >>SYS:phase.log "3-before"
C:ping $GW -c 5 >SYS:ping.log
Echo >>SYS:phase.log "4-ping"
C:ShowNetStatus >SYS:after.log
Echo >>SYS:phase.log "5-after"
Echo >SYS:done.marker "done"
Wait 5
EOF

docker run --rm --name amiberry-net --network "$NET" --ip "$IP" \
  --cap-add NET_ADMIN --cap-add NET_RAW \
  -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  export HOME=/tmp/abhome; mkdir -p /tmp/abhome
  sleep 2
  echo '>>> container eth0:'; ip -4 addr show eth0 | grep inet
  # Watch the wire if the image has tcpdump: that is what separates 'nothing was
  # ever sent' from 'it was sent, the peer replied, and the guest never saw the
  # reply' -- the only interesting question about a bridge. Not all images carry
  # it, so say so rather than leaving an empty file that reads as 'no traffic'.
  if command -v tcpdump >/dev/null 2>&1; then
    (tcpdump -i eth0 -n -l -c 60 \"host $GUEST\" > /work/$G/wire.txt 2>/dev/null &)
  else
    echo 'tcpdump not present in this image -- wire capture skipped' > /work/$G/wire.txt
  fi
  echo '>>> launching amiberry (model $MODEL, a2065=eth0 pcap bridge, ${TIMEOUT}s)'
  cd /opt/amiberry
  timeout ${TIMEOUT} ./build/amiberry --model ${MODEL} \
     -r '$ROM' \
     -s filesystem2=rw,DH0:System:'$HDD',0 \
     -s a2065=eth0 \
     -G 2>&1 | grep -viE '^\s*\$' | tail -25
  echo '>>> amiberry exited'
"

echo
echo '=== did the interface come up? ==='
docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest \
  bash -c "cat /work/$G/bridge.log 2>/dev/null" | head -5
echo '=== guest packet counters, before -> after ==='
docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest bash -c "
  grep -iE 'packets (received|sent)' /work/$G/before.log 2>/dev/null | sed 's/^/  before: /'
  grep -iE 'packets (received|sent)' /work/$G/after.log  2>/dev/null | sed 's/^/  after:  /'"
echo '=== ping ==='
docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest \
  bash -c "tail -4 /work/$G/ping.log 2>/dev/null"
echo '=== what was actually on the wire ==='
docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest \
  bash -c "head -8 /work/$G/wire.txt 2>/dev/null; echo \"  (frames captured: \$(wc -l < /work/$G/wire.txt 2>/dev/null))\""
