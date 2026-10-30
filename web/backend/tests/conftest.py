"""Shared fixtures for backend tests.

The backend modules are imported as top-level modules (`import main`), matching
how `uvicorn main:app` runs them, so the backend directory is on `sys.path`.
"""

from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

import pytest

BACKEND_DIR = Path(__file__).resolve().parents[1]
if str(BACKEND_DIR) not in sys.path:
    sys.path.insert(0, str(BACKEND_DIR))


DEFAULT_STATS_ROWS = [
    {"Tau": 0.5, "TotalLivingCells": 120, "MeanFitness": 1.02},
    {"Tau": 1.0, "TotalLivingCells": 150, "MeanFitness": 1.10},
]


@pytest.fixture
def make_run(tmp_path):
    """Create a CellEvoX-style run directory under `tmp_path`."""

    def _make(
        name: str = "run-a",
        *,
        config: dict | None = None,
        config_text: str | None = None,
        stats_rows: list[dict] | None = None,
        stats_text: str | None = None,
        populations: dict[int, list[str]] | None = None,
    ) -> Path:
        run_dir = tmp_path / name
        run_dir.mkdir(parents=True)

        if config_text is not None:
            (run_dir / "config.json").write_text(config_text)
        else:
            payload = {
                "simulation_mode": "stochastic",
                "steps": 4000,
                "tau_step": 0.005,
                "mutations": [],
            }
            if config is not None:
                payload = config
            (run_dir / "config.json").write_text(json.dumps(payload))

        if stats_text is not None:
            stats_dir = run_dir / "statistics"
            stats_dir.mkdir()
            (stats_dir / "generational_statistics.csv").write_text(stats_text)
        elif stats_rows is not None:
            stats_dir = run_dir / "statistics"
            stats_dir.mkdir()
            with open(stats_dir / "generational_statistics.csv", "w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=list(stats_rows[0].keys()))
                writer.writeheader()
                writer.writerows(stats_rows)

        if populations:
            population_dir = run_dir / "population_data"
            population_dir.mkdir()
            for generation, mutations in populations.items():
                path = population_dir / f"population_generation_{generation}.csv"
                with open(path, "w", newline="") as handle:
                    writer = csv.writer(handle)
                    writer.writerow(["CellID", "Mutations"])
                    for index, mutation in enumerate(mutations, start=1):
                        writer.writerow([index, mutation])

        return run_dir

    return _make


@pytest.fixture
def valid_config() -> dict:
    return {
        "simulation_mode": "stochastic",
        "steps": 4000,
        "tau_step": 0.005,
        "statistics_resolution": 10,
        "population_statistics_res": 10,
    }
