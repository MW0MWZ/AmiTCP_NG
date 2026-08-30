#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# TIERED smoke test. Run this before calling any stack change "done".
#
#   TIER 1  68000 build on an A600, loopback only.        <-- GATE
#   TIER 2  68020 then 68040 build on the A4000 rig.      <-- only if tier 1 passed
#
# WHY TIERED, in this order. Tier 1 is the cheap, broad one: it is the only
# machine in the harness that runs the plain 68000 multilib, and a fault in the
# library vectors, the loopback path or first-touch start-up shows up there in
# ~2 minutes without a NIC, DHCP or a link speed in the way. Running the
# expensive network tiers first only means finding a 68000 codegen fault after
# twenty minutes of DHCP runs that were never going to be informative. So tier 1
# is a GATE: if it fails, tier 2 does not run at all, because a stack that
# cannot do a UDP loopback round trip has nothing useful to say about DHCP.
#
# Tier 2 is the FIDELITY one: 68040 CPU and a link that claims 100 Mbit via the
# bps= override -- an emulated A2065 reports 10 Mbit, which puts the stack on a
# different auto-tune path than any real machine we care about.
#
# RAM is deliberately MID-LADDER (32 MB Z3 + the A4000's own ~9 MB = ~42 MB, the
# 16-64MB tier), not a big-RAM machine. ng_ram_tier() installs a different set of
# socket-buffer, mbuf-pool and ring sizes per tier, and the two ENDS of that
# ladder are the easy ones to hit by accident -- tier 1's A600 already covers the
# small end. A mid tier is where the auto-tune actually has to CHOOSE: at 42 MB
# the RAM ceiling (262800) is BELOW the 100 Mbit link target (524140), so the
# clamp is exercised, where on a big-RAM machine the link target simply wins.
#
# Both the 68020 and 68040 libraries run there because THE ARCH IS WHAT BIT US:
# the 4.1.6 DHCP failure existed only in the 68040 build, and the harness ran the
# 68000 one, so for weeks it "could not be reproduced". Never smoke one arch and
# assume the others.
#
#   ./docker/run-smoke.sh            # tier 1, then tier 2 if it passed
#   ./docker/run-smoke.sh tier1      # gate only
#   ./docker/run-smoke.sh tier2      # network tiers only (skips the gate)
#
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
G="emu/hdd/System/Workbench3.2"
AB="${AB:-amitcp-ng-amiberry:latest}"	# overridable so the preflight itself can be tested
WHICH="${1:-all}"

# ---------------------------------------------------------------------------
# PREFLIGHT. A harness that cannot run must say so and must NEVER report test
# failures it did not observe.
#
# This exists because it already went wrong: the amiberry image was pruned off a
# machine, every guest launch failed with "Unable to find image", and the suite
# dutifully reported "RESULT: 0 passed, 4 failed" -- which is indistinguishable
# from the stack having been broken by the change under test. The library had
# built and staged perfectly. A gate that reports RED without testing anything is
# exactly as dangerous as one that reports GREEN without testing anything, and the
# same rule applies to both: the result must mean what it says.
#
# Exit 2, deliberately distinct from 1 (tests ran and failed), so a caller can
# tell "the harness is broken" from "the code is broken" without parsing text.
# ---------------------------------------------------------------------------
preflight() {
  local img missing=0

  if ! docker info >/dev/null 2>&1; then
    echo "HARNESS NOT READY: docker is not usable (is the daemon running?)." >&2
    echo "NOTHING WAS TESTED. This is not a test failure." >&2
    exit 2
  fi

  for img in "$AB" amigadev/crosstools:m68k-amigaos; do
    if ! docker image inspect "$img" >/dev/null 2>&1; then
      echo "HARNESS NOT READY: docker image '$img' is missing." >&2
      missing=1
    fi
  done

  if [ "$missing" -ne 0 ]; then
    echo "" >&2
    echo "  Build the emulator image with:" >&2
    echo "    docker build -f docker/Dockerfile.amiberry -t $AB ." >&2
    echo "  (the cross-compiler image is pulled automatically on first use)" >&2
    echo "" >&2
    echo "NOTHING WAS TESTED. This is not a test failure." >&2
    exit 2
  fi

  if [ ! -d "$G" ]; then
    echo "HARNESS NOT READY: guest image directory '$G' is missing." >&2
    echo "NOTHING WAS TESTED. This is not a test failure." >&2
    exit 2
  fi
}
preflight

