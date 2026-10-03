# amiga-gcc 6.5: "combine clr" peephole2 merges zero stores of different modes

A wrong-code bug in the Amiga GCC 6.5 back end (`gcc/config/m68k/m68k.md`)
found while debugging [tolunnet](https://github.com/tolon/tolunnet). This
folder holds the fix, a minimal reproducer and a test matrix.

| File | What it is |
|---|---|
| [`0001-m68k-combine-clr-peephole2-mode.patch`](0001-m68k-combine-clr-peephole2-mode.patch) | the fix, `git am`-ready |
| [`repro.c`](repro.c) | 7-line reproducer |
| [`cases.c`](cases.c) | every wrong-code shape plus the merges that must stay |
| [`show.sh`](show.sh) | prints what each `cases.c` function compiles to, for `-m68000` and `-m68080` |

## Affected compilers

The rule exists only in the GCC 6.5 line:

- AmigaPorts/gcc `amiga6` (checked at `4d3098c4d`)
- bebbo/gcc `amiga6` (`8c1fd39db`) and `amiga6.5` (`1a94e2b9d`, 2026-09-27)
- seen in practice with `m68k-amigaos-gcc (GCC) 6.5.0b 20260731`

The GCC 15.2 and 16.2 branches (`amiga15.2`, `amiga16.2`) do not carry this
rule.

## The bug

The `define_peephole2` commented

```
;; combine clr if possible .l #0,x(a0), #0,x+4(a0) -> .q #0,x(a0)
;; or .w #0,x(a0), #0,x+2(a0) -> .l #0,x(a0)
```

merges two zero stores at `x` and `x+size` into one wider store. Both `mem`
operands in the pattern have no mode. The condition checks the mode and the
`volatile` flag of the **first** store only (`operands[4]`). The second
store can be any width, so:

| Stores | Compiled to | Effect |
|---|---|---|
| `.w #0,x` + `.l #0,x+2` (68000, 68020, 68080; `-O2`, `-Os`) | `clr.l x` | the long's low word is never cleared |
| `.w #0,x` + **volatile** `.l #0,x+2` | `clr.l x` | a volatile store is merged and narrowed |
| `.l #0,x` + `.w #0,x+4` (`-m68080`) | `clr.q x` | 8 bytes written for 6: `x+6..x+7` is overwritten |

### Reproducer

```c
struct s { long pad; short a; long b; };   /* a at 4, b at 6: long is 2-aligned */
void clear(struct s *p) { p->a = 0; p->b = 0; }
```

`m68k-amigaos-gcc -O2 -S repro.c` gives

```
	clr.l (4,a0)
```

The correct code (`-fno-peephole2` gives it) is

```
	clr.w (4,a0)
	clr.l (6,a0)
```

## The fix

Merge only when the second store has the same mode as the first and is not
volatile:

```diff
   "(operands[4] = SET_DEST(PATTERN(insn))) && 
+  (operands[6] = SET_DEST(PATTERN(peep2_next_insn(1)))) && 
+  GET_MODE(operands[6]) == GET_MODE(operands[4]) && 
+  !operands[6]->volatil && 
   (TARGET_68080 || GET_MODE_SIZE(GET_MODE(operands[4])) == 2) && 
```

The patch is made against AmigaPorts `amiga6` and also applies cleanly to
bebbo `amiga6.5`:

```sh
cd projects/gcc && git am /path/to/0001-m68k-combine-clr-peephole2-mode.patch
```

## Verification

The fix was tested with a `cc1` built from `amiga6` plus the patch. The
installed driver picked it up through `-B`.

- **`cases.c`.** All three wrong-code shapes now compile to two separate
  stores. The valid merges are still made: `.w + .w` gives `clr.l`, and on
  `-m68080` `.l + .l` gives `clr.q`. Check it with:

  ```sh
  ./show.sh                                  # stock compiler
  ./show.sh m68k-amigaos-gcc -B/dir/with/cc1/  # patched cc1 (symlink cc1 alone into that dir)
  ```

  | Function (`cases.c`) | Stock | Patched |
  |---|---|---|
  | `hi_si`, 68000 and 68080 | `clr.l (4,a0)` | `clr.w (4,a0)` + `clr.l (6,a0)` |
  | `hi_vsi`, 68000 | `clr.l (4,a0)` | `clr.w (4,a0)` + `move.l #0,(6,a0)` |
  | `si_hi`, 68080 | `clr.q (4,a0)` | `clr.l (4,a0)` + `clr.w (8,a0)` |
  | `hi_hi` | `clr.l (4,a0)` | `clr.l (4,a0)` (merge kept) |
  | `si_si`, 68080 | `clr.q (4,a0)` | `clr.q (4,a0)` (merge kept) |

- **Whole-project scan.** A script scanned the peephole2 RTL dumps of all
  156 tolunnet sources (`-O2 -m68000`). With the stock compiler it found 2
  mixed-mode merges. With the patched one it found none, and the one
  same-mode merge was still made.

- **Runtime.** tolunnet was built with the patched `cc1` and without
  `-fno-peephole2`, then run through its WinUAE conformance bench. The test
  that crashed before now passes.

## What it broke in tolunnet

Two real sites in tolunnet, built with `-O2 -m68000`:

1. **A stack frame.** A function had a `short` flag and a `long` counter in
   adjacent stack slots. Both were set to 0, and the counter's low word kept
   stack garbage. A later `memcpy(buf + counter, …)` overran a heap block,
   and the next `AvailMem(MEMF_LARGEST)` raised Guru `8100000C`
   (`AN_MemoryInsane`). Whether it crashed depended only on what was left
   on the stack, so adding debug code made it go away.

2. **lwIP 2.2 `tcp_write()`.** `u16_t extendlen = 0` and
   `struct pbuf *concat_p = NULL` are merged. On the TCP_OVERSIZE path,
   `concat_p` can then be a non-NULL garbage pointer that is passed to
   `pbuf_cat()`. The published tolunnet 1.2.0-rc4 daemon contains exactly
   this sequence (`movea.l -22(a5),a4; clr.l -10(a5); movea.l 4(a4),a3`).

## Workaround

Build with `-fno-peephole2`. tolunnet builds with it from the next release on. It adds
188 bytes to the daemon.
