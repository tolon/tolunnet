#!/usr/bin/env python3
"""test_netsvc_bind.py — prove that a foreign listener is DETECTED.

11x item 0: check_ports_free probes with SO_REUSEADDR and claims a
foreign plain listener still fails the bind; that claim was unproven.
This test opens a plain listener (no reuse options) on a free high
port and requires BOTH probe styles to report it busy:
  - the check_ports_free-style probe (SO_REUSEADDR bind)
  - bind_exclusive (SO_EXCLUSIVEADDRUSE bind)
It then closes the listener and requires both to report the port
free. Exit 0 = all assertions held.

Run from the repo root: python ci/test_netsvc_bind.py
(wired into the python-checks part of make test-host).
"""
import socket
import sys
import time

sys.path.insert(0, "ci")
import netsvc  # noqa: E402


def probe_reuse(port):
    """check_ports_free-style: REUSEADDR bind must fail when held."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("0.0.0.0", port))
        return False  # bound: port was free
    except OSError:
        return True   # bind failed: port busy
    finally:
        s.close()


def probe_exclusive(port):
    """bind_exclusive-style: EXCLUSIVEADDRUSE bind must fail when held."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        netsvc.bind_exclusive(s, port)
        return False
    except OSError:
        return True
    finally:
        s.close()


def free_high_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    failures = 0
    port = free_high_port()

    holder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # plain listener: NO reuse, NO exclusive - a foreign process
    holder.bind(("0.0.0.0", port))
    holder.listen(1)

    if probe_reuse(port):
        print(f"ok 1 - reuse-probe detects foreign listener on {port}")
    else:
        print(f"not ok 1 - reuse-probe MISSED the foreign listener on {port}")
        failures += 1

    if probe_exclusive(port):
        print(f"ok 2 - exclusive-probe detects foreign listener on {port}")
    else:
        print(f"not ok 2 - exclusive-probe MISSED the foreign listener on {port}")
        failures += 1

    holder.close()
    time.sleep(0.2)  # let the kernel release the listening socket

    if not probe_reuse(port):
        print(f"ok 3 - reuse-probe sees {port} free after close")
    else:
        print(f"not ok 3 - reuse-probe still sees {port} busy after close")
        failures += 1

    if not probe_exclusive(port):
        print(f"ok 4 - exclusive-probe sees {port} free after close")
    else:
        print(f"not ok 4 - exclusive-probe still sees {port} busy after close")
        failures += 1

    print("1..4")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
