#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Assemble the installable release archive: the Amiga Installer script + payload
# (the self-starting bsdsocket.library and the AddNetInterface command, both built
# fresh) laid out for the Installer, then packed with LhA.
#
# Sources:
#   install/          -- the Installer script, ReadMe, Network-Startup, example
#                        interface configs, example hosts (all committed, source).
#   build/            -- the freshly built binaries (NOT committed; built here).
#   COPYING/COPYRIGHTS-- licences (committed).
#
# Output: build/release/AmiTCP_NG.lha  (a compiled artifact -> release only, never
# committed to the repo).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="$ROOT/build/release/AmiTCP_NG"
# Project version -- single source of truth is src/bsdsocket.library_rev.h. Names the
# artifacts AmiTCP_NG-v<version>.{lha,adf}. Reads the FULL quoted string, so any
# pre-release suffix (e.g. 4.1.1-beta) is preserved in the artifact names and the
# installer marker, not stripped to the bare Major.Minor.Revision.
VER="$(grep -E 'define[[:space:]]+AMITCP_NG_VER' "$ROOT/src/bsdsocket.library_rev.h" | sed -E 's/.*"([^"]+)".*/\1/' | head -1)"
[ -n "$VER" ] || { echo "!!! build-release: could not read AMITCP_NG_VER from src/bsdsocket.library_rev.h" >&2; exit 1; }
LHA_NAME="AmiTCP_NG-v$VER.lha"
ADF_NAME="AmiTCP_NG-v$VER.adf"
OUT="$ROOT/build/release/$LHA_NAME"

# 1. Build the binaries the archive ships: the library and the full tool set (our
# Roadshow-compatible commands). The archive always carries the whole set; both the
# full-install and upgrade paths install our command set (the upgrade replaces the
# existing stack's tools too, since Roadshow's own tools hang our library) -- the
# Installer decides what to place and where.
echo ">>> building bsdsocket.library ..."
"$ROOT/docker/build-lib.sh" >/dev/null
"$ROOT/docker/build-tools.sh"

# The docker builds run as root and leave build/ root-owned; hand it back so the
# host user can stage and pack the release tree under it.
"$ROOT/docker/cc.sh" chown -R "$(id -u):$(id -g)" /work/build >/dev/null 2>&1 || true

# The command-line tools shipped in the release (order = display order).
NG_TOOLS="AddNetInterface ConfigureNetInterface RemoveNetInterface NetShutdown \
Online Offline AddNetRoute DeleteNetRoute GetNetStatus ShowNetStatus ping"

# 2. Lay out the Installer tree.
rm -rf "$STAGE"
mkdir -p "$STAGE/data/Libs" "$STAGE/data/C" "$STAGE/data/S" \
         "$STAGE/data/Storage/NetInterfaces" "$STAGE/data/Devs/Internet" \
         "$STAGE/data/db"

cp "$ROOT/install/Install-AmiTCP_NG"        "$STAGE/"
cp "$ROOT/install/ReadMe"                   "$STAGE/"

# Stamp the real project version into the installer's #ng-version placeholder, so the
# marker it writes (LIBS:AmiTCP_NG.version) records the version this archive ships and
# a later run can tell an existing AmiTCP_NG install apart for an in-place upgrade.
sed -i "s|(set #ng-version \"0.0.0\")|(set #ng-version \"$VER\")|" "$STAGE/Install-AmiTCP_NG"
grep -q "(set #ng-version \"$VER\")" "$STAGE/Install-AmiTCP_NG" || {
  echo "!!! build-release: failed to stamp #ng-version into the installer" >&2; exit 1; }
cp "$ROOT/COPYING"                          "$STAGE/"
cp "$ROOT/COPYRIGHTS"                        "$STAGE/"

cp "$ROOT/build/bsdsocket.library"          "$STAGE/data/Libs/"
for t in $NG_TOOLS; do cp "$ROOT/build/$t"  "$STAGE/data/C/"; done

# Strip the RELEASE copies (the dev build/ binaries keep their symbols). An Amiga
# Hunk symbol table is dead weight in a shipped binary and needlessly exposes every
# internal symbol name; stripping it shrinks the .lha and the library by ~8%.
"$ROOT/docker/cc.sh" bash -c '
  m68k-amigaos-strip build/release/AmiTCP_NG/data/Libs/bsdsocket.library build/release/AmiTCP_NG/data/C/*'
"$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" \
  build/release/AmiTCP_NG/data/Libs/bsdsocket.library build/release/AmiTCP_NG/data/C/* >/dev/null 2>&1 || true

cp "$ROOT/install/S/Network-Startup"        "$STAGE/data/S/"
cp "$ROOT/install/Storage/NetInterfaces/"*  "$STAGE/data/Storage/NetInterfaces/"
cp "$ROOT/install/Devs/Internet/hosts"      "$STAGE/data/Devs/Internet/"
cp "$ROOT/install/db/"*                     "$STAGE/data/db/"

# 2b. Workbench icons (.info). Without these the files have no icon and cannot be
# double-clicked. Install-AmiTCP_NG gets a Project icon whose Default Tool is the
# "Installer" command (double-click -> runs the Installer on the script); ReadMe
# opens in MultiView. The archive also carries a drawer icon; the floppy a disk icon.
python3 "$ROOT/docker/mkicons.py" project "$STAGE/Install-AmiTCP_NG.info" "Installer"
python3 "$ROOT/docker/mkicons.py" project "$STAGE/ReadMe.info"            "SYS:Utilities/MultiView"
python3 "$ROOT/docker/mkicons.py" drawer  "$ROOT/build/release/AmiTCP_NG.info"   # lha drawer icon
python3 "$ROOT/docker/mkicons.py" disk    "$ROOT/build/release/Disk.info"        # adf volume icon

# 3. Pack with LhA (the Amiga-standard archive format). The host's "lha" is often
# Lhasa (extract-only), so pack inside the toolchain image, whose lha can create.
# Include the drawer icon (AmiTCP_NG.info) alongside the drawer so it shows on WB.
rm -f "$OUT"
"$ROOT/docker/cc.sh" bash -c "cd /work/build/release && rm -f '$LHA_NAME' && lha a '$LHA_NAME' AmiTCP_NG AmiTCP_NG.info" >/dev/null 2>&1
"$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" "/work/build/release/$LHA_NAME" >/dev/null 2>&1 || true

if [ -f "$OUT" ]; then
  echo ">>> release archive: build/release/$LHA_NAME ($(wc -c < "$OUT") bytes)"
else
  echo "!!! lha packaging failed" >&2
  exit 1
fi

# 4. Also wrap the same install tree in an 880K DD floppy image (ADF) for real
# floppies / Gotek-style flash drives. xdftool (amitools) lives in the fsuae image.
# The disk is a plain data disk (FFS, volume "AmiTCP_NG"): boot your Amiga normally,
# insert it, and run its Installer -- it is not a bootable disk.
ADF="$ROOT/build/release/$ADF_NAME"
rm -f "$ADF"
docker run --rm -v "$ROOT":/work -w /work -e ADF_NAME="$ADF_NAME" amitcp-ng-fsuae:latest bash -c '
  set -e
  cd build/release/AmiTCP_NG
  A="/work/build/release/$ADF_NAME"
  xdftool "$A" create + format "AmiTCP_NG" ffs
  find . -type d ! -name . | sed "s|^\./||" | sort | while read d; do xdftool "$A" makedir "$d"; done
  find . -type f | sed "s|^\./||" | while read f; do xdftool "$A" write "$f" "$f"; done
  xdftool "$A" write /work/build/release/Disk.info Disk.info   # volume icon
' >/dev/null 2>&1
"$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" "/work/build/release/$ADF_NAME" >/dev/null 2>&1 || true
if [ -f "$ADF" ]; then
  echo ">>> floppy image:   build/release/$ADF_NAME ($(wc -c < "$ADF") bytes)"
else
  echo "!!! ADF creation failed" >&2
fi

echo ">>> staged tree:"
find "$STAGE" -type f | sed "s|$ROOT/||"
