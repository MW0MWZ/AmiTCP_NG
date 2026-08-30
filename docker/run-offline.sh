#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# INVOLUNTARY OFFLINE / RECOVERY test: does the stack notice its SANA-II device
# coming back when the driver never tells it?
#
# There are two ways an interface goes offline and they are not the same test.
# ConfigureNetInterface OFFLINE is the ADMINISTRATIVE one: the stack clears IFF_UP
# itself, sends S2_OFFLINE, and knows exactly what happened. The one that actually
# breaks machines is the INVOLUNTARY one -- a cable pulled, a WiFi radio
# de-associating -- where the stack finds out only because its pending reads fail
# with S2ERR_OUTOFSERVICE, and finds out that the device is BACK only if the driver
# bothers to say so. Some do not. The reported symptom on real hardware is exactly
# that: the interface goes offline fine, the device comes back, and nothing
# whatsoever is logged.
#
# The emulated A2065 never spontaneously disappears, so this harness produces the
# event deliberately: tmp/sanakick.c opens the SAME device and unit as a second
# opener and sends S2_OFFLINE, which is unit-wide and aborts the stack's pending
# reads with S2ERR_OUTOFSERVICE while the stack still believes it is up. It waits,
# sends S2_ONLINE, and then does NOTHING ELSE -- no stack call, no event. Anything
# that happens after that is the stack noticing on its own.
#
# WHAT MAKES THIS TEST NON-VACUOUS. Two failure modes would let it report a pass
# having tested nothing, so both are checked explicitly before the recovery result
# is believed at all:
#
#   1. sanakick opening a DIFFERENT instance of the driver (which is what happens
#      if the device is named by path rather than by the name the stack used).
#      Every command then succeeds against a driver nobody is using.
#   2. the stack never noticing the offline in the first place -- in which case
#      "still has an address at the end" is trivially true and means nothing.
#
# So the run must show the interface going DOWN before it is allowed to show it
# coming back.
#
# THE STOCK EMULATED DRIVER IS NOT ENOUGH ON ITS OWN. The A2065 implements
# S2_ONEVENT correctly, so it announces its own return and the event path recovers
# the interface every time -- which was MEASURED, by running this with the watchdog
# probe compiled out and watching it pass anyway. A run without NOEVENTS=1 therefore
# says nothing about the probe, however green it looks. NOEVENTS=1 first cripples
# the driver (tmp/noevents.c) so that the probe is the only mechanism left.
#
# The four runs that together cover the feature:
#
#   NOEVENTS=1 ./docker/run-offline.sh                 the probe must recover it
#   ./docker/run-offline.sh                            the event path still works
#   MODE=admin ./docker/run-offline.sh                 an operator's offline sticks
#   MODE=race NOEVENTS=1 OFFSECS=30 ./docker/run-offline.sh
#                                                      ... even mid-probe
#
# Other knobs: ARCH=-m68020, OFFSECS=n (how long the device stays away),
# SETTLE=n (recovery budget afterwards), TIMEOUT=n (emulator run length).
#
# Test infrastructure only -- nothing here ships.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
G="emu/hdd/System/Workbench3.2"
AB="${AB:-amitcp-ng-amiberry:latest}"	# overridable so the preflight itself can be tested

# PREFLIGHT -- see the long note in run-smoke.sh. Short version: if the harness
# cannot run, say so and exit 2. Never report failures we did not observe; a
# missing emulator image once produced "0 passed, 2 failed" for every MODE, which
# reads exactly like the change under test having broken the stack.
if ! docker info >/dev/null 2>&1; then
  echo "HARNESS NOT READY: docker is not usable (is the daemon running?)." >&2
  echo "NOTHING WAS TESTED. This is not a test failure." >&2
  exit 2
fi
for _img in "$AB" amigadev/crosstools:m68k-amigaos; do
  if ! docker image inspect "$_img" >/dev/null 2>&1; then
    echo "HARNESS NOT READY: docker image '$_img' is missing." >&2
    echo "  Build the emulator image with:" >&2
    echo "    docker build -f docker/Dockerfile.amiberry -t $AB ." >&2
    echo "NOTHING WAS TESTED. This is not a test failure." >&2
    exit 2
  fi
done
unset _img

ARCH="${ARCH:--m68040}"
OFFSECS="${OFFSECS:-20}"
# NOEVENTS=1 cripples the driver's S2_ONEVENT first (tmp/noevents.c), so the stack
# is not told the device came back and the watchdog probe is the ONLY way it can
# find out. WITHOUT this the emulated A2065 announces its own return correctly and
# the event path recovers the interface every time -- measured, not assumed -- so a
# NOEVENTS=0 run says nothing whatsoever about the probe, however green it is.
NOEVENTS="${NOEVENTS:-0}"

