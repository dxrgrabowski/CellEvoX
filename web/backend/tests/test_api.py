"""API contract tests for the FastAPI bridge.

The runner and parser globals are replaced with fixtures rooted at `tmp_path` so
no real binary is executed and no repository-wide scan happens.
"""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

import main
from results_parser import ResultsParser
from runner import SimulationRunner


@pytest.fixture
def client(tmp_path, monkeypatch):
    monkeypatch.setattr(main, "results_parser", ResultsParser(repo_root=tmp_path))
    monkeypatch.setattr(main, "runner", SimulationRunner(repo_root=tmp_path))
    return TestClient(main.app)


def test_config_schema_exposes_only_supported_modes(client):
    response = client.get("/api/config/schema")

    assert response.status_code == 200
    modes = response.json()["core"]["simulation_mode"]["values"]
    assert modes == ["stochastic", "spatial_3d_capacity"]


def test_results_endpoints_read_a_fixture_run(client, make_run):
    make_run(
        "run-a",
        stats_rows=[
            {"Tau": 0.5, "TotalLivingCells": 120, "MeanFitness": 1.02},
            {"Tau": 1.0, "TotalLivingCells": 150, "MeanFitness": 1.10},
        ],
        populations={1: ["", "(1,0)"]},
    )

    listing = client.get("/api/results")
    assert listing.status_code == 200
    assert [run["id"] for run in listing.json()] == ["run-a"]

    config = client.get("/api/results/run-a/config")
    assert config.status_code == 200
    assert config.json()["simulation_mode"] == "stochastic"

    summary = client.get("/api/results/run-a/summary")
    assert summary.status_code == 200
    assert summary.json()["final_population"] == 150

    stats = client.get("/api/results/run-a/stats")
    assert stats.status_code == 200
    assert "Tau" in stats.json()["columns"]


def test_muller_endpoint_returns_driver_signatures(client, make_run):
    make_run(
        "run-a",
        config={
            "simulation_mode": "stochastic",
            "mutations": [{"id": 0, "is_driver": True, "effect": 0.1, "probability": 0.01}],
        },
        populations={1: ["", "(1,0)", "(2,0)"]},
    )

    response = client.get("/api/results/run-a/muller")

    assert response.status_code == 200
    data = response.json()
    assert data["driver_mutation_ids"] == [0]
    assert data["populations"]["Step"]


@pytest.mark.parametrize(
    "suffix",
    ["config", "summary", "stats", "muller", "population"],
)
def test_unknown_run_returns_404(client, suffix):
    assert client.get(f"/api/results/unknown/{suffix}").status_code == 404


def test_batch_start_rejects_unsupported_postprocess(client, valid_config):
    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "csv"},
    )

    assert response.status_code == 400


def test_batch_start_rejects_non_numeric_steps(client, valid_config):
    valid_config["steps"] = "many"

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400


def test_batch_start_rejects_unreachable_statistics_resolution(client, valid_config):
    valid_config["steps"] = 1000  # 1000 * 0.005 = 5 < 10

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "statistics_resolution" in response.json()["detail"]


def test_start_conflicts_when_runner_is_busy(client, monkeypatch, valid_config):
    monkeypatch.setattr(main.runner, "is_running", lambda: True)

    response = client.post("/api/simulation/start", json={"config": valid_config})

    assert response.status_code == 409
