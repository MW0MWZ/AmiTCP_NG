#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Headless FS-UAE run (Mesa/llvmpipe under Xvfb), killed after $TIMEOUT sec.
# Capture is via files the Amiga writes to the directory-HD (host-visible).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMEOUT="${TIMEOUT:-90}"
CFG="${1:-emu/amitcp-ng.fs-uae}"
docker run --rm -v "$ROOT":/work -w /work amitcp-ng-fsuae:latest bash -c "
  Xvfb :99 -screen 0 1024x768x24 +extension GLX +render -noreset >/tmp/xvfb.log 2>&1 &
  export DISPLAY=:99 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
  sleep 2
  echo '>>> GL check:'; glxinfo -B 2>/dev/null | grep -iE 'renderer|OpenGL version' | head -2 || echo '(glxinfo n/a)'
  echo '>>> launching fs-uae (timeout ${TIMEOUT}s)'
  timeout ${TIMEOUT} fs-uae '$CFG' --fullscreen=0 --window_width=640 --window_height=512 2>&1 | grep -viE '^$' | tail -25
  echo '>>> fs-uae exited'
"