# MODE picks which of the three questions this run answers.
#
#   kick   the device drops out on its own and comes back. Does the stack notice
#          and reconfigure itself? (the recovery test)
#   admin  an operator takes the interface offline. Does it STAY offline?
#   race   the device drops out on its own, so the stack starts watching for it,
#          and THEN an operator takes the interface offline while that is in
#          flight -- and the device returns afterwards. Does it stay offline?
#
# The last two are the ones that matter most, and `race` especially: an automatic
# recovery that can undo a deliberate shutdown is worse than no recovery at all,
# and the window it needs is exactly the one a flapping device opens.
MODE="${MODE:-kick}"
# Recovery budget after the device returns: the probe interval is ~5 s, then the
# interface is re-raised and a fresh DHCP lease has to be acquired.
SETTLE="${SETTLE:-35}"
TIMEOUT="${TIMEOUT:-330}"

pass=0; fail=0
say()  { printf '%s\n' "$*"; }
ok()   { pass=$((pass+1)); say "    PASS  $*"; }
bad()  { fail=$((fail+1)); say "    FAIL  $*"; }
gcat() { docker run --rm -v "$ROOT":/work -w /work "$AB" bash -c "cat '/work/$G/$1' 2>/dev/null"; }
gclean() { docker run --rm -v "$ROOT":/work -w /work "$AB" \
             bash -c "cd '/work/$G' && rm -f *.log done.marker phase.log" >/dev/null 2>&1; }

# --- build and stage ---------------------------------------------------------
say "=== OFFLINE/RECOVERY mode=${MODE} noevents=${NOEVENTS} (${ARCH}, A4000/68040) ==="
say "  building library and tools at ${ARCH}"
NG_ARCH="$ARCH" ./docker/build-lib.sh   >/dev/null 2>&1 || { bad "library build"; exit 1; }
NG_ARCH="$ARCH" ./docker/build-tools.sh >/dev/null 2>&1 || { bad "tools build";   exit 1; }

say "  building sanakick (the second opener that pulls the device out)"
docker run --rm -v "$ROOT":/work -w /work amigadev/crosstools:m68k-amigaos bash -c \
  "m68k-amigaos-gcc -noixemul -O2 ${ARCH} -Wall -Isrc/netinclude tmp/sanakick.c -o build/sanakick" \
  || { bad "sanakick build"; exit 1; }

if [ "$NOEVENTS" = "1" ] || [ "$MODE" = "hang" ]; then
  say "  building noevents (makes the driver swallow S2_ONEVENT, as the real one does)"
  docker run --rm -v "$ROOT":/work -w /work amigadev/crosstools:m68k-amigaos bash -c \
    "m68k-amigaos-gcc -noixemul -O2 ${ARCH} -Wall -Isrc/netinclude tmp/noevents.c -o build/noevents" \
    || { bad "noevents build"; exit 1; }
  cp build/noevents "$G/C/noevents"
fi

cp build/bsdsocket.library "$G/Libs/bsdsocket.library"
for t in AddNetInterface RemoveNetInterface GetNetStatus ShowNetStatus ping Offline Online ConfigureNetInterface; do
  [ -f "build/$t" ] && cp "build/$t" "$G/C/$t"
done
cp build/sanakick "$G/C/sanakick"
if [ "$MODE" = "shutdown" ]; then
  docker run --rm -v "$ROOT":/work -w /work amigadev/crosstools:m68k-amigaos bash -c \
    "m68k-amigaos-gcc -noixemul -O2 ${ARCH} -Wall -Isrc/tools -Isrc tmp/shutdownclient.c -o build/shutdownclient" \
    || { bad "shutdownclient build"; exit 1; }
  cp build/shutdownclient "$G/C/shutdownclient"
  docker run --rm -v "$ROOT":/work -w /work amigadev/crosstools:m68k-amigaos bash -c \
    "m68k-amigaos-gcc -noixemul -O2 ${ARCH} -Wall tmp/libprobe.c -o build/libprobe" \
    || { bad "libprobe build"; exit 1; }
  cp build/libprobe "$G/C/libprobe"
  [ -f build/NetShutdown ] && cp build/NetShutdown "$G/C/NetShutdown"
fi

if [ "$MODE" = "dns" ]; then
  docker run --rm -v "$ROOT":/work -w /work amigadev/crosstools:m68k-amigaos bash -c \
    "m68k-amigaos-gcc -noixemul -O2 ${ARCH} -Wall tmp/dnsflushtest.c -o build/dnsflushtest" \
    || { bad "dnsflushtest build"; exit 1; }
  cp build/dnsflushtest "$G/C/dnsflushtest"
fi
say "  staged $(wc -c < build/bsdsocket.library) byte library"

