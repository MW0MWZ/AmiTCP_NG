#!/bin/sh
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Runs INSIDE the emulator container (run-amiberry.sh PRELAUNCH) as the far end
# of the AmiSpeedTest reproduction. Test infra only.
#
# Besides serving, it samples /proc/net/tcp for the test port every 2s. That is
# the whole point: the server's tx_queue tells us whether the bytes it sent were
# ACKED by the guest. If the transfer is dead and tx_queue is 0, every byte got
# through and was acknowledged -- so the data is sitting in the guest's socket
# buffer and the READER was never woken. If tx_queue is non-zero the guest
# stopped acknowledging, which is a different fault entirely. No packet capture
# needed to tell those two apart, and the image has no tcpdump.
#
#   speedtest-lansrv.sh <port> <path-to-uAmiSpeedTest>
set -u
PORT="${1:-8080}"
BIN="${2:?server binary}"

# stdout to a file is block-buffered, and the container is torn down the moment
# the emulator stops -- run 1 lost the server's entire output that way. Same
# trap as an Amiga test program that never gets to Close() its log.
if command -v stdbuf >/dev/null 2>&1; then
  stdbuf -o0 "$BIN" -S -C -p "$PORT" &
else
  "$BIN" -S -C -p "$PORT" &
fi
srv=$!

hex=$(printf ':%04X' "$PORT")
while kill -0 "$srv" 2>/dev/null; do
  echo "--- tcp $(date -u +%H:%M:%S) ---"
  awk -v h="$hex" 'NR==1 || index($2,h) || index($3,h) {print "  " $2, $3, "st="$4, "queues="$5}' \
      /proc/net/tcp 2>/dev/null
  sleep 2
done
wait "$srv"
