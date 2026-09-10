#!/usr/bin/env bash
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Assemble the installable release archives: the Amiga Installer script + payload
# (the self-starting bsdsocket.library and the command set, both built fresh) laid
# out for the Installer, then packed with LhA and as an 880K floppy image (ADF).
#
# ONE archive, compiled for 68000, which runs on every 68k Amiga:
#   AmiTCP_NG-v<ver>.lha/.adf
# The 68020 and 68040 variants were dropped -- see the note above build_variant at
# the foot of this file. Per-CPU choices are made at run time inside this binary.
#
# Sources:
#   install/          -- the Installer script, ReadMe, Network-Startup, example
#                        interface configs, example hosts (all committed, source).
#   build/            -- the freshly built binaries (NOT committed; built here).
#   COPYING/COPYRIGHTS-- licences (committed).
#
# Output: build/release/AmiTCP_NG-v<ver>[-<cpu>].{lha,adf}  (compiled artifacts ->
# release only, never committed to the repo).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="$ROOT/build/release/AmiTCP_NG"

# ---------------------------------------------------------------------------
# PREFLIGHT. Check the images we need BEFORE spending several minutes building
# three architectures, and say plainly what is missing.
#
# Without this, a pruned amitcp-ng-fsuae image let the whole build run to
# completion, produce the first .lha, and then fail at ADF creation with
#     "!!! ADF creation FAILED (xdftool rc=125)"
# -- blaming xdftool for what was docker refusing to pull a local-only image.
# 125 is docker's own "could not run the container" code, not a tool error. A
# message that names the wrong culprit costs more time than no message.
# ---------------------------------------------------------------------------
if ! docker info >/dev/null 2>&1; then
  echo "NOT READY: docker is not usable (is the daemon running?)." >&2
  exit 2
fi
for _img in amigadev/crosstools:m68k-amigaos amitcp-ng-fsuae:latest; do
  if ! docker image inspect "$_img" >/dev/null 2>&1; then
    echo "NOT READY: docker image '$_img' is missing -- nothing has been built." >&2
    case "$_img" in
      amitcp-ng-fsuae:latest)
        echo "  It makes the .adf floppy images. Build it with:" >&2
        echo "    docker build -f docker/Dockerfile.fsuae -t amitcp-ng-fsuae:latest ." >&2 ;;
      *)
        echo "  It is the cross-compiler; it is pulled automatically when reachable." >&2 ;;
    esac
    exit 2
  fi