# --- the guest sequence ------------------------------------------------------
# Back up the real Startup-sequence FIRST, and never overwrite a good backup with
# one of our own generated scripts: if a previous run was killed hard the EXIT trap
# never fired, so the live file is still a test script, and copying THAT over the
# backup destroys the only original -- emu/ is gitignored, so there is no second
# chance. The marker line is how a generated script identifies itself.
backup="$G/S/Startup-sequence.offline-backup"
if grep -q 'AMITCP_NG_SMOKE_GENERATED' "$G/S/Startup-sequence" 2>/dev/null; then
  [ -f "$backup" ] || say "WARNING: Startup-sequence is already a generated test script and there"
  [ -f "$backup" ] || say "         is NO backup -- restore the original by hand."
else
  cp "$G/S/Startup-sequence" "$backup" 2>/dev/null
fi
restore() { [ -f "$backup" ] && mv "$backup" "$G/S/Startup-sequence"; }
trap restore EXIT

# LF endings only, and FailAt 21 so a tool returning 10/20 does not abandon the
# rest of the script and leave a missing marker looking like a hang.
#
# sanakick is given the device name EXACTLY as DEVS:NetInterfaces/smoke gives it
# ("a2065.device"), so it attaches to the resident driver the stack is using
# rather than loading a second copy of it.
if [ "$NOEVENTS" = "1" ]; then		# NOT MODE=hang: it installs its own patch below
  # AFTER AddNetInterface, and the order is not a detail. OpenDevice() finds a
  # driver by its resident Exec name only once something has opened it; before the
  # stack does, "a2065.device" is not in the device list and the open falls through
  # to the DOS loader, which looks in DEVS: (not DEVS:Networks:) and fails. Patching
  # first therefore silently does nothing -- which is exactly what happened on the
  # first attempt, and the run still looked almost green.
  #
  # Nothing between here and the kick uses S2_ONEVENT (only the offline path arms
  # one), so installing it at this point is still in good time. It holds for longer
  # than the rest of the run, then removes itself.
  KICKARGS="a2065.device 0 $OFFSECS noprobe"
  PATCHLINE="Run >NIL: C:noevents a2065.device 0 $((OFFSECS + SETTLE + 60))
Wait 3"
else
  KICKARGS="a2065.device 0 $OFFSECS"
  PATCHLINE=""
fi

case "$MODE" in
  kick)
    EVENTLINES="C:sanakick $KICKARGS
Echo >>SYS:phase.log \"4-kicked\"" ;;
  admin)
    # The STACK-LEVEL administrative offline, which is the one that must never be
    # undone. Note this is ConfigureNetInterface, NOT the Offline command: Offline
    # takes a DEVICE name and drives S2_OFFLINE directly, so from the stack's point
    # of view it is indistinguishable from a cable being pulled -- and it SHOULD be
    # treated that way, which is what MODE=kick covers.
    #
    # A status is taken immediately afterwards as well, so "it stayed down" can be
    # told apart from "it never went down".
    EVENTLINES="C:ConfigureNetInterface smoke0 OFFLINE >SYS:offline.log
C:GetNetStatus >SYS:mid.log
C:ShowNetStatus >>SYS:mid.log
Echo >>SYS:phase.log \"4-admin-offline\"" ;;
  race)
    # The hard one, and the reason the probe state is cleared synchronously in
    # sana_down(). sanakick runs in the background so the device is GONE and the
    # stack is actively watching for it; the operator's offline then lands in the
    # middle of that watch, with a probe read outstanding on the driver. sanakick
    # afterwards brings the device back. Nothing about that return is allowed to
    # raise an interface an operator has just taken down.
    EVENTLINES="Run >NIL: C:sanakick $KICKARGS
Wait 12
C:ConfigureNetInterface smoke0 OFFLINE >SYS:offline.log
C:GetNetStatus >SYS:mid.log
C:ShowNetStatus >>SYS:mid.log
Echo >>SYS:phase.log \"4-race-offline\"" ;;
  dns)
    # Is the DNS cache discarded when the name server set changes? Two ways of
    # changing it are covered in one boot, because both matter and they take
    # different code paths:
    #
    #   rmns  the server is removed through RemoveDomainNameServer()
    #   cold  the server goes away WITH the interface that supplied it -- the
    #         "last interface came down" case
    #
    # The cache is re-warmed between them, because the rmns phase leaves it empty
    # and a cold phase run against an empty cache would pass for the wrong reason.
    # ORDER MATTERS, and getting it wrong makes the cold phase test nothing.
    #
    # The interface-loss path only withdraws name servers the interface OWNS
    # (Roadshow's IFC_AssociatedDNS), which is right -- an interface going offline
    # has no business deleting a server somebody else configured. The rmns phase
    # restores the server through the PUBLIC AddDomainNameServer vector, and that
    # vector deliberately marks it UNOWNED. So running rmns first leaves an
    # ownerless server that the interface will not withdraw, the cold phase's
    # flush never fires, and the run reports a stack bug that is not one. (It did.)
    #
    # So cold goes FIRST, while the server is still the one DHCP put there, and
    # rmns runs afterwards once the interface has recovered.
    EVENTLINES="C:dnsflushtest warm
