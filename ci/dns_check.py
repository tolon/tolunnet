#!/usr/bin/env python3
"""Host-side mini_dns verification for ci/bench.sh (TNET-111).

Sends an A query for test.tolunnet.lan to 127.0.0.1:<port> and exits 0 only
when the answer contains 10.0.2.2.
"""
import socket
import struct
import sys


def main():
    port = int(sys.argv[1])
    qname = b"".join(bytes([len(l)]) + l for l in
                     [b"test", b"tolunnet", b"lan"]) + b"\x00"
    q = (struct.pack(">HHHHHH", 1, 0x0100, 1, 0, 0, 0) +
         qname + struct.pack(">HH", 1, 1))
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3)
    s.sendto(q, ("127.0.0.1", port))
    r = s.recvfrom(512)[0]
    sys.exit(0 if (len(r) > 12 and b"\x0a\x00\x02\x02" in r) else 1)


if __name__ == "__main__":
    main()
