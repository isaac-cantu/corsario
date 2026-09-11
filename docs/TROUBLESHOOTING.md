# Troubleshooting

## "corsario: could not detect the CORSIKA DAT layout"

`corsario-inspect <file>` will confirm whether the file even starts with a
`RUNH` tag. If it doesn't:

- The file may not be a CORSIKA particle-output file at all (e.g. it's a
  `.long`, `.tab`, or Cherenkov-only file — see `CORSIKA_GUIDE` Sect. 10).
- It may use the `COMPACT` particle output option (Sect. 10.2.4), which has
  a genuinely different, variable-length block structure. This is not
  currently supported by corsario; if you need it, please open an issue —
  it is a bounded amount of additional work following the same
  single-pass design, since the compact format documents its own block
  lengths inline.
- The file may be corrupted (check its size against what your CORSIKA run
  log reports, and `md5sum`/transfer integrity if it moved between
  machines).

## "corsario: file ended while shower N was still open"

The file is truncated (a batch job was likely killed mid-write). Two options:

```python
run = corsario.open(path, strict=False)   # keep whatever parsed, with warnings
print(run.warnings)
```

or re-run/re-fetch the simulation for that run number.

## My row counts differ slightly from an older `corsario`-produced CSV

Two intentional, backwards-compatible additions:

1. `to_csv_ext` now has a trailing `weight` column (`1.0` unless the file
   was produced with `THIN`). If your code does `pd.read_csv(...).values`
   and relies on the *positional* column count rather than names, update it
   to index by name instead — see `docs/API.md`.
2. If the original file was produced with `LONGI`, the old reader silently
   added a handful of bogus "particles" per shower (see `docs/FORMAT.md`
   §2). Those are correctly excluded now, so `n_particles` per shower may be
   a few rows *lower* than an old CSV for the same file. Check
   `Shower.has_longitudinal_blocks` if you need to know which showers were
   affected.

## Reading a file works but is slower than expected

- Make sure you're calling `corsario.open()` once and reusing the returned
  object — every call re-parses the whole file from disk.
- `Shower.particles()` copies every `Particle` into a Python list; for bulk
  numeric work across many showers, use `Corsario.flat_*()` or
  `to_dataframe()`, which build NumPy arrays directly from the C++ side
  without going through per-particle Python objects.
- For very large files (tens of GB), corsario currently reads one
  273/312-word sub-block at a time via buffered `ifstream` reads rather than
  memory-mapping the file; this is fast (see `docs/FORMAT.md#performance`)
  but if you are converting thousands of large files on a shared filesystem,
  I/O bandwidth — not corsario's parsing — is likely the bottleneck. The
  SLURM array script in `scripts/slurm_convert_array.sh` parallelizes across
  files rather than within one file, for that reason.

## Getting more detail on a specific file

```bash
corsario-inspect run/DAT000001
```

prints the detected format, run number, shower/particle counts, whether any
`LONG` blocks were found, and any warnings — this is the fastest way to
confirm whether a file is THIN/LONGI/marker-less before debugging further.
