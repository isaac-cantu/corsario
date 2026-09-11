# Server / HPC installation

## Quick install

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install --upgrade pip
pip install ".[hdf5]"          # from a clone of this repository
```

Requirements: Python ≥ 3.9, a C++17 compiler (g++ ≥ 9 or clang ≥ 10), and
CMake ≥ 3.15. `pip` pulls in `pybind11`/`scikit-build-core` automatically —
no separate `cmake`/`make` invocation needed, and no `sudo`/root access is
required as long as the compiler and Python headers are already present.

On a cluster without internet access on compute nodes, install on the login
node (or in a `pip download`'d wheel cache) and then run jobs from the same
environment/venv.

## Container

`docker/Dockerfile` builds a self-contained image with corsario installed:

```bash
docker build -t corsario -f docker/Dockerfile .
docker run --rm -v /data/corsika:/data corsario \
    corsario-convert /data/DAT000001 /data/particles.csv
```

Use this if the target server's toolchain is unpredictable (e.g. shared
HPC login nodes with an old system compiler) — the image pins a known-good
build environment.

## Batch conversion on a SLURM cluster

`scripts/slurm_convert_array.sh` is a job-array template that converts many
`.DAT` files in parallel (one file per array task, not one shower per task —
see `docs/TROUBLESHOOTING.md` for why parallelizing within a single file's
parsing usually isn't worth it: parsing itself is already linear and fast,
and the bottleneck on shared storage is I/O bandwidth, not CPU). Edit the
`DATA_DIR`/`OUT_DIR`/array range at the top, then:

```bash
sbatch scripts/slurm_convert_array.sh
```

## Sanity-checking a batch of files before a full run

```bash
for f in /data/corsika/DAT*; do
    corsario-inspect "$f" || echo "FAILED: $f"
done
```

This is worth doing once per simulation campaign (i.e. per CORSIKA
configuration) rather than per file — if your CORSIKA build is consistent,
all files from the same campaign will report the same `format:` block, and
a single `corsario-inspect` confirms the whole batch will parse correctly
before committing cluster time to a full conversion run.