Echo >>SYS:phase.log \"4-warm\"
Run >NIL: C:sanakick $KICKARGS
Wait 15
C:dnsflushtest cold
Echo >>SYS:phase.log \"5-cold\"
Wait 60
C:dnsflushtest warm2
Echo >>SYS:phase.log \"6-rewarm\"
C:dnsflushtest rmns
Echo >>SYS:phase.log \"7-rmns\"" ;;
  onoff)
    # The full operator round trip, which is what people actually run: take it
    # offline, put it back. This became a REQUIRED test the moment an
    # administrative offline started scrubbing the address -- before that, a
    # failure to reconfigure on the way back up was partly masked because the old
    # address was still lying there. Now, if SM_Online does not put the
    # configuration back, the interface returns unnumbered and useless, which
    # would be a worse state than the bug this fixes.
    EVENTLINES="C:ConfigureNetInterface smoke0 OFFLINE >SYS:offline.log
C:GetNetStatus >SYS:mid.log
C:ShowNetStatus >>SYS:mid.log
Echo >>SYS:phase.log \"4-offline\"
Wait 5
C:ConfigureNetInterface smoke0 ONLINE >SYS:online.log
Echo >>SYS:phase.log \"5-online\"" ;;
  ifname)
    # Reproduces the X-Surf-100 report: the operator names the INTERFACE, not the
    # device. `smoke` is DEVS:NetInterfaces/smoke, which says device=a2065.device --
    # exactly the shape of `Offline X-Surf-100` against DEVS:NetInterfaces/X-Surf-100.
    # On the old build this fails with "could not open device"; the whole point of the
    # change is that it resolves. Online is exercised too, since both share the code.
    # BOTH forms, in one boot. The classic DEVICE name must keep working exactly as it
    # did -- the interface-name lookup is additive and must not have cost us Roadshow
    # compatibility -- and then the interface name must work too.
    EVENTLINES="C:Offline a2065.device >SYS:offdev.log
C:Online a2065.device >>SYS:offdev.log
Echo >>SYS:phase.log \"4-devicename\"
Wait 5
C:Offline smoke >SYS:offname.log
C:GetNetStatus >SYS:mid.log
C:ShowNetStatus >>SYS:mid.log
Echo >>SYS:phase.log \"5-offline-by-ifname\"
Wait 5
C:Online smoke >SYS:onname.log
Echo >>SYS:phase.log \"6-online-by-ifname\"" ;;
  hang)
    # DOES A SILENT DRIVER TAKE THE WHOLE LIBRARY DOWN?
    #
    # sana_device_online() sends S2_ONLINE through sana_doio_bounded(), from inside
    # ConfigureInterfaceTagList, which holds the library's syscall semaphore -- and that
    # semaphore serialises much of the socket API. The command is bounded now (15s, then
    # AbortSanaIO, then abandon), so a driver that swallows it should cost one interface
    # and fifteen seconds. This test is what proves that: it was written when the call was
    # a plain DoIO() with no timeout and no abort, and it caught two separate faults that
    # made the bound useless in practice -- first an unbounded wait, later a CheckIO() that
    # reported a never-sent request as already complete. Keep it: the asynchronous path it
    # exercises is the one no healthy driver ever takes, and therefore the one that rots.
    #
    # So: cripple S2_ONLINE (command 25), ask for ONLINE, and then ask the stack a
    # COMPLETELY UNRELATED question. If GetNetStatus never answers and the sequence never
    # reaches done.marker, the whole library is wedged, not just this interface -- which
    # is a far bigger fault than "the interface will not come back".
    KICKARGS="a2065.device 0 $OFFSECS noprobe"
    # CRIPPLE=0 runs the identical sequence with NO driver patch at all. That is the
    # bisect: if it still wedges, the cripple is irrelevant and the fault is in the
    # plain OFFLINE->ONLINE path; if it does not, the swallowed command is implicated.
    if [ "${CRIPPLE:-1}" = "1" ]; then
      CRIPPLELINE="Run >NIL: C:noevents a2065.device 0 200 ${SWALLOW:-25}
Wait 3"
    else
      CRIPPLELINE="Wait 3"
    fi
    EVENTLINES="C:ConfigureNetInterface smoke0 OFFLINE >SYS:offline.log
Echo >>SYS:phase.log \"4-offline\"
$CRIPPLELINE
Echo >>SYS:phase.log \"5-swallowing-S2_ONLINE\"
Run >NIL: C:ConfigureNetInterface smoke0 ONLINE >SYS:online.log
Echo >>SYS:phase.log \"6-online-launched\"
Wait 25
C:GetNetStatus >SYS:probe.log
Echo >>SYS:phase.log \"7-stack-still-answers\"" ;;
  shutdown)
    # A real application is holding bsdsocket.library open and sitting in a blocking
    # wait -- exactly the situation the report describes. NetShutdown must notify it,
    # give it time, and only then take the network down.
    #
    # STUBBORN=1 makes that client refuse to let go, which tests the other half: the
    # shutdown must be DECLINED with a client count, and the stack must carry on
    # serving afterwards (the api_show() restore is the easy thing to get wrong).
    if [ "${STUBBORN:-0}" = "1" ]; then CLIENTARG="120 hold"; else CLIENTARG="120"; fi
    EVENTLINES="Run >NIL: C:shutdownclient $CLIENTARG