done
unset _img
# Project version -- single source of truth is src/bsdsocket.library_rev.h. Names the
# artifacts AmiTCP_NG-v<version>[-<cpu>].{lha,adf}. Reads the FULL quoted string, so any
# pre-release suffix (e.g. 4.1.2-beta) is preserved in the artifact names and the
# installer marker, not stripped to the bare Major.Minor.Revision.
VER="$(grep -E 'define[[:space:]]+AMITCP_NG_VER' "$ROOT/src/bsdsocket.library_rev.h" | sed -E 's/.*"([^"]+)".*/\1/' | head -1)"
[ -n "$VER" ] || { echo "!!! build-release: could not read AMITCP_NG_VER from src/bsdsocket.library_rev.h" >&2; exit 1; }

# The command-line tools shipped in the release (order = display order).
# NOTE: this list is what actually SHIPS. build-tools.sh building a command is not
# enough -- arp was built for a whole revision and reached no archive because it was
# never added here.
NG_TOOLS="AddNetInterface ConfigureNetInterface RemoveNetInterface NetShutdown \
Online Offline AddNetRoute DeleteNetRoute GetNetStatus ShowNetStatus AmiTCPControl RoadshowControl \
arp SampleNetSpeed traceroute CheckAmiTCPNGConfig NetLogViewer \
ManageNetInterfaces PacketCapture ping netstat tftp nslookup ftp sntp hostname"

# Diagnostics ship ONLY in -beta builds. A stable release carries the ordinary
# Roadshow-compatible command set and nothing else; a beta is a test build handed to
# people who are trying to find out why something is slow, so it carries the tools
# that answer that. Gated on the version string itself rather than a separate flag,
# so it cannot be forgotten in either direction -- dropping the -beta suffix for the
# real release automatically drops the diagnostics with it.
case "$VER" in
  *-beta*)
    NG_TOOLS="$NG_TOOLS rxprofile"
    echo ">>> beta build: including diagnostics (rxprofile)"
    ;;
esac

# The installer carries copies of the settings it appends to an EXISTING db/ file on
# upgrade (it cannot diff two files -- it can only test for a string and append). Each
# block is guarded by a sentinel that must actually appear in the shipped file, or the
# guard would never match and every upgrade would append the block again. Verify the
# pairing here so the duplication cannot silently drift.
check_installer_sentinel() {
  local sentinel="$1" dbfile="$2"
  grep -qF -- "$sentinel" "$ROOT/install/db/$dbfile" || {
    echo "!!! build-release: installer merge sentinel '$sentinel' is not in install/db/$dbfile" >&2
    echo "!!! (Install-AmiTCP_NG would re-append its block on every upgrade.)" >&2
    exit 1; }
  # NOT a plain grep of the whole installer: the sentinel appears there anyway as
  # the (set #mf-key "...") argument, so that would pass no matter what. What has
  # to be true is that the sentinel is in the text that gets APPENDED -- otherwise
  # the next upgrade will not find it and will append the block all over again.
  # check_installer_block_matches enforces that; this only pins the shipped side.
}
# Structural check on the Installer script. It cannot be executed here (no Installer
# on the build host), so the one class of error a machine CAN catch is checked: paren
# balance, and `if` arity. Installer `if` takes cond/then[/else] and nothing more --
# five bare statements where the then-branch belongs silently disables a whole code
# path, which is exactly how the db-merge got broken once already.
python3 - "$ROOT" <<'PYEOF'
import sys
src = open(sys.argv[1] + "/install/Install-AmiTCP_NG").read()
out=[]; instr=incom=esc=False
for ch in src:
    if ch == "\n": incom=False; out.append("\n"); continue
    if incom: continue
    if instr:
        if esc: esc=False
        elif ch == "\\": esc=True
        elif ch == '"': instr=False; out.append("S")
        continue
    if ch == '"': instr=True; continue
    if ch == ";": incom=True; continue
    out.append(ch)
t="".join(out)
def parse(i):
    items=[]
    while i < len(t):
        c=t[i]
        if c == "(":
            sub,i = parse(i+1); items.append(sub); continue
        if c == ")": return items, i+1
        if c.isspace(): i+=1; continue
        j=i
        while j < len(t) and not t[j].isspace() and t[j] not in "()": j+=1
        items.append(t[i:j]); i=j
    return items, i
tree,_ = parse(0)
if t.count("(") != t.count(")"):
    sys.exit("!!! build-release: Install-AmiTCP_NG parens unbalanced")
bad=[]
def walk(n, path="top"):
    if isinstance(n, list):
        if n and n[0] == "if" and len(n)-1 not in (2,3):
            bad.append("%s: if with %d args" % (path, len(n)-1))
        name = n[1] if len(n) > 1 and n and n[0] == "procedure" else path
        for c in n: walk(c, name)
walk(tree)
if bad:
    sys.exit("!!! build-release: Install-AmiTCP_NG malformed if forms:\n    " + "\n    ".join(bad))
print("    Install-AmiTCP_NG: parens balanced, all if forms well-formed")
PYEOF
echo ">>> installer structure verified"

# EVERY tool build-tools.sh builds must also be in NG_TOOLS, or it is built and
# never shipped. That is not hypothetical: `arp` was built for an entire revision
# and reached no archive, and nothing caught it -- the existing checks only fail
# the other way round (a name in NG_TOOLS that was never built makes the `cp`
# fail). Closing the direction that actually bit us.
check_tools_are_shipped() {
  local built shipped missing=""
  built=$(grep -oE '^build_(one|boopsi) +[A-Za-z0-9_]+' "$ROOT/docker/build-tools.sh" \
          | awk '{print $2}' | sort -u)
  shipped=$(printf '%s\n' $NG_TOOLS rxprofile | sort -u)
  for t in $built; do
    printf '%s\n' "$shipped" | grep -qx -- "$t" || missing="$missing $t"
  done
  [ -z "$missing" ] || {
    echo "!!! build-release: built by build-tools.sh but missing from NG_TOOLS:$missing" >&2
    echo "!!! (it would be compiled every release and shipped in none of them.)" >&2
    exit 1; }
}
check_tools_are_shipped

check_installer_sentinel ng-services-v2 netdb
check_installer_sentinel HOSTNAME  AmiTCP.config
check_installer_sentinel LOGGING     AmiTCP.config
check_installer_sentinel LOGLEVEL    AmiTCP.config
check_installer_sentinel LOGCONSOLE  AmiTCP.config
check_installer_sentinel LOGFILENAME AmiTCP.config
check_installer_sentinel MBUFCHECK   AmiTCP.config
check_installer_sentinel SANADMA     AmiTCP.config

# The installer's merge blocks are meant to be VERBATIM copies of the corresponding
# blocks in install/db/*, so that an upgraded machine ends up with the same file text
# as a freshly installed one. Nothing enforces that by construction -- the installer
# stores them as quoted Installer-script strings -- so check it here: unescape each
# "..."\n line back to plain text and require every line to be present in the shipped
# file. Catches an edit to one side that was not made to the other.
check_installer_block_matches() {
  local marker="$1" dbfile="$2" sentinel="$3"
  python3 - "$marker" "$dbfile" "$ROOT" "$sentinel" <<'PYEOF'
import re, sys
marker, dbfile, root, sentinel = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
inst = open(root + "/install/Install-AmiTCP_NG").read()
m = re.search(r"\(set " + re.escape(marker) + r" \(cat\n(.*?)\n\)\)", inst, re.S)
if not m:
    sys.exit("!!! build-release: cannot find %s block in Install-AmiTCP_NG" % marker)
lines = []
for raw in m.group(1).split("\n"):
    q = re.match(r'\s*"(.*)"\s*$', raw)
    if not q:
        sys.exit("!!! build-release: unparsable line in %s: %s" % (marker, raw))
    t = q.group(1)
    if not t.endswith("\\n"):
        sys.exit("!!! build-release: %s line does not end in \\n: %s" % (marker, raw))
    t = t[:-2]
    while t.startswith("\\n"):        # leading blank-line escapes are spacing, not content
        t = t[2:]
    t = t.replace('\\"', '"').replace("\\\\", "\\")
    if t:
        lines.append(t)
if not lines:
    sys.exit("!!! build-release: %s produced 0 content lines -- the block asserts "
             "nothing and would pass vacuously" % marker)
if sentinel and not any(sentinel in t for t in lines):
    sys.exit("!!! build-release: sentinel '%s' does not appear in the %s block.\n"
             "    The block would be appended again on EVERY future upgrade."
             % (sentinel, marker))
shipped = open(root + "/install/db/" + dbfile).read().split("\n")
for t in lines:
    if t not in shipped:
        sys.exit("!!! build-release: %s carries a line not in install/db/%s:\n    %s"
                 % (marker, dbfile, t))
print("    %s: %d lines match install/db/%s" % (marker, len(lines), dbfile))
PYEOF
}
# KNOWN LIMIT: this is one-directional -- it proves the installer block is a subset
# of the shipped file, not that the shipped file has nothing the block lacks. Adding
# a service to install/db/netdb and forgetting the installer block would still ship
# silently, and existing users would again not receive it. Closing that needs explicit
# region markers in install/db/*, since not every shipped line belongs to a block.
check_installer_block_matches "#db-newhostname" AmiTCP.config HOSTNAME
check_installer_block_matches "#db-newlogging"     AmiTCP.config LOGGING
check_installer_block_matches "#db-newloglevel"    AmiTCP.config LOGLEVEL
check_installer_block_matches "#db-newlogconsole"  AmiTCP.config LOGCONSOLE
check_installer_block_matches "#db-newlogfilename" AmiTCP.config LOGFILENAME
check_installer_block_matches "#db-newmbufcheck"   AmiTCP.config MBUFCHECK
check_installer_block_matches "#db-newsanadma" AmiTCP.config SANADMA
check_installer_block_matches "#db-newservices" netdb ng-services-v2

# Every setting the shipped AmiTCP.config offers must be the KEY (#mf-key) of one
# of the installer's merges. Not merely present in some block -- the KEY.
#
# This exists because of a real escape: LOGCONSOLE was added to the config and to
# the #db-newlogging block, and every check here passed, but that block is guarded
# by LOGLEVEL -- which every machine running the previous release already had. The
# merge was therefore skipped and the new setting reached new installs only. The
# checks could not see it: the block WAS a faithful copy of the shipped file, it
# simply could never be applied again.
#
# Keying each setting on its own name makes "add a setting" and "deliver it to
# existing users" the same action, and this check makes forgetting it fail the
# build instead of shipping.
python3 - "$ROOT" <<'PYEOF'
import re, sys
root = sys.argv[1]
cfg  = open(root + "/install/db/AmiTCP.config").read()
inst = open(root + "/install/Install-AmiTCP_NG").read()

# `#?` so a LIVE setting counts too: HOSTNAME=amiga ships uncommented, and a
# check that only saw commented examples would not have covered it.
settings = re.findall(r"^#?([A-Z_]+)=", cfg, re.M)
# Comment-stripped: a key mentioned in a ';' note is documentation, not a merge.
keys = set(re.findall(r'\(set #mf-key\s+"([^"]+)"\)', re.sub(r"(?m);.*$", "", inst)))
missing = [s for s in dict.fromkeys(settings) if s not in keys]
if missing:
    sys.exit("!!! build-release: these AmiTCP.config settings are not the key of any\n"
             "    installer merge, so an existing installation would never receive them:\n"
             "      " + ", ".join(missing) + "\n"
             "    Add a (set #mf-key \"<SETTING>\") merge for each -- see #db-newlogconsole.")
print("    every AmiTCP.config setting has its own merge key (%d checked)"
      % len(dict.fromkeys(settings)))

# CROSS-BLOCK COLLISION. C:Search is a plain substring match over the whole file,
# so a block that MENTIONS a later setting's name in its prose plants that name
# in the file before that setting's own merge runs -- whose search then reports
# "already present" and silently skips it.
#
# This is not hypothetical. It shipped: the LOGGING block's documentation said
# "LOGLEVEL=7" and "send the file named by LOGFILENAME", so on a 4.1.4 upgrade
# LOGGING appended, and LOGLEVEL and LOGFILENAME were both skipped -- while the
# installer told the user everything had been added.
#
# For each merge, in invocation order, assert no EARLIER block for the SAME FILE
# contains this one's key.
#
# CASE-INSENSITIVE, because C:Search is. Ground truth is the Search binary this
# repo ships for the emulator: its ReadArgs template is
#   FROM/M,SEARCH/A,ALL/S,NONUM/S,QUIET/S,QUICK/S,FILE/S,PATTERN/S,CASE/S
# -- CASE is an opt-in SWITCH, and the installer's invocation passes only SEARCH
# and QUIET. So "Logfilename" in someone's prose would match the LOGFILENAME key
# on a real machine. A case-sensitive check here would call that clean and the
# setting would silently go missing again, which is the whole bug this guards.
#
# Scoped per #mf-file: each C:Search only ever scans its own target, so the netdb
# block cannot collide with an AmiTCP.config key however similar the words are.
# Comments are stripped first, so a key mentioned in a ';' note is not mistaken
# for one in the text that actually gets appended.
inst_live = re.sub(r"(?m);.*$", "", inst)
seq = re.findall(r'\(set #mf-file\s+"([^"]+)"\)\s*\n\s*'
                 r'\(set #mf-key\s+"([^"]+)"\)\s*\n\s*'
                 r'\(set #mf-add\s+(#[a-z0-9-]+)\)', inst_live)
def block_text(var):
    m = re.search(r"\(set " + re.escape(var) + r" \(cat\n(.*?)\n\)\)", inst, re.S)
    return m.group(1).lower() if m else ""
clashes = []
for i, (file_i, key_i, var_i) in enumerate(seq):
    body = block_text(var_i)
    for file_j, key_j, _ in seq[i+1:]:
        if file_j == file_i and key_j.lower() in body:
            clashes.append("%s (merged earlier into %s) contains the later key '%s'"
                           % (var_i, file_i, key_j))
if clashes:
    sys.exit("!!! build-release: installer merge keys collide by substring --\n"
             "    an earlier block's text would make a later merge think its\n"
             "    setting is already present, and skip it:\n      "
             + "\n      ".join(clashes)
             + "\n    Reword the prose so it does not name the later setting.")
print("    no merge key is pre-echoed by an earlier block (%d merges, case-folded)"
      % len(seq))
PYEOF
echo ">>> installer db-merge blocks verified against install/db/*"

# --------------------------------------------------------------------------------
# build_variant <cpu> <name-suffix>
#   cpu    : 68000 | 68020 | 68040  (drives NG_ARCH=-m<cpu>)
#   suffix : ""  | "-68020" | "-68040"  (appended to the artifact names)
# Builds the library + tools for this CPU, stages the Installer tree, and packs the
# .lha and .adf. Everything below is per-variant because the binaries are rebuilt for
# each CPU target.
# --------------------------------------------------------------------------------
build_variant() {
  local cpu="$1" sfx="$2"
  local adf_rc=0			# reset per variant; see the ADF step
  local LHA_NAME="AmiTCP_NG-v$VER$sfx.lha"
  local ADF_NAME="AmiTCP_NG-v$VER$sfx.adf"
  local OUT="$ROOT/build/release/$LHA_NAME"

  echo "==============================================================="
  echo ">>> variant: $cpu   ->   $LHA_NAME / $ADF_NAME"
  echo "==============================================================="

  # 1. Build the binaries this archive ships, for this CPU target. Exporting NG_ARCH
  # makes build-lib.sh/build.sh (via -e NG_ARCH) and build-tools.sh compile + link for
  # the variant.
  #
  # Objects live in a PER-ARCH directory (build/obj-m68000 and friends, see
  # build.sh), so a stale tree from another variant can no longer be linked into
  # this one by accident -- which is what the wipe that used to be here was
  # guarding against. A release build still starts from a clean object dir for its
  # own arch, so nothing from an earlier source state survives into a shipped
  # archive. Removed as root inside docker: the previous build left those objects
  # root-owned and a host rm would fail and leave them in place.
  export NG_ARCH="-m$cpu"
  "$ROOT/docker/cc.sh" rm -rf "/work/build/obj-m$cpu" >/dev/null 2>&1 || rm -rf "$ROOT/build/obj-m$cpu"
  echo ">>> building bsdsocket.library ($cpu) ..."
  "$ROOT/docker/build-lib.sh" >/dev/null
  # Codegen gate: catch expensive instructions creeping into the per-packet path.
  # Runs HERE, on the freshly built library, before anything is stripped or packed
  # -- check-codegen.sh needs the symbol table to attribute instructions to
  # functions, and the release strip removes it. A regression fails the whole
  # build rather than shipping a variant that is slower than the plain one.
  "$ROOT/docker/check-codegen.sh"

  echo ">>> building usergroup.library ($cpu) ..."
  "$ROOT/docker/build-usergroup.sh" >/dev/null
  "$ROOT/docker/build-tools.sh"

  # The docker builds run as root and leave build/ root-owned; hand it back so the
  # host user can stage and pack the release tree under it.
  "$ROOT/docker/cc.sh" chown -R "$(id -u):$(id -g)" /work/build >/dev/null 2>&1 || true

  # 2. Lay out the Installer tree.
  rm -rf "$STAGE"
  mkdir -p "$STAGE/data/Libs" "$STAGE/data/C" "$STAGE/data/S" \
           "$STAGE/data/Storage/NetInterfaces" "$STAGE/data/Devs/Internet" \
           "$STAGE/data/db" "$STAGE/data/Legacy" "$STAGE/data/Legacy416"

  cp "$ROOT/install/Install-AmiTCP_NG"        "$STAGE/"
  cp "$ROOT/install/ReadMe"                   "$STAGE/"

  # Stamp the real project version into the installer's #ng-version placeholder, so the
  # marker it writes (LIBS:AmiTCP_NG.version) records the version this archive ships and
  # a later run can tell an existing AmiTCP_NG install apart for an in-place upgrade.
  sed -i "s|(set #ng-version \"0.0.0\")|(set #ng-version \"$VER\")|" "$STAGE/Install-AmiTCP_NG"
  grep -q "(set #ng-version \"$VER\")" "$STAGE/Install-AmiTCP_NG" || {
    echo "!!! build-release: failed to stamp #ng-version into the installer" >&2; exit 1; }

  # Stamp which CPU build this archive carries, so the installer can refuse a
  # machine that cannot run it. Without this all three archives shipped the
  # SAME installer, which stated there was no CPU choice -- true once, false
  # from the moment the variants were introduced. Installing the 68040 archive
  # on a 68000 would Guru on the next boot (the installer puts a command in
  # S:User-Startup), before the user could reach Uninstall.
  case "$cpu" in
    68020) NGCPU=20 ;;
    68040) NGCPU=40 ;;
    *)     NGCPU=0  ;;			# plain 68000: runs on everything
  esac
  sed -i "s|(set #ng-cpu 0)|(set #ng-cpu $NGCPU)|" "$STAGE/Install-AmiTCP_NG"
  grep -q "(set #ng-cpu $NGCPU)" "$STAGE/Install-AmiTCP_NG" || {
    echo "!!! build-release: failed to stamp #ng-cpu into the installer" >&2; exit 1; }

  # Mark a pre-release AS a pre-release, in the installer and on the front of the
  # ReadMe. Driven off the version string for the same reason the diagnostics are:
  # dropping the -beta suffix drops the warning with it, and nothing has to be
  # remembered in either direction.
  #
  # The installer's gate refuses by default, so a Novice run -- where the Installer
  # answers every question itself -- stops rather than silently putting a test build
  # on a machine somebody depends on.
  case "$VER" in
    *-beta*)
      sed -i "s|(set #ng-prerelease 0)|(set #ng-prerelease 1)|" "$STAGE/Install-AmiTCP_NG"
      grep -q "(set #ng-prerelease 1)" "$STAGE/Install-AmiTCP_NG" || {
        echo "!!! build-release: failed to stamp #ng-prerelease into the installer" >&2
        exit 1; }
      mv "$STAGE/ReadMe" "$STAGE/ReadMe.body"
      {
        echo "*******************************************************************"
        echo "***  PRE-RELEASE TEST BUILD -- NOT A RELEASE                    ***"
        echo "*******************************************************************"
        echo
        echo "AmiTCP_NG $VER is UNFINISHED SOFTWARE, published so that people who"
        echo "are willing to help test it can do so. It is not the version to put on"
        echo "a machine you rely on."
        echo
        echo "  * It REPLACES your TCP/IP stack and your network commands. If it is"
        echo "    wrong, your networking stops working."
        echo "  * It has NOT had the testing a release gets. Parts of it have never"
        echo "    been run on real hardware at all."
        echo "  * THESE FILES ARE REPLACED WITHOUT NOTICE. The archive you have may"
        echo "    be gone or different tomorrow, and two people holding the same file"
        echo "    name may not have the same build. If you report a problem, say"
        echo "    which build you have."
        echo "  * There is no upgrade path from a pre-release, and no promise that a"
        echo "    configuration it writes will still be understood by a later one."
        echo
        echo "Back up anything you cannot lose BEFORE installing, and know how to"
        echo "boot the machine without it. The installer will ask you to confirm all"
        echo "of this, and will stop if you do not."
        echo
        echo "If you want AmiTCP_NG to USE rather than to test, take the newest"
        echo "release that is NOT marked as a pre-release."
        echo
        echo "*******************************************************************"
        echo
        echo
        cat "$STAGE/ReadMe.body"
      } > "$STAGE/ReadMe"
      rm -f "$STAGE/ReadMe.body"
      grep -q "PRE-RELEASE TEST BUILD" "$STAGE/ReadMe" || {
        echo "!!! build-release: failed to band the ReadMe as a pre-release" >&2; exit 1; }
      echo ">>> beta build: pre-release warning stamped into installer and ReadMe"
      ;;
    *)
      # A real release must NOT carry the gate. Prove it rather than assume it.
      grep -q "(set #ng-prerelease 0)" "$STAGE/Install-AmiTCP_NG" || {
        echo "!!! build-release: release build has a pre-release gate stamped" >&2
        exit 1; }
      ;;
  esac
  cp "$ROOT/COPYING"                          "$STAGE/"
  cp "$ROOT/COPYRIGHTS"                        "$STAGE/"

  cp "$ROOT/build/bsdsocket.library"          "$STAGE/data/Libs/"
  cp "$ROOT/build/usergroup.library"          "$STAGE/data/Libs/"
  for t in $NG_TOOLS; do cp "$ROOT/build/$t"  "$STAGE/data/C/"; done

  # Both libraries must actually be here before we pack. A missing one would ship a
  # silently incomplete archive -- the installer's copyfiles would then abort mid-run
  # on the user's machine, which is the worst place to discover it.
  for f in bsdsocket.library usergroup.library; do
    [ -s "$STAGE/data/Libs/$f" ] || {
      echo "!!! build-release: data/Libs/$f is missing or empty" >&2; exit 1; }
  done

  # Strip the RELEASE copies (the dev build/ binaries keep their symbols). An Amiga
  # Hunk symbol table is dead weight in a shipped binary and needlessly exposes every
  # internal symbol name; stripping it shrinks the .lha and the library by ~8%.
  #
  # ONE FILE PER INVOCATION. THIS IS NOT STYLE -- m68k-amigaos-strip given several
  # files on one command line SILENTLY TRUNCATES them. Handing it the 2 libraries
  # and 25 commands together damaged 21 of the 25: each came out smaller than the
  # same binary stripped alone, with real content gone, not just symbols.
  # GetNetStatus lost 252 bytes that way and Gurued at boot (8000000B) on real
  # hardware, from a binary that was correct until it was stripped.
  #
  # It also explains why v4.1.5 was fine: its strip line named ONE library plus
  # C/*, and this revision added usergroup.library and nine more commands -- a
  # different file list, so a different set of binaries got mangled.
  #
  # Verified: same input file, byte-identical, strips to 7200 bytes alone and 6948
  # in the batch. Never batch this tool.
  "$ROOT/docker/cc.sh" bash -c '
    for f in build/release/AmiTCP_NG/data/Libs/*.library build/release/AmiTCP_NG/data/C/*; do
      m68k-amigaos-strip "$f" || exit 1
    done'
  "$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" \
    build/release/AmiTCP_NG/data/Libs/*.library build/release/AmiTCP_NG/data/C/* >/dev/null 2>&1 || true
  # Prove the strip did not eat anything. Every stripped binary is re-stripped in
  # isolation into a scratch copy; if the sizes disagree, the shipped one has been
  # damaged. This is the check that was missing when a truncated GetNetStatus went
  # out and Gurued a real machine at boot -- the archive looked perfectly normal.
  "$ROOT/docker/cc.sh" bash -c '
    rm -rf /tmp/stripchk && mkdir -p /tmp/stripchk && rc=0
    for f in build/release/AmiTCP_NG/data/Libs/*.library build/release/AmiTCP_NG/data/C/*; do
      cp "$f" /tmp/stripchk/one && m68k-amigaos-strip /tmp/stripchk/one
      a=$(stat -c%s "$f"); b=$(stat -c%s /tmp/stripchk/one)
      if [ "$a" != "$b" ]; then
        echo "!!! strip damaged $(basename "$f"): shipped $a bytes, clean $b" >&2; rc=1
      fi
    done
    exit $rc' || {
      echo "!!! build-release: stripped binaries do not survive a re-strip -- aborting" >&2
      exit 1; }
  echo ">>> strip verified: every binary is byte-stable under a second strip"

  cp "$ROOT/install/S/Network-Startup"        "$STAGE/data/S/"
  cp "$ROOT/install/Storage/NetInterfaces/"*  "$STAGE/data/Storage/NetInterfaces/"
  cp "$ROOT/install/Devs/Internet/hosts"      "$STAGE/data/Devs/Internet/"
  cp "$ROOT/install/db/"*                     "$STAGE/data/db/"

  # The record of what releases BEFORE the manifest existed put into C:. The
  # installer needs it to tell its own previously-installed commands from the
  # user's files on any machine running v4.1.5 or earlier -- get it wrong in one
  # direction and a user's command is overwritten unbacked, in the other and our
  # own binaries are filed as the user's and "restored" over the new ones.
  #
  # Checked, not assumed. An empty or missing drawer answers "none of these names
  # is ours", which renames all 16 of our own commands to <name>.orig, reports them
  # to the user as their files, and has Uninstall restore the old binaries over the
  # new ones. No user file is lost that way -- the genuinely destructive direction
  # is a name wrongly listed AS ours, which is an unbacked overwrite -- but the
  # record is corrupted either way, so neither is allowed to ship.
  cp "$ROOT/install/Legacy/"*                 "$STAGE/data/Legacy/"
  cp "$ROOT/install/Legacy416/"*              "$STAGE/data/Legacy416/"
  # Names, not just the count: a typo'd or renamed entry still counts 17, and a
  # name missing from this list is the direction that overwrites a user's file.
  legacy_want="AddNetInterface AddNetRoute ConfigureNetInterface DeleteNetRoute \
GetNetStatus NetShutdown Offline Online RemoveNetInterface ShowNetStatus ftp \
netstat nslookup ping rxprofile sntp tftp"
  legacy_have=$(ls -1 "$STAGE/data/Legacy" | sort | tr '\n' ' ')
  legacy_want=$(printf '%s\n' $legacy_want | sort | tr '\n' ' ')
  [ "$legacy_have" = "$legacy_want" ] || {
    echo "!!! build-release: data/Legacy does not match the expected name list" >&2
    echo "    have: $legacy_have" >&2
    echo "    want: $legacy_want" >&2; exit 1; }

  # The same record for the 4.1.6 betas, which shipped the full command set and
  # usergroup.library but still wrote no manifest.
  #
  # BOTH LISTS ARE FROZEN. Do NOT regenerate either from NG_TOOLS when the command
  # set changes: they describe what particular archives PUT ON A MACHINE, which is
  # history and cannot change. Rebuild one from the current payload and a release
  # that adds a command has Uninstall delete a file of that name on a machine that
  # never received it.
  legacy416_want="AddNetInterface ConfigureNetInterface RemoveNetInterface NetShutdown \
Online Offline AddNetRoute DeleteNetRoute GetNetStatus ShowNetStatus AmiTCPControl \
arp SampleNetSpeed traceroute CheckAmiTCPNGConfig NetLogViewer ManageNetInterfaces \
PacketCapture ping netstat tftp nslookup ftp sntp rxprofile"
  legacy416_have=$(ls -1 "$STAGE/data/Legacy416" | sort | tr '\n' ' ')
  legacy416_want=$(printf '%s\n' $legacy416_want | sort | tr '\n' ' ')
  [ "$legacy416_have" = "$legacy416_want" ] || {
    echo "!!! build-release: data/Legacy416 does not match the expected name list" >&2
    echo "    have: $legacy416_have" >&2
    echo "    want: $legacy416_want" >&2; exit 1; }

  # 2a. Structural check of the Installer script against what we just staged.
  # The Installer utility is Commodore-licensed and is not in the test image, so an
  # install cannot be rehearsed in the harness -- this catches the failure modes that
  # would otherwise surface half way through an install on a user's machine: a
  # (source) naming a file we forgot to ship, a procedure that is called but never
  # defined, or parens that do not balance.
  python3 "$ROOT/docker/check-installer.py" "$STAGE/Install-AmiTCP_NG" "$STAGE" || {
    echo "!!! build-release: the staged Installer script did not check out" >&2; exit 1; }

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
  # `|| lha_rc=$?`, for the same reason as the ADF step below: under `set -e` a
  # bare failing command kills the script THERE, so the diagnostic underneath
  # could never print and the real error was discarded to /dev/null as well --
  # a release build that stopped without saying why. This site was left behind
  # when the ADF one was fixed, which is the same "fix the instance, miss the
  # sibling" mistake the fix itself was written to correct.
  local lha_rc=0
  "$ROOT/docker/cc.sh" bash -c "cd /work/build/release && rm -f '$LHA_NAME' && lha a '$LHA_NAME' AmiTCP_NG AmiTCP_NG.info" >/dev/null 2>&1 || lha_rc=$?
  "$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" "/work/build/release/$LHA_NAME" >/dev/null 2>&1 || true

  if [ $lha_rc -ne 0 ] || [ ! -f "$OUT" ]; then
    echo "!!! lha packaging FAILED (rc=$lha_rc) for $LHA_NAME" >&2
    exit 1
  fi
  echo ">>> release archive: build/release/$LHA_NAME ($(wc -c < "$OUT") bytes)"

  # 4. Also wrap the same install tree in an 880K DD floppy image (ADF) for real
  # floppies / Gotek-style flash drives. xdftool (amitools) lives in the fsuae image.
  # The disk is a plain data disk (FFS, volume "AmiTCP_NG"): boot your Amiga normally,
  # insert it, and run its Installer -- it is not a bootable disk.
  local ADF="$ROOT/build/release/$ADF_NAME"
  rm -f "$ADF"
  docker run --rm -v "$ROOT":/work -w /work -e ADF_NAME="$ADF_NAME" amitcp-ng-fsuae:latest bash -c '
    set -e
    cd build/release/AmiTCP_NG
    A="/work/build/release/$ADF_NAME"
    xdftool "$A" create + format "AmiTCP_NG" ffs
    find . -type d ! -name . | sed "s|^\./||" | sort | while read d; do xdftool "$A" makedir "$d"; done
    find . -type f | sed "s|^\./||" | while read f; do xdftool "$A" write "$f" "$f"; done
    xdftool "$A" write /work/build/release/Disk.info Disk.info   # volume icon
  ' || adf_rc=$?
  # `|| adf_rc=$?` and NOT a bare `adf_rc=$?` on the next line. This script runs
  # under `set -e`, so a plain failing command kills it THERE -- the assignment
  # would never execute and the message below could never print. Putting the
  # docker run on the left of `||` both suppresses that and captures the real
  # exit code. (The build did still abort without this, but by accident, with no
  # explanation of what failed.)
  "$ROOT/docker/cc.sh" chown "$(id -u):$(id -g)" "/work/build/release/$ADF_NAME" >/dev/null 2>&1 || true
  # `xdftool create` runs FIRST, so the file exists from the moment the image is
  # started -- testing for it proves only that we began, not that every write
  # landed. A disk that filled up half way through would leave a partial image
  # that this reported as a success, and the first anyone would know is a floppy
  # that will not boot. Trust the exit status, and fail the build on it: shipping
  # a silently truncated .adf is worse than shipping no .adf.
  if [ $adf_rc -ne 0 ] || [ ! -f "$ADF" ]; then
    echo "!!! ADF creation FAILED (xdftool rc=$adf_rc) -- see the output above" >&2
    exit 1
  fi
  echo ">>> floppy image:   build/release/$ADF_NAME ($(wc -c < "$ADF") bytes)"
}

# ONE archive, built for 68000, which runs on every 68k Amiga.
#
# The 68020 and 68040 archives were dropped after real-world testing showed the
# higher -march buys nothing: gcc's codegen at 68020/68040 did not make the stack
# measurably faster, and on at least one occasion made it SLOWER (the ip_v/ip_hl
# bitfields becoming a read-modify-write BFINS once per transmitted packet, an
# instruction the 68000 build cannot even encode).
#
# Nothing is lost by dropping them. The copy path no longer varies by CPU AT ALL:
# ng_bcopy() (kern/ng_bcopy.S, contributed by Timm Mueller) is one plain 68000
# routine that picks its strategy from the pointers and the length, not from
# AttnFlags, so there is no per-CPU routine left to pre-select at build time. That
# also removes the way a user could install a build their CPU cannot run.
build_variant 68000 ""

echo ">>> built:"
ls -l "$ROOT/build/release/"AmiTCP_NG-v"$VER"*.lha "$ROOT/build/release/"AmiTCP_NG-v"$VER"*.adf 2>/dev/null \
  | awk '{print "    "$5"\t"$NF}'
