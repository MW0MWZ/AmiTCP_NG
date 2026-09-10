#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Issue #9 reproduction rig: run AmiSpeedTest in the guest against a LAN server
# built from AmiSpeedTest's OWN source, with the receive ring under observation
# for the whole transfer. Test infra only.
#
# WHY THIS EXISTS. Every transfer test we had reads one connection to EOF.
# AmiSpeedTest does not, and it is the only thing known to trigger #9:
#
#   * up to 6 rounds, each a FRESH connection, sizes multiplied every round
#   * each connect is non-blocking -> select() -> back to blocking
#   * the LAN download loop stops the moment it has the bytes it asked for and
#     closes with data still arriving -- an abortive close, repeated, at speed
#
# So the shape under test here is connection churn and abortive close, not bulk
# throughput. That is the part of the stack no other harness has ever touched.
#
# HOW IT IS WIRED. Amiberry's SLIRP puts the guest on 10.0.2.15 with the gateway
# at 10.0.2.2, and the gateway is the container itself -- so the LAN server runs
# INSIDE the container (via run-amiberry.sh's PRELAUNCH hook) and the guest dials
# 10.0.2.2. The server's own output is printed at the end of the run: it is an
# INDEPENDENT account of how many bytes went out, which is what separates "the
# guest stopped receiving" from "the sender stopped sending".
#
# rxprofile runs in WATCH mode in the background for the whole test, so if the
# transfer dies the ring state AT THE TIME OF DEATH is on disk. Reading the ring
# after the fact would tell us nothing -- the point is to catch it mid-stall.
#
#   ./docker/run-speedtest.sh              # download then upload, 68040 / 256 MB
#   DIR=DOWN ./docker/run-speedtest.sh     # download only
#   CPU=68020 RAM=32 ./docker/run-speedtest.sh
#
# AmiSpeedTest itself is third-party (Karl Jeacle, MIT) and is NOT in this repo:
# fetch https://aminet.net/comm/net/AmiSpeedTest.lha and unpack it to
# tmp/amispeed/ (gitignored).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
G="emu/hdd/System/Workbench3.2"
AST="tmp/amispeed/AmiSpeedTest"
UBUILD="tmp/amispeed/ubuild"

ARCH="${ARCH:--m68000}"          # the shipped library is the one users run
CPU="${CPU:-68040}"              # Andy's PiStorm presents as an 040
# RAM DEFAULTS TO THE CPU, and it has to: a 68000 has a 24-bit address bus and
# CANNOT REACH ZORRO III MEMORY AT ALL. Handing it "RAM=256" builds a machine
# that does not exist -- the guest simply never boots, and it looks exactly like
# a hang in the code under test. Ask for a RAM size explicitly and you get it,
# with a warning if the processor cannot address it.
RAM_EXPLICIT="${RAM+yes}"
if [ -z "$RAM_EXPLICIT" ]; then
  case "$CPU" in
    68000) RAM=0   ;;            # QuickStart chip+slow only, ~9MB: an A500/A600 tier
    68020) RAM=32  ;;            # accelerated A1200 tier
    *)     RAM=256 ;;            # PiStorm tier
  esac
elif [ "$CPU" = "68000" ] && [ "$RAM" != "0" ]; then
  # say() is not defined this early in the file
  printf 'WARNING: CPU=68000 cannot address Zorro III RAM (24-bit bus).\n'
  printf '         RAM=%s will not be reachable by the guest; expect no boot.\n' "$RAM"