Wait 4
Echo >>SYS:phase.log \"4-client-running\"
C:NetShutdown TIMEOUT=15 >SYS:netshutdown.log
Echo >>SYS:phase.log \"5-netshutdown-returned\"
Wait 3
Echo >>SYS:phase.log \"5b-guest-alive-after-shutdown\"
C:libprobe
Echo >>SYS:phase.log \"5c-probe-returned\"
C:GetNetStatus >SYS:after2.log
Echo >>SYS:phase.log \"6-getnetstatus-returned\"
C:AddNetInterface DEVS:NetInterfaces/smoke >SYS:readd.log
Echo >>SYS:phase.log \"7-interface-re-added\"" ;;
  *)
    say "unknown MODE '$MODE' (want: kick, admin, race, dns, onoff, ifname, hang, shutdown)"; exit 2 ;;
esac

cat > "$G/S/Startup-sequence" <<EOF
; AMITCP_NG_SMOKE_GENERATED
C:SetPatch >NIL: QUIET
FailAt 21
Assign >NIL: AmiTCP: SYS:AmiTCP
MakeDir >NIL: RAM:ENV
Assign >NIL: ENV: RAM:ENV
Copy >NIL: ENVARC: RAM:ENV ALL QUIET
Echo >SYS:phase.log "1-boot"
C:AddNetInterface DEVS:NetInterfaces/smoke >SYS:addif.log
Echo >>SYS:phase.log "2-addif"
$PATCHLINE
C:GetNetStatus >SYS:before.log
C:ShowNetStatus >>SYS:before.log
Echo >>SYS:phase.log "3-before"
$EVENTLINES
Wait $SETTLE
C:GetNetStatus >SYS:after.log
C:ShowNetStatus >>SYS:after.log
Echo >>SYS:phase.log "5-after"
Echo >SYS:done.marker "done"
Wait 5
EOF

gclean
RAM=32 NET=1 CPU=68040 TIMEOUT="$TIMEOUT" ./docker/run-amiberry.sh >/dev/null 2>&1

# --- results -----------------------------------------------------------------
say ""
[ -n "$(gcat done.marker)" ] && ok "boot completed (done.marker)" \
                             || bad "no done.marker -- the sequence never finished (hang?)"

kick="$(gcat sanakick.log)"
log="$(gcat AmiTCP.log)"
before="$(gcat before.log)"
after="$(gcat after.log)"

say ""
say "  --- sanakick said ---"
printf '%s\n' "$kick" | sed 's/^/      /'

# 1. Did the interface have an address to lose? True for every mode: without this
#    every "no address at the end" result below is trivially true and worthless.
case "$before" in
  *10.0.2.15*) ok "interface was up with a DHCP lease beforehand" ;;
  *) bad "no lease beforehand -- nothing was tested"; say ""; say "RESULT: $pass passed, $fail failed"; exit 1 ;;
esac

if [ "$MODE" = "shutdown" ]; then
  # --- NetShutdown notify + grace period -----------------------------------------
  cl="$(gcat shutdownclient.log)"
  ns="$(gcat netshutdown.log)"
  say ""
  say "  --- the application ---"; printf '%s\n' "$cl" | sed 's/^/      /'
  say "  --- NetShutdown ---";     printf '%s\n' "$ns" | sed 's/^/      /'
  say ""

  case "$cl" in
    *"CANNOT TEST"*) bad "the test application never got going -- nothing was tested"
                     say ""; say "RESULT: $pass passed, $fail failed"; exit 1 ;;
  esac

  # 1. THE reported fault: no notification at all.
  case "$cl" in
    *"BREAK RECEIVED"*) ok "the application WAS sent a break" ;;
    *"WAS NOT NOTIFIED"*) bad "the application got NO break -- THIS IS THE REPORTED BUG" ;;
    *) bad "the application never reported either way" ;;
  esac

  # 2. NetShutdown must not have returned before the application was told. If it
  #    returned first, the notification is decorative.
  case "$(gcat phase.log)" in
    *"5-netshutdown-returned"*) ok "NetShutdown ran to completion" ;;
    *) bad "NetShutdown never returned (hung?)" ;;
  esac

  if [ "${STUBBORN:-0}" = "1" ]; then
    # A client that ignores the warning must NOT be able to keep the network up. The
    # breaks are a courtesy, not a veto -- it still comes down, and the caller is told
    # how many were still holding on when it did.
    case "$ns" in
      *"has been shut down (1 program was still using it)"*)
         ok "it came down anyway, and said who was still holding on" ;;
      *"has been shut down"*)
         bad "it came down but did not report the straggler" ;;
      *"still using the network"*)
         bad "a single stubborn application blocked the shutdown -- it must not" ;;
      *) bad "NetShutdown gave no verdict" ;;
    esac
  else
    case "$ns" in
      *"has been shut down"*)      ok "the network shut down once the client let go" ;;
      *"still using the network"*) bad "refused even though the client closed its base" ;;
      *) bad "NetShutdown gave no verdict" ;;
    esac
  fi

  say ""
  say "RESULT: $pass passed, $fail failed"
  [ "$fail" -eq 0 ]
  exit $?
