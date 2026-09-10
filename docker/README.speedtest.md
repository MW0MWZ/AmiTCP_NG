# run-speedtest.sh — the issue #9 rig

Reproduces a *dead transfer* rather than a slow one, and measures the two halves
of the symptom separately: the transfer stopping, and the machine going slow.

    ./docker/run-speedtest.sh                       # AmiSpeedTest down+up, 68040
    CPUS="68000 68020 68040" ZW=1 BULK=100 \
      DIR=NONE ./docker/run-speedtest.sh            # the full matrix

## What it runs

| knob | what it does |
|---|---|
| `DIR=DOWN\|UP\|BOTH\|NONE` | AmiSpeedTest against a LAN server in the container |
| `ZW=1` | zero-window recovery test (`tmp/zwtest.c`) |
| `BULK=<MiB>` | `ftp GET test-<n>m.bin` off the transferhost — the only leg that does a genuinely large transfer |
| `CPUS="..."` | run the whole thing once per processor |
| `RAM=<MiB>` | override the per-CPU default |

`rxprofile WATCH` samples the interface every 2 s for the whole run, and
`latmeter` reports how late a one-second `Delay()` comes back — that is the
"machine went to treacle" symptom, measured without adding load.

## Things that will catch you out

**A 68000 cannot address Zorro III RAM.** Its bus is 24-bit. Pass `RAM=256` with
`CPU=68000` and the machine does not exist: the guest never boots and it looks
exactly like a hang in the code under test. RAM therefore defaults FROM the CPU,
and an impossible pairing is warned about rather than silently run.

**The NIC forces an A4000.** The A2065 is Zorro II, so an A500/A600/A1200 has
nowhere to put it and `a2065.device` never autoconfigures. Amiberry can also
emulate `ne2000_pcmcia` and `ariadne`, which would give a real A1200 a card, but
that needs a guest-side SANA-II driver we do not have. Overriding `CPU` on the
A4000 is how 68000/68020/68040 get covered, and it is honest: one shipped 68000
library is what runs on all of them.

**AmiSpeedTest's CLI does not set LAN mode.** `HOST=` selects `MODE_CLI_CLIENT`;
`MODE_LAN_CLIENT` is reachable only from the GUI. `speedTest()` is therefore
called with `isLAN=false`, which makes the byte-count exit in its receive loop
dead code:

    while ((rcvd = recv(...)) > 0) { bytes += rcvd;
        if (isLAN && bytes >= sizekB*1000) break; }   /* never taken */

It exits on EOF alone — what speedtest.net gives it — and never sends the "ACK"
the stock LAN server blocks waiting for. Run the two together unpatched and they
wait for each other for ever, which is indistinguishable from a stack that has
stopped delivering data: transfer dead, machine alive, nothing logged. That cost
four emulator runs and a false lead. `amispeedtest-lanserver-close.patch` makes
the server close after sending, like speedtest.net, which also unlocks the size
escalation — the client only multiplies the transfer size after a round
COMPLETES, so unpatched it never gets past its first 100 kB.

**The emulator is not a throughput rig.** ~4 Mb/s through an emulated 10 Mbit
A2065 and SLIRP, against 37 Mbit on real hardware. It proves correctness —
stalls, drops, ring state, window behaviour — not speed. And SLIRP terminates
the guest's TCP and opens its own socket onward, so anything read on the Linux
side describes SLIRP, not what the guest acknowledged. Use the guest's own
`netstat -s`.

## Third-party

AmiSpeedTest (Karl Jeacle, MIT) is NOT in this repo. Fetch
`aminet.net/comm/net/AmiSpeedTest.lha`, unpack it and its `src.lha` into
`tmp/amispeed/` (gitignored). The rig builds the far end from that source.