fi
PORT="${PORT:-8080}"
DIR="${DIR:-BOTH}"               # DOWN | UP | BOTH | NONE
# Bulk leg: pull test-<BULK>m.bin off the transferhost with our own ftp client.
# AmiSpeedTest cannot do this -- it abandons the climb once a round passes 10s,
BULK="${BULK:-0}"               # MiB, 0 = skip. 100 and 166 are pre-generated
# Zero-window leg: stop reading mid-transfer so the receive buffer fills and we
# advertise a zero window, then read again. Needs no link speed -- a fast NIC
# only makes the buffer fill by itself. See tmp/zwtest.c.
ZW="${ZW:-0}"                   # 1 = run the zero-window recovery test
# SMB leg: the OTHER protocol the stall is reported on. smbfs 2.22 is already
# in the guest; the transferhost share is guest-readable.
SMB="${SMB:-0}"                 # MiB, 0 = skip
# SMBMANY copies a directory of small files instead of one big one. That is the
# workload the stall is actually reported on: thousands of opens, closes and
# directory round trips, not one long stream.
SMBMANY="${SMBMANY:-0}"         # 1 = copy the many-file directory
# CONCURRENT runs the bulk copy and a download AT THE SAME TIME. Every other leg
# here drives one connection; the reported failure has both, and dies on the
# download first while the copy keeps going. Contention for the mbuf pool, the
# receive ring and the single stack task is untested otherwise.
CONCURRENT="${CONCURRENT:-0}"
# REAL=1 runs AmiSpeedTest the way a USER runs it: no HOST=/PORT=, so it takes
# MODE_SPEEDTEST -- fetch the server list, open TCP to each candidate to time
# the latency, pick the closest, then a real HTTP GET/POST against it. That is
# a completely different regime from the LAN server: tens of ms of RTT instead
# of four, real loss, and hundreds of KB in flight rather than almost nothing.
# It sends real traffic over the host connection to third-party servers.
REAL="${REAL:-0}"
# WEB leg: a large download from a REAL internet server over port 80.
# This exists because AmiSpeedTest's own servers are all on 8080, which some
# networks (including this one) drop outright -- 30 SYNs, no SYN-ACKs. The
# point of a real server is not the brand, it is the PATH: tens of ms of RTT
# and hundreds of KB in flight, which no loopback or container peer can give.
# The address is resolved HERE, on purpose: a DNS failure in the guest must
# not be able to masquerade as a transfer stall.
WEB="${WEB:-0}"                 # 1 = run it
WEBHOST="${WEBHOST:-speedtest.tele2.net}"
WEBPATH="${WEBPATH:-/100MB.zip}"
WEBMB="${WEBMB:-0}"             # stop after N MiB; 0 = read to EOF
# e.g. NETEM="delay 100ms" or NETEM="delay 100ms loss 0.5%" -- see run-amiberry.sh
NETEM="${NETEM:-}"
FTPUSER="${FTPUSER:-amiga}"; FTPPASS="${FTPPASS:-amiga}"
DOCKNET="${DOCKNET:-amitcp-net}"
IFACE="${IFACE:-smoke}"          # DEVS:NetInterfaces/<name>, interface <name>0
TIMEOUT="${TIMEOUT:-300}"

say() { printf '%s\n' "$*"; }

# CPUS="68000 68020 68040" runs the whole thing once per processor. The library
# is the SAME shipped 68000 binary every time -- the CPU is what selects code at
# run time (ng_cpu_tune gates the RFC 1323 options on AFF_68020|...|AFF_68060),
# so an A500 and a PiStorm take different routes through the same file.
CPUS="${CPUS:-}"
if [ -n "$CPUS" ]; then
  rc=0
  say "building once for the whole matrix ($ARCH)"
  NG_ARCH="$ARCH" ./docker/build-lib.sh   >/dev/null 2>&1 || { say "ERROR: library build failed"; exit 2; }
  NG_ARCH="$ARCH" ./docker/build-tools.sh >/dev/null 2>&1 || { say "ERROR: tools build failed"; exit 2; }
  for c in $CPUS; do
    say ""; say "############################ CPU=$c ############################"
    CPUS="" CPU="$c" SKIP_BUILD=1 "$0" || rc=1
  done
  exit $rc
fi
die() { say "ERROR: $*"; exit 2; }

# --- preflight --------------------------------------------------------------
[ -f "$AST/AmiSpeedTest" ] || die "no $AST/AmiSpeedTest -- unpack aminet comm/net/AmiSpeedTest.lha into tmp/amispeed/"
[ -f "$AST/src/uamispeedtest.c" ] || die "no $AST/src -- unpack the src.lha inside the archive too"
[ -f "$G/Devs/NetInterfaces/$IFACE" ] || die "no $G/Devs/NetInterfaces/$IFACE"
docker image inspect amitcp-ng-amiberry:latest >/dev/null 2>&1 \
  || die "amitcp-ng-amiberry:latest missing -- docker build -f docker/Dockerfile.amiberry -t amitcp-ng-amiberry:latest docker/"