fi

if [ "$MODE" = "hang" ]; then
  # --- does a silent S2_ONLINE wedge the library? -------------------------------
  ph="$(gcat phase.log)"
  say ""
  say "  --- phases reached ---"; printf '%s\n' "$ph" | sed 's/^/      /'
  say ""

  # Prove the cripple took, and took on the RIGHT command -- otherwise the whole
  # experiment is unfalsifiable.
  if [ "${CRIPPLE:-1}" = "0" ]; then
    say "    NOTE  no driver patch this run (CRIPPLE=0) -- pure OFFLINE/ONLINE path"
  else
  case "$(gcat noevents.log)" in
    *"now swallowing command ${SWALLOW:-25}"*) ok "driver is swallowing command ${SWALLOW:-25}" ;;
    *"now swallowing command"*)
       bad "noevents swallowed the WRONG command -- the argument did not take"
       gcat noevents.log | sed 's/^/          /' ;;
    *) bad "noevents did not install -- nothing was crippled"
       gcat noevents.log | sed 's/^/          /' ;;
  esac
  fi

  case "$ph" in
    *"5-swallowing-S2_ONLINE"*) ok "the driver was crippled and ONLINE was issued" ;;
    *) bad "the run never got as far as issuing ONLINE -- test inconclusive"
       say ""; say "RESULT: $pass passed, $fail failed"; exit 1 ;;
  esac

  # THE question. GetNetStatus needs the same semaphore ConfigureInterfaceTagList holds.
  if [ -n "$(gcat probe.log)" ] && case "$ph" in *"7-stack-still-answers"*) true;; *) false;; esac; then
    ok "the stack still answered an unrelated query -- the library did NOT wedge"
  else
    bad "THE WHOLE LIBRARY WEDGED: an unrelated query never returned after a silent"
    say "          S2_ONLINE. One blocked device command takes the stack with it."
  fi

  case "$(gcat done.marker)" in
    "") bad "the guest never reached the end of its sequence (consistent with a hang)" ;;
    *)  ok "the guest sequence ran to completion" ;;
  esac

  say ""
  say "RESULT: $pass passed, $fail failed"
  [ "$fail" -eq 0 ]
  exit $?
fi

if [ "$MODE" = "ifname" ]; then
  # --- naming an INTERFACE where a DEVICE is expected --------------------------
  offlog="$(gcat offname.log)"
  onlog="$(gcat onname.log)"
  midlog="$(gcat mid.log)"
  say ""
  say "  --- Offline smoke ---"; printf '%s\n' "$offlog" | sed 's/^/      /'
  say "  --- Online smoke  ---"; printf '%s\n' "$onlog"  | sed 's/^/      /'
  say ""

  # Regression first: the documented Roadshow form must be untouched by all this.
  case "$(gcat offdev.log)" in
    *"could not open"*|*[Ee]rror*)
      bad "a plain DEVICE name stopped working -- Roadshow compatibility broken"
      gcat offdev.log | sed 's/^/          /' ;;
    *) ok "a plain device name still works (Offline/Online a2065.device)" ;;
  esac

  case "$offlog" in
    *"could not open"*)
      bad "Offline still cannot resolve an interface name -- THIS IS THE REPORTED BUG" ;;
    *"using 'a2065.device'"*)
      ok "Offline resolved interface 'smoke' to its device" ;;
    *) bad "Offline gave neither the resolution message nor the old error" ;;
  esac

  case "$midlog" in
    *"No networking interfaces are available"*)
       ok "the interface really did go offline (not just a friendly message)" ;;
    "") bad "no status captured after the offline" ;;
    *)  bad "the interface did not go down -- the command reported success falsely" ;;
  esac

  case "$onlog" in
    *"could not open"*) bad "Online cannot resolve an interface name" ;;
    *"using 'a2065.device'"*) ok "Online resolved interface 'smoke' too" ;;
    *) bad "Online gave neither the resolution message nor the old error" ;;
  esac

  say ""
  say "RESULT: $pass passed, $fail failed"
  [ "$fail" -eq 0 ]
  exit $?
fi

