# corsario

Fast, robust Python/C++ reader for CORSIKA `.DAT` particle-output files —
built for the ARCHES cosmic-ray / neural-network pipeline, but generally
useful for anyone parsing CORSIKA binary output.

```python
import corsario

with corsario.open("DAT000001") as run:
    print(run.n_showers(), "showers,", run.total_particles(), "particles")

    df = run.to_dataframe("particles")   # pandas.DataFrame
    run.to_csv_ext("particles.csv")      # historical corsario CSV columns
    run.to_json("particles.json")
    run.to_hdf5("particles.h5")
```

## Why this rewrite exists

The previous corsario implementation assumed one specific CORSIKA binary
layout (Fortran record markers present, no thinning). That assumption broke
in two very common situations:

- **The THIN option** (used for most high-energy showers to keep file sizes
  manageable) changes the sub-block size from 273 to 312 words and the
  particle record from 7 to 8 words. Reading a thinned file with the old
  fixed-273 stride desynchronizes from the real record boundaries.
- **The LONGI option** (longitudinal profile output) inserts `LONG`
  sub-blocks between the particle data and the event end. The old reader had
  no notion of them and quietly parsed their contents as if they were
  particles.

Both are exactly the kind of run configuration used for larger showers, which
is why the old reader "worked for a while, then failed as showers grew" —
see [`docs/FORMAT.md`](docs/FORMAT.md) for the full technical explanation,
and [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) if you hit a parsing
error with your own files.

`corsario` now **auto-detects** the record-marker convention and the
THIN option from the file itself, and does a single sequential pass over
the file (previously: one file re-open per shower, and in one experimental
variant, one `seek()` per *particle*) — see the benchmark in
[`docs/FORMAT.md#performance`](docs/FORMAT.md#performance).

## Installation

```bash
pip install .            # from a clone of this repository
# or, for HDF5 export:
pip install ".[hdf5]"
```

Requires a C++17 compiler and CMake ≥ 3.15 (both are pulled in automatically
by `pip` via `scikit-build-core`/`pybind11`, no manual `cmake` step needed).

See [`docs/INSTALL_SERVER.md`](docs/INSTALL_SERVER.md) for HPC/cluster
installation notes, and the top-level `docker/Dockerfile` for a
containerized build.

## Large files (millions of showers)

`corsario.open()` loads everything into memory at once. For files too
large for that, `corsario.iter_shower_batches(path, batch_size=50_000)`
streams the file in batches — see [`docs/LARGE_FILES.md`](docs/LARGE_FILES.md)
for memory budgeting and a worked example (including
`corsario-convert --stream` for streaming CSV conversion).

## CSV column compatibility

`to_csv`, `to_csv_ext`, and `shower_csv` produce the **exact same column
names and order** as the historical corsario output, so pipelines that
already read these files (e.g. ARCHES) require no changes:

| Method | Columns |
|---|---|
| `to_csv` | `particle_id,px,py,pz,x,y,t` |
| `to_csv_ext` | `shower,particle_type,no_particle,hadr_gen,no_obs,mass,energy,particle_id,px,py,pz,x,y,t,weight` |
| `shower_csv` | `event_no,particle_id,total_energy,altitude,no_target,z,px,py,pz,zenith,azimuth` |

The only change is a trailing `weight` column appended to `to_csv_ext`
(`1.0` for non-thinned files, the real THIN weight otherwise) — existing
name-based column access (`df["x"]`, `pd.read_csv(...)`) is unaffected.

## Command-line tools

```bash
corsario-inspect DAT000001                 # diagnostics: detected format, counts, warnings
corsario-convert DAT000001 particles.csv
corsario-convert DAT000001 particles.json --kind particles --orient nested
corsario-convert DAT000001 particles.h5    --kind both
```

## Documentation

- [`docs/FORMAT.md`](docs/FORMAT.md) — the CORSIKA binary layout and how corsario parses it
- [`docs/API.md`](docs/API.md) — full Python API reference
- [`docs/LARGE_FILES.md`](docs/LARGE_FILES.md) — streaming files with millions of showers
- [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) — diagnosing files that fail to parse
- [`docs/INSTALL_SERVER.md`](docs/INSTALL_SERVER.md) — server/HPC installation and batch conversion
- [`examples/`](examples/) — runnable usage examples
- [`CHANGELOG.md`](CHANGELOG.md) — what changed vs. the previous corsario

## Development

```bash
pip install -e ".[dev]"
pytest
```

`tests/make_synthetic.py` generates small synthetic `.DAT` files (with and
without record markers, with and without thinning, with `LONG` blocks) with
known ground truth, so the test suite does not depend on having a real
CORSIKA installation available.
