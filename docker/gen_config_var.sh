#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Generate kern/config_var.c from kern/variables.src (the AmiTCP config-variable
# table). PORT (AmiTCP_NG): modern gawk leaks the final keyword-spec record raw
# into the externs+globals sections (a getline/EOF-at-last-record quirk vs 1994
# awk); its real C decls are still emitted correctly. Strip the leaked spec lines
# (the only lines shaped `IDENT=IDENT ;` at column 0 — no valid C line matches).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT/src"
gawk -f kern/config_var.awk -v TARGETTI=C kern/variables.src \
  | grep -vE '^[A-Za-z_][A-Za-z_0-9]*=[A-Za-z_]' \
  | sed -E 's/^STRPTR KW_VARS =[[:space:]]*$/STRPTR KW_VARS = (STRPTR)/' > kern/config_var.c
echo "generated kern/config_var.c ($(wc -l < kern/config_var.c) lines)"
