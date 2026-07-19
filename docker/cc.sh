#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# AmiTCP_NG cross-compile wrapper — bebbo m68k-amigaos-gcc 6.5 via prebuilt image.
# Toolchain: amigadev/crosstools:m68k-amigaos (Docker Hub). Disposable (--rm).
# Usage:  docker/cc.sh <any command>       e.g.  docker/cc.sh make -C src
#         docker/cc.sh m68k-amigaos-gcc -c foo.c
set -euo pipefail
IMG=amigadev/crosstools:m68k-amigaos
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec docker run --rm -v "$ROOT":/work -w /work "$IMG" "$@"
