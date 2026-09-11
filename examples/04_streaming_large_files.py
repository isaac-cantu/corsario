"""Process a CORSIKA .DAT file with millions of showers without ever
holding the full particle table in memory -- see docs/LARGE_FILES.md.

Computes a simple per-shower feature vector (particle count, mean/std of
x, y, t, and log-energy) batch by batch, and saves the compact result
(one row per shower, however many millions there are) to a single .npz
file that will comfortably fit in memory on the next run.
"""
import sys
import time

import numpy as np

import corsario


def shower_features(df_particles, df_shower):
    """One row per shower in df_shower. Replace this with your own feature
    function -- e.g. ARCHES's data.features.stats_generator -- the calling
    pattern (batch-by-batch, accumulate only the small result) stays the
    same regardless of what the features are."""
    feats = []
    targets = []
    for event_no, group in df_particles.groupby("shower"):
        row = df_shower[df_shower["event_no"] == event_no]
        if row.empty:
            continue
        feats.append([
            len(group),
            group["x"].mean(), group["x"].std(),
            group["y"].mean(), group["y"].std(),
            group["t"].mean(), group["t"].std(),
            np.log1p(group["energy"]).mean(),
        ])
        targets.append(row["total_energy"].iloc[0])
    return np.array(feats, dtype=np.float32), np.array(targets, dtype=np.float32)


def main(path: str, out_path: str, batch_size: int = 50_000) -> None:
    all_X, all_y = [], []
    n_showers = 0
    t0 = time.time()

    for i, (df_particles, df_shower) in enumerate(
        corsario.iter_shower_batches(path, batch_size=batch_size)
    ):
        X_batch, y_batch = shower_features(df_particles, df_shower)
        all_X.append(X_batch)
        all_y.append(y_batch)
        n_showers += len(df_shower)
        elapsed = time.time() - t0
        print(f"batch {i}: +{len(df_shower)} showers ({n_showers} total), "
              f"{elapsed:.1f}s elapsed, {n_showers/max(elapsed,1e-9):.0f} showers/s")

    X = np.concatenate(all_X, axis=0)
    y = np.concatenate(all_y, axis=0)
    np.savez(out_path, X=X, y=y)
    print(f"\nwrote {out_path}: X={X.shape}, y={y.shape} "
          f"({X.nbytes / 1e6:.1f} MB) in {time.time()-t0:.1f}s")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        print(f"usage: python {sys.argv[0]} <path/to/DATnnnnnn> <out.npz> [batch_size]")
        raise SystemExit(1)
    batch_size = int(sys.argv[3]) if len(sys.argv) == 4 else 50_000
    main(sys.argv[1], sys.argv[2], batch_size)
