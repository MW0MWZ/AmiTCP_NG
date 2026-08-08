#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
# Generate kern/config_var.c from kern/variables.src (the AmiTCP config-variable
# table). PORT (AmiTCP_NG): modern gawk leaks the final keyword-spec record raw
# into the externs+globals sections (a getline/EOF-at-last-record quirk vs 1994
# awk); its real C decls are still emitted correctly. Strip the leaked spec lines.
#
# Match the SHAPE of a spec line -- `NAME ;<level> ;` at column 0 -- rather than
# just an aliased `IDENT=IDENT`, which is what this used to do. That older pattern
# silently stopped working the moment the last entry in variables.src had no alias
# (the leaked line went straight through as C and broke the build), and the failure
# looked nothing like its cause. No valid C line can match this: a declaration at
# column 0 followed by ` ;`, a number and another ` ;` is not C.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT/src"
gawk -f kern/config_var.awk -v TARGETTI=C kern/variables.src \
  | grep -vE '^[A-Za-z_][A-Za-z_0-9=]*[[:space:]]*;[[:space:]]*[0-9]+[[:space:]]*;' \
  | sed -E 's/^STRPTR KW_VARS =[[:space:]]*$/STRPTR KW_VARS = (STRPTR)/' > kern/config_var.c
echo "generated kern/config_var.c ($(wc -l < kern/config_var.c) lines)"
