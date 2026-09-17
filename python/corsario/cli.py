"""Command-line tools installed alongside the corsario Python package.

    corsario-inspect DAT000001
    corsario-convert DAT000001 particles.csv
    corsario-convert DAT000001 particles.json --kind particles --orient nested
    corsario-convert DAT000001 particles.h5   --kind both
"""

from __future__ import annotations

import argparse
import builtins
import sys

from . import open as corsario_open


def inspect_main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        prog="corsario-inspect",
        description="Print diagnostic information about a CORSIKA .DAT file "
        "(detected binary layout, number of showers/particles, warnings). "
        "Useful when a file fails to parse or when in doubt about whether "
        "it was produced with the THIN or LONGI options.",
    )
    parser.add_argument("path", help="Path to the CORSIKA .DAT file")
    parser.add_argument(
        "--tolerant",
        action="store_true",
        help="Do not raise on truncated/malformed input; report warnings instead.",
    )
    args = parser.parse_args(argv)

    run = corsario_open(args.path, strict=not args.tolerant)
    fmt = run.format
    print(f"file:                 {run.filename}")
    print(f"corsario version:     {run.version}")
    print(f"run number:           {run.run_number}")
    print(f"format:")
    print(f"  Fortran record markers: {fmt.has_markers}")
    print(f"  THIN option:             {fmt.thinning}")
    print(f"  words / sub-block:       {fmt.block_words}")
    print(f"  words / particle:        {fmt.words_per_particle}")
    print(f"n_showers:            {run.n_showers()}")
    print(f"total_particles:      {run.total_particles()}")
    print(f"n_events_processed:   {run.n_events_processed} (from RUNE)")

    longi = sum(1 for sh in run.showers() if sh.has_longitudinal_blocks)
    if longi:
        print(f"longitudinal (LONG) blocks detected in {longi} shower(s) — skipped, "
              f"not included in the particle output.")

    if run.warnings:
        print("\nwarnings:")
        for w in run.warnings:
            print(f"  - {w}")
    return 0


def convert_main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        prog="corsario-convert",
        description="Convert a CORSIKA .DAT file to CSV, JSON, or HDF5.",
    )
    parser.add_argument("input", help="Path to the CORSIKA .DAT file")
    parser.add_argument("output", help="Output path; format is guessed from the extension "
                         "unless --format is given (.csv, .json, .h5/.hdf5)")
    parser.add_argument("--format", choices=["csv", "csv-basic", "json", "hdf5"], default=None)
    parser.add_argument("--kind", choices=["particles", "showers", "both"], default="particles",
                         help="For json/hdf5: which table(s) to write (default: particles)")
    parser.add_argument("--orient", choices=["records", "nested"], default="records",
                         help="For json: flat rows ('records') or grouped by shower ('nested')")
    parser.add_argument("--tolerant", action="store_true",
                         help="Do not raise on truncated/malformed input.")
    parser.add_argument("--stream", action="store_true",
                         help="For --format csv: convert in batches via iter_shower_batches() "
                              "instead of loading the whole file into memory first. Use this "
                              "for files with many millions of showers -- see docs/LARGE_FILES.md.")
    parser.add_argument("--batch-size", type=int, default=50_000,
                         help="Showers per batch when --stream is set (default: 50000)")
    args = parser.parse_args(argv)

    fmt = args.format
    if fmt is None:
        lower = args.output.lower()
        if lower.endswith(".csv"):
            fmt = "csv"
        elif lower.endswith(".json"):
            fmt = "json"
        elif lower.endswith(".h5") or lower.endswith(".hdf5"):
            fmt = "hdf5"
        else:
            parser.error("cannot guess format from output extension; pass --format explicitly")

    if args.stream:
        if fmt not in ("csv", "csv-basic"):
            parser.error("--stream currently only supports --format csv/csv-basic")
        n_showers, n_particles = _convert_streaming(args.input, args.output, fmt, args.kind,
                                                      args.batch_size, strict=not args.tolerant)
        print(f"wrote {fmt} -> {args.output} (streamed, {n_showers} showers, {n_particles} particles)")
        return 0

    run = corsario_open(args.input, strict=not args.tolerant)

    if fmt == "csv":
        if args.kind == "showers":
            run.shower_csv(args.output)
        else:
            run.to_csv_ext(args.output)
    elif fmt == "csv-basic":
        run.to_csv(args.output)
    elif fmt == "json":
        run.to_json(args.output, kind=args.kind, orient=args.orient)
    elif fmt == "hdf5":
        run.to_hdf5(args.output, kind=args.kind)

    print(f"wrote {fmt} -> {args.output} "
          f"({run.n_showers()} showers, {run.total_particles()} particles)")
    if run.warnings:
        print(f"({len(run.warnings)} warning(s); run corsario-inspect for details)")
    return 0


def _convert_streaming(input_path, output_path, fmt, kind, batch_size, strict):
    from . import iter_shower_batches

    n_showers = 0
    n_particles = 0
    header_written = False
    with builtins.open(output_path, "w") as f:
        for df_particles, df_shower in iter_shower_batches(input_path, batch_size=batch_size, strict=strict):
            df_out = df_shower if (fmt == "csv" and kind == "showers") else df_particles
            if fmt == "csv-basic":
                df_out = df_particles[["particle_id", "px", "py", "pz", "x", "y", "t"]]
            df_out.to_csv(f, header=not header_written, index=False)
            header_written = True
            n_showers += len(df_shower)
            n_particles += len(df_particles)
    return n_showers, n_particles


if __name__ == "__main__":
    sys.exit(convert_main())