if [ "$MODE" = "onoff" ]; then
  # --- operator round trip: offline, then online again ---------------------------
  midlog="$(gcat mid.log)"
  say ""
  say "  --- offline said ---";  gcat offline.log | sed 's/^/      /'
  say "  --- online said  ---";  gcat online.log  | sed 's/^/      /'
  say ""

  case "$midlog" in
    *"No networking interfaces are available"*)
       ok "it went down when asked" ;;
    "") bad "no status captured after the offline" ;;
    *)  bad "it did not go down -- nothing below is meaningful" ;;
  esac
  case "$midlog" in
    *"Local host address"*10.0.2.15*) bad "its address survived the offline" ;;
    *)                                ok "its address was withdrawn while offline" ;;
  esac

  # The half that matters most: it has to come all the way back, on its own.
  case "$after" in
    *"No networking interfaces are available"*)
       bad "IT NEVER CAME BACK UP after being put online" ;;
    *) ok "it came back up" ;;
  esac
  case "$after" in
    *169.254.*)  bad "it came back on an APIPA address, not a lease" ;;
    *10.0.2.15*) ok "it re-acquired its DHCP lease" ;;
    *)           bad "it came back UNNUMBERED -- worse than before the offline" ;;
  esac
  case "$after" in
    *"Domain name system servers"*10.0.2.3*) ok "its name servers came back" ;;
    *) bad "it came back with no name servers -- nothing would resolve" ;;
  esac

  say ""
  say "RESULT: $pass passed, $fail failed"
  [ "$fail" -eq 0 ]
  exit $?
fi

if [ "$MODE" = "dns" ]; then
  # --- DNS cache flush ---------------------------------------------------------
  # Each phase's verdict is its own RESULT line. A phase that could not run says
  # CANNOT TEST rather than passing, because every "it failed to resolve" check
  # here is trivially satisfied by a machine that could never resolve anything.
  for ph in warm cold warm2 rmns; do
    say ""
    say "  --- dnsflushtest $ph ---"
    gcat "dnsflush-$ph.log" | sed 's/^/      /'
  done
  say ""

  case "$(gcat dnsflush-warm.log)" in
    *"RESULT: WARM OK"*)     ok "a name resolved twice, so there is something to go stale" ;;
    *"CANNOT TEST"*)         bad "no DNS available in this run -- nothing below is meaningful" ;;
    *)                       bad "the warm-up phase did not complete" ;;
  esac

  case "$(gcat dnsflush-rmns.log)" in
    *"RESULT: FLUSHED ON REMOVE"*)
       ok "cache discarded when the name server was removed" ;;
    *"the cache was not flushed"*)
       bad "STALE: it resolved a name with NO name servers configured" ;;
    *) bad "the remove phase did not reach a verdict" ;;
  esac

  case "$(gcat dnsflush-warm2.log)" in
    *"RESULT: WARM OK"*) ok "cache re-warmed after the interface recovered" ;;
    *)                   bad "could not re-warm the cache -- the remove phase proves nothing" ;;
  esac

  case "$(gcat dnsflush-cold.log)" in
    *"RESULT: FLUSHED ON INTERFACE LOSS"*)
       ok "cache discarded when the interface supplying DNS went away" ;;
    *"stale cache"*)
       bad "STALE: it resolved a name after its name servers went with the interface" ;;
    *) bad "the cold phase did not reach a verdict" ;;
  esac

  say ""
  say "RESULT: $pass passed, $fail failed"
  [ "$fail" -eq 0 ]
  exit $?
fi

if [ "$MODE" != "admin" ]; then
  # 2. Did the kick actually reach the driver the stack is using? A second, separate
  #    instance of the driver would also report io_Error 0 here, so this alone is
  #    not enough -- check 3 is what proves it was the right one.
  case "$kick" in
    *"S2_OFFLINE sent, io_Error 0"*) ok "sanakick offlined the device" ;;
    *) bad "sanakick could not offline the device -- the test did not run" ;;
  esac

  # 3. Did the STACK notice? This is the check that makes the whole run meaningful:
  #    it is the only proof that sanakick attached to the driver the stack is using.
  # Match the message the INTERFACE logs, not the words "out of service": the stack
  # renders S2ERR_OUTOFSERVICE through sana2perror as "Device driver is offline",
  # and prefixes it with the interface name. The interface prefix matters -- the
  # bare string also appears when ConfigureNetInterface's own S2_OFFLINE finds the
  # device already offline ("S2_OFFLINE: Device driver is offline"), which is a
  # different event entirely and would make this check pass without the stack ever
  # having noticed anything.
  case "$log" in
    *"smoke: Device driver is offline"*)
       ok "the stack saw its device go out of service" ;;
    *) bad "the stack never noticed the device drop -- sanakick probably opened a"
       say "          SECOND copy of the driver, so nothing below means anything" ;;
  esac
fi

