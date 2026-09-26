# STOP-REPORT — z.ai step 9a item 1 (2026-09-26)

## Verdict

Step 9a item 1 (`feat(cmds): status tools read live daemon state`) is code-complete and
committed, but its bench is blocked by a deterministic regression of the pre-existing
conformance row `tc_cmd_ifctl` ("interface not up", row 60) that appeared the moment the
six new status binaries were staged, survives a full revert of every item-1 source file,
and has no identifiable mechanism after six controlled bench experiments. Items 2 and 3
were NOT STARTED. Per the order's rule ("Otherwise write STOP-REPORT.md ... and stop"),
the work stops here for owner review.

## What landed (HEAD at stop time: `7477c1e` + follow-ups, tree clean)

- `0a21a5b` feat(cmds): status tools read live daemon state (tn_cmd_snapshot in cmdlib,
  GetNetStatus/TolunnetStatus/ShowNetStatus live tables, Makefile links, bench staging of
  GetNetStatus/ShowNetStatus/route/Online/Offline/NetShutdown, 3 new rows registered)
- `92c6773` bench: staged-HDF space reclaim v1 + netsvc silent-server log typo fix
- `63bb972` bench: reclaim v2 (empty the Storage drawer too)
- `2a90c28`, `dfe6527`, `11c6724`, `7477c1e` diag: tc_cmd_ifctl LIST dump + fixes
- `94093d6`, `9500a53`, `ede5276` refactor/build: move tn_cmd_snapshot out of cmdlib.o
  into snapshot.c; status tools link it; explicit ipc_client.o on IPC-using links

## The regression

Every bench since the +6 staging landed fails identically on BOTH profiles:

```
not ok 60 - tc_cmd_ifctl # interface not up
# tc_cmd_ifctl: LIST result=1 rows:
#   [0] name=eth0 in_use=1 up=0 dhcp=0 link=0 addr=a00020f mask=ffffff00 gw=a000202
not ok 60 - tc_cmd_ifctl # interface not up
```

(the %lx widths were fixed mid-diagnosis; the final dump shows addr=10.0.2.15,
mask=255.255.255.0, gw=10.0.2.2 — the full DHCP configuration — with all three flag
bytes zero). 18 further rows fail with `connect failed errno=61` against 10.0.2.2 from
row 65 on: the lwIP netif is DOWN for routed traffic while the DHCP lease itself
succeeded ("DHCP lease obtained!" in tolunnet-task.log). Loopback rows keep passing.

## Experiments (all benches 20260926)

1. `20260926-070302` item-1 tree, no space reclaim: staging FATAL "No Free Blocks" at
   traceroute — the pristine HDF has only ~687 KB of scattered free space; the +6
   binaries do not fit without reclaiming.
2. `20260926-070822` (92c6773, C: junk reclaim): row 60 fails, both profiles.
3. `e8cc48e` bisect (item-1 SOURCES fully reverted to 3d10350; junk reclaim + staging
   kept): row 60 STILL fails, both profiles — item-1 client code exonerated.
4. `20260926-072520` space check without reclaim: staging FATAL again.
5. `20260926-075052` (snapshot moved to its own TU; only status tools relink):
   row 60 still fails — relink-layout theory dead.
6. `20260926-080852` (+Storage emptying, ~200 KB free): row 60 still fails.
7. Green control: exact 3d10350 tree in a worktree, built and benched today:
   ALL-GREEN 87 ok / 0 not ok — the environment (WinUAE, netsvc host, pristine HDF)
   is healthy; the trigger is inside this session's delta.
8. Pair A (green + netsvc fix + staging + Storage-only reclaim): staging FATAL
   (insufficient contiguous space without the C: junk deletions) — the +6 staging and
   the C: deletions are confounded by the space requirement and could not be separated.

## Ruled out, by name

- Item-1 client code: experiment 3 (full source revert) still fails.
- Relink layout of command binaries: experiment 5 (snapshot decoupled, cmdlib.o back to
  the 8c object) still fails.
- Daemon source: zero diffs since 3d10350; the only netif_set_down call sites are the
  three boot-failure exits (daemon would have exited), shutdown (row 91, after), and
  IFCTL DOWN (ipc_ifctl.c:110) — and no client sends IFCTL DOWN before row 60
  (NetShutdown.c:33 / Offline.c:28 are the only senders; neither runs before row 60).
- netsvc host services: DHCP is answered by WinUAE slirp, not netsvc; the lease is
  obtained in every failing run.
- Environment: experiment 7 (green control today) is ALL-GREEN.
- Daemon-side state vs IPC copy: the ifctl LIST handler writes the client's buffer
  directly; the dump above shows the daemon genuinely reporting the cleared flags with
  an intact address — the state lives in the daemon's TnNetif, not in transport.

## Unresolved

Something about staging six additional binaries into the ephemeral HDF (plus the
reclaim deletions that make that possible) leaves the daemon's primary netif with
NETIF_FLAG_UP, is_dhcp and link_up cleared while the DHCP address is fully applied.
No code path in the daemon (unchanged since the green control) can do that, and the
suite's row-60 code reads the daemon's reply unmodified. Next step for the owner:
reproduce with the 68000 HDF from `docs/bench-logs/20260926-080852-*/68000` era
staging, or run the cycle-1 daemon under WinUAE interactively and dump
`d->ifs[0].lwip_if.flags` at boot and at row 60.

## Last 5 lines of each log (20260926-080852, verbatim)

68000/conformance.log:
```
TolunnetControl: STOP...
ok 91 - tc_cmd_stop_start
tolunnet: daemon stopped
1..91
# bench: asking daemon to stop (restart-cycle proof)
```

a1200/conformance2.log:
```
TolunnetControl: STOP...
ok 91 - tc_cmd_stop_start
tolunnet: daemon stopped
1..91
# bench: asking daemon to stop (restart-cycle proof)
```

a1200/tolunnet-task.log:
```
tolunnet: socket(domain=2, type=1, proto=0) -> fd 0 (slot 0)
tolunnet: CloseSocket(fd=0, slot=0) -> ok
tolunnet: client task 0x235548 closing bsdsocket.library
tolunnet: client task 0x235548 opened bsdsocket.library
tolunnet: socket(domain=2, type=1, proto=0) -> fd 0 (slot 0)
```
