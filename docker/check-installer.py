#!/usr/bin/env python3
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
"""Structural check of the Amiga Installer script against the staged release tree.

The Installer utility is Commodore-licensed and is not in the emulator test image, so
an install cannot be rehearsed in the harness. These are the failure modes that would
otherwise only surface part-way through an install on a user's machine:

  1. unbalanced parentheses            -- the script does not parse at all
  2. a (source ...) naming a file that is not in the archive -> copyfiles ABORTS
  3. a procedure called at top level before it is defined, or never defined

WHAT THIS DELIBERATELY DOES NOT DO: guess. Every check either proves its point or
says nothing. A checker that can pass a broken script is worse than no checker,
because the green result is taken as evidence -- an earlier version of this file
counted parens over the raw text while its own comment claimed it skipped comments,
so a stray ')' inside a comment could cancel out a genuinely missing one.
"""
import re, sys, os


def strip_comments(t):
    """Blank out ';' comments, preserving line count and string contents."""
    out, in_str, esc, i = [], False, False, 0
    while i < len(t):
        ch = t[i]
        if in_str:
            out.append(ch)
            if esc:            esc = False
            elif ch == '\\':   esc = True
            elif ch == '"':    in_str = False
        elif ch == '"':
            in_str = True
            out.append(ch)
        elif ch == ';':
            while i < len(t) and t[i] != '\n':
                i += 1
            out.append('\n')
            continue
        else:
            out.append(ch)
        i += 1
    return ''.join(out)


