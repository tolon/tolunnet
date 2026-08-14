# tolunnet — System Architecture & Design Specification

The definitive architecture and design document for the `tolunnet` AmigaOS TCP/IP stack.

---

## 1. System Overview & Component Layout

```
                  +-----------------------------------+
                  |        Client Applications        |
                  |  (IBrowse, AmiSSL, Ping, Telnet)  |
                  +-----------------+-----------------+
                                    |
                            OpenLibrary("bsdsocket.library", 4)
                                    |
                  +-----------------v-----------------+
                  |         bsdsocket.library         |
                  |  (Per-task SocketBase, LVO jump   |
                  |   table, Zero-Allocation IPC)     |
                  +-----------------+-----------------+
                                    |
                          Exec PutMsg / WaitPort
                                    |
                  +-----------------v-----------------+
                  |       tolunnet Network Task       |
                  |  - lwIP 2.2.0 Core (NO_SYS=1)     |
                  |  - timer.device 100ms Ticker      |
                  |  - IPC Dispatch & Descriptors     |
                  +-----------------+-----------------+
                                    |
                            lwIP Netif Bridge
                                    |
                  +-----------------v-----------------+
                  |       SANA-II Netif Adapter       |
                  |  - Isolated TX & RX MsgPorts      |
                  |  - CopyMem fast data pump         |
                  |  - L2 Filter (Whitelisting/Anti-  |
                  |    Spoofing)                      |
                  +-----------------+-----------------+
                                    |
                            SANA-II Standard
                                    |
                  +-----------------v-----------------+
                  |   SANA-II Device (ethernet.device)|
                  |   Amiga Hardware / WinUAE A2065   |
                  +-----------------------------------+
```

---

## 2. Threading & Execution Model

- **Single-Task lwIP Ownership (`NO_SYS=1`):**  
  lwIP runs strictly in the `tolunnet` network task context. This eliminates the need for heavyweight OS semaphores, mutexes, or complex task context switching inside the protocol stack.
- **Asynchronous SANA-II I/O Isolation:**  
  To prevent Exec message port signal corruption:
  - `tx_port`: Dedicated port for synchronous `DoIO` (`CMD_WRITE`, `S2_BROADCAST`).
  - `rx_port`: Dedicated port for asynchronous `SendIO` receive pump (`CMD_READ`).
- **Zero-Allocation Client IPC:**  
  Client tasks calling `socket()`, `send()`, `recv()`, `CloseSocket()` communicate with `tolunnet` via Exec `PutMsg()` and `WaitPort()`. Each client `SocketBase` possesses a preallocated `struct TnIpcMsg`, eliminating memory allocation overhead on every network call.
- **Caller-Side Blocking & Signal Responsiveness:**  
  When an application performs a blocking call (e.g. `WaitSelect`), the application task sleeps in 20ms slices with signal monitoring (`sig_mask`), keeping the CPU idle while remaining immediately responsive to `Ctrl-C` breaks.

---

## 3. Network Security & Stack Hardening

- **L2 Link Layer Validation:**
  - Frame length bounds checking (`flen <= MTU`).
  - EtherType whitelisting (`0x0800` IPv4, `0x0806` ARP).
  - Source MAC anti-spoofing (rejection of multicast source bit, all-00, all-FF, or loopback echoes).
- **L3 / L4 Stack Hardening:**
  - Smurf/Broadcast ping suppression (`LWIP_BROADCAST_PING = 0`, `LWIP_MULTICAST_PING = 0`).
  - Mandatory checksum validation on IP, UDP, TCP, ICMP headers.
  - IP fragment reassembly timeout and buffer limits (Teardrop attack defense).
  - TCP SYN backlog queuing and ISN randomization via `tn_rand()` PRNG seeded from hardware timers before stack init.
- **IPC Boundary Hardening:**
  - Strict validation of client pointers and buffer lengths.
  - Per-task socket descriptor isolation preventing descriptor hijacking.
  - Automatic descriptor cleanup in `LIB_CLOSE` upon client task termination.

---

## 4. Configuration Architecture

- **Single Source of Truth (`DEVS:tolunnet.config`):**  
  Standard `KEY=VALUE` text file storing `DEVICE`, `UNIT`, `DHCP`, `IP`, `NETMASK`, `GATEWAY`, and `DNS`.
- **Persistent Mirroring:**  
  `TolunnetPrefs` writes both `DEVS:tolunnet.config` and `ENVARC:tolunnet.prefs`, ensuring full compatibility with ART pre-seeded environments and Workbench user preferences.

---

## 5. Memory & Performance

- **Resident Budget:** ≤ 250 KB total RAM (task + library + lwIP heap).
- **Fast Buffer Transfers:** Exec `CopyMem()` used throughout link output and frame ingress.
- **Timer Granularity:** 100 ms periodic `timer.device` ticks driving `sys_check_timeouts()`.
- **Byte Order:** Motorola 68000/020 Big-Endian native. Identity conversions for network byte order.
