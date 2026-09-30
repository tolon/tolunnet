#!/usr/bin/env python3
"""
ci/netsvc.py — Hermetic mock services on the slirp host (10.0.2.2 / 127.0.0.1).
Provides:
  - TCP Delayed sender (sleeps 1s before sending data)
  - UDP Echo server
  - SNTP server (RFC 4330)
  - TFTP server (RFC 1350 RRQ -> DATA block 1)
  - FTP server (RFC 959 control + PASV)
  - WHOIS server (RFC 3912)
  - HTTP server (HTTP/1.1 with Range support)

Runs in a single process with worker threads on Windows python.
"""

import sys
import os
import time
import socket
import select
import struct
import threading
import argparse
import subprocess
import signal

DEFAULT_PORTS_FILE = os.path.join(os.path.dirname(__file__), "netsvc.ports")

def load_ports(path=DEFAULT_PORTS_FILE):
    ports = {
        "ECHO_PORT": 15007,
        "TCP_DELAY_PORT": 15009,
        "SILENT_PORT": 15011,   # z.ai step 5: accepts, never sends
        "FTP_PORT": 15021,
        "FTP_PASV_PORT": 15020,
        "WHOIS_PORT": 15043,
        "TFTP_PORT": 15069,
        "HTTP_PORT": 15880,
        "SNTP_PORT": 15123,
        "DNS_PORT": 15353,
    }
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    k, v = line.split("=", 1)
                    k = k.strip()
                    try:
                        ports[k] = int(v.strip())
                    except ValueError:
                        pass
    return ports

g_log_file = None
g_log_lock = threading.Lock()
g_running = True

def log(tag, msg):
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{ts}] [{tag}] {msg}\n"
    with g_log_lock:
        sys.stdout.write(line)
        sys.stdout.flush()
        if g_log_file:
            try:
                g_log_file.write(line)
                g_log_file.flush()
            except Exception:
                pass

