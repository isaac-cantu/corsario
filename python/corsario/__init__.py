"""
corsario: fast reader for CORSIKA ".DAT" particle-output files.

Quick start
-----------
    import corsario

    with corsario.open("DAT000001") as run:
        print(run.n_showers(), "showers")
        df = run.to_dataframe("particles")   # pandas.DataFrame
        run.to_csv_ext("particles.csv")      # same columns as before
        run.to_json("particles.json")
        run.to_hdf5("particles.h5")

See docs/API.md for the full reference and docs/FORMAT.md for a description
of the underlying CORSIKA binary layout and how corsario auto-detects it.
"""

from __future__ import annotations

import builtins
import json
from typing import Optional

from ._corsario_core import (  # noqa: F401
    FormatInfo,
    Particle,
    Shower,
    ShowerStream,
    __version__,
)
from ._corsario_core import Corsario as _CoreCorsario
from ._corsario_core import _particles_dict, _shower_headers_dict

__all__ = [
    "Corsario",
    "Shower",
    "Particle",
    "FormatInfo",
    "ShowerStream",
    "open",
    "iter_shower_batches",
    "__version__",
]

# Column layouts kept identical to the historical corsario CSV output (plus
# the new trailing "weight" column), so existing consumers such as ARCHES
# keep working with zero changes.
_PARTICLE_COLUMNS = [
    "shower",
    "particle_type",
    "no_particle",
    "hadr_gen",
    "no_obs",
    "mass",
    "energy",
    "particle_id",
    "px",
    "py",
    "pz",
    "x",
    "y",
    "t",
    "weight",
]

_SHOWER_COLUMNS = [
    "event_no",
    "particle_id",
    "total_energy",
    "altitude",
    "no_target",
    "z",
    "px",
    "py",
    "pz",
    "zenith",
    "azimuth",
]


class Corsario(_CoreCorsario):
    """Python-side extension of the C++ ``Corsario`` reader.

    Adds convenience export methods (``to_dataframe``, ``to_json``,
    ``to_hdf5``) on top of the fast C++ core, which already provides
    ``to_csv`` / ``to_csv_ext`` / ``shower_csv``.
    """

    # ------------------------------------------------------------------
    # pandas
    # ------------------------------------------------------------------
    def to_dataframe(self, kind: str = "particles"):
        """Return the run's data as a :class:`pandas.DataFrame`.

        Parameters
        ----------
        kind:
            ``"particles"`` (default) — one row per particle, same columns
            as :meth:`to_csv_ext`.
            ``"showers"`` — one row per shower (event header + a few
            derived fields), same core columns as :meth:`shower_csv`.

        For files with millions of showers, building one DataFrame for the
        whole run this way requires holding every particle in memory at
        once (exactly what :func:`iter_shower_batches` avoids) — see
        docs/LARGE_FILES.md.
        """
        import pandas as pd

        if kind == "particles":
            return pd.DataFrame(_particles_dict(self.showers()), columns=_PARTICLE_COLUMNS)
        if kind == "showers":
            return pd.DataFrame(_shower_headers_dict(self.showers()))
        raise ValueError(f"unknown kind={kind!r}; expected 'particles' or 'showers'")

    # ------------------------------------------------------------------
    # JSON
    # ------------------------------------------------------------------
    def to_json(
        self,
        path: str,
        kind: str = "particles",
        orient: str = "records",
        indent: Optional[int] = 2,
    ) -> None:
        """Write the run's data to a JSON file.

        ``orient="records"`` (default) writes a flat list of row objects,
        one per particle (or shower) — mirrors the CSV layout and is the
        friendliest format for most downstream tools (e.g.
        ``pandas.read_json(path, orient="records")``).

        ``orient="nested"`` writes one object per shower with its header
        fields and an embedded list of its particles — more verbose, but
        keeps the shower/particle relationship explicit without needing a
        second file.
        """
        if orient == "records":
            df = self.to_dataframe(kind)
            df.to_json(path, orient="records", indent=indent)
            return

        if orient == "nested":
            if kind != "particles":
                raise ValueError("orient='nested' only applies to kind='particles'")
            payload = {
                "meta": {
                    "corsario_version": self.version,
                    "filename": self.filename,
                    "run_number": self.run_number,
                    "n_showers": self.n_showers(),
                    "total_particles": self.total_particles(),
                    "thinned": self.is_thinned,
                    "has_markers": self.has_markers,
                },
                "showers": [],
            }
            for sh in self.showers():
                payload["showers"].append(
                    {
                        "event_no": sh.event_no,
                        "primary_id": sh.primary_id,
                        "total_energy_gev": sh.total_energy_gev,
                        "altitude_gcm2": sh.starting_altitude_gcm2,
                        "zenith_rad": sh.zenith_rad,
                        "azimuth_rad": sh.azimuth_rad,
                        "n_particles": sh.n_particles(),
                        "particles": [
                            {
                                "particle_id": p.particle_id,
                                "particle_type": p.particle_type,
                                "energy": p.energy,
                                "px": p.px,
                                "py": p.py,
                                "pz": p.pz,
                                "x": p.x,
                                "y": p.y,
                                "t": p.time,
                                "weight": p.weight,
                            }
                            for p in sh.particles()
                        ],
                    }
                )
            with builtins.open(path, "w") as f:
                json.dump(payload, f, indent=indent)
            return

        raise ValueError(f"unknown orient={orient!r}; expected 'records' or 'nested'")

    # ------------------------------------------------------------------
    # HDF5
    # ------------------------------------------------------------------
    def to_hdf5(self, path: str, kind: str = "both", compression: str = "gzip") -> None:
        """Write the run's data to an HDF5 file using h5py.

        Layout::

            /particles/<column>      1D dataset, one row per particle
            /showers/<column>        1D dataset, one row per shower
            (root attrs) corsario_version, filename, run_number,
                         n_showers, total_particles, thinned, has_markers

        ``kind`` selects which groups to write: ``"particles"``,
        ``"showers"``, or ``"both"`` (default).
        """
        try:
            import h5py
        except ImportError as exc:  # pragma: no cover
            raise ImportError(
                "to_hdf5() requires h5py. Install it with: pip install corsario[hdf5]"
            ) from exc
        import numpy as np

        with h5py.File(path, "w") as f:
            f.attrs["corsario_version"] = self.version
            f.attrs["filename"] = self.filename
            f.attrs["run_number"] = self.run_number
            f.attrs["n_showers"] = self.n_showers()
            f.attrs["total_particles"] = self.total_particles()
            f.attrs["thinned"] = bool(self.is_thinned)
            f.attrs["has_markers"] = bool(self.has_markers)

            if kind in ("particles", "both"):
                g = f.create_group("particles")
                cols = {
                    "shower": np.asarray(self.flat_shower_index(), dtype="i4"),
                    "particle_id": np.asarray(self.flat_particle_id(), dtype="i8"),
                    "no_particle": np.asarray(self.flat_particle_no(), dtype="i4"),
                    "hadr_gen": np.asarray(self.flat_hadron_gen(), dtype="i4"),
                    "no_obs": np.asarray(self.flat_obs_level(), dtype="i4"),
                    "mass": np.asarray(self.flat_mass(), dtype="f4"),
                    "energy": np.asarray(self.flat_energy(), dtype="f4"),
                    "px": np.asarray(self.flat_px(), dtype="f4"),
                    "py": np.asarray(self.flat_py(), dtype="f4"),
                    "pz": np.asarray(self.flat_pz(), dtype="f4"),
                    "x": np.asarray(self.flat_x(), dtype="f4"),
                    "y": np.asarray(self.flat_y(), dtype="f4"),
                    "t": np.asarray(self.flat_t(), dtype="f4"),
                    "weight": np.asarray(self.flat_weight(), dtype="f4"),
                    "particle_type": np.asarray(self.flat_particle_type(), dtype=h5py.string_dtype()),
                }
                for name, arr in cols.items():
                    kwargs = {"compression": compression} if arr.size else {}
                    g.create_dataset(name, data=arr, **kwargs)

            if kind in ("showers", "both"):
                g = f.create_group("showers")
                showers = self.showers()
                n = len(showers)
                fields = {
                    "event_no": "f4",
                    "primary_id": "f4",
                    "total_energy_gev": "f4",
                    "starting_altitude_gcm2": "f4",
                    "z_first_interaction_cm": "f4",
                    "px_momentum": "f4",
                    "py_momentum": "f4",
                    "pz_momentum": "f4",
                    "zenith_rad": "f4",
                    "azimuth_rad": "f4",
                    "n_photons_at_obs": "f4",
                    "n_electrons_at_obs": "f4",
                    "n_hadrons_at_obs": "f4",
                    "n_muons_at_obs": "f4",
                }
                arrays = {name: np.empty(n, dtype=dt) for name, dt in fields.items()}
                n_particles = np.empty(n, dtype="i4")
                for i, sh in enumerate(showers):
                    for name in fields:
                        arrays[name][i] = getattr(sh, name)
                    n_particles[i] = sh.n_particles()
                for name, arr in arrays.items():
                    g.create_dataset(name, data=arr, compression=compression if n else None)
                g.create_dataset("n_particles", data=n_particles, compression=compression if n else None)


