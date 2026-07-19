# Cross-compiler image — `amigadev/crosstools:m68k-amigaos`

Builds AmiTCP_NG for 68000 AmigaOS. This image is **pulled from Docker Hub**, not
built by us — it is the community [`amigadev/crosstools`](https://hub.docker.com/r/amigadev/crosstools)
toolchain (bebbo `m68k-amigaos-gcc` 6.5.0). Everything runs in a disposable
`--rm` container with the repo bind-mounted at `/work`.

## Prerequisites

- Docker.
- ~2 GB free disk for the image (pulled automatically on first use).
- No host toolchain, headers, or Amiga libraries required.

## Quick start — build the whole stack

```bash
./docker/build.sh
```

Expected tail:

```
generated kern/config_var.c (126 lines)
compiled 73 objects
linked: build/amitcp
build/amitcp: AmigaOS loadseg()ble executable/binary
```

The output, `build/amitcp`, is an AmigaOS Hunk executable. It is not a library
file — running it on the Amiga **installs `bsdsocket.library`** into the system
and then runs the TCP/IP stack task. (That is the AmiTCP model: `amitcp` is the
program; `bsdsocket.library` is what applications open.)

## What `build.sh` does

1. Runs `docker/gen_config_var.sh` to regenerate `kern/config_var.c` from
   `kern/variables.src` (the config-variable table).
2. Compiles every translation unit under `src/{api,kern,net,netinet}` plus the
   glue file into `build/obj/`.
3. Links `build/amitcp` with `-noixemul -m68000`, `libamiga.a`, and
   `--allow-multiple-definition`.

All compiler flags live in one place, `docker/ccflags.sh`, which is `source`d
inside the container (its paths are image-internal, under `/opt/m68k-amigaos`).
Key choices, all deliberate:

- `-noixemul` — link the light **libnix** runtime, **not** clib2/ixemul. This
  matters at *runtime*: clib2's per-process C-runtime state is absent in the
  tasks the stack spawns with `CreateNewProcTags`, which hung the stack; libnix
  has no such dependency.
- `-nostdinc` with explicit `-isystem` re-adds, ordered so our
  `src/netinclude` wins over the AmigaOS NDK headers.
- `-std=gnu89 -fno-builtin -O1`.

## Helper scripts

| Script | Use |
|--------|-----|
| `docker/build.sh` | Full build → `build/amitcp`. |
| `docker/compile1.sh <file.c>` | Compile one translation unit with the standard flags (fast error-checking). |
| `docker/cc.sh <cmd>` | Run any command inside the toolchain container. |
| `docker/gen_config_var.sh` | Regenerate `kern/config_var.c` (also called by `build.sh`). |

## Compile a single file (the common iteration idiom)

```bash
docker run --rm -v "$PWD":/work -w /work amigadev/crosstools:m68k-amigaos bash -c '
  source docker/ccflags.sh
  m68k-amigaos-gcc -c src/api/amiga_roadshow.c -o /tmp/o.o \
    $NG_INC $NG_DEF $NG_CFLAGS $NG_FORCEINC'
```

## Compiling a test program

The guest test programs (`tmp/exttest.c`, `tmp/nictest.c`, `tmp/dhcptest.c`) are
ordinary Amiga executables, built `-noixemul -O2 -m68000`:

```bash
docker run --rm -v "$PWD":/work -w /work amigadev/crosstools:m68k-amigaos bash -c '
  m68k-amigaos-gcc -noixemul -O2 -m68000 tmp/exttest.c -o tmp/exttest'
```

Then deploy the binary into the emulator's boot volume and run one of the test
harnesses — see [README.fsuae.md](README.fsuae.md) /
[README.amiberry.md](README.amiberry.md).

## Notes

- The 68000 target has no MMU: a stray pointer write corrupts the machine rather
  than faulting. Compile-test every change; the harnesses catch runtime damage.
- `gen_config_var.sh` strips a modern-gawk EOF-record quirk from the generated
  file; the workaround is documented in the script itself.
