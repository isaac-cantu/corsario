"""pytest suite for corsario.

Run with: pytest  (from the repository root, with corsario installed:
`pip install -e .`).
"""
from __future__ import annotations

import json

import numpy as np
import pandas as pd
import pytest

import corsario
from make_synthetic import ShowerSpec, build_dat, default_showers, showers_with_long_blocks


@pytest.fixture()
def normal_dat(tmp_path):
    path = tmp_path / "DAT_NORMAL"
    gt = build_dat(str(path), default_showers(), thin=False, markers=True)
    return str(path), gt


@pytest.fixture(params=[
    dict(thin=False, markers=False),
    dict(thin=True, markers=True),
    dict(thin=True, markers=False),
])
def format_variant_dat(tmp_path, request):
    path = tmp_path / "DAT_VARIANT"
    showers = [ShowerSpec(1, 7), ShowerSpec(2, 50), ShowerSpec(3, 2500)]
    gt = build_dat(str(path), showers, **request.param)
    return str(path), gt, request.param


@pytest.fixture()
def long_block_dat(tmp_path):
    path = tmp_path / "DAT_LONG"
    gt = build_dat(str(path), showers_with_long_blocks(), thin=False, markers=True)
    return str(path), gt


# ---------------------------------------------------------------------------

def test_basic_counts(normal_dat):
    path, gt = normal_dat
    run = corsario.open(path)
    assert run.n_showers() == len(gt)
    assert run.total_particles() == sum(gt.values())
    for sh, (event_no, n) in zip(run.showers(), gt.items()):
        assert sh.n_particles() == n
        assert int(sh.event_no) == event_no


def test_large_shower_spanning_multiple_superblocks(normal_dat):
    """Regression test for the original 'fails after N particles' bug:
    shower 3 has 5000 particles, spanning multiple 21-block super-blocks,
    which is exactly the scenario the fragile fixed-stride arithmetic in
    the previous implementation could desynchronize on."""
    path, gt = normal_dat
    run = corsario.open(path)
    shower3 = run.shower(2)  # 0-indexed
    assert int(shower3.event_no) == 3
    assert shower3.n_particles() == 5000
    # spot-check a few individual particle field values are sane, not garbage
    p0 = shower3.particle(0)
    assert p0.particle_id == 6003
    last = shower3.particle(shower3.n_particles() - 1)
    assert last.particle_id in (6003, 6004, 6005, 6006, 6007)


@pytest.mark.parametrize("all_variants", [True])
def test_format_auto_detection(format_variant_dat, all_variants):
    path, gt, params = format_variant_dat
    run = corsario.open(path)
    assert run.format.thinning == params["thin"]
    assert run.format.has_markers == params["markers"]
    assert run.n_showers() == len(gt)
    assert run.total_particles() == sum(gt.values())


def test_long_blocks_are_skipped_not_counted_as_particles(long_block_dat):
    path, gt = long_block_dat
    run = corsario.open(path)
    assert run.total_particles() == sum(gt.values())
    for sh in run.showers():
        assert sh.has_longitudinal_blocks is True


