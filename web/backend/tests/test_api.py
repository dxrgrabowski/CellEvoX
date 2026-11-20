"""API contract tests for the FastAPI bridge.

The runner and parser globals are replaced with fixtures rooted at `tmp_path` so
no real binary is executed and no repository-wide scan happens.
"""

from __future__ import annotations

import json

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


def test_batch_start_rejects_gamma_limit_below_mean(client, valid_config):
    valid_config["mutations"] = [
        {
            "id": 0,
            "is_driver": True,
            "probability": 0.01,
            "effect_mode": "gamma",
            "effect_mean": 0.2,
            "effect_std": 0.1,
            "effect_direction": "positive",
            "effect_limit": 0.1,
        }
    ]

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "effect_limit" in response.json()["detail"]


def test_batch_start_rejects_neutral_tolerance_out_of_range(client, valid_config):
    valid_config["mutation_effect_neutral_tolerance"] = 1.0

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "mutation_effect_neutral_tolerance" in response.json()["detail"]


def test_batch_start_rejects_null_mutation_list(client, valid_config):
    valid_config["mutations"] = None

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "mutations" in response.json()["detail"]


def _post_batch_raw(client, payload):
    """Post a hand-crafted body: httpx refuses NaN/Infinity, client libraries do not."""
    return client.post(
        "/api/simulation/batch/start",
        content=json.dumps(payload),
        headers={"Content-Type": "application/json"},
    )


@pytest.mark.parametrize("bad_value", [float("nan"), float("inf")])
def test_batch_start_rejects_non_finite_gamma_fields(client, valid_config, bad_value):
    valid_config["mutations"] = [
        {
            "id": 0,
            "is_driver": True,
            "probability": 0.01,
            "effect_mode": "gamma",
            "effect_mean": bad_value,
            "effect_std": 0.02,
            "effect_direction": "positive",
            "effect_limit": 0.2,
        }
    ]

    response = _post_batch_raw(client, {"configs": [valid_config], "postprocess": "full"})

    assert response.status_code == 400
    assert "effect_mean" in response.json()["detail"]


@pytest.mark.parametrize("bad_value", [float("nan"), float("inf")])
def test_batch_start_rejects_non_finite_neutral_tolerance(client, valid_config, bad_value):
    valid_config["mutation_effect_neutral_tolerance"] = bad_value

    response = _post_batch_raw(client, {"configs": [valid_config], "postprocess": "full"})

    assert response.status_code == 400
    assert "mutation_effect_neutral_tolerance" in response.json()["detail"]


def test_valid_gamma_mutation_passes_batch_validation(valid_config):
    valid_config["mutations"] = [
        {
            "id": 0,
            "is_driver": True,
            "probability": 0.01,
            "effect_mode": "gamma",
            "effect_mean": 0.05,
            "effect_std": 0.02,
            "effect_direction": "negative",
            "effect_limit": 0.2,
        }
    ]

    main._validate_web_batch_outputs([valid_config], "full")


def test_valid_hybrid_count_models_pass_batch_validation(valid_config):
    valid_config["mutations"] = [
        {"id": 0, "is_driver": True, "effect": 0.1, "probability": 0.01},
        {"id": 1, "is_driver": False, "effect": 0.0, "count_model": "poisson", "lambda": 2.5},
    ]

    main._validate_web_batch_outputs([valid_config], "full")


def test_batch_start_rejects_poisson_without_lambda(client, valid_config):
    valid_config["mutations"] = [
        {"id": 0, "is_driver": True, "effect": 0.1, "count_model": "poisson"}
    ]

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "lambda" in response.json()["detail"]


def test_batch_start_rejects_poisson_with_probability(client, valid_config):
    valid_config["mutations"] = [
        {
            "id": 0,
            "is_driver": True,
            "effect": 0.1,
            "count_model": "poisson",
            "lambda": 1.0,
            "probability": 0.01,
        }
    ]

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "probability" in response.json()["detail"]


def test_batch_start_rejects_bernoulli_with_lambda(client, valid_config):
    valid_config["mutations"] = [
        {"id": 0, "is_driver": True, "effect": 0.1, "probability": 0.01, "lambda": 1.0}
    ]

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "lambda" in response.json()["detail"]


def test_batch_start_rejects_unknown_count_model(client, valid_config):
    valid_config["mutations"] = [
        {"id": 0, "is_driver": True, "effect": 0.1, "probability": 0.01, "count_model": "binomial"}
    ]

    response = client.post(
        "/api/simulation/batch/start",
        json={"configs": [valid_config], "postprocess": "full"},
    )

    assert response.status_code == 400
    assert "count_model" in response.json()["detail"]


@pytest.mark.parametrize("bad_value", [float("nan"), float("inf"), -0.1])
def test_batch_start_rejects_invalid_lambda(client, valid_config, bad_value):
    valid_config["mutations"] = [
        {"id": 0, "is_driver": True, "effect": 0.1, "count_model": "poisson", "lambda": bad_value}
    ]

    response = _post_batch_raw(client, {"configs": [valid_config], "postprocess": "full"})

    assert response.status_code == 400
    assert "lambda" in response.json()["detail"]


def test_start_conflicts_when_runner_is_busy(client, monkeypatch, valid_config):
    monkeypatch.setattr(main.runner, "is_running", lambda: True)

    response = client.post("/api/simulation/start", json={"config": valid_config})

    assert response.status_code == 409
