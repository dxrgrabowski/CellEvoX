#!/usr/bin/env python3
"""Cross-language contract tests for the CELXPOP1 population snapshot ABI.

The checked-in fixtures under ``CellEvoX/tests/fixtures/mini_run`` are the canonical
binary snapshots. The C++ writer produces them (see ``tests/test_population_snapshot_io.cpp``
for the reader-side assertions) and the companion CSV files were exported from the same
binaries by ``CellEvoX --analyze``. This module pins the Python reader to the same bytes
and cross-checks it against that CSV export.

Run directly (no pytest dependency required):
    python3 CellEvoX/scripts/test_snapshot_io.py
"""

import math
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pandas as pd

import snapshot_io

FIXTURE_ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "tests",
    "fixtures",
    "mini_run",
)

# Mirror of the C++ static_assert values in include/io/PopulationSnapshotIO.hpp.
HEADER_SIZE = 48
RECORD_SIZE = 36
MUTATION_RECORD_SIZE = 5

FLAG_DRIVER_PAYLOAD = 0x1
FLAG_FULL_PAYLOAD = 0x2


def _population_data(name: str) -> str:
    return os.path.join(FIXTURE_ROOT, "population_data", name)


def _invalid(name: str) -> str:
    return os.path.join(FIXTURE_ROOT, "invalid", name)


def _read_header(path: str):
    with open(path, "rb") as handle:
        raw = handle.read(HEADER_SIZE)
    return snapshot_io._HEADER_STRUCT.unpack(raw)


def _load_bin_frame(generation: int, path: str, driver_type_ids):
    source = snapshot_io.PopulationFrameSource(generation=generation, path=Path(path), kind="bin")
    return snapshot_io.load_population_frame(source, driver_type_ids)


def _load_csv_frame(generation: int, path: str, driver_type_ids):
    source = snapshot_io.PopulationFrameSource(generation=generation, path=Path(path), kind="csv")
    return snapshot_io.load_population_frame(source, driver_type_ids)


class StructSizesMatchContract(unittest.TestCase):
    def test_python_struct_sizes_match_cpp_static_asserts(self):
        self.assertEqual(snapshot_io._HEADER_STRUCT.size, HEADER_SIZE)
        self.assertEqual(snapshot_io._RECORD_STRUCT.size, RECORD_SIZE)
        self.assertEqual(snapshot_io._DRIVER_MUTATION_STRUCT.size, MUTATION_RECORD_SIZE)


class DriverOnlySnapshotFixture(unittest.TestCase):
    def setUp(self):
        self.path = _population_data("population_generation_5.bin")
        self.header = _read_header(self.path)

    def test_header_fields(self):
        (
            magic,
            version,
            record_size,
            tau,
            record_count,
            driver_mutation_count,
            spatial_dimensions,
            mutation_record_size,
            flags,
        ) = self.header

        self.assertEqual(magic, snapshot_io.SNAPSHOT_MAGIC)
        self.assertEqual(version, snapshot_io.SNAPSHOT_VERSION)
        self.assertEqual(record_size, RECORD_SIZE)
        self.assertEqual(mutation_record_size, MUTATION_RECORD_SIZE)
        self.assertEqual(tau, 5.0)
        self.assertEqual(record_count, 3)
        self.assertEqual(driver_mutation_count, 4)
        self.assertEqual(spatial_dimensions, 0)
        self.assertEqual(flags, FLAG_DRIVER_PAYLOAD)
        self.assertEqual(os.path.getsize(self.path), HEADER_SIZE + 3 * RECORD_SIZE + 4 * MUTATION_RECORD_SIZE)

    def test_reader_fields(self):
        frame = _load_bin_frame(5, self.path, {1, 2})
        data = frame.data

        self.assertEqual(frame.generation, 5)
        self.assertEqual(frame.tau, 5.0)
        self.assertEqual(frame.spatial_dimensions, 0)
        self.assertEqual(data["CellID"].tolist(), [0, 1, 2])
        self.assertEqual(data["ParentID"].tolist(), [0, 0, 1])
        self.assertEqual(data["MutationCount"].tolist(), [1, 2, 1])
        self.assertEqual(
            data["Mutations"].tolist(),
            ["(0,1)", "(0,1) (1,2)", "(0,1)"],
        )
        self.assertEqual(data["PositionValid"].tolist(), [0, 0, 0])
        self.assertTrue(all(math.isnan(value) for value in data["X"]))
        self.assertTrue(all(math.isnan(value) for value in data["Y"]))
        self.assertTrue(all(math.isnan(value) for value in data["Z"]))
        self.assertEqual(data["CloneSignature"].tolist(), ["0", "0,1", "0"])

    def test_matches_csv_export(self):
        bin_frame = _load_bin_frame(5, self.path, {1, 2})
        csv_frame = _load_csv_frame(5, _population_data("population_generation_5.csv"), {1, 2})
        pd.testing.assert_frame_equal(bin_frame.data, csv_frame.data, check_dtype=False)


