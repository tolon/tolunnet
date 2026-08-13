# vendor/

Vendored third-party code. Master prompt §2: vendored sources are **unmodified**.
Patches, if ever unavoidable, live in `vendor/patches/` and are applied by the
build (see `Makefile`), so the vendored tree here stays pristine and a future
upstream upgrade stays possible.

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

The tree under `vendor/lwip/` is the exact contents of the release archive,
extracted as-is. Do not edit files under `vendor/lwip/`. If a change to lwIP is
unavoidable, add a patch under `vendor/patches/` that the build applies to a
clean tree.

### Re-vendoring

```
curl -fSL -o lwip-2.2.0.zip https://download.savannah.gnu.org/releases/lwip/lwip-2.2.0.zip
sha256sum lwip-2.2.0.zip   # must match the value above
unzip -q lwip-2.2.0.zip -d /tmp/lwip-stage
rm -rf vendor/lwip && mv /tmp/lwip-stage/lwip-2.2.0 vendor/lwip
rm lwip-2.2.0.zip
```

### Build scope (for reference; wiring happens in M2)

For a `NO_SYS=1` raw/callback-API build, the build compiles the core sources
under `src/core/` (plus `src/core/ipv4/`), `src/netif/ethernet.c`, and the
headers under `src/include/`. The socket/netif layers under `src/api/` are
excluded when `NO_SYS=1` / `LWIP_SOCKET=0` / `LWIP_NETCONN=0` (see
`lwipopts/lwipopts.h`). No `contrib/ports/*` is used — tolunet provides its own
`sys_arch`-equivalent glue via the network task (master prompt §1.1).