# exttest carries assertions that went stale as the stack grew -- they assert on
# netdb entry counts and RoadshowData shapes the shipped stack no longer matches.
# They are NOT stack faults, and chasing them has wasted real time before.
#
# BY NAME, not by count. A count alone has a blind spot: one stale assertion
# getting fixed while a real regression appears leaves the total unchanged and
# the gate green. Comparing the SET means a newly-broken vector is reported even
# if something else quietly started passing.
#
# Verified as pre-existing on 2026-08-15 by stashing the working tree and running
# this same gate against the unmodified sources: identical six. Re-baseline only
# after doing that A/B yourself -- never to make a red run go green.
EXTTEST_KNOWN_FAILS="netstat sizes
protoent count
rsd change
rsd enospc
rsd nodecount
servent count"

pass=0; fail=0
say()  { printf '%s\n' "$*"; }
ok()   { pass=$((pass+1)); say "    PASS  $*"; }
bad()  { fail=$((fail+1)); say "    FAIL  $*"; }

# Guest files are written by the emulator running as root in the container, so
# read them back through a container rather than fighting host permissions.
gcat() { docker run --rm -v "$ROOT":/work -w /work "$AB" bash -c "cat '/work/$G/$1' 2>/dev/null"; }
gclean() { docker run --rm -v "$ROOT":/work -w /work "$AB" \
             bash -c "cd '/work/$G' && rm -f *.log done.marker phase.log" >/dev/null 2>&1; }

# AmigaDOS refuses a script with CR line endings, so these MUST stay LF-only.
# printf, not echo -e, and no editor round-trips.
#
# `FailAt 21` matters more than it looks. exttest exits 20 when any assertion
# trips, and the default FailAt 10 makes AmigaDOS abandon the rest of the script
# at that point -- so a stale assertion silently swallowed every later step,
# including the done.marker. That made a MERELY-FAILING run indistinguishable
# from a HUNG one. With FailAt 21 the sequence always runs to the end, so
# done.marker means exactly "the guest did not hang" and pass/fail comes from
# the logs, where it can be judged properly.
#
# udptest also runs BEFORE exttest deliberately: it is the broadest single
# statement about whether the stack works at all, so it should not sit behind
# the test most likely to trip.
seq_tier1() {
  cat > "$G/S/Startup-sequence" <<'EOF'
; AMITCP_NG_SMOKE_GENERATED
C:SetPatch >NIL: QUIET
FailAt 21
Assign >NIL: AmiTCP: SYS:AmiTCP
MakeDir >NIL: RAM:ENV
Assign >NIL: ENV: RAM:ENV
Copy >NIL: ENVARC: RAM:ENV ALL QUIET
Echo >SYS:phase.log "1-boot"
AmiTCP:firsttouchtest
Echo >>SYS:phase.log "2-firsttouch"
AmiTCP:udptest
Echo >>SYS:phase.log "3-udp"
AmiTCP:exttest
Echo >>SYS:phase.log "4-ext"
C:CheckAmiTCPNGConfig >SYS:check.log
Echo >>SYS:phase.log "5-check"
Echo >SYS:done.marker "done"
Wait 5
EOF
}