class FullPayloadSnapshotFixture(unittest.TestCase):
    def setUp(self):
        self.path = _population_data("population_generation_10.bin")
        self.header = _read_header(self.path)

    def test_header_fields(self):
        (
            magic,
            version,
            record_size,
            tau,
            record_count,
            driver_mutation_count,
            spatial_dimensions,
            mutation_record_size,
            flags,
        ) = self.header

        self.assertEqual(magic, snapshot_io.SNAPSHOT_MAGIC)
        self.assertEqual(version, snapshot_io.SNAPSHOT_VERSION)
        self.assertEqual(record_size, RECORD_SIZE)
        self.assertEqual(mutation_record_size, MUTATION_RECORD_SIZE)
        self.assertEqual(tau, 10.0)
        self.assertEqual(record_count, 2)
        self.assertEqual(driver_mutation_count, 4)
        self.assertEqual(spatial_dimensions, 3)
        self.assertEqual(flags, FLAG_FULL_PAYLOAD)
        self.assertEqual(os.path.getsize(self.path), HEADER_SIZE + 2 * RECORD_SIZE + 4 * MUTATION_RECORD_SIZE)

    def test_reader_fields(self):
        frame = _load_bin_frame(10, self.path, {1, 2, 3, 4})
        data = frame.data

        self.assertEqual(frame.generation, 10)
        self.assertEqual(frame.tau, 10.0)
        self.assertEqual(frame.spatial_dimensions, 3)
        self.assertEqual(data["CellID"].tolist(), [7, 8])
        self.assertEqual(data["ParentID"].tolist(), [3, 7])
        self.assertEqual(data["Fitness"].tolist(), [1.5, 0.875])
        self.assertEqual(
            data["Mutations"].tolist(),
            ["(3,1) (7,3)", "(3,1) (8,4)"],
        )
        self.assertEqual(data["PositionValid"].tolist(), [1, 1])
        self.assertEqual(data["X"].tolist(), [1.5, 4.0])
        self.assertEqual(data["Y"].tolist(), [2.25, 5.5])
        self.assertEqual(data["Z"].tolist(), [3.0, 6.25])
        self.assertEqual(data["CloneSignature"].tolist(), ["3,7", "3,8"])

    def test_matches_csv_export(self):
        bin_frame = _load_bin_frame(10, self.path, {1, 2, 3, 4})
        csv_frame = _load_csv_frame(10, _population_data("population_generation_10.csv"), {1, 2, 3, 4})
        pd.testing.assert_frame_equal(bin_frame.data, csv_frame.data, check_dtype=False)


class InvalidSnapshotFixtures(unittest.TestCase):
    def test_truncated_v2_is_rejected(self):
        path = _invalid("truncated_v2.bin")
        self.assertEqual(os.path.getsize(path), HEADER_SIZE)
        with self.assertRaises(ValueError):
            _load_bin_frame(5, path, set())

    def test_unknown_version_is_rejected(self):
        path = _invalid("unknown_version.bin")
        self.assertEqual(os.path.getsize(path), HEADER_SIZE)
        header = _read_header(path)
        self.assertEqual(header[0], snapshot_io.SNAPSHOT_MAGIC)
        self.assertEqual(header[1], 99)
        with self.assertRaises(ValueError):
            _load_bin_frame(5, path, set())


class FixtureDiscovery(unittest.TestCase):
    def test_prefers_csv_unless_bin_requested(self):
        csv_sources = snapshot_io.discover_population_sources(FIXTURE_ROOT)
        self.assertEqual([source.kind for source in csv_sources], ["csv", "csv"])
        self.assertEqual([source.generation for source in csv_sources], [5, 10])

        bin_sources = snapshot_io.discover_population_sources(FIXTURE_ROOT, prefer_bin=True)
        self.assertEqual([source.kind for source in bin_sources], ["bin", "bin"])
        self.assertEqual([source.generation for source in bin_sources], [5, 10])


if __name__ == "__main__":
    unittest.main()