def test_truncated_file_strict_raises(tmp_path, normal_dat):
    path, _ = normal_dat
    data = open(path, "rb").read()
    truncated = tmp_path / "DAT_TRUNC"
    truncated.write_bytes(data[: len(data) // 2])
    with pytest.raises(RuntimeError):
        corsario.open(str(truncated), strict=True)


def test_truncated_file_tolerant_mode_recovers_partial_data(tmp_path, normal_dat):
    path, _ = normal_dat
    data = open(path, "rb").read()
    truncated = tmp_path / "DAT_TRUNC"
    truncated.write_bytes(data[: len(data) // 2])
    run = corsario.open(str(truncated), strict=False)
    assert run.n_showers() >= 1
    assert len(run.warnings) > 0


def test_csv_columns_match_historical_schema(normal_dat, tmp_path):
    path, _ = normal_dat
    run = corsario.open(path)

    basic = tmp_path / "particles.csv"
    run.to_csv(str(basic))
    header = basic.read_text().splitlines()[0]
    assert header == "particle_id,px,py,pz,x,y,t"

    ext = tmp_path / "particles_ext.csv"
    run.to_csv_ext(str(ext))
    header_ext = ext.read_text().splitlines()[0]
    assert header_ext == (
        "shower,particle_type,no_particle,hadr_gen,no_obs,mass,energy,"
        "particle_id,px,py,pz,x,y,t,weight"
    )

    shower_csv = tmp_path / "shower.csv"
    run.shower_csv(str(shower_csv))
    header_sh = shower_csv.read_text().splitlines()[0]
    assert header_sh == (
        "event_no,particle_id,total_energy,altitude,no_target,z,px,py,pz,zenith,azimuth"
    )


def test_dataframe_export(normal_dat):
    path, gt = normal_dat
    run = corsario.open(path)
    df = run.to_dataframe("particles")
    assert len(df) == sum(gt.values())
    assert list(df.columns)[:2] == ["shower", "particle_type"]

    df_showers = run.to_dataframe("showers")
    assert len(df_showers) == len(gt)


def test_json_export_records(normal_dat, tmp_path):
    path, gt = normal_dat
    run = corsario.open(path)
    out = tmp_path / "particles.json"
    run.to_json(str(out))
    rows = json.loads(out.read_text())
    assert len(rows) == sum(gt.values())
    assert rows[0]["particle_id"] == 6003


def test_json_export_nested(normal_dat, tmp_path):
    path, gt = normal_dat
    run = corsario.open(path)
    out = tmp_path / "particles_nested.json"
    run.to_json(str(out), orient="nested")
    payload = json.loads(out.read_text())
    assert payload["meta"]["n_showers"] == len(gt)
    assert sum(len(s["particles"]) for s in payload["showers"]) == sum(gt.values())


def test_hdf5_export(normal_dat, tmp_path):
    h5py = pytest.importorskip("h5py")
    path, gt = normal_dat
    run = corsario.open(path)
    out = tmp_path / "particles.h5"
    run.to_hdf5(str(out))
    with h5py.File(out, "r") as f:
        assert f["particles/x"].shape[0] == sum(gt.values())
        assert f.attrs["n_showers"] == len(gt)


def test_weight_defaults_to_one_when_not_thinned(normal_dat):
    path, _ = normal_dat
    run = corsario.open(path)
    weights = run.flat_weight()
    assert all(w == 1.0 for w in weights)


def test_weight_is_read_when_thinned(tmp_path):
    path = tmp_path / "DAT_THIN"
    gt = build_dat(str(path), [ShowerSpec(1, 45)], thin=True, markers=True)
    run = corsario.open(str(path))
    assert run.is_thinned is True
    weights = run.flat_weight()
    assert len(weights) == gt[1]
    assert all(w == 0.5 for w in weights)


# ---------------------------------------------------------------------------
# Streaming (large-file) API
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("batch_size", [1, 2, 3, 1000])
def test_iter_shower_batches_matches_eager_reading(normal_dat, batch_size):
    """The whole point of the streaming API is that it must produce
    identical results to the eager reader, batch size and all -- this is
    the regression test for that guarantee, including the pathological
    batch_size=1 case (one shower per batch)."""
    path, gt = normal_dat

    parts, showers = [], []
    for df_p, df_s in corsario.iter_shower_batches(path, batch_size=batch_size):
        parts.append(df_p)
        showers.append(df_s)
    df_particles_stream = pd.concat(parts, ignore_index=True)
    df_shower_stream = pd.concat(showers, ignore_index=True)

    run = corsario.open(path)
    df_particles_ref = run.to_dataframe("particles")
    df_shower_ref = run.to_dataframe("showers")

    assert list(df_particles_stream.columns) == list(df_particles_ref.columns)
    assert list(df_shower_stream.columns) == list(df_shower_ref.columns)
    assert len(df_particles_stream) == sum(gt.values())

    numeric_cols = df_particles_ref.select_dtypes(include="number").columns
    assert np.allclose(df_particles_stream[numeric_cols].values,
                        df_particles_ref[numeric_cols].values)
    assert df_shower_stream.equals(df_shower_ref)


def test_iter_shower_batches_respects_batch_size(normal_dat):
    path, gt = normal_dat
    batch_shower_counts = [len(df_s) for _, df_s in corsario.iter_shower_batches(path, batch_size=2)]
    assert sum(batch_shower_counts) == len(gt)
    assert all(c <= 2 for c in batch_shower_counts)


def test_shower_stream_low_level_api(normal_dat):
    path, gt = normal_dat
    stream = corsario.ShowerStream(path)
    assert not stream.done()
    total = 0
    while not stream.done():
        batch = stream.next_batch(1)
        if not batch:
            break
        total += len(batch)
    assert total == len(gt)
    assert stream.n_events_processed == len(gt)


def test_csv_streaming_cli_matches_eager(normal_dat, tmp_path):
    """corsario-convert --stream must write the same data as the default
    (eager) path -- see docs/LARGE_FILES.md."""
    from corsario.cli import convert_main

    path, gt = normal_dat
    streamed = tmp_path / "streamed.csv"
    eager = tmp_path / "eager.csv"
    convert_main([path, str(streamed), "--stream", "--batch-size", "3"])
    convert_main([path, str(eager)])

    df_stream = pd.read_csv(streamed)
    df_eager = pd.read_csv(eager)
    numeric_cols = df_eager.select_dtypes(include="number").columns
    assert np.allclose(df_stream[numeric_cols].values, df_eager[numeric_cols].values)