# --- the far end, built from AmiSpeedTest's own source -----------------------
# Static, because it has to run inside the emulator container. -DLINUX picks the
# author's own Linux branch in server.c; sys/time.h is only pulled in under
# __FreeBSD__ upstream, so force it.
#
# The far end is patched, and the reason matters. From the CLI, AmiSpeedTest sets
# MODE_CLI_CLIENT, not MODE_LAN_CLIENT (that one is GUI-only), so speedTest() is
# called with isLAN=false and the receive loop's byte-count exit is dead code:
#
#     while ((rcvd = recv(...)) > 0) { bytes += rcvd;
#         if (isLAN && bytes >= sizekB*1000) break; }   /* never taken */
#
# It exits on EOF alone -- which is what speedtest.net gives it -- and it never
# sends the "ACK" the stock LAN server blocks for. Pair the two unpatched and
# they wait for each other for ever, which looks EXACTLY like a stack that has
# stopped delivering data: transfer dead, machine alive, nothing logged.
# So the server closes after sending, like speedtest.net. It also unlocks the
# size escalation: the client only multiplies the size after a round COMPLETES,
# so unpatched it never gets past its first 100 kB.
PATCH="docker/amispeedtest-lanserver-close.patch"
if [ ! -x "$UBUILD/uAmiSpeedTest" ] || [ "$AST/src/client.c" -nt "$UBUILD/uAmiSpeedTest" ] \
   || [ "$PATCH" -nt "$UBUILD/uAmiSpeedTest" ]; then
  say "building the LAN server from $AST/src + $PATCH"
  rm -rf "$UBUILD/src"; mkdir -p "$UBUILD/src"
  cp "$AST"/src/*.c "$AST"/src/*.h "$UBUILD/src/" || die "cannot copy AmiSpeedTest source"
  chmod u+w "$UBUILD"/src/*
  ( cd "$UBUILD/src" && patch -s -p0 < "$ROOT/$PATCH" ) || die "patch did not apply"
  ( cd "$UBUILD/src" && gcc -O2 -static -w -DLINUX -include sys/time.h \
      -o "$ROOT/$UBUILD/uAmiSpeedTest" \
      uamispeedtest.c client.c server.c find.c portable.c ) \
    || die "LAN server build failed (needs host gcc)"
fi

# --- stage the guest --------------------------------------------------------
# All three CPUs run the SAME shipped 68000 library, so a CPUS= matrix rebuilt
# an identical binary three times -- 90s a leg for nothing. Build once, then let
# the per-CPU children reuse it.
if [ "${SKIP_BUILD:-0}" = "1" ] && [ -f build/bsdsocket.library ]; then
  say "reusing the $ARCH library already built"
else
  say "building library and tools at $ARCH"
  NG_ARCH="$ARCH" ./docker/build-lib.sh   >/dev/null 2>&1 || die "library build failed"
  NG_ARCH="$ARCH" ./docker/build-tools.sh >/dev/null 2>&1 || die "tools build failed"
fi
cp build/bsdsocket.library "$G/Libs/bsdsocket.library"
for t in AddNetInterface RemoveNetInterface GetNetStatus ShowNetStatus ping rxprofile; do
  [ -f "build/$t" ] && cp "build/$t" "$G/C/$t"
done
# taskprobe is a local diagnostic (tmp/, gitignored): it snapshots the stuck
# task rather than the stack, which is the only way to tell "asleep on the
# socket buffer" from "asleep on something else entirely".
# A missing test binary must STOP the run. The boot script still calls it, the
# call fails, FailAt 21 carries on, done.marker is still written -- and the run
# reports a clean pass having tested nothing. A gate that goes green without
# checking is exactly as dangerous as one that goes red without checking.
[ "$ZW" = "0" ] || [ -f tmp/zwtest ] || die "ZW=1 but tmp/zwtest is not built"
[ "$WEB" = "0" ] || [ -f tmp/httpget ] || die "WEB=1 but tmp/httpget is not built"
[ "$SMB" = "0" ] || [ -f "$G/C/smbfs" ] || die "SMB=$SMB but the guest has no C:smbfs"
[ "$SMBMANY" = "0" ] || [ -f "$G/C/smbfs" ] || die "SMBMANY=1 but the guest has no C:smbfs"
[ -f tmp/taskprobe ] && cp tmp/taskprobe "$G/C/taskprobe"
[ -f tmp/zwtest ]    && cp tmp/zwtest    "$G/C/zwtest"
[ -f tmp/latmeter ]  && cp tmp/latmeter  "$G/C/latmeter"
[ -f tmp/httpget ]   && cp tmp/httpget   "$G/C/httpget"
mkdir -p "$G/AmiTCP"
cp "$AST/AmiSpeedTest" "$G/AmiTCP/AmiSpeedTest"
say "staged $(wc -c < build/bsdsocket.library) byte library, $CPU host, ${RAM}MB"

# --- the bulk far end -------------------------------------------------------
FTPHOST=""
if [ "$BULK" != "0" ] || [ "$SMB" != "0" ] || [ "$SMBMANY" != "0" ] || [ "$CONCURRENT" != "0" ]; then
  FTPHOST="$(docker inspect transferhost \
    --format "{{(index .NetworkSettings.Networks \"$DOCKNET\").IPAddress}}" 2>/dev/null)"
  [ -n "$FTPHOST" ] || die "transferhost is not running on $DOCKNET -- TESTFILE_SIZES=\"$BULK\" DETACH=1 ./docker/run-transferhost.sh"
  for mb in $BULK $SMB; do [ "$mb" = "0" ] && continue
    docker exec transferhost sh -c "[ -f /srv/share/test-${mb}m.bin ]" \
      || die "transferhost has no test-${mb}m.bin (generate it with TESTFILE_SIZES=\"$mb\")"
  done
  [ "$BULK" != "0" ] && say "bulk leg: ftp GET test-${BULK}m.bin from $FTPHOST"
  [ "$SMB"  != "0" ] && say "smb leg:  copy test-${SMB}m.bin from //$FTPHOST/share"
fi

WEBIP=""
if [ "$WEB" != "0" ]; then
  # ahostsv4, NOT hosts: plain getent returns the AAAA first for a dual-stack
  # name and the guest has no IPv6 -- httpget just said "bad ip" and stopped.
  WEBIP="$(getent ahostsv4 "$WEBHOST" 2>/dev/null | awk '{print $1; exit}')"
  case "$WEBIP" in
    *.*.*.*) : ;;
    *) die "cannot resolve $WEBHOST to IPv4 (got '$WEBIP')" ;;
  esac
  say "web leg: http://$WEBHOST$WEBPATH  ($WEBIP)"
fi

# --- guest boot script ------------------------------------------------------
# LF endings only -- AmigaDOS refuses a script with CRs. The AmiTCP: assign is
# not optional: without it AmigaDOS puts up an insert-volume requester and waits
# for ever, which looks EXACTLY like the stack hanging.
run_down=""; run_up=""
if [ "$DIR" = "NONE" ]; then :; else
[ "$DIR" = "DOWN" ] || [ "$DIR" = "BOTH" ] && run_down=1
[ "$DIR" = "UP" ]   || [ "$DIR" = "BOTH" ] && run_up=1
fi

{
cat <<EOF
; AMITCP_NG_SPEEDTEST_GENERATED
C:SetPatch >NIL: QUIET
FailAt 21
Assign >NIL: AmiTCP: SYS:AmiTCP
MakeDir >NIL: RAM:ENV
Assign >NIL: ENV: RAM:ENV
Copy >NIL: ENVARC: RAM:ENV ALL QUIET
Echo >SYS:phase.log "1-boot"
C:AddNetInterface DEVS:NetInterfaces/$IFACE >SYS:ifup.log
Echo >>SYS:phase.log "2-addif"
C:GetNetStatus >>SYS:ifup.log
C:ping 10.0.2.2 -c 2 >SYS:ping.log
Echo >>SYS:phase.log "3-ping"
C:Run >NIL: C:rxprofile ${IFACE}0 WATCH >SYS:rxwatch.log
C:Run >NIL: C:latmeter 400 >NIL:
Echo >>SYS:phase.log "4-watch"
EOF
ASTARGS="HOST=10.0.2.2 PORT=$PORT"
[ "$REAL" != "0" ] && ASTARGS=""          # no HOST -> real speedtest.net servers
[ -n "$run_down" ] && cat <<EOF
C:Run >NIL: AmiTCP:AmiSpeedTest $ASTARGS DOWN UNIT=MEGABIT >SYS:speed-down.log
Echo >>SYS:phase.log "5-down-launched"
Wait 20
C:nslookup www.speedtest.net >SYS:dns.log
C:netstat -s >SYS:ns-20.log
C:taskprobe AmiSpeedTest >SYS:tp-20.log
C:rxprofile ${IFACE}0 >SYS:rx-20.log
Echo >>SYS:phase.log "6-ns20"
Wait 40
C:netstat -s >SYS:ns-60.log
C:taskprobe AmiSpeedTest >SYS:tp-60.log
C:rxprofile ${IFACE}0 >SYS:rx-60.log
Echo >>SYS:phase.log "7-ns60"
Wait 60
C:netstat -s >SYS:ns-120.log
C:taskprobe AmiSpeedTest >SYS:tp-120.log
C:rxprofile ${IFACE}0 >SYS:rx-120.log
Echo >>SYS:phase.log "8-ns120"
EOF
[ -n "$run_up" ] && cat <<EOF
C:Run >NIL: AmiTCP:AmiSpeedTest $ASTARGS UP UNIT=MEGABIT >SYS:speed-up.log
Echo >>SYS:phase.log "8-up-launched"
Wait 40
EOF
# The bulk transfer runs in the FOREGROUND: rxprofile WATCH is already sampling
# every 2s, so a stall is timestamped either way, and a foreground run means a
# hang shows up as a missing done.marker instead of being silently stepped over.
[ "$ZW" != "0" ] && cat <<EOF
C:zwtest 10.0.2.2 $PORT >SYS:zw.log
Echo >>SYS:phase.log "11-zerowindow"
C:rxprofile ${IFACE}0 >SYS:rx-zw.log
EOF
[ "$BULK" != "0" ] && cat <<EOF
C:rxprofile ${IFACE}0 >SYS:rx-bulk-pre.log
Echo >>SYS:phase.log "9-bulk-start"
C:ftp GET $FTPHOST test-${BULK}m.bin NIL: USER $FTPUSER PASS $FTPPASS >SYS:bulk.log
Echo >>SYS:phase.log "10-bulk-done"
C:rxprofile ${IFACE}0 >SYS:rx-bulk-post.log
EOF
# smbfs is a filesystem HANDLER: it stays resident once started, so it has to be
# Run in the background and given a moment to mount before the volume exists.
[ "$CONCURRENT" != "0" ] && cat <<EOF
C:Run >NIL: C:smbfs SERVICE=//$FTPHOST/share VOLUME=SMB USER=$FTPUSER PASSWORD=$FTPPASS QUIET
Wait 15
C:List SMB: >SYS:smb-mount.log
Echo >>SYS:phase.log "19-concurrent-start"
C:MakeDir >NIL: SYS:manycopy
C:Run >NIL: C:Copy SMB:many SYS:manycopy ALL QUIET
Wait 5
C:Run >NIL: AmiTCP:AmiSpeedTest HOST=10.0.2.2 PORT=$PORT DOWN UNIT=MEGABIT >SYS:speed-conc.log
Wait 30
C:rxprofile ${IFACE}0 >SYS:rx-conc-30.log
C:netstat -s >SYS:ns-conc-30.log
Echo >>SYS:phase.log "20-conc-30"
Wait 60
C:rxprofile ${IFACE}0 >SYS:rx-conc-90.log
C:netstat -s >SYS:ns-conc-90.log
C:taskprobe >SYS:tp-conc.log
Echo >>SYS:phase.log "21-conc-90"
Wait 90
C:List SYS:manycopy >SYS:conc-files.log
C:rxprofile ${IFACE}0 >SYS:rx-conc-180.log
Echo >>SYS:phase.log "22-conc-180"
EOF
[ "$SMBMANY" != "0" ] && cat <<EOF
C:Run >NIL: C:smbfs SERVICE=//$FTPHOST/share VOLUME=SMB USER=$FTPUSER PASSWORD=$FTPPASS QUIET
Wait 15
C:List SMB: >SYS:smb-mount.log
C:rxprofile ${IFACE}0 >SYS:rx-many-pre.log
Echo >>SYS:phase.log "17-many-start"
C:MakeDir >NIL: SYS:manycopy
C:Copy SMB:many SYS:manycopy ALL QUIET
Echo >>SYS:phase.log "18-many-done"
C:List SYS:manycopy >SYS:many-result.log
C:taskprobe >SYS:tp-many.log
C:rxprofile ${IFACE}0 >SYS:rx-many-post.log
EOF
[ "$SMB" != "0" ] && cat <<EOF
C:Run >NIL: C:smbfs SERVICE=//$FTPHOST/share VOLUME=SMB USER=$FTPUSER PASSWORD=$FTPPASS QUIET
Wait 15
C:List SMB: >SYS:smb-mount.log
C:rxprofile ${IFACE}0 >SYS:rx-smb-pre.log
Echo >>SYS:phase.log "12-smb-start"
C:Type SMB:test-${SMB}m.bin >NIL:
Echo >>SYS:phase.log "13-smb-done"
C:List SMB: >>SYS:smb-mount.log
C:rxprofile ${IFACE}0 >SYS:rx-smb-post.log
EOF
# A real test is server selection plus up to six escalating rounds EACH WAY over
# a real internet path. The LAN waits are nowhere near enough, and cutting it
# short loses the client's output entirely -- it buffers until it exits.
[ "$REAL" != "0" ] && cat <<EOF
Wait 240
C:rxprofile ${IFACE}0 >SYS:rx-real.log
Echo >>SYS:phase.log "14-real-tail"
EOF
[ "$WEB" != "0" ] && cat <<EOF
C:rxprofile ${IFACE}0 >SYS:rx-web-pre.log
Echo >>SYS:phase.log "15-web-start"
C:httpget $WEBIP $WEBHOST $WEBPATH $WEBMB
Echo >>SYS:phase.log "16-web-done"
C:rxprofile ${IFACE}0 >SYS:rx-web-post.log
EOF
cat <<EOF
C:ShowNetStatus >SYS:netstat-final.log
Echo >>SYS:phase.log "7-final"
Echo >SYS:done.marker "done"
Wait 5
EOF
} > "$G/S/Startup-sequence.new"
tr -d '\r' < "$G/S/Startup-sequence.new" > "$G/S/Startup-sequence.gen"
rm -f "$G/S/Startup-sequence.new"

# --- swap it in, and always put the real one back ---------------------------
backup="$G/S/Startup-sequence.speedtest-backup"
if grep -q 'AMITCP_NG_.*_GENERATED' "$G/S/Startup-sequence" 2>/dev/null; then
  [ -f "$backup" ] || say "WARNING: live Startup-sequence is generated and there is NO backup."
else
  cp "$G/S/Startup-sequence" "$backup" 2>/dev/null
fi
restore() { [ -f "$backup" ] && mv "$backup" "$G/S/Startup-sequence"; }
trap restore EXIT
mv "$G/S/Startup-sequence.gen" "$G/S/Startup-sequence"

rm -f "$G"/*.log "$G/done.marker"

# --- run --------------------------------------------------------------------
say ""
say "=== AmiSpeedTest $DIR against the LAN server on 10.0.2.2:$PORT ==="
PRELAUNCH="/work/docker/speedtest-lansrv.sh $PORT /work/$UBUILD/uAmiSpeedTest" \
  NETWORK="${FTPHOST:+$DOCKNET}" \
  NET=1 CPU="$CPU" RAM="$RAM" TIMEOUT="$TIMEOUT" NETEM="$NETEM" ./docker/run-amiberry.sh 2>&1 \
  | tee /tmp/ng-speedtest-emu.log | grep -E --line-buffered '^>>>|prelaunch|kB' 

# --- report -----------------------------------------------------------------
gcat() { docker run --rm -v "$ROOT":/work -w /work amitcp-ng-amiberry:latest \
           bash -c "cat '/work/$G/$1' 2>/dev/null"; }
for f in phase.log ifup.log ping.log speed-down.log speed-up.log rxwatch.log tp-120.log rx-120.log ns-120.log latmeter.log dns.log rx-real.log httpget.log rx-web-pre.log rx-web-post.log zw.log rx-zw.log bulk.log rx-bulk-post.log smb-mount.log rx-smb-post.log rx-many-post.log tp-many.log speed-conc.log rx-conc-90.log tp-conc.log rx-conc-180.log netstat-final.log; do
  body="$(gcat "$f")"
  [ -n "$body" ] || continue
  say ""; say "--- $f ---"; printf '%s\n' "$body"
done

say ""
if [ -s "$G/done.marker" ]; then
  say "guest ran to completion -- read speed-down.log and rxwatch.log for a stall"
else
  say "NO done.marker: the guest did not finish. phase.log above names the last step"
  say "it got past; rxwatch.log is the ring state while it was stuck."
fi
