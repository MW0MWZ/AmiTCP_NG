#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Build the checksum correctness+speed harness (docker/bench/cksumbench.c) for a CPU
# target, run it headless in fs-uae (CPU-bound -- no network needed), and print the
# result log. The harness fuzz-checks the 68k asm in_cksum() byte-for-byte against a
# reference and times it against the portable C version.
#
#   ./docker/run-cksumbench.sh 68000     # (default) emulate a 68000
#   ./docker/run-cksumbench.sh 68020
#   ./docker/run-cksumbench.sh 68040
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU="${1:-68000}"
TIMEOUT="${TIMEOUT:-120}"
G="$ROOT/emu/hdd/System/Workbench3.2"
XIMG=amigadev/crosstools:m68k-amigaos

echo ">>> building cksumbench for -m$CPU ..."
docker run --rm -v "$ROOT":/work -w /work "$XIMG" bash -c "
  set -e
  m68k-amigaos-gcc -c -m$CPU src/netinet/in_cksum_asm.S -o /tmp/ck.o
  m68k-amigaos-gcc -noixemul -O2 -m$CPU docker/bench/cksumbench.c /tmp/ck.o -o build/cksumbench
  m68k-amigaos-strip build/cksumbench" || { echo '!!! harness build failed'; exit 1; }
"$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" /work/build/cksumbench >/dev/null 2>&1 || true
cp "$ROOT/build/cksumbench" "$G/C/cksumbench"

# Stage a bench Startup-sequence (back up + restore the real one on exit).
cp "$G/S/Startup-sequence" "$G/S/Startup-sequence.ckbak"
trap 'mv -f "$G/S/Startup-sequence.ckbak" "$G/S/Startup-sequence" 2>/dev/null' EXIT
printf 'C:SetPatch QUIET\nWait 2\ncksumbench\nWait 300\n' > "$G/S/Startup-sequence"
rm -f "$G/cksumbench.log"

# Per-CPU fs-uae config. Must live UNDER the repo root -- run-fsuae.sh only mounts
# /work into the container, so a scratchpad/tmp path would be invisible there. build/
# is gitignored. Use the real Amiga model + Kickstart for each CPU (extracted from the
# OS 3.2 CD: A1200=68020, A4000/040=68040) rather than a cpu_model override, which
# emulated unreliably.
case "$CPU" in
  68020) MODEL="A1200";      ROM="kicka1200.rom" ;;
  68040) MODEL="A4000/040";  ROM="kicka4000.rom" ;;
  *)     MODEL="A600";       ROM="kickCDTVa1000a500a2000a600.rom" ;;
esac
CFGREL="build/cksum-$CPU.fs-uae"
CFG="$ROOT/$CFGREL"
cat > "$CFG" <<CFGEOF
amiga_model = $MODEL
kickstart_file = /work/emu/rom/$ROM
hard_drive_0 = /work/emu/hdd/System/Workbench3.2
hard_drive_0_label = System
CFGEOF

echo ">>> running fs-uae ($MODEL / $CPU, up to ${TIMEOUT}s) ..."
TIMEOUT="$TIMEOUT" bash "$ROOT/docker/run-fsuae.sh" "$CFGREL" >/dev/null 2>&1

echo "=============================================================="
echo " cksumbench result -- emulated $CPU"
echo "=============================================================="
cat "$G/cksumbench.log" 2>/dev/null || echo "(no log produced)"
