#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Build the full set of AmiTCP_NG command-line tools -- our own name/argument/output-
# compatible equivalents of the Roadshow commands, so a "full install" ships a complete
# drop-in tool set (Roadie, NetMon and existing scripts drive them unchanged). Output
# binaries land in build/ (NOT committed -- compiled artifacts, release only).
#
# A -noixemul build. The route/interface/status tools share src/tools/ng_lvo.h, so
# they compile with -Isrc/tools. NG_ARCH selects the CPU multilib (default -m68000);
# build-release.sh sets -m68020/-m68040 to match the library variant it is packaging.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC="$ROOT/docker/cc.sh m68k-amigaos-gcc"
CFLAGS="-noixemul -O2 ${NG_ARCH:--m68000} -Wall -Werror -Isrc/tools -Isrc"

echo ">>> building AmiTCP_NG tools ..."

# One source (+ the shared $VER cookie), one binary.
build_one() {   # <output> <source> [extra flags...]
  local out="$1" src="$2"; shift 2
  echo "    $out"
  $CC $CFLAGS "$@" "$src" src/tools/ng_vertag.c -o "build/$out"
}

# Online / Offline share netonoff.c, selected by a -D.
build_one Online  src/tools/netonoff.c -DDO_ONLINE
build_one Offline src/tools/netonoff.c -DDO_OFFLINE

# The rest are one-source-per-command.
build_one AddNetInterface       src/tools/AddNetInterface.c
build_one ConfigureNetInterface src/tools/ConfigureNetInterface.c
build_one RemoveNetInterface    src/tools/RemoveNetInterface.c
build_one NetShutdown           src/tools/NetShutdown.c
build_one AddNetRoute           src/tools/AddNetRoute.c
build_one DeleteNetRoute        src/tools/DeleteNetRoute.c
build_one GetNetStatus          src/tools/GetNetStatus.c
build_one ShowNetStatus         src/tools/ShowNetStatus.c
build_one ping                  src/tools/ping.c
build_one netstat               src/tools/netstat.c
build_one tftp                  src/tools/tftp.c
build_one nslookup              src/tools/nslookup.c
build_one ftp                   src/tools/ftp.c
build_one sntp                  src/tools/sntp.c

# Diagnostics. Compiled always; PACKAGED only into -beta releases (build-release.sh).
build_one rxprofile             src/tools/rxprofile.c

# The docker builds run as root and leave build/ root-owned; hand it back so the host
# user can stage the release tree.
"$ROOT/docker/cc.sh" chown -R "$(id -u):$(id -g)" /work/build >/dev/null 2>&1 || true

echo ">>> tools built:"
for t in Online Offline AddNetInterface ConfigureNetInterface RemoveNetInterface \
         NetShutdown AddNetRoute DeleteNetRoute GetNetStatus ShowNetStatus ping netstat tftp nslookup ftp sntp rxprofile; do
  if [ -f "$ROOT/build/$t" ]; then
    printf '    %-22s %s bytes\n' "$t" "$(wc -c < "$ROOT/build/$t")"
  else
    echo "    !!! MISSING: $t" >&2; exit 1
  fi
done
