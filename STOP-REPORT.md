# Tolunnet STOP-REPORT: W4 part-2 bench — recurring 68000-leg system freeze

**Timestamp:** 2026-09-09 11:05
**Commits this session (all committed, tree clean):** `4bdae1f` (W4 P1 chrome),
`6830262` (proof), `f803e48` (W4 P2 pages), `06b0aae` (TNET-112 requester guard)
**Stop rule fired:** bench red twice in a row (`f803e48`, `06b0aae`) — both 68000 freezes.

## 1. What was delivered (all a1200-proven)

- **W4 P1 (`4bdae1f`):** v2 window chrome — step rail, recessed pane + bold
  titles, status line + 1 s timer tick, Cancel/Back/Next bar (Next→Finish),
  FONT=name/size CLI font engine, NTSC compact, n/5 screen title.
  Bench `20260909-054813-4bdae1f`: **ALL-GREEN** (a1200 + 68000, 34/34 ×2 cycles).
- **W4 P2 (`f803e48`):** page internals — LISTVIEWs (stacks, adapters with
  '>' marker + unusable flag, networks with #/.- signal bars, checks),
  editable SSID, associate-on-Next (≤30 s, actionable failure text),
  address validation (tn_inet_addr_parse_ex + EasyRequest), DNS2 + MTU
  fields flowing into the config writer.
- **TNET-112 (`06b0aae`):** requester-proof `assign_exists()` DosList guard on
  every `AmiTCP:`/`Miami:` access in stack_detect (import reads, install
  detection, stopnet Execute). Root cause: the owner SAW the Amiga ask for
  the "AmiTCP" disk — an unassigned-volume access raises a DOS insert-volume
  requester that blocks the whole machine; it froze the 68000 leg at the
  wizard test in `f803e48`.

## 2. The two red benches (verbatim)

```
f803e48 / 68000 cycle 1: ok 31 rows, then TIMEOUT 600 s — frozen during
        tc_wizard_wired (AmiTCP requester, owner-witnessed → fixed by 06b0aae)
06b0aae / 68000 cycle 1: core: 9 ok / 0 not ok, then TIMEOUT 600 s:
        frozen right after tc_listen_accept_loopback, mid tc_connect_refused
        — daemon task log ends at that test's socket() creation, no client
        output despite the 5 s IPC watchdog ⇒ system-level freeze.
a1200 (same three benches): 34/34 × 2 cycles, every time — including the
        wizard test with the new LISTVIEW code.
```

## 3. Analysis

- The `06b0aae` freeze signature (frozen mid `tc_connect_refused`, ~ok 9,
  daemon silent, client starved) is **identical to last night's `c7c3c6d`
  68000 freeze**, which predates all W4 code: a nondeterministic,
  68000-profile-only system freeze at SANA-II/slirp round-trip points.
- The AmiTCP requester class is real and fixed (user-witnessed, guard cannot
  raise a requester by construction — DosList scan only); the residual freeze
  is the older, unrelated instability.
- a1200 is rock solid across five consecutive cycles tonight, so the W4 P1/P2
  code and TNET-112 are exercised and green where the host cooperates.

## 4. Next steps

1. Owner observation: was a volume requester visible in the `06b0aae` 68000
   run too? (Window text tells which volume — any name other than the log
   volume means another unguarded path to grep.)
2. Re-run `ci/bench.sh` on an idle host (tonight's box carried a long
   session + extra processes).
3. If the 68000 freeze persists on an idle host: WinUAE debugger armed
   (`il 8`), reproduce in the 68000 config, record PC + fault address, and
   open a separate TNET row — do not chase it inside W4.
4. W4 part 3 punch list: Advanced… requester (PRIORITY/LOG/DATABASE_ORDER/
   NTP/IPv6 placeholder), ASL "Save log…", clipboard "Copy report", saved
   networks + WIFI_PRIORITY=, ToolType FONT=, tc_wizard_ntsc, PAL+NTSC
   screenshots, docs/iron-test-wizard.md, ISSUES TNET-110/112 rows.

Evidence dirs: `docs/bench-logs/20260909-102321-f803e48/` (deleted after
lesson recorded), `docs/bench-logs/20260909-105717-06b0aae/` (kept — cited
above). Green proofs: `20260909-054813-4bdae1f/` (both profiles).
