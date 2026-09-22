TNET-115 fix proof rig runs (chip-only pistorm-68000, DIAG build with
the tcp_output cycle guards + per-chunk flush):

run 1: COMPLETE conformance 36 ok
run 2: TIMEOUT 450 s — no bench-done, no FreezeWatch dump
       (evidence overwritten by run 3 staging; consistent with the
       documented a1200-variant post-wizard wedge class, NOT the #32
       freeze: no FreezeWatch dump means bsdsocktest.log was not
       stalled mid-suite < 5000 bytes when its 45 s timer ran)
run 3: COMPLETE conformance 36 ok
run 4: COMPLETE conformance 36 ok

Before the fix this exact rig froze at bsdsocktest #32 (41 log lines)
2 of 2 runs (occurrences 7-8, STOP-REPORT 2026-09-15). After the fix:
zero #32-class freezes in 4 runs. The one timeout is recorded against
the separate post-wizard wedge variant.