# ---------------------------------------------------------------------------
# 1. TCP Delayed Sender
# ---------------------------------------------------------------------------
def run_tcp_delay(port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    bind_exclusive(srv, port)
    srv.listen(5)
    srv.settimeout(1.0)
    log("tcp_delay", f"listening on TCP {port}")

    while g_running:
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        except Exception:
            break

        def handler(c, a):
            try:
                log("tcp_delay", f"accepted from {a}, sleeping 1.0 s before reply")
                time.sleep(1.0)
                c.sendall(b"TOLUNNET_TCP_DELAYED_OK\n")
                # Wait briefly for client to receive and close
                c.settimeout(2.0)
                try:
                    c.recv(1024)
                except Exception:
                    pass
            except Exception as e:
                log("tcp_delay", f"error: {e}")
            finally:
                c.close()

        t = threading.Thread(target=handler, args=(conn, addr), daemon=True)
        t.start()
    srv.close()

# ---------------------------------------------------------------------------
# 2. UDP Echo Server
# ---------------------------------------------------------------------------
def run_udp_echo(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_exclusive(sock, port)
    sock.settimeout(1.0)
    log("udp_echo", f"listening on UDP {port}")

    while g_running:
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except Exception:
            break

        log("udp_echo", f"echoing {len(data)} bytes to {addr}")
        try:
            sock.sendto(data, addr)
        except Exception as e:
            log("udp_echo", f"send error: {e}")
    sock.close()

# ---------------------------------------------------------------------------
# 2b. TCP Echo Server (z.ai step 3: conformance rows #68/#69 connect over
# TCP to ECHO_PORT; thread per connection, echo until EOF)
# ---------------------------------------------------------------------------
def run_tcp_echo(port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    bind_exclusive(srv, port)
    srv.listen(8)
    srv.settimeout(1.0)
    log("tcp_echo", f"listening on TCP {port}")

    while g_running:
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        except Exception:
            break

        def handler(c, a):
            try:
                c.settimeout(1.0)
                while g_running:
                    try:
                        data = c.recv(4096)
                    except socket.timeout:
                        continue
                    if not data:
                        break
                    log("tcp_echo", f"echoing {len(data)} bytes to {a}")
                    c.sendall(data)
            except Exception as e:
                log("tcp_echo", f"error: {e}")
            finally:
                c.close()

        t = threading.Thread(target=handler, args=(conn, addr), daemon=True)
        t.start()
    srv.close()

# ---------------------------------------------------------------------------
# 2c. Silent TCP Server (z.ai step 5: accepts connections and never
# sends - lets the suite prove Ctrl-C interrupts a parked recv)
# ---------------------------------------------------------------------------
def run_tcp_silent(port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    bind_exclusive(srv, port)
    srv.listen(8)
    srv.settimeout(1.0)
    log("tcp_silent", f"listening on TCP {port}")

    while g_running:
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        except Exception:
            break
        log("tcp_silent", f"accepted from {addr}, staying silent")
        # hold the connection open, never send
    srv.close()

# ---------------------------------------------------------------------------
# 3. SNTP Server (RFC 4330)
# ---------------------------------------------------------------------------
def run_sntp(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_exclusive(sock, port)
    sock.settimeout(1.0)
    log("sntp", f"listening on UDP {port}")

    # NTP epoch is 1900-01-01, Unix epoch is 1970-01-01 -> delta 2208988800s
    NTP_DELTA = 2208988800

    while g_running:
        try:
            data, addr = sock.recvfrom(1024)
        except socket.timeout:
            continue
        except Exception:
            break

        if len(data) < 48:
            continue

        now = time.time()
        ntp_sec = int(now + NTP_DELTA)
        ntp_frac = int((now - int(now)) * (2**32))

        # Originate timestamp: copy client transmit timestamp (bytes 40-47)
        orig_ts = data[40:48]

        resp = bytearray(48)
        resp[0] = 0x24  # LI=0, VN=4, Mode=4 (server)
        resp[1] = 1     # Stratum 1 (primary)
        resp[2] = 4     # Poll
        resp[3] = 0xEC  # Precision (-20)
        # Root delay (0), Root dispersion (0) -> bytes 4..11 = 0
        resp[12:16] = b"LOCL"  # Ref ID
        # Ref timestamp
        struct.pack_into("!II", resp, 16, ntp_sec, ntp_frac)
        # Originate timestamp
        resp[24:32] = orig_ts
        # Receive timestamp
        struct.pack_into("!II", resp, 32, ntp_sec, ntp_frac)
        # Transmit timestamp
        struct.pack_into("!II", resp, 40, ntp_sec, ntp_frac)

        log("sntp", f"replying NTP packet to {addr}")
        try:
            sock.sendto(resp, addr)
        except Exception as e:
            log("sntp", f"send error: {e}")
    sock.close()

# ---------------------------------------------------------------------------
# 4. TFTP Server (RFC 1350)
# ---------------------------------------------------------------------------
def run_tftp(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_exclusive(sock, port)
    sock.settimeout(1.0)
    log("tftp", f"listening on UDP {port}")

    while g_running:
        try:
            data, addr = sock.recvfrom(1024)
        except socket.timeout:
            continue
        except Exception:
            break

        if len(data) < 4:
            continue
        opcode = struct.unpack("!H", data[:2])[0]
        if opcode == 1:  # RRQ
            parts = data[2:].split(b"\x00")
            filename = parts[0].decode("latin1", errors="replace") if len(parts) > 0 else "unknown"
            mode = parts[1].decode("latin1", errors="replace") if len(parts) > 1 else "octet"
            log("tftp", f"RRQ for {filename} ({mode}) from {addr}")

            if filename == "bigfile":
                payload = bytes(((i * 7 + 3) & 0xFF) for i in range(1200))
            else:
                payload = b"TOLUNNET_TFTP_OK\n"
            # Multi-block transfer: send DATA, wait for the matching
            # ACK, resend on timeout or stale ACK (RFC 1350 lockstep).
            block = 0
            offset = 0
            try:
                while g_running:
                    block = (block + 1) & 0xFFFF
                    chunk = payload[offset:offset + 512]
                    sock.sendto(struct.pack("!HH", 3, block) + chunk, addr)
                    if len(chunk) < 512:
                        break
                    acked = False
                    for _ in range(3):
                        sock.settimeout(2.0)
                        try:
                            ack, _a = sock.recvfrom(1024)
                        except socket.timeout:
                            sock.sendto(struct.pack("!HH", 3, block) + chunk, addr)
                            continue
                        if len(ack) >= 4 and struct.unpack("!H", ack[:2])[0] == 4:
                            ablock = struct.unpack("!H", ack[2:4])[0]
                            if ablock == block:
                                acked = True
                                break
                            sock.sendto(struct.pack("!HH", 3, block) + chunk, addr)
                    if not acked:
                        log("tftp", "giving up on block %d" % block)
                        break
                    offset += 512
            except Exception as e:
                log("tftp", f"transfer error: {e}")
        elif opcode == 2:  # WRQ
            parts = data[2:].split(b"\x00")
            filename = parts[0].decode("latin1", errors="replace") if len(parts) > 0 else "unknown"
            log("tftp", f"WRQ for {filename} from {addr}")
            sock.sendto(struct.pack("!HH", 4, 0), addr)
            total = 0
            expected = 1
            sock.settimeout(3.0)
            try:
                while True:
                    dpack, _a = sock.recvfrom(2048)
                    if len(dpack) >= 4 and struct.unpack("!H", dpack[:2])[0] == 3:
                        dblock = struct.unpack("!H", dpack[2:4])[0]
                        if dblock == expected:
                            total += len(dpack) - 4
                            sock.sendto(struct.pack("!HH", 4, dblock), addr)
                            expected = (expected + 1) & 0xFFFF
                            if len(dpack) - 4 < 512:
                                break
            except socket.timeout:
                pass
            log("tftp", f"WRQ {filename} received {total} bytes")
    sock.close()

# ---------------------------------------------------------------------------
# 5. FTP Server (RFC 959)
# ---------------------------------------------------------------------------
def run_ftp(ctrl_port, pasv_port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    bind_exclusive(srv, ctrl_port)
    srv.listen(5)
    srv.settimeout(1.0)
    log("ftp", f"listening on TCP {ctrl_port} (PASV port: {pasv_port})")

    while g_running:
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        except Exception:
            break

        def client_handler(c, a):
            pasv_srv = None
            try:
                log("ftp", f"connected from {a}")
                c.sendall(b"220-Tolunnet Mock FTP Server ready\r\n"
                          b"220 Multiline greeting per RFC 959\r\n")
                buf = b""
                while g_running:
                    chunk = c.recv(1024)
                    if not chunk:
                        break
                    buf += chunk
                    while b"\r\n" in buf or b"\n" in buf:
                        if b"\r\n" in buf:
                            line, buf = buf.split(b"\r\n", 1)
                        else:
                            line, buf = buf.split(b"\n", 1)
                        cmd_line = line.decode("latin1", errors="replace").strip()
                        if not cmd_line:
                            continue
                        parts = cmd_line.split(" ", 1)
                        cmd = parts[0].upper()
                        arg = parts[1] if len(parts) > 1 else ""
                        log("ftp", f"command: {cmd} {arg}")

                        if cmd == "USER":
                            c.sendall(b"331 User name okay, need password\r\n")
                        elif cmd == "PASS":
                            c.sendall(b"230 User logged in, proceed\r\n")
                        elif cmd == "TYPE":
                            c.sendall(b"200 Type set to I\r\n")
                        elif cmd == "SYST":
                            c.sendall(b"215 UNIX Type: L8\r\n")
                        elif cmd == "PWD":
                            c.sendall(b"257 \"/\" is current directory\r\n")
                        elif cmd == "PASV":
                            if pasv_srv:
                                pasv_srv.close()
                            pasv_srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                            bind_exclusive(pasv_srv, pasv_port)
                            pasv_srv.listen(1)
                            pasv_srv.settimeout(5.0)
                            p1 = pasv_port // 256
                            p2 = pasv_port % 256
                            # 10.0.2.2 is the slirp host viewed from the guest
                            resp = f"227 Entering Passive Mode (10,0,2,2,{p1},{p2})\r\n"
                            c.sendall(resp.encode("ascii"))
                        elif cmd in ("LIST", "NLST"):
                            c.sendall(b"150 Opening data connection\r\n")
                            if pasv_srv:
                                try:
                                    dconn, _ = pasv_srv.accept()
                                    dconn.sendall(b"-rw-r--r-- 1 ftp ftp 17 Sep 23 12:00 test.bin\r\n"
                                                  b"-rw-r--r-- 1 ftp ftp 23 Sep 23 12:00 testfile.txt\r\n")
                                    dconn.close()
                                except Exception as e:
                                    log("ftp", f"PASV accept error: {e}")
                                pasv_srv.close()
                                pasv_srv = None
                            c.sendall(b"226 Transfer complete\r\n")
                        elif cmd == "RETR":
                            if arg != "test.bin":
                                log("ftp", f"RETR {arg}: no such file")
                                if pasv_srv:
                                    pasv_srv.close()
                                    pasv_srv = None
                                c.sendall(b"550 File not found\r\n")
                            else:
                                c.sendall(b"150 Opening data connection\r\n")
                                if pasv_srv:
                                    try:
                                        dconn, _ = pasv_srv.accept()
                                        dconn.sendall(b"TOLUNNET_FTP_OK\n")
                                        dconn.close()
                                    except Exception as e:
                                        log("ftp", f"PASV accept error: {e}")
                                    pasv_srv.close()
                                    pasv_srv = None
                                c.sendall(b"226 Transfer complete\r\n")
                        elif cmd == "STOR":
                            c.sendall(b"150 Ready to receive data\r\n")
                            if pasv_srv:
                                try:
                                    dconn, _ = pasv_srv.accept()
                                    dconn.settimeout(5.0)
                                    total = 0
                                    while True:
                                        chunk = dconn.recv(4096)
                                        if not chunk:
                                            break
                                        total += len(chunk)
                                    dconn.close()
                                    log("ftp", f"STOR received {total} bytes")
                                except Exception as e:
                                    log("ftp", f"PASV accept error: {e}")
                                pasv_srv.close()
                                pasv_srv = None
                            c.sendall(b"226 Transfer complete\r\n")
                        elif cmd == "QUIT":
                            c.sendall(b"221 Goodbye\r\n")
                            return
                        else:
                            c.sendall(b"500 Unknown command\r\n")
            except Exception as e:
                log("ftp", f"session error: {e}")
            finally:
                if pasv_srv:
                    pasv_srv.close()
                c.close()

        t = threading.Thread(target=client_handler, args=(conn, addr), daemon=True)
        t.start()
    srv.close()

# ---------------------------------------------------------------------------
# 6. WHOIS Server (RFC 3912)
# ---------------------------------------------------------------------------
def run_whois(port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    bind_exclusive(srv, port)
    srv.listen(5)
    srv.settimeout(1.0)
    log("whois", f"listening on TCP {port}")

    while g_running:
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        except Exception:
            break

        def client_handler(c, a):
            try:
                c.settimeout(3.0)
                data = c.recv(1024)
                query = data.decode("latin1", errors="replace").strip()
                log("whois", f"query '{query}' from {a}")
                resp = (
                    f"Domain Name: {query.upper()}\r\n"
                    "Registry Domain ID: 12345_DOMAIN_LAN-VRSN\r\n"
                    "Registrar: Tolunnet Registrar\r\n"
                    "Registrar IANA ID: 9999\r\n"
                    "Creation Date: 2026-09-23T00:00:00Z\r\n"
                    "Status: active\r\n"
                    "Name Server: NS1.TOLUNNET.LAN\r\n"
                    "Name Server: NS2.TOLUNNET.LAN\r\n"
                )
                c.sendall(resp.encode("latin1"))
            except Exception as e:
                log("whois", f"error: {e}")
            finally:
                c.close()

        t = threading.Thread(target=client_handler, args=(conn, addr), daemon=True)
        t.start()
    srv.close()

# ---------------------------------------------------------------------------
# 6b. DNS Server (z.ai step 9b item 4): answers A for tolunbench.test
# and PTR for 10.0.2.2 ( -> tolunnet-guest.test ) over UDP.
# ---------------------------------------------------------------------------
def run_dns(port):
    import struct as _struct
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_exclusive(sock, port)
    sock.settimeout(1.0)
    log("dns", f"listening on UDP {port}")

    A_NAME = "tolunbench.test"
    A_ADDR = "10.0.2.55"
    PTR_SUFFIX = ".in-addr.arpa"
    PTR_NAME = "tolunnet-guest.test"

    def enc_name(name):
        out = b""
        for lab in name.strip(".").split("."):
            out += bytes([len(lab)]) + lab.encode("latin1")
        return out + b"\x00"

    def read_name(pkt, off, depth=0):
        labels, jumped = [], False
        orig = off
        guard = 0
        while True:
            if off >= len(pkt) or depth > 8:
                return "", orig + 1
            l = pkt[off]
            if l == 0:
                off += 1
                break
            if l & 0xC0 == 0xC0:
                if not jumped:
                    orig = off + 2
                off = ((l & 0x3F) << 8) | pkt[off + 1]
                jumped = True
                depth += 1
                guard += 1
                if guard > 32:
                    return "", orig + 1
                continue
            lab = pkt[off + 1:off + 1 + l].decode("latin1", errors="replace")
            labels.append(lab)
            off += 1 + l
        name = ".".join(labels)
        return name, (orig if jumped else off)

    while g_running:
        try:
            data, addr = sock.recvfrom(1024)
        except socket.timeout:
            continue
        except Exception:
            break
        if len(data) < 12:
            continue
        qid = data[:2]
        try:
            qdcount = _struct.unpack("!H", data[4:6])[0]
        except Exception:
            continue
        if qdcount < 1:
            continue
        off = 12
        qname, qend = read_name(data, off)
        qtype, qclass = _struct.unpack("!HH", data[qend:qend + 4])
        qend += 4
        log("dns", f"query {qname} type={qtype} from {addr}")

        rname = qname
        rdata = None
        rtype = qtype
        if qtype == 1 and qname.lower() == A_NAME:
            rdata = bytes(int(x) for x in A_ADDR.split("."))
        elif qtype == 12 and qname.lower().endswith(PTR_SUFFIX):
            parts = qname[: -len(PTR_SUFFIX)].split(".")
            ip = ".".join(reversed(parts))
            if ip == "10.0.2.2":
                rtype = 12
                rdata = enc_name(PTR_NAME)
                rname = qname
            else:
                sock.sendto(qid + b"\x81\x83" + b"\x00\x00\x00\x00\x00\x00", addr)
                continue
        else:
            sock.sendto(qid + b"\x81\x83" + b"\x00\x00\x00\x00\x00\x00", addr)
            continue

        ancount = 1
        reply = qid + b"\x81\x80" + _struct.pack("!HHHH", 1, ancount, 0, 0)
        reply += enc_name(qname) + _struct.pack("!HH", qtype, 1)
        reply += enc_name(qname) + _struct.pack("!HHIH", rtype, 1, 60, len(rdata)) + rdata
        try:
            sock.sendto(reply, addr)
        except Exception as e:
            log("dns", f"send error: {e}")

# 7. HTTP Server (RFC 2616 / 7230)
# ---------------------------------------------------------------------------
def run_http(port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    bind_exclusive(srv, port)
    srv.listen(5)
    srv.settimeout(1.0)
    log("http", f"listening on TCP {port}")

    BODY = b"TOLUNNET_HTTP_OK\n"

    while g_running:
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        except Exception:
            break

        def client_handler(c, a):
            try:
                c.settimeout(3.0)
                req = c.recv(4096).decode("latin1", errors="replace")
                lines = req.split("\r\n")
                req_line = lines[0] if lines else ""
                log("http", f"request '{req_line}' from {a}")

                # Check Range: bytes=X-
                range_start = None
                for line in lines[1:]:
                    if line.lower().startswith("range:"):
                        val = line.split(":", 1)[1].strip()
                        if val.startswith("bytes="):
                            spec = val[6:].split("-")[0]
                            try:
                                range_start = int(spec)
                            except ValueError:
                                pass

                if range_start is not None and 0 <= range_start < len(BODY):
                    part = BODY[range_start:]
                    content_range = f"bytes {range_start}-{len(BODY)-1}/{len(BODY)}"
                    resp = (
                        f"HTTP/1.1 206 Partial Content\r\n"
                        f"Content-Type: text/plain\r\n"
                        f"Content-Length: {len(part)}\r\n"
                        f"Content-Range: {content_range}\r\n"
                        f"Connection: close\r\n"
                        f"\r\n"
                    ).encode("ascii") + part
                else:
                    resp = (
                        f"HTTP/1.1 200 OK\r\n"
                        f"Content-Type: text/plain\r\n"
                        f"Content-Length: {len(BODY)}\r\n"
                        f"Connection: close\r\n"
                        f"\r\n"
                    ).encode("ascii") + BODY

                c.sendall(resp)
            except Exception as e:
                log("http", f"error: {e}")
            finally:
                c.close()

        t = threading.Thread(target=client_handler, args=(conn, addr), daemon=True)
        t.start()
    srv.close()

# ---------------------------------------------------------------------------
# Port Free & Readiness Checks
# ---------------------------------------------------------------------------
def probe_readiness(ports, timeout=5.0):
    start = time.time()
    tcp_ports = [ports["TCP_DELAY_PORT"], ports["FTP_PORT"], ports["WHOIS_PORT"], ports["HTTP_PORT"], ports["ECHO_PORT"], ports["SILENT_PORT"]]
    while time.time() - start < timeout:
        all_ok = True
        for p in tcp_ports:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            try:
                s.connect(("127.0.0.1", p))
                s.close()
            except Exception:
                all_ok = False
                s.close()
                break
        if all_ok:
            return True
        time.sleep(0.1)
    return False

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def bind_exclusive(sock, port):
    """Bind (host, port). On Windows use SO_EXCLUSIVEADDRUSE instead of
    SO_REUSEADDR: REUSEADDR lets a second process silently share the
    port (an antivirus service held 15080 for hours and the bench
    connections went to it). Keeps SO_REUSEADDR on other platforms."""
    if sys.platform == "win32":
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    else:
        bind_exclusive(sock, port)


def port_holders(ports):
    """Yield (port, pid) for every netsvc port held by some process,
    found by parsing 'netstat -ano' output (list-argv subprocess only)."""
    wanted = {int(p) for p in ports.values()}
    out = subprocess.run(["netstat", "-ano"], capture_output=True, text=True)
    for line in (out.stdout or "").splitlines():
        parts = line.split()
        if len(parts) >= 5 or (len(parts) == 4 and parts[0].startswith("TCP")):
            pass
        f = line.split()
        if len(f) < 4:
            continue
        local = f[1] if f[0].upper().startswith("TCP") or f[0].upper().startswith("UDP") else f[0]
        state = f[3] if f[0].upper().startswith("TCP") else ""
        pid = f[-1]
        if ":" not in local:
            continue
        try:
            lport = int(local.rsplit(":", 1)[1])
            lpid = int(pid)
        except ValueError:
            continue
        if lport in wanted and (f[0].upper().startswith("UDP") or state in ("Bound", "Listen", "Listening")):
            yield lport, lpid


def holder_cmdline(pid):
    """Command line of a Windows PID via PowerShell, list-argv form."""
    filt = f"ProcessId = {pid}"
    try:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "(Get-CimInstance Win32_Process -Filter '" + filt + "').CommandLine"],
            capture_output=True, text=True)
    except Exception as e:
        return f"<powershell failed: {e}>"
    return (out.stdout or "").strip() or "<command line not accessible>"


def check_free(ports, holders=False):
    busy = []
    for name in ("TCP_DELAY_PORT", "FTP_PORT", "FTP_PASV_PORT", "WHOIS_PORT", "HTTP_PORT", "ECHO_PORT", "SILENT_PORT"):
        p = ports[name]
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            bind_exclusive(s, p)
        except Exception:
            busy.append(name)
            if holders:
                for lport, lpid in port_holders(ports):
                    if lport == p:
                        print(f"HOLDER port={p} pid={lpid} cmd={holder_cmdline(lpid)}")
        finally:
            s.close()
    for name in ("ECHO_PORT", "TFTP_PORT", "SNTP_PORT", "DNS_PORT"):
        p = ports[name]
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            bind_exclusive(s, p)
        except Exception:
            busy.append(name)
        finally:
            s.close()
    return busy


def kill_stale(ports):
    """Kill a port holder ONLY when its command line contains netsvc.py
    (our own stale service from a killed bench). Returns True when the
    ports are free afterwards."""
    for name in ("TCP_DELAY_PORT", "FTP_PORT", "FTP_PASV_PORT", "WHOIS_PORT", "HTTP_PORT", "ECHO_PORT", "SILENT_PORT"):
        p = ports[name]
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            bind_exclusive(s, p)
        except Exception:
            for lport, lpid in port_holders(ports):
                if lport != p:
                    continue
                cmd = holder_cmdline(lpid)
                if "netsvc.py" in cmd:
                    print(f"kill-stale: killing our netsvc pid={lpid}")
                    subprocess.run(["taskkill", "/F", "/PID", str(lpid)],
                                   capture_output=True)
            # UDP/TCP recheck below
        finally:
            s.close()
    busy = check_free(ports)
    return not busy


def main():
    global g_running, g_log_file

    parser = argparse.ArgumentParser(description="Hermetic mock services on slirp host")
    parser.add_argument("--ports", default=DEFAULT_PORTS_FILE, help="Path to ports file")
    parser.add_argument("--log", default=None, help="Path to log file")
    parser.add_argument("--check-free", action="store_true", help="Check that all ports are currently free")
    parser.add_argument("--kill-stale", action="store_true", help="Kill a busy-port holder ONLY if it is our own netsvc.py")
    parser.add_argument("--probe", action="store_true", help="Probe running services for readiness")
    args = parser.parse_args()

    ports = load_ports(args.ports)

    if args.check_free:
        busy = check_free(ports, holders=True)
        if not busy:
            print("OK: all netsvc ports are free.")
            return 0
        return 1

    if args.kill_stale:
        return 0 if kill_stale(ports) else 1

    if args.probe:
        if probe_readiness(ports, timeout=5.0):
            print("OK: netsvc readiness verified.")
            return 0
        else:
            sys.stderr.write("ERROR: netsvc failed readiness check within 5s\n")
            return 1

    if args.log:
        os.makedirs(os.path.dirname(os.path.abspath(args.log)), exist_ok=True)
        g_log_file = open(args.log, "a", encoding="utf-8")

    # 11w item 0: publish the Windows PID so ci/bench.sh can stop
    # exactly this process (never by image name) on any exit path.
    os.makedirs("build", exist_ok=True)
    with open(os.path.join("build", "netsvc.pid"), "w") as pf:
        pf.write(str(os.getpid()))

    def handle_sig(sig, frame):
        global g_running
        log("main", f"signal {sig} received, stopping all services")
        g_running = False

    signal.signal(signal.SIGINT, handle_sig)
    signal.signal(signal.SIGTERM, handle_sig)

    log("main", "starting hermetic slirp host services")
    threads = [
        threading.Thread(target=run_tcp_delay, args=(ports["TCP_DELAY_PORT"],), daemon=True),
        threading.Thread(target=run_udp_echo, args=(ports["ECHO_PORT"],), daemon=True),
        threading.Thread(target=run_tcp_echo, args=(ports["ECHO_PORT"],), daemon=True),
        threading.Thread(target=run_tcp_silent, args=(ports["SILENT_PORT"],), daemon=True),
        threading.Thread(target=run_sntp, args=(ports["SNTP_PORT"],), daemon=True),
        threading.Thread(target=run_dns, args=(ports["DNS_PORT"],), daemon=True),
        threading.Thread(target=run_tftp, args=(ports["TFTP_PORT"],), daemon=True),
        threading.Thread(target=run_ftp, args=(ports["FTP_PORT"], ports["FTP_PASV_PORT"]), daemon=True),
        threading.Thread(target=run_whois, args=(ports["WHOIS_PORT"],), daemon=True),
        threading.Thread(target=run_http, args=(ports["HTTP_PORT"],), daemon=True),
    ]

    for t in threads:
        t.start()

    log("main", "all 7 service threads started, ready for traffic")

    try:
        while g_running:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        g_running = False
        log("main", "shutting down")
        if g_log_file:
            g_log_file.close()

    return 0

if __name__ == "__main__":
    sys.exit(main())
