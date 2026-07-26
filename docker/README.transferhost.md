# Transfer-test host (Samba + FTP + Fitz)

Test infrastructure for measuring **AmiTCP_NG bulk-transfer throughput** from an
Amiga guest — the three methods the Amiga community actually uses for moving large
files. One Docker container serves the same directory over all three, so the same
file can be pulled/pushed each way and timed. Built to reproduce and then fix
[issue #1](https://github.com/MW0MWZ/AmiTCP_NG/issues/1) (downloads slower than
Roadshow on high-bandwidth links). **Never shipped in a release.**

## What it runs

| Service | Port(s) | Access |
|---------|---------|--------|
| **Samba** (SMB1/NT1 … SMB3) | 445, 139 | guest, or user `amiga` / pass `amiga`, share `//<ip>/share` |
| **FTP** (vsftpd) | 21 + passive 30000–30009 | user `amiga` / pass `amiga`, chrooted to the share |
| **Fitz** (Aminet `comm/tcp/Fitz`, its own P2P protocol) | 17711 | `fitz mount <ip>:17711 <mountpoint>` |

The share is `/srv/share`. It holds a spread of **incompressible** test files so
the stack can be benched across CPU classes — a stock 68000 chews through the small
ones, an accelerated machine (PiStorm etc.) is timed on the big ones:

```
test-5m.bin  test-50m.bin  test-100m.bin  test-166m.bin  test-500m.bin  test-1024m.bin
```

(`test-166m.bin` matches issue #1; `test-1024m.bin` is 1 GiB.) Plus a tiny
`hello.txt`. The files live in the `amitcp-ng-share` Docker volume, generated once
(~1.8 GiB total, so the first start takes a while) and reused thereafter. Override
the set with `TESTFILE_SIZES` (space-separated MiB), e.g. `TESTFILE_SIZES="5 50"`.

## Run it

```bash
./docker/run-transferhost.sh                 # 166 MiB file, foreground
TESTFILE_MB=32 ./docker/run-transferhost.sh  # smaller file for a quick check
DETACH=1 ./docker/run-transferhost.sh        # background; docker logs -f transferhost
```

It builds the image, creates a Docker network `amitcp-net`, and runs the container
publishing the ports above. The startup banner prints the container's address and
the three service URLs.

## Reaching it from the Amiga guest

The container joins the `amitcp-net` Docker network. Put the **vAmiga** networking
container (task #43) on the **same network** (`--network amitcp-net`) and the guest
reaches the transfer host directly at its container IP — which is also what FTP
passive mode advertises (`pasv_address`), so passive transfers work without port
juggling. (Host port publishing is there too, for verifying the services from the
Docker host.)

## Verifying the host side (no Amiga needed)

```bash
# SMB
docker exec transferhost smbclient //127.0.0.1/share -U amiga%amiga -c 'ls'
# FTP
docker exec transferhost curl -s --user amiga:amiga ftp://127.0.0.1/
# Fitz — confirm it is listening
docker exec transferhost ss -ltn | grep 17711
```

## Notes

- **Fitz** is built from source at image-build time (MIT, Mercurial repo). If that
  fetch/build ever fails the image still builds with **SMB + FTP only** and the
  startup log says so (a `/FITZ_UNAVAILABLE` marker is set and `fitz serve` skipped).
- Loopback numbers from the Docker host are meaningless for throughput — the real
  measurement is the Amiga guest pulling `test-166m.bin` to `RAM:` over the emulated
  NIC, timed against Roadshow (task #46).