seq_tier2() {
  cat > "$G/S/Startup-sequence" <<'EOF'
; AMITCP_NG_SMOKE_GENERATED
C:SetPatch >NIL: QUIET
FailAt 21
Assign >NIL: AmiTCP: SYS:AmiTCP
MakeDir >NIL: RAM:ENV
Assign >NIL: ENV: RAM:ENV
Copy >NIL: ENVARC: RAM:ENV ALL QUIET
Echo >SYS:phase.log "1-boot"
C:GetNetStatus DEBUG >SYS:tier.log
Echo >>SYS:phase.log "2-tier"
C:AddNetInterface DEVS:NetInterfaces/smoke >SYS:pass1.log
Echo >>SYS:phase.log "3-addif"
C:GetNetStatus >>SYS:pass1.log
C:ShowNetStatus >>SYS:pass1.log
C:GetNetStatus DEBUG >>SYS:tier.log
Echo >>SYS:phase.log "4-status"
C:ping 10.0.2.2 -c 3 >SYS:ping.log
Echo >>SYS:phase.log "5-ping"
C:RemoveNetInterface smoke0 >SYS:teardown.log
Echo >>SYS:phase.log "6-teardown"
Echo >SYS:done.marker "done"
Wait 5
EOF
}

stage() {   # stage <arch>
  local arch="$1"
  say "  building library and tools at ${arch}"
  NG_ARCH="$arch" ./docker/build-lib.sh   >/dev/null 2>&1 || { bad "library build ($arch)"; return 1; }
  NG_ARCH="$arch" ./docker/build-tools.sh >/dev/null 2>&1 || { bad "tools build ($arch)";   return 1; }
  cp build/bsdsocket.library "$G/Libs/bsdsocket.library"
  for t in AddNetInterface RemoveNetInterface GetNetStatus ShowNetStatus \
           CheckAmiTCPNGConfig ping; do
    [ -f "build/$t" ] && cp "build/$t" "$G/C/$t"
  done
  say "  staged $(wc -c < build/bsdsocket.library) byte library"
}

# --- TIER 1 -----------------------------------------------------------------
tier1() {
  say ""; say "=== TIER 1: 68000 build, A600, loopback (GATE) ==="
  stage -m68000 || return 1
  seq_tier1; gclean
  TIMEOUT=150 ./docker/run-amiberry.sh >/dev/null 2>&1

  local before=$fail
  [ -n "$(gcat done.marker)" ] && ok "boot completed (done.marker)" \
                               || bad "no done.marker -- the sequence never finished (hang?)"
  case "$(gcat firsttouchtest.log)" in
    *"FIRST TOUCH OK"*) ok "first-touch start-up" ;;
    *)                  bad "first-touch (lazy stack start)" ;;
  esac
  case "$(gcat udptest.log)" in
    *"UDP LOOPBACK ROUND-TRIP OK"*) ok "UDP loopback round trip" ;;
    *)                              bad "UDP loopback round trip" ;;
  esac
  local got new fixed
  # A missing or empty log must NOT read as "no failures". Require exttest's own
  # trailer before trusting a FAIL-line diff at all -- otherwise a guest that died
  # before writing anything scores a clean pass, which is the worst way for a gate
  # to be wrong.
  case "$(gcat exttest.log)" in
    *"RESULT: EXTENSION VECTORS"*) ;;
    *) bad "exttest wrote no RESULT line -- it did not run to completion"
       return 1 ;;
  esac
  got=$(gcat exttest.log | sed -n 's/^FAIL *//p' | sort -u)
  new=$(comm -23 <(printf '%s\n' "$got") <(printf '%s\n' "$EXTTEST_KNOWN_FAILS" | sort -u))
  fixed=$(comm -13 <(printf '%s\n' "$got") <(printf '%s\n' "$EXTTEST_KNOWN_FAILS" | sort -u))
  if [ -z "$new" ]; then
    ok "extension vectors (only the known-stale failures)"
  else
    bad "extension vectors: failures that are NOT in the known-stale set"
    printf '%s\n' "$new" | sed 's/^/          NEW: /'
  fi
  [ -n "$fixed" ] && { say "    NOTE  known-stale failures that now PASS -- re-baseline deliberately:";
                       printf '%s\n' "$fixed" | sed 's/^/          FIXED: /'; }
  [ "$fail" -eq "$before" ]
}

