# bench.md — WinUAE test bench

> Master prompt §8 / §M0: "Every exit test reproducible from bench.md alone."
> This file is the canonical bench configuration. A human following only this
> document must be able to reproduce any milestone's exit test.

## Host

- **Emulator:** WinUAE — <https://www.winuae.net/>
- **Emulated CPU:** 68040 (fast, representative of Tier-1 territory)
- **Emulated OS:** clean AmigaOS 3.2 install (licensed Kickstart/Workbench —
  the human's own copies; tolunet never redistributes OS media).
- **RAM:** 8 MB Zorro III (plenty for the bench; the real target is 4 MB, see
  §9 — memory budget is asserted separately, not by shrinking the bench).

## Network

- **Emulated NIC:** A2065 (or `uaenet.device`), wired to the host's `slirp`
  NAT. slirp gives the Amiga a DHCP lease and lets the host ping it.
- **Host side:** the host PC runs on the same machine; `ping <amiga-ip>` from
  the host is part of the M2 exit test. Where wire behaviour is disputed,
  capture with WinUAE's slirp logging or a host pcap (§8: "wire disputes
  settled by pcap capture, not prints").

## WORK: — the host-mounted log directory

A host directory is mounted into the emulated Amiga as **`WORK:`** so that
test outputs survive a reboot and are trivially reachable from the host.

### How to set up WORK:

1. Create a host folder, e.g. `E:\amiga\Amigatolon\work`.
2. In WinUAE → Hard drives → "Add directory" / "Add filesystem":
   - Device name / volume label: `WORK`
   - Path: the host folder above.
   - Read-write.
3. Anything the Amiga writes to `WORK:` appears in that host folder instantly.

All exit-test artefacts (hello-task log, DHCP lease dump, TolunetGet output,
probe captures) are written under `WORK:` and referenced from STATUS.md.

## Reproducing the M0 exit test

```
1. Build: make all                       # produces build/tolunet-hello
2. Copy build/tolunet-hello into the WinUAE Amiga (under WORK: or SYS:)
3. From the Amiga shell:  tolunet-hello
4. Expected on screen:
     tolunet M0: hello-task alive
     tolunet M0: wrote WORK:tolunet-hello.log
5. Expected file WORK:tolunet-hello.log contains:
     tolunet M0: hello-task alive
6. Paste the on-screen output + the file contents into STATUS.md under "Proven".
```

If step 4 instead prints "could not open WORK:tolunet-hello.log", the WORK:
volume is not mounted — fix the bench, not the program.

## Reproducing later exit tests

Each milestone's exit test is written into its own section here as it nears.
For now:

- M1 (SANA-II raw): broadcast + incoming-frame logger — to be detailed.
- M2 (IP alive): DHCP lease dump + host `ping` — to be detailed.

## Oracle: Roadshow demo

For any disputed bsdsocket semantic (M4+), the oracle is the Roadshow demo
(<http://roadshow.apc-tcp.de/index-en.php>): the same probe program run under
Roadshow demo and under tolunet must match. Outputs archived under
`docs/probes/NNN-name/`.
