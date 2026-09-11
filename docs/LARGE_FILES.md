# Large files: streaming millions of showers

`corsario.open()` / `Corsario` loads every shower's particles into memory
at once. That is simplest and fastest for typical files, but does not scale
to a file with millions of showers: even a modest few hundred particles per
shower means hundreds of millions to billions of particle records, which
will not fit in RAM on a single machine.

## Memory budgeting

Each particle record is ~7-8 floats on disk (28-32 bytes); in memory, a
`Particle` object is somewhat larger (it also caches decoded fields like
mass/energy). As a rule of thumb, budget **~60-80 bytes per particle** if
you hold them in memory. For a file with `N` showers averaging `k`
particles each, that's `N * k * ~70` bytes for the particle data alone —
e.g. 5,000,000 showers x 500 particles/shower is already ~175 GB, well
beyond a typical 32-128 GB node.

**What does scale**: per-shower *features* (a handful to a few hundred
floats per shower, not per particle). 15,000,000 showers x 16 floats
(a compact feature set) is ~1 GB; even a 384-float grid representation is
only ~23 GB. The pattern below computes features from a bounded window of
raw particles at a time and only accumulates the compact result.

## `corsario.iter_shower_batches()`

```python
import corsario

for df_particles, df_shower in corsario.iter_shower_batches("DAT000001", batch_size=50_000):
    # df_particles / df_shower hold only THIS batch's showers -- same
    # columns as Corsario.to_dataframe("particles"/"showers"), just fewer
    # rows. Process them (e.g. compute features) and let them go out of
    # scope before the next batch is read.
    ...
```

This is a thin wrapper around `corsario.ShowerStream`, which does the
actual resumable parsing (`stream.next_batch(n)` returns the next `n`
complete showers and remembers where it left off — see
`include/corsario/stream.hpp` if you need the C++-level API directly, e.g.
to embed corsario in a non-Python pipeline). `Corsario` itself is
implemented as `ShowerStream` run to completion internally, so both paths
share one parsing implementation — streaming a file in batches of 1 shower
each and concatenating the results is verified (see `tests/`) to produce
byte-identical output to `Corsario.to_dataframe()`.

**Choosing `batch_size`**: larger batches amortize fixed per-batch overhead
better (fewer, larger pandas DataFrame constructions) at the cost of more
peak memory. 10,000-100,000 showers is a reasonable starting point for
typical (non-thinned) showers; reduce it if a single batch is still too
large (e.g. very high-energy, very large showers), increase it if batches
are cheap and you want fewer of them.

## Streaming CSV conversion

```bash
corsario-convert DAT000001 particles.csv --stream --batch-size 50000
```

Writes the CSV incrementally (one batch at a time, header written once),
never holding more than one batch in memory. Verified to produce
numerically identical output to the non-streaming conversion (`tests/`).

## Building a compact per-shower feature dataset

The pattern that actually matters for training on huge files: don't
materialize the particle table at all beyond one batch at a time — compute
your per-shower features (whatever those are for your problem) inside the
loop, and only keep the small, per-shower result:

```python
import numpy as np
import corsario

all_features = []
all_targets = []
for df_particles, df_shower in corsario.iter_shower_batches("DAT000001", batch_size=50_000):
    # your own per-shower feature computation goes here, e.g. something
    # equivalent to ARCHES's data.features.stats_generator(df_particles, df_shower)
    X_batch, y_batch = my_feature_function(df_particles, df_shower)
    all_features.append(X_batch)
    all_targets.append(y_batch)

X = np.concatenate(all_features, axis=0)
y = np.concatenate(all_targets, axis=0)
np.savez("features.npz", X=X, y=y)   # a few GB at most, loads instantly later
```

See `examples/04_streaming_large_files.py` for a complete, runnable version
of this, and (if you're using the ARCHES pipeline) `data/build_feature_dataset.py`
there for a checkpointed version of exactly this pattern.
