#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Build + run the transfer-test host (Samba + FTP + Fitz) for throughput testing
# against an Amiga guest. See docker/README.transferhost.md. Test infra only.
#
#   ./docker/run-transferhost.sh                        # full set (5..1024 MiB), foreground
#   TESTFILE_SIZES="5 50" ./docker/run-transferhost.sh  # smaller set for a quick check
#   DETACH=1 ./docker/run-transferhost.sh               # run in the background
#
# The full default set (5 50 100 166 500 1024 MiB ~= 1.8 GiB) is generated ONCE into
# the amitcp-ng-share volume and reused, so first start takes a while, later starts none.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMG=amitcp-ng-transferhost
NET=amitcp-net                       # shared net so a vAmiga container can reach it
TESTFILE_SIZES="${TESTFILE_SIZES:-5 50 100 166 500 1024}"

docker build -f "$HERE/Dockerfile.transferhost" -t "$IMG" "$HERE"
docker network inspect "$NET" >/dev/null 2>&1 || docker network create "$NET"
docker rm -f transferhost >/dev/null 2>&1 || true

RUNARGS=(--name transferhost --network "$NET"
  -v amitcp-ng-share:/srv/share
  -p 445:445 -p 139:139 -p 21:21 -p 17711:17711 -p 30000-30009:30000-30009
  -e TESTFILE_SIZES="$TESTFILE_SIZES")

if [ "${DETACH:-0}" = "1" ]; then
  docker run -d --rm "${RUNARGS[@]}" "$IMG"
  echo ">>> transfer host running detached as container 'transferhost' on network '$NET'"
  echo ">>> logs: docker logs -f transferhost   |   stop: docker rm -f transferhost"
else
  exec docker run --rm "${RUNARGS[@]}" "$IMG"
fi
