# AmiTCP_NG — porting log (AmiTCP 3.0b2 → bebbo m68k-amigaos-gcc 6.5)

Chronological record of the gcc-2.x(1994) → gcc-6.5 port. Each entry: symptom → cause → fix.

## Toolchain
- **Compiler:** prebuilt Docker image `amigadev/crosstools:m68k-amigaos` = bebbo **m68k-amigaos-gcc 6.5.0b**, target `m68k-amigaos`, with NDK headers, `libnix`, `amiga.lib`.
  - The classic `github.com/bebbo/amiga-gcc` source repo is **gone (404)**; its Docker Hub image (`sebastianbergmann/amiga-gcc`) is also gone. Self-building is dead; the `amigadev/crosstools` prebuilt is the working path.
- **Smoke test:** compiles + links a `dos.library` hello to a valid AmigaOS Hunk exe (magic `0x000003f3`). ✓
- **Harness:** `docker/cc.sh` (run any cmd in the toolchain, `--rm`), `docker/ccflags.sh` (the flag set), `docker/compile1.sh` (compile one file).

## Build flags (replicating AmiTCP's original `GCCOPTS`, modernized)
```
-nostdinc
-Isrc/netinclude   (AmiTCP's OWN 1994 BSD headers — see below)
-Isrc -Isrc/conf -Isrc/protos
-isystem <ndk-include>      (AmigaOS: exec/, dos/, proto/, inline/ …)
-isystem <sys-include>      (fills machine/limits.h etc.)
-isystem <gcc-6.5.0b/include> (re-add gcc builtins killed by -nostdinc: stdarg.h …)
-std=gnu89 -fno-builtin -O1 -fomit-frame-pointer
-DAMITCP -DKERNEL -DSOCKBUF_DEBUG -DTCPDEBUG -DDIRECTED_BROADCAST -DICMPPRINTFS
-include src/conf/rcs.h -include src/conf/amitcp_ng_bases.h
```

## Key source acquisition
- The AmiTCP **source** distro (`AmiTCP-src-30b2`) references `-I../netinclude` but does **not** contain it. Pulled the matching **`AmiTCP-api-30b2.lha`** (SDK) from Aminet `comm/net/` — its `netinclude/` is the exact 1994 BSD header set, folded into `src/netinclude/`. Critically it has **no `sys/mbuf.h`** (that's kernel-internal in `src/sys/`), so no clash — do NOT substitute Roadshow/NDK-3.2 netinclude (different mbuf/struct layouts).

## Fixes applied (getting it to compile)
1. **`netinclude/amitcp/types.h`** — `#if __SASC && (__VERSION__ > 6 ...)`: modern gcc's `__VERSION__` is a *string literal*, illegal in `#if`; `&&` still parses the RHS. Nested the `#ifdef __SASC` so `__VERSION__` is never seen under gcc.
2. **`sys/param.h` → `machine/limits.h` not found** — flag fix: added `-isystem sys-include` (searched after our netinclude).
3. **`sys/systm.h` → `stdarg.h` not found** — `-nostdinc` also strips gcc's builtin headers; re-added `-isystem <gcc>/include`.
4. **`sys/protosw.h`** — `void (* STKARGFUN pr_input)(...)`: ISO C forbids a lone `(...)`; changed to `()` (K&R unspecified-args = the BSD protosw intent). Same in **`api/apicalls.h`** (`f_void`) and the **`rtsock.c`** protosw-table cast `(void (*)(...))` → `(void (*)())`.
5. **Missing root headers** not forked — folded in `bsdsocket.library_rev.h` + `.rev`, `all_includes.h`. `api/sockargs.h` existed but was `#include <sockargs.h>` (angle brackets don't search the file's dir) → changed to `"sockargs.h"`.
6. **Library bases undeclared** (`DOSBase`, `TimerBase`, `SysBase`) — bebbo's `<inline/*.h>` emit only call stubs and need the base in scope; old amiga-gcc implied it, and AmiTCP's `amiga_includes.h` never declared `DOSBase`. Added force-included **`src/conf/amitcp_ng_bases.h`** declaring the bases as externs (types matched to AmiTCP). Fixed ~7 files at once.
7. **`api/resolv.h` `#define _res (libPtr->res_state)` vs `apicalls_gnuc.h`** — the inline-asm API stubs used `_res` as a local `d0` register temp; the resolver macro rewrote the declaration into garbage. Renamed the local `_res` → `_api_d0` (108 sites).
8. **`apicalls_gnuc.h` asm clobber conflict** — every stub binds the output to `d0` (`_api_d0 __asm("d0")`) *and* listed `"d0"` in the clobber list; gcc 6 rejects that. Stripped `"d0",` from all clobber lists (d0 is always the output).

**Status: 67/67 core .c files compile (100%).** ✓

## The final-9 fixes
9. **`api/amiga_api.c:89`** — `#if sizeof(fd_mask)!=4 ...`: cpp can't evaluate `sizeof` (SAS/C extension). Converted to a C-level static assertion (negative-array-size typedef).
10. **`api/amiga_generic2.c:698`** — `SetErrnoPtr(...) < 0`: the SFD-generated gnuc stub declared `void`, but the impl returns `LONG` in d0 and callers test it. Changed the stub to return `LONG`.
11. **`api/auto_netdb.c` + `api/auto_protocols.c`** — pure **autodoc** files (0 functions); their `/****** … ******/` doc blocks embed example code with literal `/* … */` that closes the comment early. Neutralised the *embedded* markers (`/*`→`(*`, `*/`→`*)`) on all lines except block-opens (`^/*`) and terminators (`^*/`), leaving each block a single clean comment.
12. **`kern/amiga_config.c:538`** — `Printf("…%s", CMDLINETEMP)`: bebbo's varargs `Printf` packs args into an `_sfdc_vararg[]` (APTR) array, and a bare string literal can't initialise that element. Cast the arg `(APTR)CMDLINETEMP`.
13. **`kern/amiga_log.c:510`** — `struct CSource cs;` incomplete: `struct CSource` is an **AmigaDOS** type from `<dos/rdargs.h>`, which `amiga_log.c` didn't include. Added it.
14. **`net/sana2config.c:346`** — "conflicting types for 'ssconfig'": `sana2config.h`'s prototype used `struct sana_softc *` without the tag being declared, so it became a prototype-scope incomplete type ≠ the file-scope one at the definition. Forward-declared `struct sana_softc;` in the header.
15. **`net/if_sana.c:91`** — `CheckIO` redeclaration clashed with bebbo's correct `<proto/exec.h>` inline. Guarded the manual redecl to `#ifdef __SASC` (it only existed to fix old SAS/C's wrong prototype).
16. **`net/if_sana.c:110`** — `extern struct wiretype_parameters wiretype_table[];`: `struct wiretype_parameters` was **never defined anywhere in AmiTCP 3.0b2** and `wiretype_table` is unused; gcc 6 rejects an extern array of incomplete element type. Removed the dead declaration.
17. **`kern/amiga_main.c`** (surfaced as `sys/time.h:36`) — pulling bebbo's libc `<signal.h>` dragged in `pthread_t` + a `tv_sec` use that collided with AmiTCP's `timeval` field-remap macros. `<signal.h>` was used only for `signal(SIGINT, SIG_IGN)` — a no-op in a `-noixemul` library (no POSIX signals; native `SIGBREAKF_CTRL_C` is handled in the wait loop). Removed the include and the call.

