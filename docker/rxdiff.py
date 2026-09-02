#!/usr/bin/env python3
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
"""Difference two rxprofile snapshots so a leg's numbers are its own.

rxprofile reports counters that are cumulative for the whole boot. Reading the
post-transfer snapshot alone attributes every earlier test's traffic to the
transfer -- which already produced one phantom finding in this harness: 52
"no socket buf space" drops that belonged entirely to the zero-window test that
ran before it, and contributed nothing to the transfer at all.

    rxdiff.py before.log after.log
"""
import re, sys

# rxprofile prints three shapes and all three matter:
#   "  PacketsIn  = 115022      BytesIn  = 114406090"   two pairs on one line
#   "    fast data       = 112434     (98%)"            trailing percentage
#   "      out of sequence     2192       (85%)"        bucket, no '='
# A line-anchored "label = number$" catches none of the first two, which is how
# the first version of this silently dropped every headline counter.
PAIR = re.compile(r'([A-Za-z][A-Za-z0-9 _/\-\.]*?)\s*=\s*(\d+)')
BUCK = re.compile(r'^\s{6}(\S.*?)\s{2,}(\d+)\s+\(\d+%\)\s*$')

# Levels, not counters: differencing them is meaningless (see delta() in
# rxprofile.c -- a permanently full ring would read 0).
LEVELS = ('receive ring', 'BPS', 'MTU', 'posted')

def parse(path):
    out = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        m = BUCK.match(line)
        hits = [(m.group(1), m.group(2))] if m else PAIR.findall(line)
        for k, v in hits:
            k = k.strip()
            if not k or any(l in k for l in LEVELS):
                continue
            out[k] = int(v)
    return out

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    a, b = parse(sys.argv[1]), parse(sys.argv[2])
    keys = [k for k in b if k in a]
    if not keys:
        sys.exit("rxdiff: no comparable counters -- are both files rxprofile output?")
    width = max(len(k) for k in keys)
    print("counter deltas (this leg only):")
    for k in keys:
        d = b[k] - a[k]
        if d:
            print("  %-*s %12d" % (width, k, d))

main()
