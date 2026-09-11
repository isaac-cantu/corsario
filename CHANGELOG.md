# Changelog

## 1.1.0 — streaming for large files

### Added
- `corsario.iter_shower_batches(path, batch_size=...)` and the underlying
  `corsario.ShowerStream` class: stream a file in batches of N showers
  without ever holding the whole file's particles in memory. `Corsario` is
  now implemented internally as `ShowerStream` run to completion, so both
  paths share one parsing implementation (verified: streaming in batches
  as small as 1 shower and concatenating the results is byte-identical to
  `Corsario.to_dataframe()`, see `tests/`).
- `corsario-convert --stream --batch-size N`: streaming CSV conversion.
- `docs/LARGE_FILES.md`, `examples/04_streaming_large_files.py`: memory
  budgeting and a worked example, including the pattern for building a
  compact per-shower feature dataset from a huge file (see also ARCHES's
  `data/build_feature_dataset.py`, which uses this API).

### Fixed
- `to_csv`/`to_csv_ext`/`shower_csv` were writing floats at the default
  `std::ostream` precision (6 significant digits), silently truncating
  values below their full `float32` precision. This was harmless on its
  own, but meant the C++ (eager) and pandas-based (streaming) export paths
  disagreed on the exact text for the same underlying value. Both now use
  `std::numeric_limits<float>::max_digits10` so the text round-trips to
  the exact same bits either way.

## 1.0.0 — rewrite

### Fixed
- Reader no longer desynchronizes on CORSIKA files produced with the
  **THIN** option (312-word sub-blocks, 8-word particle records) — the
  layout is now auto-detected instead of hard-coded. See `docs/FORMAT.md`.
- Reader no longer misinterprets **`LONG`** (longitudinal-profile)
  sub-blocks as particle data. They are now recognized and skipped;
  `Shower.has_longitudinal_blocks` flags affected showers.
- Reader no longer assumes Fortran record-length markers are present;
  files written with plain stream I/O (no markers) now also parse
  correctly.
- Word/byte offsets use `long` instead of `int`, removing a silent-overflow
  risk on files above ~2 GB.
- Removed hard-coded, developer-specific absolute paths
  (`/home/icantu24/...`) from `#include` directives and example code —
  the project did not compile out of the box for anyone else.
- Removed committed compiled binaries (`include/binary`, `include/main`,
  `src/binary`, `src/binary_reader`, `src/read`) that were accidentally
  checked into git.

### Changed
- Single sequential pass over the file (one file handle, no per-shower
  re-open, no per-particle `seek()`), which is both the correctness fix
  above and a significant speed-up — see the benchmark in
  `docs/FORMAT.md#performance`.
- `to_csv_ext()` gains a trailing `weight` column (`1.0` unless the file was
  thinned). All other CSV column names/order are unchanged from the
  historical corsario output.
- `Corsario`/`Shower`/`Particle` are now exposed to Python via pybind11
  (previously the Python bindings were an incomplete stub); see
  `bindings/module.cpp` and `python/corsario/__init__.py`.
- Particle name/mass table completed for all CORSIKA particle codes used in
  practice (the previous table only covered codes 0–18).

### Added
- JSON export (`to_json`, flat "records" or shower-"nested" layout).
- HDF5 export (`to_hdf5`, requires `pip install corsario[hdf5]`).
- `to_dataframe()` for direct `pandas.DataFrame` access.
- `corsario-inspect` / `corsario-convert` command-line tools.
- `strict=False` tolerant parsing mode for truncated/malformed files.
- pytest suite with a synthetic-`.DAT`-file generator covering all
  4 marker/thinning combinations plus `LONG` blocks and truncation
  (`tests/`).
- Packaging via `scikit-build-core` + `pyproject.toml` (`pip install .`
  now works end to end; previously `pyproject.toml`/`CMakeLists.txt` were
  incomplete stubs and the extension did not build).
- `docker/Dockerfile`, `scripts/slurm_convert_array.sh`,
  `.github/workflows/ci.yml` for server/cluster deployment.
