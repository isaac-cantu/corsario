"""Export the same run to CSV, pandas, JSON, and HDF5."""
import sys

import corsario


def main(path: str) -> None:
    run = corsario.open(path)

    # CSV (identical columns to the historical corsario output)
    run.to_csv_ext("particles.csv")
    run.shower_csv("showers.csv")

    # pandas, in-memory (handy for quick exploration / feature engineering)
    df = run.to_dataframe("particles")
    print(df.describe(include="all").T[["count", "mean"]])

    # JSON: flat rows (easy to load anywhere) ...
    run.to_json("particles.json", orient="records")
    # ... or grouped by shower (self-describing, keeps the relationship)
    run.to_json("particles_by_shower.json", orient="nested")

    # HDF5 (requires: pip install "corsario[hdf5]")
    try:
        run.to_hdf5("particles.h5", kind="both")
        print("wrote particles.h5")
    except ImportError as exc:
        print(f"skipped HDF5 export: {exc}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: python {sys.argv[0]} <path/to/DATnnnnnn>")
        raise SystemExit(1)
    main(sys.argv[1])
