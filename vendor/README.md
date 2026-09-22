# vendor/

Vendored third-party code. Licence notices: `THIRD_PARTY_LICENSES.md`.

Goal: keep vendored trees as close to upstream as possible so an upgrade stays
easy. `vendor/patches/` is reserved for patch files but is currently empty —
the lwIP changes below are applied in place.

## lwIP — `vendor/lwip/`

| Field    | Value                                                                 |
|----------|-----------------------------------------------------------------------|
| Project  | lwIP — A Lightweight TCP/IP stack                                     |
| Version  | 2.2.0                                                                 |
| Upstream | <https://savannah.nongnu.org/projects/lwip/>                          |
| Archive  | `lwip-2.2.0.zip`                                                      |
| URL      | <https://download.savannah.gnu.org/releases/lwip/lwip-2.2.0.zip>      |
| sha256   | `e12c769be5a1da9a1edf1b8f38d645c6c87d52a26a636172cea4b8c63ec04994`   |
| Licence  | BSD (3-clause) — see `vendor/lwip/COPYING` and `THIRD_PARTY_LICENSES.md` |

The tree under `vendor/lwip/` is the release archive plus these in-place
changes (check with `git diff --ignore-cr-at-eol 267b814 HEAD -- vendor/lwip`):

| File | Change | Commit |
|---|---|---|
| `src/core/tcp_out.c` | livelock guard + walk cap for a circular unsent queue in `tcp_output` | `740dfb3` |
| `src/core/dns.c`, `src/include/lwip/dns.h` | `dns_set_dest_port()` resolver port override (bench) | `1c3b182` |

`dns.c`/`dns.h` also had their line endings rewritten, so the tree is not
byte-identical to the zip. When re-vendoring, re-apply these changes (ideally
as patch files under `vendor/patches/`).

### Re-vendoring

```
curl -fSL -o lwip-2.2.0.zip https://download.savannah.gnu.org/releases/lwip/lwip-2.2.0.zip
sha256sum lwip-2.2.0.zip   # must match the value above
unzip -q lwip-2.2.0.zip -d /tmp/lwip-stage
rm -rf vendor/lwip && mv /tmp/lwip-stage/lwip-2.2.0 vendor/lwip
rm lwip-2.2.0.zip
```

### Build scope

For a `NO_SYS=1` raw/callback-API build, the Makefile compiles the core sources
under `src/core/` (plus `src/core/ipv4/`, including `dhcp.c`, `acd.c`,
`autoip.c` and `igmp.c`), `src/netif/ethernet.c`, the mDNS responder
`src/apps/mdns/{mdns,mdns_domain,mdns_out}.c`, and the headers under
`src/include/`. AutoIP and mDNS are enabled in `lwipopts/lwipopts.h`
(`LWIP_AUTOIP`, `LWIP_MDNS_RESPONDER`). The socket/netif layers under `src/api/` are
excluded when `NO_SYS=1` / `LWIP_SOCKET=0` / `LWIP_NETCONN=0` (see
`lwipopts/lwipopts.h`). No `contrib/ports/*` is used — tolunnet provides its own
`sys_arch`-equivalent glue via the network task.

## bsdsocktest — `vendor/bsdsocktest/`

| Field    | Value |
|----------|-------|
| Project  | bsdsocktest — bsdsocket.library conformance suite (tbdye) |
| Upstream | <https://github.com/tbdye/bsdsocktest> |
| Commit   | `cb08680843bc9cff93d57ca4760e59ab71e93b57` (2026-02-16) |
| Licence  | GPL-3.0 — `vendor/bsdsocktest/LICENSE` |

Only `src/`, `LICENSE`, `README.md` and `bsdsocktest.readme` were imported
(upstream `Makefile`, `docs/` and `host/` were not), so links in the upstream
README to those are dead here. Unmodified since import (`989eebf`). It is built
by the top-level Makefile as `build/bsdsocktest` with the project flags
(`-m68000`) and used by the bench only; it is not shipped.

## Other third-party material outside `vendor/`

- `include/netinclude/**`, `include/libraries/bsdsocket.h`, `sfd/*.sfd` —
  Roadshow SDK 1.8 (Olaf Barthel), with BSD-derived headers.
- `include/devices/sana2.h`, `include/devices/sana2specialstats.h` — AmigaOS
  NDK 3.9 (Release 50.1).
