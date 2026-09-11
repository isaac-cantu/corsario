"""Basic corsario usage: open a file, inspect showers, export CSV."""
import sys

import corsario


def main(path: str) -> None:
    with corsario.open(path) as run:
        print(f"file:              {run.filename}")
        print(f"format:            {run.format}")
        print(f"n_showers:         {run.n_showers()}")
        print(f"total_particles:   {run.total_particles()}")

        first = run.shower(0)
        print(f"\nshower 1: primary energy = {first.total_energy_gev:.3e} GeV, "
              f"{first.n_particles()} particles")
        for p in list(first.particles())[:5]:
            print(f"  {p.particle_type:>6s}  E={p.energy:8.4f} GeV  "
                  f"x={p.x:9.2f} cm  y={p.y:9.2f} cm  t={p.time:8.2f} ns")

        run.to_csv_ext("particles.csv")
        run.shower_csv("showers.csv")
        print("\nwrote particles.csv and showers.csv")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: python {sys.argv[0]} <path/to/DATnnnnnn>")
        raise SystemExit(1)
    main(sys.argv[1])
