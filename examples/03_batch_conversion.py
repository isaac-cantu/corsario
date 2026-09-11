"""Convert every DAT* file in a directory to CSV, tolerating truncated files
(e.g. left over from a batch farm job that was killed mid-write) and
reporting which files had warnings, instead of aborting the whole batch."""
import sys
from pathlib import Path

import corsario


def main(data_dir: str, out_dir: str) -> None:
    data_dir_p = Path(data_dir)
    out_dir_p = Path(out_dir)
    out_dir_p.mkdir(parents=True, exist_ok=True)

    files = sorted(p for p in data_dir_p.glob("DAT*") if p.is_file())
    print(f"found {len(files)} candidate files in {data_dir}")

    ok, failed, warned = 0, 0, 0
    for f in files:
        try:
            run = corsario.open(str(f), strict=False)
        except Exception as exc:  # e.g. not a CORSIKA file at all
            print(f"  [FAIL]  {f.name}: {exc}")
            failed += 1
            continue

        out_csv = out_dir_p / f"{f.name}.particles.csv"
        out_shower_csv = out_dir_p / f"{f.name}.showers.csv"
        run.to_csv_ext(str(out_csv))
        run.shower_csv(str(out_shower_csv))

        if run.warnings:
            print(f"  [WARN]  {f.name}: {run.n_showers()} showers, "
                  f"{len(run.warnings)} warning(s) -> {run.warnings[0]}")
            warned += 1
        else:
            print(f"  [OK]    {f.name}: {run.n_showers()} showers, "
                  f"{run.total_particles()} particles")
        ok += 1

    print(f"\ndone: {ok} converted ({warned} with warnings), {failed} failed")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"usage: python {sys.argv[0]} <data_dir> <out_dir>")
        raise SystemExit(1)
    main(sys.argv[1], sys.argv[2])
