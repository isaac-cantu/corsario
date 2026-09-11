"""Generate small synthetic CORSIKA ".DAT"-like files with known ground truth.

Used by the test suite so it does not depend on having a real CORSIKA
installation or real DAT files available. Covers the structurally distinct
cases corsario has to auto-detect:

  - with / without Fortran record-length markers
  - with / without the THIN option (312-word blocks, 8-word particles)
  - with / without interleaved 'LONG' (longitudinal profile) sub-blocks

Not a CORSIKA-output-bit-exact fixture — field values are arbitrary but
consistent, only the *block/record structure* matters for these tests.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple


def _tag(s: str) -> bytes:
    assert len(s) == 4
    return s.encode("ascii")


def _f(x: float) -> bytes:
    return struct.pack("<f", float(x))


class _Writer:
    def __init__(self, block_words: int, markers: bool):
        self.block_words = block_words
        self.markers = markers
        self._words: List[bytes] = []
        self._block_count = 0

    def _pad_if_needed(self) -> None:
        if self.markers and self._block_count > 0 and self._block_count % 21 == 0:
            self._words.append(_f(0.0))
            self._words.append(_f(0.0))

    def write_block(self, words: List[bytes]) -> None:
        assert len(words) <= self.block_words
        self._pad_if_needed()
        block = list(words)
        while len(block) < self.block_words:
            block.append(_f(0.0))
        self._words.extend(block)
        self._block_count += 1

    def to_bytes(self) -> bytes:
        body = b"".join(self._words)
        if self.markers:
            return _f(0.0) + body + _f(0.0)
        return body


def _evth(event_no: int) -> List[bytes]:
    vals = [event_no, 14, 1.0e6, 112.8, 0, 0, 0, 0, -1, 0.1, 0.2]
    return [_tag("EVTH")] + [_f(v) for v in vals]


def _evte(event_no: int, n_written: int) -> List[bytes]:
    vals = [event_no, 0, 0, 0, n_written]
    return [_tag("EVTE")] + [_f(v) for v in vals]


def _long(event_no: int, block_no: int) -> List[bytes]:
    vals = [event_no, 14, 1.0e6, 2600, block_no] + [0] * 8
    return [_tag("LONG")] + [_f(v) for v in vals]


def _runh() -> List[bytes]:
    return [_tag("RUNH")] + [_f(v) for v in [1] + [0] * 10]


def _rune(n_events: int) -> List[bytes]:
    return [_tag("RUNE")] + [_f(v) for v in [1, n_events, 0]]


@dataclass
class ShowerSpec:
    event_no: int
    n_particles: int
    n_long_blocks: int = 0


def write_shower(w: _Writer, spec: ShowerSpec, words_per_particle: int, id_base: int = 6003) -> int:
    w.write_block(_evth(spec.event_no))
    idx = 0
    remaining = spec.n_particles
    while remaining > 0:
        n_this = min(39, remaining)
        words: List[bytes] = []
        for _ in range(n_this):
            pid = id_base + (idx % 5)
            rec = [pid, 0.1 * idx, 0.2 * idx, 1.0 + 0.01 * idx, 10.0 * idx, -5.0 * idx, 100.0 + idx]
            if words_per_particle == 8:
                rec.append(0.5)  # weight
            words.extend(_f(v) for v in rec)
            idx += 1
        w.write_block(words)
        remaining -= n_this
    for b in range(spec.n_long_blocks):
        w.write_block(_long(spec.event_no, b + 1))
    w.write_block(_evte(spec.event_no, spec.n_particles))
    return idx


def build_dat(
    path: str,
    showers: List[ShowerSpec],
    thin: bool = False,
    markers: bool = True,
) -> dict:
    """Write a synthetic DAT file to `path`; returns {event_no: n_particles}."""
    block_words = 312 if thin else 273
    wpp = 8 if thin else 7
    w = _Writer(block_words, markers)
    w.write_block(_runh())
    ground_truth = {}
    for spec in showers:
        ground_truth[spec.event_no] = write_shower(w, spec, wpp)
    w.write_block(_rune(len(showers)))
    Path(path).write_bytes(w.to_bytes())
    return ground_truth


# Convenience presets used by the test suite -------------------------------

def default_showers() -> List[ShowerSpec]:
    return [
        ShowerSpec(1, 5),
        ShowerSpec(2, 39),          # exactly one full data sub-block
        ShowerSpec(3, 5000),        # spans several super-blocks (21-block groups)
        ShowerSpec(4, 10),
    ]


def showers_with_long_blocks() -> List[ShowerSpec]:
    return [
        ShowerSpec(1, 5, n_long_blocks=2),
        ShowerSpec(2, 39, n_long_blocks=3),
        ShowerSpec(3, 6000, n_long_blocks=5),
        ShowerSpec(4, 10, n_long_blocks=1),
    ]
