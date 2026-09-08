#!/usr/bin/env python3
"""mini_dns — hermetic bench resolver (TNET-111).

Answers A and PTR queries for the tolunnet bench zone from a single UDP
socket, so the conformance suite never depends on the host resolver or the
Internet:

    A   test.tolunnet.lan        -> 10.0.2.2
    PTR 2.2.0.10.in-addr.arpa    -> test.tolunnet.lan
    *                            -> NXDOMAIN   (incl. nx.tolunnet.lan)

Usage: mini_dns.py [--port 5353] [--bind 0.0.0.0]
The bench verifies reachability with a host-side query before WinUAE starts.
"""
import argparse
import socket
import struct

A, PTR = 1, 12
ZONE_A = {b"test.tolunnet.lan": b"\x0a\x00\x02\x02"}          # -> 10.0.2.2
ZONE_PTR = {b"2.2.0.10.in-addr.arpa": b"\x04test\x08tolunnet\x03lan"}  # -> test.tolunnet.lan


def parse_name(pkt, off):
    labels = []
    while True:
        if off >= len(pkt):
            return None, off
        n = pkt[off]
        if n == 0:
            off += 1
            return b".".join(labels), off
        if n & 0xC0:  # compression pointer — not expected in questions
            return None, off
        labels.append(pkt[off + 1:off + 1 + n])
        off += 1 + n


def encode_name(name):
    out = b""
    for label in name.split(b"."):
        out += bytes([len(label)]) + label
    return out + b"\x00"


def build_reply(q, qid, rcode):
    qname, off = parse_name(q, 12)
    if qname is None or off + 4 > len(q):
        return None
    qtype, qclass = struct.unpack(">HH", q[off:off + 4])
    flags = 0x8180 | (rcode & 0xF)  # QR|RD|RA (+rcode)
    hdr = struct.pack(">HHHHHH", qid, flags, 1, 0 if rcode else 1, 0, 0)
    question = q[12:off + 4]
    if rcode:
        return hdr + question
    if qtype == A and qname in ZONE_A:
        rdata = ZONE_A[qname]
        rrtype = A
    elif qtype == PTR and qname in ZONE_PTR:
        rdata = ZONE_PTR[qname]
        rrtype = PTR
    else:
        # NXDOMAIN for everything else
        return struct.pack(">HHHHHH", qid, flags | 3, 1, 0, 0, 0) + question
    rr = encode_name(qname) + struct.pack(">HHIH", rrtype, qclass, 60, len(rdata)) + rdata
    return hdr + question + rr


def serve(sock):
    while True:
        try:
            q, addr = sock.recvfrom(512)
        except ConnectionResetError:
            continue  # Windows UDP: previous sendto hit a dead port
        except OSError:
            return
        if len(q) < 12:
            continue
        qid = struct.unpack(">H", q[:2])[0]
        try:
            reply = build_reply(q, qid, 0)
            if reply is not None:
                sock.sendto(reply, addr)
        except OSError:
            continue


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=5353)
    ap.add_argument("--bind", default="0.0.0.0")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    print("mini_dns: listening on %s:%d" % (args.bind, args.port), flush=True)

    serve(sock)


if __name__ == "__main__":
    main()
