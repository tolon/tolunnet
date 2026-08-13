# STATUS.md

> The single source of truth. Proven / Built-unproven / Missing. Updated every
> session. When docs and code disagree, fix the doc. (Master prompt §4.4)
>
> "Nothing is verified until it has been seen running." A milestone is done only
> when its exit test has run and its output is pasted below.

## Snapshot

| Field         | Value                                          |
|---------------|------------------------------------------------|
| Date          | 2026-08-13                                     |
| Milestone     | M0 (scaffold) — M1 source added, unproven      |
| lwIP          | 2.2.0 (vendored, unmodified) — see vendor/     |
| Toolchain     | amiga-gcc (AmigaPorts build) — version TBD on first CI green |
| CI            | workflow added; first green pending            |
| Last proven    | (nothing yet)                                  |
| Next exit test | M0: hello task runs in WinUAE, writes WORK:    |

## Proven

_(nothing yet — M0 exit test has not run)_

## Built, unproven

These are written and (where applicable) compile, but have **not** been seen
running in WinUAE. Treat as drafts.

- **M0 scaffold** — repo, LICENSE, README, tracking docs, lwIP vendored,
  `include/tolunet/protocol.h`, `lwipopts/lwipopts.h`, Makefile, hello task
  (`src/task/main.c`), two-job CI workflow, bench.md.
  - Proven-by: M0 exit test (human runs hello task in WinUAE, pastes output).
- **M1 SANA-II raw** (source only, not wired into a runnable build yet):
  - `src/sana2/sana2_netif.c` — device open, `S2_DEVICEQUERY`, copyfuncs,
    `S2_ONLINE`, ≥4 outstanding `CMD_READ`, `S2_OFFLINE`+close.
  - `src/sana2/buffers.c` — copyfuncs + >4 KB task-owned copy ring skeleton.
  - `src/cmds/TolunetStatus.c` — shell tool skeleton + raw frame logger hook.
  - Proven-by: M1 exit test (broadcast sent, incoming frames logged with types).

## Missing

- lwIP init wired into the network task (M2).
- bsdsocket.library skeleton (M3).
- Roadshow probe harness (M4+).
- Real-iron bench (M7).
- `TolunetStatus MEM` resident-RAM measurement (no value yet; budget ≤ 250 KB).

## Budgets

| Budget             | Target       | Measured | Notes                          |
|--------------------|--------------|----------|--------------------------------|
| Resident RAM       | ≤ 250 KB     | —        | `TolunetStatus MEM` (M-later)  |
| Task stack         | ≥ 16 KB      | —        | explicit, lwIP paths need room |
| Timer granularity  | 100 ms       | —        | timer.device tick              |

## M0 exit test (target)

```
1. Human: stage bench per docs/bench.md (WinUAE, OS 3.2, 68040, host dir as WORK:)
2. Human: run the hello-task artefact in WinUAE
3. Human: confirm a line was written to WORK:
4. Human: paste the line + run context here, under "Proven"
```

## Session log

- 2026-08-13 — M0 scaffold + M1 source created. No exit test run yet. Awaiting
  human for M0 WinUAE run. Toolchain location (bebbo → AmigaPorts) logged in
  QUESTIONS.md.