## Link
Full reproducible build: **`docker/build.sh`** → `build/amitcp` (AmigaOS Hunk executable, 133 KB text / 2.5 KB data / 6.6 KB bss). Strings confirm `bsdsocket.library`, `"AmiTCP/IP release 3 bsdsocket.library 3.30"` (an early-port version string; the library was later re-versioned to 4.1 for Roadshow-v4 ABI compatibility — see below).

**Architecture note:** `amitcp` is *not* a `.library` file — there is no Resident romtag anywhere in AmiTCP. It is the stack **program** that calls `MakeLibrary(LibVectors, Library_initTable, …)` (`api_init`, `api/amiga_api.c:441`) to *create and install* `bsdsocket.library` into the system at runtime, then runs the stack (SANA-II + IP/TCP/UDP, `kern/amiga_main.c:main`). (Roadshow instead ships an on-disk `bsdsocket.library`; AmiTCP_NG also provides an on-disk drop-in `LIBS:bsdsocket.library` that lazily self-starts the whole stack on the first API call.)

### Build steps captured in `docker/build.sh`
1. `docker/gen_config_var.sh` — regenerate `kern/config_var.c` from `kern/variables.src` (gawk + leaked-line filter, see above). `netinet/in_cksum.asm` is NOT needed — the C `in_cksum.c` compiles and is used.
2. Compile all 67 core `.c` + `src/amitcp_ng_glue.c` (68 objects).
3. Link: `m68k-amigaos-gcc -mcrt=clib2 -o amitcp *.o -Wl,--allow-multiple-definition <libamiga.a>`.

