# STOP-REPORT — 11ac item 2 (real-Installer pretend row): 3 counted reds

## What was built
`tc_installer_pretend` (commit 962ac31, invocation fixed in 98feb99):
probes the real `C:Installer ?` template, then runs
`C:Installer S:Install_Tolunnet.script NOLOG NOPRINT DEFUSER NOVICE
>Work:installer-pretend.log` and asserts rc 0, no error/Undefined/unknown
text, and that the log names the first and last copyfiles sources.
`ci/bench.sh` now stages the shipped `C:Installer` (it never did -
commit 4e8e16d).

## The REAL template (bench log, installer 44.10 (1.10.99))
```
USAGE: Installer [SCRIPT] filename <[APPNAME] name> <[MINUSER] level>
       <[DEFUSER] default> <[LOGFILE] logname> <[LANGUAGE] language> <NOPRETEND> <NOLOG> <NOPRINT>
```
There is NO `PRETEND` switch. Per the tool's own usage, pretend mode is
the DEFAULT: only `NOPRETEND` makes the Installer write for real. The
row therefore runs without any pretend argument (passing the literal
`PRETEND` hangs the tool on an option requester - proven in run 2).

## The three counted reds
1. `20261001-162632-v1.2.0-rc4-481-g962ac31`: 107 ok / 1 not ok then the
   leg stalled at the row: `Installer ?` rc=10, no usage file - the bench
   had never staged `C/Installer` at all (harness gap, fixed in
   4e8e16d).
2. `20261001-164859-v1.2.0-rc4-482-g4e8e16d`: with the binary staged and
   the literal `PRETEND` argument, the row HUNG after printing the
   template (option requester, headless); the 68000 leg was then killed
   by a leftover cleanup from the still-running previous wrapper
   (shared `ci/.bench-tolunnet.config` deleted mid-staging - my
   overlapping-invocation mistake, uncounted FATAL on top).
3. `20261001-173354-v1.2.0-rc4-483-g98feb99` (correct invocation, the
   parent-commit script deliberately staged): the row hung AGAIN after
   the template - with the OLD script's `39<<16` the REAL Installer
   opens a script-error REQUESTER that nobody can answer headlessly.
   The hang IS the proof the old script is broken, but it produces no
   assertable TAP line.

## Why the prove cannot complete headlessly
The real Installer is GUI-bound at exactly the two points the prove
needs to pass through: a script error opens a Retry/Abort requester
(the old script), and even a clean script opens its welcome panel
(auto-answered only in Novice mode). Testing "the real Installer parses
the script" headlessly would need either a scripted-input WinUAE driver
or a script copy with the welcome stripped - neither is the shipped
artifact the row is meant to test.

## Consequence
- The row stays in the suite and is a valid headless check for the GOOD
  script (Novice mode auto-answers; a clean run ends rc 0 with the
  copyfiles transcript), but its green has never been demonstrated: the
  suite is `110 ok / 1 not ok` (the known `tc_floppy_install`
  TolunnetSetup.info failure) plus this row unfinished, plan `1..112`
  (the 1..113 renumber was never validated by a bench).
- README/STATUS continue to say the installer path is untested in the
  emulator.
- Next session candidates: run the row under a WinUAE input driver that
  answers the welcome/error requesters, or test with a welcome-stripped
  copy clearly labelled as such.