# --- TIER 2 -----------------------------------------------------------------
tier2_one() {   # tier2_one <arch>
  local arch="$1"
  say ""; say "=== TIER 2: ${arch} build, A4000/68040, ~42 MB, 100 Mbit link ==="
  stage "$arch" || return 1
  seq_tier2; gclean
  RAM=32 NET=1 CPU=68040 TIMEOUT=300 ./docker/run-amiberry.sh >/dev/null 2>&1

  local before=$fail
  [ -n "$(gcat done.marker)" ] && ok "boot completed (done.marker)" \
                               || bad "no done.marker -- sequence never finished (hang?)"
  # A lease, specifically. An APIPA 169.254 address is the shape the 4.1.6 bug
  # took, and it is NOT a pass -- the interface comes up either way.
  case "$(gcat pass1.log)" in
    *"169.254."*)     bad "DHCP fell back to APIPA link-local (this is the 4.1.6 failure shape)" ;;
    *"10.0.2.15"*)    ok  "DHCP lease 10.0.2.15" ;;
    *)                bad "DHCP: no lease and no APIPA -- interface did not come up" ;;
  esac
  case "$(gcat tier.log)" in
    *"100000000 bps"*) ok "link-speed override applied (100 Mbit auto-tune path)" ;;
    *)                 bad "bps= override did not reach the stack" ;;
  esac
  case "$(gcat tier.log)" in
    *"16-64MB"*) ok "RAM tier is 16-64MB (the middle of the ladder)" ;;
    *)           bad "wrong RAM tier -- rig is not representative" ;;
  esac
  # ping is here because a CLI tool once shipped crashing on exit, having been
  # build-verified but never actually run. Executing it, and reaching the marker
  # AFTER it, is the check that matters.
  case "$(gcat ping.log)" in
    *"bytes from"*) ok "ping to the gateway got replies" ;;
    *)              bad "ping produced no replies" ;;
  esac
  [ "$fail" -eq "$before" ]
}

# --- drive ------------------------------------------------------------------
# Back up the live Startup-sequence -- but NEVER overwrite a good backup with one
# of our own generated scripts. If a previous run was killed hard (SIGKILL, OOM,
# host crash) the EXIT trap never fired, so the live file is still a generated
# test script; copying THAT over the backup destroys the only surviving original,
# and emu/ is gitignored so there is no second chance. The marker line is how a
# generated script identifies itself.
backup="$G/S/Startup-sequence.smoke-backup"
if grep -q 'AMITCP_NG_SMOKE_GENERATED' "$G/S/Startup-sequence" 2>/dev/null; then
  if [ -f "$backup" ]; then
    say "NOTE: leftover generated Startup-sequence from an interrupted run;"
    say "      keeping the existing backup rather than overwriting it."
  else
    say "WARNING: Startup-sequence is a generated test script and there is NO backup."
    say "         The original boot sequence is already lost -- restore it by hand."
  fi
else
  cp "$G/S/Startup-sequence" "$backup" 2>/dev/null
fi
restore() { [ -f "$backup" ] && mv "$backup" "$G/S/Startup-sequence"; }
trap restore EXIT

rc=0
if [ "$WHICH" = "all" ] || [ "$WHICH" = "tier1" ]; then
  if ! tier1; then
    say ""
    say "TIER 1 FAILED -- stopping. Tier 2 not run: a stack that cannot do a UDP"
    say "loopback round trip has nothing useful to say about DHCP."
    say ""
    say "RESULT: $pass passed, $fail failed"
    exit 1
  fi
fi
if [ "$WHICH" = "all" ] || [ "$WHICH" = "tier2" ]; then
  tier2_one -m68020 || rc=1
  tier2_one -m68040 || rc=1
fi

say ""
say "RESULT: $pass passed, $fail failed"
exit $rc
