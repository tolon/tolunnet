# tolunnet — step GH (after W3 is clean-benched). Rules as before. Two commits.

## G — RECONFIG hot-reload (commit `feat(config): live-apply priority, log, database order, stats`)
`tn_apply_live_config` applies DNS/hostname/MTU/debug today. Add, each with a `test_config` round-trip case and one bench assertion via `tolunnet RECONFIG`:
`PRIORITY=` (`SetTaskPri` live), `LOG=` (close old / open new with `pr_WindowPtr=-1`, append), `LOGLEVEL=`, `DATABASE_ORDER=` (netdb reload flag for §D1), `SELECTORS=` (grow selector table), `STATS=YES|NO` (counter reset), `SYSLOG=host`.
Reply carries two bitmasks `applied | needs_restart` (versioned struct); `tolunnet RECONFIG` prints both lists. Interface keys stay restart-only. ISSUES TNET-108.

## H — SANA-II link events (commit `feat(sana2): S2_ONEVENT online/offline/error`)
`sana2_netif.c` has no `S2_ONEVENT`. Implement per SANA-II Rev 7: dedicated `IOSana2Req event_io` on the RX port, `io_Command=S2_ONEVENT`, `ios2_WireError` = mask from `S2EVENTS=` key (default `ONLINE|OFFLINE|ERROR`), `SendIO`; on reply read triggered bits from `ios2_WireError`, handle, re-arm at once.
OFFLINE → `netif_set_link_down`; ONLINE → `netif_set_link_up` + (DHCP ? `dhcp_renew` or `dhcp_start` if no lease : `netif_set_up`); ERROR → counter + BASIC log. `LWIP_NETIF_LINK_CALLBACK 1`; `GETSTATUS` gains `link_up`; Prefs/Setup status line shows "link down".
Shutdown: `AbortIO(event_io)`+`WaitIO` **before** aborting CMD_READs and `CloseDevice`.
Test: `tests/amiga/S2Toggle` (opens same unit, issues `S2_OFFLINE`/`S2_ONLINE`) → `tc_link_events` sees `link_up` flip and a DHCP renew line in `daemon.log`; note per driver (uaenet vs a2065) whether ONEVENT is supported. ISSUES TNET-109. Clean bench both configs.

Report: commits, TAP lines, bench dir.
