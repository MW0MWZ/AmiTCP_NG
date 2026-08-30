#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Catch EXPENSIVE INSTRUCTIONS creeping into the hot path.
#
# WHY THIS EXISTS. The -m68020/-m68040 archives are gcc's default codegen at a
# higher -march; nothing about them is hand-tuned. That is usually fine, but the
# m68k backend will happily pick an instruction that is legal-and-slower on the
# real chip. The case that prompted this: struct ip's ip_v:4 / ip_hl:4 bitfields
# made gcc emit BFINS -- a read-modify-write of the packet header IN MEMORY --
# once per transmitted packet on 68020+, where the 68000 build (which cannot
# encode BFINS) got a plain byte store and was therefore FASTER on a real 68030.
# Nothing would have noticed; there was no check that looks at what we emit.
#
# WHAT IT CHECKS. For a list of hot functions, count the instructions that are
# disproportionately slow on 68020/030 -- bitfield ops and 32-bit divides -- and
# compare against a recorded baseline. A rise is a regression and fails the build;
# a fall means somebody improved things and should re-baseline deliberately.
#
# THIS IS A COUNT, NOT A VERDICT. It cannot tell you an instruction is wrong, only
# that there are more of them than last time in code that runs per packet. Read
# the disassembly before acting -- and measure on real hardware before believing
# any of it makes a difference.
#
#   usage:  docker/check-codegen.sh [--update]
#           operates on build/bsdsocket.library as currently built (NG_ARCH)
#           --update rewrites the baseline for this arch instead of checking
#
# Exit 0 = no regression, 1 = regression, 2 = cannot run (harness not ready).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
LIB="${LIB:-build/bsdsocket.library}"
ARCH="${NG_ARCH:--m68000}"
BASE="docker/codegen-baseline.txt"
CC_IMG=amigadev/crosstools:m68k-amigaos

if ! docker image inspect "$CC_IMG" >/dev/null 2>&1; then
  echo "NOT READY: docker image '$CC_IMG' is missing -- nothing was checked." >&2
  exit 2
fi
if [ ! -f "$LIB" ]; then
  echo "NOT READY: '$LIB' does not exist -- build first. Nothing was checked." >&2
  exit 2
fi

# Functions that run per packet or per connection. Deliberately a SHORT list: a
# gate over everything would be noise, and cold code is allowed to be slow.
HOT='_ip_input _ip_output _tcp_input _tcp_output _udp_input _udp_output
_in_pcblookup _sana_read _sana_output _sana_start _sana_poll _m_copym _m_copydata
_arpresolve _in_arpinput _looutput _in_cksum'

# The instructions worth counting. Bitfield ops are microcoded and slow on
# 68020/030; 32-bit divide is slow everywhere. All are ABSENT from a -m68000
# build by construction, which is exactly why the plain build can win.
WATCH='bfins bfextu bfexts bfins bftst bfclr bfset bfffo divul.l divsl.l divu.l divs.l'

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

docker run --rm -v "$ROOT":/work -w /work "$CC_IMG" \
  bash -c "m68k-amigaos-nm -n '$LIB' 2>/dev/null" > "$tmp/nm.txt"
docker run --rm -v "$ROOT":/work -w /work "$CC_IMG" \
  bash -c "m68k-amigaos-objdump -d '$LIB' 2>/dev/null" > "$tmp/dis.txt"

if [ ! -s "$tmp/dis.txt" ]; then
  echo "NOT READY: objdump produced nothing for '$LIB'. Nothing was checked." >&2
  exit 2
fi

# Parse by TAB, not by regex or $NF.
#
# objdump prints:   <addr>:\t<hex bytes>\t<mnemonic> <operands>
# Getting this wrong is the whole reason an earlier MOVE16 check reported zero
# when there was one: it took the LAST whitespace field, which for
# "move16 (a0)+,(a1)+" is "(a1)+" and never the mnemonic. Tabs are unambiguous.
LC_ALL=C awk -F'\t' -v hot="$HOT" -v watch="$WATCH" '
  BEGIN { n=split(hot,H," "); for(i=1;i<=n;i++) hotf[H[i]]=1
          n=split(watch,W," "); for(i=1;i<=n;i++) watchi[W[i]]=1 }
  # symbol table: "<addr> T _name"
  FILENAME==ARGV[1] { if ($0 ~ /^[0-9a-f]+ [Tt] /) { split($0,sym," ");
                        addr[++k]=strtonum("0x" sym[1]); name[k]=sym[3] } ; next }
  # disassembly: address \t bytes \t mnemonic+operands
  NF>=3 {
    split($1,p,":"); gsub(/[ \t]/,"",p[1])
    if (p[1] !~ /^[0-9a-f]+$/) next
    # strtonum() silently yields 0 for "0x   22eb8" -- the leading spaces MUST be
    # stripped first, or every instruction is attributed to the first symbol and
    # the whole check quietly reports nothing.
    here=strtonum("0x" p[1]); split($3,m," "); mn=m[1]
    seen++
    if (!(mn in watchi)) next
    lo=0; for(i=1;i<=k;i++) { if (addr[i]<=here) lo=i; else break }
    if (lo==0) next
    f=name[lo]; if (!(f in hotf)) next
    cnt[f" "mn]++
  }
  END {
    # A checker that inspects nothing must not report "clean". If we never
    # decoded a single instruction the parse is broken, not the binary.
    if (seen < 1000) { print "PARSE-FAILED " seen > "/dev/stderr"; exit 3 }
    for (c in cnt) print c, cnt[c]
  }
' "$tmp/nm.txt" "$tmp/dis.txt" | LC_ALL=C sort > "$tmp/now.txt"
awkrc=${PIPESTATUS[0]}
if [ "$awkrc" != 0 ]; then
  echo "NOT READY: the disassembly parse failed (awk rc=$awkrc). Nothing was checked." >&2
  exit 2
fi

echo ">>> hot-path expensive instructions ($ARCH):"
if [ -s "$tmp/now.txt" ]; then LC_ALL=C sed 's/^/      /' "$tmp/now.txt"; else echo "      (none)"; fi

if [ "${1:-}" = "--update" ]; then
  [ -f "$BASE" ] && LC_ALL=C grep -v "^$ARCH " "$BASE" > "$tmp/keep.txt" || : > "$tmp/keep.txt"
  LC_ALL=C sed "s/^/$ARCH /" "$tmp/now.txt" >> "$tmp/keep.txt"
  LC_ALL=C sort "$tmp/keep.txt" > "$BASE"
  echo ">>> baseline updated for $ARCH"
  exit 0
fi

if [ ! -f "$BASE" ]; then
  echo "no baseline yet -- run: docker/check-codegen.sh --update" >&2
  exit 0
fi

LC_ALL=C grep "^$ARCH " "$BASE" 2>/dev/null | LC_ALL=C sed "s/^$ARCH //" > "$tmp/base.txt" || : > "$tmp/base.txt"
rc=0
while read -r f mn c; do
  [ -z "${f:-}" ] && continue
  was=$(LC_ALL=C awk -v f="$f" -v m="$mn" '$1==f && $2==m {print $3}' "$tmp/base.txt")
  was=${was:-0}
  if [ "$c" -gt "$was" ]; then
    echo "!!! REGRESSION: $f gained $mn ($was -> $c) at $ARCH" >&2
    rc=1
  fi
done < "$tmp/now.txt"

if [ "$rc" = 0 ]; then echo ">>> codegen check: no hot-path regression at $ARCH"; fi
exit $rc