def match_paren(code, open_at):
    """Offset of the ')' matching the '(' at open_at, or -1. String-aware."""
    depth, in_str, esc, i = 0, False, False, open_at
    while i < len(code):
        ch = code[i]
        if in_str:
            if esc:            esc = False
            elif ch == '\\':   esc = True
            elif ch == '"':    in_str = False
        elif ch == '"':
            in_str = True
        elif ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def main():
    script, stage = sys.argv[1], sys.argv[2]
    text = open(script, encoding='latin-1').read()
    code = strip_comments(text)          # every check below reads THIS, never `text`
    fail = []

    # ---- 1. parentheses -------------------------------------------------
    depth, in_str, esc, line = 0, False, False, 1
    for ch in code:
        if ch == '\n':
            line += 1
            continue
        if in_str:
            if esc:            esc = False
            elif ch == '\\':   esc = True
            elif ch == '"':    in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth < 0:
                fail.append("unbalanced ')' at line %d" % line)
                break
    if depth > 0:
        fail.append("parens end at depth %d (should be 0) -- a form is unclosed" % depth)

    # ---- 2. every (source ...) must exist in the staged tree -------------
    # Resolve (set ...) forms first. A single form may set SEVERAL name/value pairs,
    # so pair them up inside the form rather than requiring ')' after the first.
    setvars, computed, reassigned = {}, set(), set()
    for m in re.finditer(r'\(set\b', code):
        end = match_paren(code, m.start())
        if end < 0:
            continue
        body = code[m.start() + 4:end]
        for name, val in re.findall(r'(#[\w-]+)\s+"([^"]*)"', body):
            if name in setvars and setvars[name] != val:
                # Assigned twice with different values: checking a (source) against
                # either one would be a guess, and a guess in a build gate is how
                # you get a false pass. Record it as unknowable instead.
                reassigned.add(name)
            setvars.setdefault(name, val)
        # A name followed by '(' takes a computed value -- defined, but only knowable
        # at run time, so not checkable against the archive.
        for name in re.findall(r'(#[\w-]+)\s+\(', body):
            computed.add(name)

    # Paths on the MACHINE BEING INSTALLED TO, not in the archive -- so there is
    # nothing here to check them against. "AmiTCP:" is ours: the installer copies a
    # user's existing configuration out of it when the AmiTCP drawer moves.
    ON_TARGET = ("LIBS:", "SYS:", "C:", "DEVS:", "ENVARC:", "ENV:", "S:", "RAM:", "T:",
                 "AmiTCP:")

    def resolve(arg):
        """A (source ...) argument -> a path, or None if it cannot be known here."""
        arg = arg.strip()
        if arg.startswith('"'):
            return arg.strip('"')
        if arg.startswith('#'):
            # "Unknowable" MUST be tested before "known", not after. Every name in
            # `reassigned` is by construction also in `setvars` (it only lands there
            # on a second, different assignment), so testing setvars first made the
            # guard below dead code and did the one thing this file's own comment
            # forbids: silently resolve a multi-valued variable to whichever value
            # happened to be seen first. A (source) built from it was then checked
            # against that one guess and the other values never checked at all.
            if arg in computed or arg in reassigned:
                return None            # known to exist, value not knowable here
            if arg in setvars:
                return setvars[arg]
            fail.append("(source %s) -- variable is never set" % arg)
            return None
        if arg.startswith('('):
            # (tackon A B) is the one nested form this script uses, and skipping it
            # meant a whole archive reference went unchecked -- exactly failure mode
            # 2, silently. Resolve it when both halves are knowable.
            inner = arg[1:-1].strip() if arg.endswith(')') else arg[1:].strip()
            parts = re.findall(r'"[^"]*"|#[\w-]+', inner)
            if inner.split()[:1] == ['tackon'] and len(parts) == 2:
                a, b = resolve(parts[0]), resolve(parts[1])
                if a is None or b is None:
                    return None
                return a.rstrip('/') + '/' + b
            return None
        return None

    checked_sources = skipped_sources = 0
    for m in re.finditer(r'\(source\b', code):
        end = match_paren(code, m.start())
        if end < 0:
            continue
        val = resolve(code[m.start() + len('(source'):end])
        if val is None or val.startswith(ON_TARGET):
            skipped_sources += 1       # a path on the user's machine, or unknowable
            continue
        checked_sources += 1
        if not os.path.exists(os.path.join(stage, val)):
            fail.append("(source) target is not in the staged archive: %s" % val)

    # ---- 3. procedures defined before use --------------------------------
    # A call inside a procedure BODY is fine whatever the textual order, because the
    # body is not evaluated until that procedure is called. Only TOP-LEVEL calls are
    # bound by textual order -- so compute the body spans and test membership, rather
    # than assuming (as an earlier version did) that any earlier definition anywhere
    # makes a call safe.
    defs, spans = {}, []
    for m in re.finditer(r'\(procedure\s+(\w+)', code):
        end = match_paren(code, m.start())
        defs.setdefault(m.group(1), m.start())
        if end > 0:
            spans.append((m.start(), end))

    def inside_a_procedure(at):
        return any(a < at < b for a, b in spans)

    # Calls are matched loosely (any bare "(name)"), but only two things are
    # reported: a P_-prefixed name that is never defined -- our own convention, so
    # a miss really is a bug -- and any DEFINED procedure called at top level
    # before its definition. Reporting every unknown bare name would flag the
    # Installer's own built-ins ((noreq), (expert), ...) as undefined procedures.
    for m in re.finditer(r'\(\s*(\w+)\s*\)', code):
        name, at = m.group(1), m.start()
        if name in defs:
            if at < defs[name] and not inside_a_procedure(at):
                fail.append("%s is called at top level before it is defined" % name)
        elif name.startswith("P_"):
            fail.append("call to %s but it is never defined" % name)

    # ---- 4. option keywords must be legal for their statement -------------
    #
    # This check exists because the ones above all passed a script that could not
    # run: (delete <file> (quiet)) is perfectly balanced, names no missing source
    # and calls no undefined procedure, and the Installer rejects it outright with
    # "invalid parameter for statement". QUIET is a real Installer keyword -- just
    # not one DELETE accepts -- so nothing short of knowing the per-statement
    # option lists could have caught it.
    #
    # The lists are what the shipped AmiTCP 3.0b2 script and our own released
    # installers actually use, plus options documented for a statement and used
    # here deliberately. An option NOT in this table is not necessarily illegal --
    # it is unproven, which after a broken release is treated the same way.
    OPTIONS = {
        "copyfiles": {"prompt", "help", "source", "dest", "newname", "choices",
                      "all", "pattern", "files", "infos", "confirm", "safe",
                      "optional", "delopts", "nogauge", "fonts"},
        "copylib":   {"prompt", "help", "source", "dest", "newname", "infos",
                      "confirm", "safe", "optional", "nogauge"},
        "delete":    {"prompt", "help", "confirm", "safe", "optional", "all",
                      "infos", "delopts", "pattern", "files"},
        "rename":    {"prompt", "help", "confirm", "safe", "optional", "disk"},
        "makedir":   {"prompt", "help", "infos", "confirm", "safe", "all"},
        "makeassign":{"prompt", "help", "confirm", "safe"},
        "textfile":  {"prompt", "help", "dest", "append", "include", "confirm",
                      "safe", "optional"},
        "startup":   {"prompt", "help", "command", "confirm"},
        "askdir":    {"prompt", "help", "default", "newpath", "disk", "assigns"},
        "askfile":   {"prompt", "help", "default", "newpath", "disk"},
        "askchoice": {"prompt", "help", "choices", "default"},
        "askbool":   {"prompt", "help", "default", "choices"},
        "asknumber": {"prompt", "help", "default", "range"},
        "askstring": {"prompt", "help", "default"},
        "askoptions":{"prompt", "help", "choices", "default"},
        "message":   {"prompt", "help", "all", "conclude"},
        "run":       {"prompt", "help", "confirm", "safe"},
        "execute":   {"prompt", "help", "confirm", "safe"},
        "exit":      {"prompt", "help", "quiet", "confirm"},
        "working":   set(),
        "complete":  set(),
        "welcome":   set(),
        "abort":     set(),
        "transcript": set(),
    }
    # Forms whose children are expressions or statements, never options.
    NOT_STATEMENTS = {"if", "and", "or", "not", "set", "cat", "tackon", "exists",
                      "procedure", "foreach", "while", "until", "select", "database",
                      "=", "<", ">", "<=", ">=", "<>", "+", "-", "*", "/", "in",
                      "BITAND", "BITOR", "patmatch", "pathonly", "fileonly",
                      "expandpath", "strlen", "substr", "getassign", "getenv",
                      "getdevice", "getsize", "getversion", "earlier", "trap",
                      "onerror", "user", "debug", "getdiskspace"}

    def audit_options(node, path="top"):
        if not isinstance(node, list) or not node:
            return
        head = node[0]
        if isinstance(head, str) and head in OPTIONS:
            for child in node[1:]:
                if (isinstance(child, list) and child
                        and isinstance(child[0], str)
                        and re.fullmatch(r"[a-z][a-z0-9-]*", child[0])
                        and child[0] not in NOT_STATEMENTS
                        and child[0] not in OPTIONS):
                    if child[0] not in OPTIONS[head]:
                        fail.append("(%s ...) is given (%s ...), which is not a "
                                    "known option of %s" % (head, child[0], head))
        for child in node:
            audit_options(child, path)

    # Parse into forms for this check. String contents are irrelevant here, so they
    # collapse to a placeholder -- the only thing that matters is the shape.
    def parse_forms(src):
        toks, i = [], 0
        buf = []
        while i < len(src):
            c = src[i]
            if c == '"':
                i += 1
                while i < len(src):
                    if src[i] == '\\': i += 2; continue
                    if src[i] == '"': break
                    i += 1
                buf.append('"S"'); i += 1; continue
            if c in '()':
                if buf: toks.append(''.join(buf)); buf = []
                toks.append(c); i += 1; continue
            if c.isspace():
                if buf: toks.append(''.join(buf)); buf = []
                i += 1; continue
            buf.append(c); i += 1
        if buf: toks.append(''.join(buf))
        pos = [0]
        def rd():
            out = []
            while pos[0] < len(toks):
                t = toks[pos[0]]; pos[0] += 1
                if t == '(': out.append(rd())
                elif t == ')': return out
                else: out.append(t)
            return out
        return rd()

    audit_options(parse_forms(code))

    print("checked: %s" % script)
    print("  (set) variables resolved : %d" % len(setvars))
    print("  procedures defined       : %d" % len(defs))
    print("  (source) forms checked   : %d" % checked_sources)
    print("  (source) forms skipped   : %d  (target-machine or computed paths)"
          % skipped_sources)
    if fail:
        print("\nFAILURES:")
        for f in fail:
            print("  - " + f)
        return 1
    # Say what was actually proved. The old wording claimed "every (source)" while
    # quietly skipping the ones it could not resolve -- and a green result that
    # overstates its own coverage is how a gate ends up trusted for something it
    # never checked.
    print("\nOK: parens balanced, %d of %d (source) forms resolved and present in "
          "the archive, every top-level procedure call defined first"
          % (checked_sources, checked_sources + skipped_sources))
    return 0


if __name__ == "__main__":
    sys.exit(main())
