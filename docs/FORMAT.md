# CORSIKA `.DAT` binary layout, and how corsario parses it

This document explains the on-disk format corsario reads, why the previous
implementation broke on real files, and how the current implementation
avoids the same class of bug. References are to `CORSIKA_GUIDE 7.8050`,
Section 10.2.

## 1. The layout

CORSIKA writes the particle-output file as a sequential Fortran unformatted
file. Conceptually it is a flat sequence of **sub-blocks**, each a fixed
number of 4-byte words:

| Sub-block | Marks the... |
|---|---|
| `RUNH` | start of the run (once) |
| `EVTH` | start of a shower/event |
| *(unmarked)* | a **DATA** sub-block: up to 39 particle records |
| `LONG` | a longitudinal-profile sub-block (only if `LONGI` was enabled) |
| `EVTE` | end of a shower/event |
| `RUNE` | end of the run (once) |

```
RUNH
  EVTH            (shower 1)
  DATA ... DATA
  [LONG ... LONG] (only with LONGI)
  EVTE
  EVTH            (shower 2)
  ...
RUNE
```

A DATA sub-block has no tag of its own — you only find out it's a DATA
sub-block by not recognizing its first word as one of `RUNH`/`EVTH`/`LONG`/
`EVTE`/`RUNE`. Each particle record inside it is:

```
[0] description = particle_id * 1000 + hadronic_generation * 10 + obs_level
[1] px  (GeV/c)      [4] x (cm)
[2] py  (GeV/c)      [5] y (cm)
[3] pz  (GeV/c)      [6] t (ns)
[7] weight            <- only present with the THIN option
```

**Two structural parameters are not recorded anywhere inside the file**, and
both change how many bytes wide everything above is:

1. **Fortran record markers.** gfortran/g77 (the default on Linux) wraps
   every *physical record* — 21 sub-blocks — with a 4-byte length marker
   before and after it. Adjacent records' markers sit back-to-back, so in
   practice you see 2 extra words between every 21st and 22nd sub-block
   (and one lone marker word at the very start and end of the file). Files
   written with plain stream I/O have none of this.
2. **The THIN option.** If enabled, each sub-block grows from 273 to 312
   words, and each particle record grows from 7 to 8 words (the extra word
   is the thinning weight). `RUNH`/`EVTH`/`EVTE`/`RUNE` also become 312
   words (zero-padded).

That's 4 possible combinations, and CORSIKA installations differ on which
one they produce.

## 2. What the previous implementation assumed — and why it broke

The previous reader hard-coded **one specific combination**: markers
present, no thinning, 273-word sub-blocks, 7-word particles. It scanned the
file for `RUNH`/`EVTH`/`EVTE`/`RUNE` using a fixed stride of 273 words, with
a `+2` correction every 21 sub-blocks to account for the markers.

That arithmetic is internally consistent *for that one combination* — we
verified this with synthetic files (see `tests/`) including a 5000-particle
shower spanning many super-blocks, and the original stride/`+2` logic does
track boundaries correctly in that case. The actual failure modes are:

- **THIN files**: the reader keeps striding by 273/7 through a file that is
  actually laid out in 312/8-word units. The offsets drift further apart
  from the true boundaries with every sub-block, so the reader either mis-parses
  particle fields as garbage, or eventually indexes past the shower's real
  `EVTE`/end of file and throws. Since THIN is the standard choice for
  higher-energy (i.e. higher particle-count) showers, this presents exactly
  as "works on small showers, fails once showers get big" — a direct match
  for the reported symptom.
- **LONGI files**: `LONG` sub-blocks between the last DATA sub-block and
  `EVTE` were not recognized at all, and were parsed as if they were 39 more
  particle records — silently corrupting the particle stream with bogus
  rows (their contents look like legitimate floats, they just aren't
  particles) rather than raising an error.
- **Files without record markers**: the very first `RUNH` search assumed
  word 1 (not word 0) holds the tag; without markers, `RUNH` is at word 0,
  so the scan finds nothing and reports zero showers. Reproduced in
  `tests/test_corsario.py::test_format_auto_detection`.
- **Performance**: showers were read lazily, each one **re-opening the file
  from disk** and re-scanning from byte 0 to find its own boundaries. One
  variant found in `ARCHES/corsario` went further and issued one `seek()` +
  `read()` *per particle*. Both are avoidable — see the benchmark below.
- 32-bit `int` word offsets, which will silently overflow for files beyond
  roughly 2 GB (some high-statistics / low-thinning runs do get this large).

None of this required a malformed file to trigger — just a file produced
with a different (and common) CORSIKA configuration than the one originally
tested against.

## 3. What corsario does instead

`FormatInfo detect_format(...)` (`include/corsario/format.hpp`) tries the
four structurally possible combinations against the start of the file: it
looks for `RUNH` at word 0 or word 1, and — since `RUNH` alone can't
disambiguate 273- vs. 312-word blocks — confirms the following sub-block's
tag (`EVTH`, or `RUNE` for a run with zero showers) lands where a 273-word
or 312-word block would put it. Whichever combination matches is used for
the rest of the file. No flags, no recompiling.

`Corsario::parse()` (`include/corsario/corsario.hpp`) then makes **one
sequential pass** over the file, one sub-block at a time, using a single
`blocks_since_boundary` counter (reset every 21 sub-blocks, adding the
2-word marker gap if markers were detected) — no per-shower replay, no
per-particle seeking, no modular-arithmetic checkpoints computed relative to
each shower's own start. `LONG` sub-blocks are recognized and skipped
(flagged on the `Shower` via `has_longitudinal_blocks`); DATA sub-blocks are
parsed using the detected `words_per_particle` (7 or 8), with the weight
word populated only when present. All offsets are `long`, not `int`.

## 4. Performance

Benchmarked against a synthetic 2.72M-particle / 12,000-shower file
(`tests/`, `scripts/bench_synthetic.py`), single run, `-O2`:

| Implementation | Time |
|---|---|
| previous `corsario` (main branch) | ~285 ms |
| `ARCHES/corsario` variant (per-particle `seek`) | ~3.0 s |
| this rewrite | ~185 ms |

The gap between the first two rows grows with file size (the per-particle
`seek()` variant is not linear in the number of particles); the rewrite is
linear in file size by construction, since it reads the file exactly once.

## 5. Tolerant mode

Batch farms occasionally kill a job mid-write, leaving a truncated `.DAT`
file. `Corsario(path, strict=False)` will not raise on a truncated or
malformed file; it returns whatever complete showers it managed to parse
and records human-readable messages in `.warnings`. `strict=True` (the
default) raises immediately, which is almost always what you want in an
interactive/analysis context.
