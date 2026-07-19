# AmiTCP_NG — commenting conventions

AmiTCP_NG is meant to read as a *teaching* reference: a student should be able to
learn how a BSD TCP/IP stack is built and how it is grafted onto AmigaOS by reading
the code top to bottom. These conventions keep the annotations consistent and
useful rather than noisy.

## Ground rules

1. **Comments only — never change behaviour in a commenting pass.** If a comment
   reveals a real bug, note it (`XXX`/`TODO`) and raise it separately; don't fix it
   inline.
2. **Add, don't delete.** The original AmiTCP `$Log$`/RCS history and the authors'
   comments are primary sources — keep them. New teaching comments sit alongside.
3. **Explain *why*, not *what*.** `i++; /* increment i */` is noise. Comment the
   non-obvious: the algorithm, the invariant, the reason for an odd cast, the
   Amiga/BSD concept in play, the consequence of getting it wrong.
4. **Prefer teaching the concept once, at the right place.** Explain mbufs in
   `uipc_mbuf.c`'s header, the protosw switch in `uipc_domain.c`, ARP in
   `sana2arp.c` — then reference, don't repeat.
5. **Name the canonical source.** Where a file is stock 4.4BSD, say so and point to
   *TCP/IP Illustrated Vol 2* / the RFC, so a student knows what's Amiga-specific
   and what's universal.
6. **Mark every functional port change** with `PORT (AmiTCP_NG): <why>` — these are
   the diffs from the 1994 original and are themselves a lesson.

## Layers of comment

### File header
Every source file opens with a block that answers: *what is this file, where does it
sit in the architecture (link to `docs/ARCHITECTURE.md` §), what are its key data
structures, and what should I read first?* Keep the original copyright/`$Log$`
below it. Template:

```c
/*
 * <file> --- <one-line role>
 *
 * <2-6 sentences: what this file does, how it fits the stack (ARCHITECTURE §N),
 *  the central data structure(s), and any Amiga-vs-BSD note. For BSD-core files,
 *  name the upstream and what (if anything) AmiTCP changed.>
 *
 * Read first: <the key function or struct to start from>.
 */
```

### Function header
Non-trivial functions get a block: purpose, the interesting parameters/return, the
algorithm in a few lines, and gotchas (locking/spl, who calls it, what context —
main task vs. spawned task vs. emulated interrupt).

```c
/*
 * <name> --- <what it does, one line>.
 *
 * <How it works: the steps that matter. Context it runs in. Preconditions.
 *  Why it's written this way if that's non-obvious.>
 */
```

Trivial getters/setters and self-evident wrappers don't need a block — a one-liner
or nothing is fine. Don't pad.

### Inline
Reserve for the genuinely tricky line: a bit-twiddle, an mbuf pointer adjustment, a
register-argument `jsr`, a checksum fold, an off-by-one that matters. One clear
sentence beats three vague ones.

### Teaching aside
Where a concept first appears, a short `/* --- aside: ... --- */` block is welcome
to explain the AmigaOS or BSD idea for a newcomer. Use sparingly and only at the
natural first encounter.

## Tone
Plain, precise, unhurried. Assume a competent programmer who does *not* yet know
AmigaOS internals or BSD networking. British/American spelling either way; match the
file. Wrap at ~80 columns to match the surrounding code.

## What NOT to do
- No restating the code in English.
- No apologising for the 1994 code or editorialising ("this is ugly").
- No comments that will silently rot (avoid line numbers; refer to functions/structs
  by name).
- No ASCII art that won't survive reformatting, except small, stable diagrams in
  file headers.
