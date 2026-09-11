# API reference

## `corsario.open(path, strict=True) -> Corsario`

Open and fully parse a CORSIKA `.DAT` file. Equivalent to
`Corsario(path, strict=strict)`. Supports use as a context manager:

```python
with corsario.open("DAT000001") as run:
    ...
```

`strict=True` (default) raises `RuntimeError` on truncated/malformed input.
`strict=False` returns a best-effort partial parse; check `run.warnings`.

## `class Corsario`

| Member | Description |
|---|---|
| `.filename` | path passed to `open()` |
| `.version` | corsario library version string |
| `.format` | `FormatInfo`: `.has_markers`, `.thinning`, `.block_words`, `.words_per_particle` |
| `.is_thinned` | shortcut for `.format.thinning` |
| `.has_markers` | shortcut for `.format.has_markers` |
| `.run_number` | run number from the `RUNH` sub-block |
| `.n_events_processed` | event count reported in the `RUNE` sub-block |
| `.warnings` | list of `str`; non-empty only in `strict=False` mode (or if the run has no `RUNE`) |
| `.n_showers()` | number of complete showers parsed |
| `.total_particles()` | total particle count across all showers |
| `.shower(i)` / `run[i]` | `Shower` at index `i` (0-based, file order) |
| `.showers()` | list of all `Shower` objects |
| `.to_csv(path)` | write `particle_id,px,py,pz,x,y,t` |
| `.to_csv_ext(path)` | write the extended particle CSV (see below) |
| `.shower_csv(path)` | write one row per shower (event header fields) |
| `.to_dataframe(kind="particles")` | `pandas.DataFrame`; `kind` is `"particles"` or `"showers"` |
| `.to_json(path, kind="particles", orient="records", indent=2)` | JSON export; see below |
| `.to_hdf5(path, kind="both", compression="gzip")` | HDF5 export (requires `h5py`) |
| `.flat_*()` | flat (all-showers) NumPy-friendly accessors, e.g. `.flat_energy()`, `.flat_px()` — used internally by the exporters, also handy for quick vectorized analysis without pandas |

### CSV columns

```
to_csv:      particle_id,px,py,pz,x,y,t
to_csv_ext:  shower,particle_type,no_particle,hadr_gen,no_obs,mass,energy,
             particle_id,px,py,pz,x,y,t,weight
shower_csv:  event_no,particle_id,total_energy,altitude,no_target,z,
             px,py,pz,zenith,azimuth
```

These match the historical corsario output exactly (see the repository
`CHANGELOG.md` for the one addition: the trailing `weight` column).

### JSON export

- `orient="records"` (default): a flat JSON array of row objects — the same
  data as `to_dataframe(kind).to_dict("records")`. Read back with
  `pandas.read_json(path, orient="records")`.
- `orient="nested"` (particles only): one object per shower, with its
  header fields and an embedded `"particles"` list. More verbose, but keeps
  the shower/particle relationship in a single self-describing file.

### HDF5 export

Writes `/particles/<column>` and/or `/showers/<column>` as 1D datasets
(gzip-compressed by default), plus run-level metadata as root attributes
(`corsario_version`, `filename`, `run_number`, `n_showers`,
`total_particles`, `thinned`, `has_markers`). Requires `pip install
"corsario[hdf5]"`.

## `class Shower`

| Member | Description |
|---|---|
| `.index` | 1-based shower number, in file order |
| `.event_no`, `.primary_id`, `.total_energy_gev`, `.starting_altitude_gcm2`, `.first_target_no`, `.z_first_interaction_cm`, `.px_momentum`, `.py_momentum`, `.pz_momentum`, `.zenith_rad`, `.azimuth_rad` | `EVTH` fields |
| `.has_event_end`, `.n_photons_at_obs`, `.n_electrons_at_obs`, `.n_hadrons_at_obs`, `.n_muons_at_obs`, `.n_particles_written` | `EVTE` fields |
| `.has_longitudinal_blocks` | `True` if the run used `LONGI` for this shower |
| `.thinned` | `True` if this shower's particle records include a weight |
| `.n_particles()` / `len(shower)` | particle count |
| `.particle(i)` / `shower[i]` | `Particle` at index `i` |
| `.particles()` | list of all `Particle` objects (copies — for bulk work prefer `Corsario.flat_*()`) |
| `.header_csv_row()` | the row `shower_csv` writes for this shower |

## `class Particle`

`.particle_id`, `.particle_no`, `.particle_type`, `.hadron_generation`,
`.observation_level`, `.mass`, `.energy`, `.px`, `.py`, `.pz`, `.momentum`
(tuple), `.x`, `.y`, `.time`, `.weight`.

## Command-line tools

See `README.md#command-line-tools` and `docs/TROUBLESHOOTING.md`.
