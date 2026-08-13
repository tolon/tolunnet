# compat.md — application compatibility

> Master prompt §M6: "no app marked PASS without a pasted run." This file is
> the honest compatibility table. It is empty until M6.

The table below is filled only after an application has run against tolunet and
its output is pasted (under STATUS.md or a linked log). Until then, every cell
stays "untested".

| Application | Version | Result | tolunet build | Notes |
|-------------|---------|--------|---------------|-------|
| AmiSSL      | —       | untested | —           | first app in the M6 gauntlet |
| amiget      | —       | untested | —           | Aminet fetch |
| Amelinium   | —       | untested | —           | browser |
| smbfs       | —       | untested | —           | SMB mount |

## What "PASS" requires

- A real run against the current tolunet build, with the build's `$VER` string.
- Output pasted (download log, page HTML, mount listing).
- Any regression that was fixed carries a note linking the probe under
  `docs/probes/`.

Anything else is "untested" or "FAIL — <reason>". Never "should work".
