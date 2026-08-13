# tolunet

An open-source TCP/IP stack for classic AmigaOS (68k), built on a pinned,
unmodified copy of [lwIP](https://savannah.nongnu.org/projects/lwip/) running
as a single Amiga task (`NO_SYS=1`), exposed to applications through a
`bsdsocket.library`-compatible runtime library.

## Why

Classic AmigaOS has no modern, actively developed, truly open-source TCP/IP
stack. Roadshow is good but commercial and closed. Miami is dead. AmiTCP's only
open release is 3.0b from 1994. tolunet fills that gap: GPL-3.0-or-later,
modern, documented, tested, released on Aminet and GitHub for everyone.

**Author of record:** tolon
**Licence:** GPL-3.0-or-later — see [LICENSE](LICENSE).
**lwIP licence:** BSD — see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

> Applications talk to tolunet through `OpenLibrary()` on a runtime library —
> the OS library boundary. Using tolunet from a closed application does **not**
> GPL that application.

## Status

This project is in milestone **M0** (scaffold). Nothing is proven yet. See
[STATUS.md](STATUS.md) for the current snapshot — what is proven, what is built
but unproven, and what is missing. The project follows a strict rule: a
milestone is done only when its exit test has run and its output is pasted into
STATUS.md. "Compiles" is not a test.

## Target matrix

| Tier | CPU/OS           | Delivers                              | When        |
|------|------------------|---------------------------------------|-------------|
| 1    | 68020+ · OS 3.1+ | full stack + GUI                      | v1 (M0–M9)  |
| 2    | 68000/010 · OS 2.04+ | `tolunet000`: reduced pools, no stats | M10         |
| 3    | 68000 · OS 1.3   | experimental; static IP first         | M11 (optional) |

Tier 1 is never held back by 2/3. README claims support only per-tier, with
proof in STATUS.md.

## Not in v1

PPP/SLIP, firewall, IPv6, TLS (that is AmiSSL's job), more than one
simultaneous interface (the design allows it; v1 ships one), and
Roadshow-private tags (under review — see QUESTIONS.md).

## Building

Cross-compiled with the [amiga-gcc](https://github.com/bebbo/amiga-gcc)
toolchain (now hosted at
[AmigaPorts/m68k-amigaos-gcc](https://github.com/AmigaPorts/m68k-amigaos-gcc))
on a Linux host, binaries tested in [WinUAE](https://www.winuae.net/). Toolchain
location is a QUESTIONS.md item (see there).

```
make all        # build everything
make clean      # remove build artefacts
```

See [docs/bench.md](docs/bench.md) for the WinUAE bench configuration used to
run exit tests.

## Project layout

See [TOLUNET-master-prompt.md](TOLUNET-master-prompt.md) §2 for the binding
repository layout. Working tracking documents:

- [STATUS.md](STATUS.md) — proven / built-unproven / missing
- [ISSUES.md](ISSUES.md) — defects (TNET-xxx)
- [QUESTIONS.md](QUESTIONS.md) — open questions for the human