def open(path: str, strict: bool = True) -> Corsario:
    """Open a CORSIKA .DAT file and return a :class:`Corsario` object.

    Equivalent to ``Corsario(path, strict=strict)``; provided as a module
    level function so ``with corsario.open(path) as run:`` reads naturally.

    Loads every shower's particles into memory at once -- for files with
    up to a few million particles this is simplest and fastest. For files
    with millions of *showers* (very large campaigns), use
    :func:`iter_shower_batches` instead; see docs/LARGE_FILES.md.
    """
    return Corsario(path, strict=strict)


def iter_shower_batches(path: str, batch_size: int = 50_000, strict: bool = True):
    """Stream a CORSIKA .DAT file in batches of ``batch_size`` showers.

    Yields ``(df_particles, df_shower)`` pairs, one per batch, with the
    exact same columns as ``Corsario.to_dataframe("particles"/"showers")``
    -- but never holds more than one batch's particles in memory at once,
    which is what makes this usable on files with millions of showers
    (see docs/LARGE_FILES.md for memory budgeting and
    ``examples/04_streaming_large_files.py`` for a full example, including
    building a compact per-shower feature dataset without ever
    materializing the full particle table).

    Parameters
    ----------
    path:
        Path to the CORSIKA .DAT file.
    batch_size:
        Number of showers to parse per batch. Larger batches are slightly
        more CPU-efficient (fixed per-batch overhead is amortized further)
        at the cost of more peak memory; 10,000-100,000 is a reasonable
        starting range for typical (non-thinned) shower sizes.
    strict:
        See :class:`Corsario`.
    """
    import pandas as pd

    stream = ShowerStream(path, strict)
    while not stream.done():
        batch = stream.next_batch(batch_size)
        if not batch:
            break
        df_particles = pd.DataFrame(_particles_dict(batch), columns=_PARTICLE_COLUMNS)
        df_shower = pd.DataFrame(_shower_headers_dict(batch))
        yield df_particles, df_shower