### Link-layer fixes
- **`-mcrt=clib2`** — our objects reference `__locale_ctype_ptr` (clib2's ctype, pulled via `<ctype.h>` in `inet_aton` etc.); libnix (`-noixemul` default) lacks it, clib2 provides it.
- **`--allow-multiple-definition`** — AmiTCP ships its own `ultoa`, which also exists in libnix.
- **explicit `libamiga.a`** — provides ROM-call stubs like `Amiga2Date`; `-lamiga` isn't on clib2's lib search path.
- **`src/amitcp_ng_glue.c`** — stubs for `__assert_func` (clib2 declares but doesn't define it; used by `sana2config.c` asserts) and the API-form `gethostname(SocketBase,…)` (real home is the library host-id / client inline stub). Both marked `TODO`.

## Runtime — harness validated, startup hang found and fixed

### Harness (works, fully reproducible)
- **`docker/Dockerfile.fsuae`** — headless FS-UAE 3.1.66 + Mesa/llvmpipe software GL (no GPU) + amitools. **`docker/run-fsuae.sh`** — Xvfb-wrapped, hard `TIMEOUT`, disposable.
- **`emu/AmiTCP_NG.fs-uae`** — A600 (68000/ECS), your licensed **KS 47.96** ROM, 2 MB chip + 8 MB fast, boots a **directory hard drive** = `emu/hdd/System/Workbench3.2/` (extracted from your WB 3.2 ADF with `xdftool`).
- **Capture without a display:** the directory-HD is a host directory, so anything the Amiga writes to `SYS:` appears as a host file. No serial needed.
- **Config/db** from Aminet `comm/net/AmiTCP-bin-30b2.lha` (in `ref/amitcp-bin-30b2/`): `AmiTCP:db/{AmiTCP.config,netdb,interfaces}`, loopback-only. netdb must be self-contained (no `WITH` includes) or supply the included files.

### End-to-end PROOF (control test)
Running the **original** AmiTCP 3.0b2 binary in this harness → `bsdsocket.library 3.28 (2.5.94)` installed, resident as a process. **So the harness + config + boot are correct.**

### Our binary ran — root cause of the startup hang (FIXED)
Our `amitcp` hung at startup. Bisected with a file-based trace (writing markers to the directory-HD) down to a single statement in `log_init`: the first **variable×variable 32-bit multiply** (`log_cnf.log_bufs * log_cnf.log_buf_len`). On the 68000 that becomes a `__mulsi3` call, and **bebbo's `__mulsi3` implements 32-bit multiply by calling `utility.library`'s `SMult32`/`UMult32` through the global `UtilityBase`** (`movea.l _UtilityBase,a0; jmp -138(a0)`). AmiTCP defines its own `UtilityBase` (`amiga_rexx.c`) and only opens it later in `rexx_init()`, so the first multiply — long before that — jumped through a NULL base and crashed. Because AmiTCP provides the symbol, libnix's auto-open never fired.
**Fix:** open `utility.library` at the very top of `main()` (`kern/amiga_main.c`) so `UtilityBase` is valid before any integer math. Also switched the runtime to **`-noixemul`/libnix** (see the build-flag notes above) and added **`-m68000`** to the link so the 68000-multilib libgcc is selected.
Two red herrings ruled out along the way: it was NOT clib2 crt state, and NOT illegal 68020 instructions (the whole binary has zero `muls.l`/`divs.l`).

### Result — the stack runs and installs the library
`docker/build.sh` → `build/amitcp`; run on the harness, it installs **`bsdsocket.library 3.30 (19.5.94)`** (our version *at this early port stage* — later re-versioned to **4.1** for Roadshow-v4 ABI compatibility; see `src/bsdsocket.library_rev.h`), resident, `Version FULL` confirms it, log clean. **Our compiled AmiTCP stack runs on real emulated AmigaOS 3.2 and installs our bsdsocket.library.**

### Packets flow — loopback round-trip
Proved the datapath works end-to-end with a **UDP loopback round-trip** through our own library (`tmp/udptest.c`, direct LVO calls — bypasses the reference `net.lib` tools):
```
socket() fd=0 · SIOCSIFADDR(lo0=127.0.0.1) r=0 · bind r=0 · sendto r=21 · recvfrom r=21
RECEIVED: "amitcp-ng-loopback-42" · RESULT: UDP LOOPBACK ROUND-TRIP OK
```
A datagram went socket → UDP → IP output → route → `lo0` (`if_loop`) → IP input → UDP → socket buffer → `recvfrom`, byte-identical. `ioctl(SIOCSIFADDR)` configured the interface (SIOCSIFADDR=`0x8020690C`, `ifreq`=32 bytes). Regression tests: `tmp/socktest.c` (open+socket), `tmp/udptest.c` (full round-trip).

Notes:
- `useloopback=YES` creates `lo0` but does NOT address it — you must `SIOCSIFADDR` 127.0.0.1 (or `ifconfig lo0 localhost`) before binding to it (else `bind` → EADDRNOTAVAIL/49).
- The reference `net.lib` tools (`ping`/`ifconfig`) exit early headless (they write to a CON: window, and hit `net.lib` init quirks); our library itself is proven good by the direct tests. Exercising the stock tools and external ICMP ping needs an emulator with SLIRP + a SANA-II driver — this Debian `fs-uae` has no networking. That external-NIC path was subsequently validated on Amiberry v7.1.1 + SLIRP and on real 68k hardware (PiStorm + wifipi.device).

## Known port debt

Two loose ends remain from the port itself (both cosmetic to the working stack, not blockers):
- The `gethostname` shim in `src/amitcp_ng_glue.c` is a stack-args stub returning an empty name; it still needs wiring to the configured host id (`SBTC_HOSTID`) and to the register-argument calling convention of the other API entry points (see the `TODO` there).
- The client inline stubs are the hand-patched `api/apicalls_gnuc.h`; regenerating them from the SFD via bebbo `sfdc` would remove the hand edits.