if [ "$MODE" = "kick" ]; then
# 4. Recovery. Both halves matter: the log line says the stack worked out that the
#    device was back, and the address says it actually reconfigured itself rather
#    than coming back up unnumbered.
case "$log" in
  *"is online again"*) ok "the stack noticed the device return on its own" ;;
  *) bad "the stack never noticed the device coming back" ;;
esac

# 4b. WHICH mechanism found it. With the driver crippled there must be no event to
#     act on, so a recovery credited to the driver would mean the cripple did not
#     take -- which is the one way this run could look green while testing nothing.
if [ "$NOEVENTS" = "1" ] || [ "$MODE" = "hang" ]; then
  case "$(gcat noevents.log)" in
    *"now swallowing command 23"*) ok "driver was crippled: S2_ONEVENT swallowed" ;;
    *) bad "the noevents patch did not install -- the driver still reports events" ;;
  esac
  case "$log" in
    *"is online again (watchdog probe"*) ok "recovery came from the WATCHDOG PROBE" ;;
    *"is online again (reported by its driver"*)
       bad "recovery came from a driver event -- the cripple did not take, so the"
       say "          probe was never exercised" ;;
    *) : ;;
  esac
else
  case "$log" in
    *"is online again (reported by its driver"*) ok "recovery came from the driver's event" ;;
    *"is online again (watchdog probe"*) ok "recovery came from the watchdog probe" ;;
    *) : ;;
  esac
fi

case "$after" in
  *169.254.*) bad "recovered to an APIPA link-local address, not a lease" ;;
  *10.0.2.15*) ok "interface reconfigured itself: DHCP lease back" ;;
  *) bad "interface did not get its address back" ;;
esac

else
  # --- admin / race: the interface MUST STAY DOWN ----------------------------
  #
  # This is the safety half of the feature. Automatic recovery that can undo a
  # deliberate shutdown is worse than no automatic recovery, so these two failures
  # are more serious than a missed recovery would be.
  # The offline command must have WORKED, and "no complaint on stdout" is not
  # evidence of that -- a run where the command failed outright scored a pass on
  # exactly that reasoning, because its message ("could not open ...") happened to
  # contain neither "error" nor "fail". So require the interface to be observably
  # down straight afterwards. Without this, every check below is vacuous: an
  # interface that never went offline trivially "stays" offline.
  offlog="$(gcat offline.log)"
  midlog="$(gcat mid.log)"
  case "$offlog" in
    *"could not"*|*[Ee]rror*|*[Ff]ail*)
      bad "the offline command itself failed"
      printf '%s\n' "$offlog" | sed 's/^/          /' ;;
    *) ok "the offline command was accepted" ;;
  esac
  #
  # GetNetStatus's "No networking interfaces are available and configured" is the
  # up/down signal, NOT the presence of an address in ShowNetStatus's summary. An
  # administrative offline takes the interface DOWN but does not scrub its address
  # (only the involuntary path does, via ss_offcleanup), so ShowNetStatus keeps
  # printing "Local host address = ..." for a down interface. Asserting on the
  # address here reported a failure that was not one.
  case "$midlog" in
    *"No networking interfaces are available"*)
       ok "the interface was observably down straight after the offline" ;;
    "") bad "no status was captured after the offline" ;;
    *)  bad "the interface was still up right after the offline -- it never went"
        say "          down, so nothing below means anything" ;;
  esac

  case "$log" in
    *"is online again"*)
      bad "THE INTERFACE WAS BROUGHT BACK UP AFTER AN OPERATOR TOOK IT OFFLINE"
      printf '%s\n' "$log" | grep -n "is online again" | sed 's/^/          /' ;;
    *) ok "nothing tried to bring it back up" ;;
  esac

  case "$after" in
    *"No networking interfaces are available"*)
       ok "it was STILL down at the end of the run" ;;
    *) bad "the interface came back UP after an operator took it offline" ;;
  esac

  if [ "$MODE" = "admin" ]; then
    # An operator's offline must deconfigure the interface exactly as a device
    # that vanished on its own does. It used to go down still advertising an
    # address it could not use and name servers that could not be reached.
    # Checked in `after` rather than `mid` on purpose: the teardown is deferred to
    # the network task, so testing it the instant the command returns would be a
    # race, and a flaky test is worse than no test.
    case "$after" in
      *"Local host address"*10.0.2.15*)
         bad "it still advertises 10.0.2.15 after being taken offline" ;;
      *) ok "its address was withdrawn" ;;
    esac
    case "$after" in
      *"Domain name system servers"*10.0.2.3*)
         bad "it still offers name server 10.0.2.3 after being taken offline" ;;
      *) ok "its name servers were withdrawn" ;;
    esac
  fi
fi

say ""
say "  --- what the stack logged around the event ---"
printf '%s\n' "$log" | grep -iE "service|offline|online again|dhcp|probe|abort" | tail -20 | sed 's/^/      /'

say ""
say "RESULT: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
