# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Test SNTP server for src/tools/sntp.c. Test infra only -- not a real NTP server.
#
#   python3 sntpd.py now             answer with the real time
#   python3 sntpd.py <ntp-seconds>   answer with a FIXED raw NTP value
#   python3 sntpd.py now bad-echo    reply, but do NOT echo the client's nonce
#   python3 sntpd.py now li3         reply with LI=3 (unsynchronised)
#   python3 sntpd.py now stratum0    reply with stratum 0 (kiss-o'-death)
#   python3 sntpd.py now flood       stream junk datagrams continuously
#
# The fixed-value mode is how the epoch and the 2036 era rollover are checked
# exactly rather than approximately:
#   3944678400 must come out as 2025-01-01 00:00:00   (era 0)
#    123010304 must come out as 2040-01-01 00:00:00   (era 1, wrapped)
#
# The adversarial modes exist because a server that only ever behaves correctly
# cannot test the code that handles misbehaviour -- the client's retry, echo and
# stratum handling are all invisible to a well-behaved server.
import socket, struct, sys, time, threading

FIXED = sys.argv[1] if len(sys.argv) > 1 else "now"
MODE  = sys.argv[2] if len(sys.argv) > 2 else "ok"
NTP_UNIX = 2208988800

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", 123))
print("sntpd: time=%s mode=%s" % (FIXED, MODE), flush=True)

def flood(addr):
    """Junk aimed at the client's port -- never a valid reply."""
    f = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    while True:
        f.sendto(b"\x00" * 48, addr)
        time.sleep(0.05)

while True:
    d, a = s.recvfrom(1024)
    if len(d) < 48:
        continue
    if MODE == "flood":
        threading.Thread(target=flood, args=(a,), daemon=True).start()
        print("flooding %s:%d" % a, flush=True)
        continue

    t = int(time.time()) + NTP_UNIX if FIXED == "now" else int(FIXED)
    r = bytearray(48)
    li = 3 if MODE == "li3" else 0
    r[0] = (li << 6) | (4 << 3) | 4              # LI, VN=4, Mode=4 (server)
    r[1] = 0 if MODE == "stratum0" else 2        # stratum
    r[2] = 4; r[3] = 0xEC
    # originate = the client's transmit timestamp, verbatim -- unless we are
    # deliberately failing to echo it
    r[24:32] = b"\xDE\xAD\xBE\xEF" * 2 if MODE == "bad-echo" else d[40:48]
    struct.pack_into("!I", r, 32, t)             # receive
    struct.pack_into("!I", r, 40, t)             # transmit
    s.sendto(bytes(r), a)
    print("replied to %s:%d ntp=%d mode=%s" % (a[0], a[1], t, MODE), flush=True)
