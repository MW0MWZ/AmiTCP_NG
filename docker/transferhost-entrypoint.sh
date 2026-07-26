#!/usr/bin/env bash
# AmiTCP_NG transfer-test host entrypoint: prepare the share + start SMB, FTP, Fitz.
set -u
SHARE=/srv/share

# A spread of incompressible test files, in MiB, so the stack can be benched
# across CPU classes -- a stock 68000 will chew through the small ones while an
# accelerated machine (PiStorm etc.) is timed on the big ones. 1024 = 1 GiB.
# Override with e.g. TESTFILE_SIZES="5 50" for a quick check.
SIZES="${TESTFILE_SIZES:-5 50 100 166 500 1024}"

mkdir -p "$SHARE"
[ -f "$SHARE/hello.txt" ] || echo "AmiTCP_NG transfer test host" > "$SHARE/hello.txt"
for MB in $SIZES; do
  f="$SHARE/test-${MB}m.bin"
  if [ ! -f "$f" ]; then
    echo ">>> generating ${MB} MiB incompressible test file: $f"
    dd if=/dev/urandom of="$f" bs=1M count="$MB" status=none
  fi
done
chown -R amiga:amiga "$SHARE"

# Container's own address -- used both for the banner and for FTP passive mode
# (works when the Amiga guest reaches the container directly on a shared network).
IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
[ -n "$IP" ] || IP="127.0.0.1"
grep -q '^pasv_address=' /etc/vsftpd.conf || echo "pasv_address=$IP" >> /etc/vsftpd.conf

echo ">>> starting smbd / nmbd ..."
nmbd -D
smbd -D

echo ">>> starting vsftpd ..."
mkdir -p /var/run/vsftpd/empty          # secure_chroot_dir (tmpfs /var/run is empty at start)
vsftpd /etc/vsftpd.conf &

if [ -f /FITZ_UNAVAILABLE ]; then
  echo ">>> Fitz NOT available (source fetch/build failed at image build) -- SMB + FTP only"
else
  echo ">>> starting 'fitz serve' on :17711 ..."
  fitz serve "$SHARE" PORT 17711 &
fi

# Raw TCP throughput endpoints for the Amiga bench client -- these isolate the TCP
# stack from any SMB/FTP protocol overhead, so they measure the stack itself.
#   :9000  SOURCE -- on connect, streams zeros as fast as the peer will take them
#                    (download / RX test: the Amiga receives)
#   :9001  SINK   -- reads and discards everything (upload / TX test: the Amiga sends)
echo ">>> starting raw TCP throughput source :9000 + sink :9001 ..."
# SOURCE: stream /dev/zero -> client (bidirectional; the client just recv's).
socat TCP-LISTEN:9000,fork,reuseaddr,nodelay OPEN:/dev/zero &
# SINK: client -> /dev/null (unidirectional read-and-discard).
socat -u TCP-LISTEN:9001,fork,reuseaddr,nodelay OPEN:/dev/null,create &

echo "========================================================================"
echo " AmiTCP_NG transfer test host is UP"
echo "   share dir : $SHARE"
echo "   test files: $(cd "$SHARE" && ls -1 test-*.bin 2>/dev/null | tr '\n' ' ')"
echo "   SMB       : //$IP/share            (guest, or user amiga / pass amiga)"
echo "   FTP       : ftp://amiga@$IP/       (pass amiga)"
echo "   Fitz      : $IP:17711              (fitz mount $IP:17711 ...)"
echo "========================================================================"

# Keep the container in the foreground.
exec sleep infinity
