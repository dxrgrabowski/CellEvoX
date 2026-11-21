"""Contract-level tests for ResultsParser against on-disk run directories."""

from __future__ import annotations

import json

import pytest

from results_parser import (
    ResultsParser,
    parse_clone_signature,
    render_clone_signature,
)


def test_list_runs_reports_available_outputs(make_run, tmp_path):
    make_run(
        "run-a",
        stats_rows=[
            {"Tau": 0.5, "TotalLivingCells": 120, "MeanFitness": 1.02},
            {"Tau": 1.0, "TotalLivingCells": 150, "MeanFitness": 1.10},
        ],
        populations={1: ["", "(1,0)"]},
    )

    runs = ResultsParser(repo_root=tmp_path).list_runs()

    assert [run["id"] for run in runs] == ["run-a"]
    assert runs[0]["sim_mode"] == "stochastic"
    assert runs[0]["has_stats"] is True
    assert runs[0]["has_population"] is True
    assert runs[0]["has_muller"] is True


def test_run_id_round_trips_for_nested_run(make_run, tmp_path):
    run_dir = make_run("nested/run-b")
    parser = ResultsParser(repo_root=tmp_path)

    run_id = parser.list_runs()[0]["id"]

    assert run_id == "nested__run-b"
    assert parser.resolve_run_dir(run_id) == run_dir


def test_summary_reads_last_statistics_row(make_run, tmp_path):
    make_run(
        "run-a",
        stats_rows=[
            {"Tau": 0.5, "TotalLivingCells": 120, "MeanFitness": 1.02},
            {"Tau": 1.0, "TotalLivingCells": 150, "MeanFitness": 1.10},
        ],
    )

    summary = ResultsParser(repo_root=tmp_path).get_summary("run-a")

    assert summary["final_tau"] == 1.0
    assert summary["final_population"] == 150
    assert summary["final_mean_fitness"] == pytest.approx(1.10)
    assert summary["total_steps"] == 2


def test_missing_statistics_keeps_summary_without_final_numbers(make_run, tmp_path):
    make_run("run-a")

    summary = ResultsParser(repo_root=tmp_path).get_summary("run-a")
    stats = ResultsParser(repo_root=tmp_path).get_stats("run-a")

    assert summary["run_id"] == "run-a"
    assert "final_population" not in summary
    assert stats is None


def test_malformed_statistics_are_reported_not_raised(make_run, tmp_path):
    make_run("run-a", stats_text='Tau,TotalLivingCells\n"0.5,10\n')

    result = ResultsParser(repo_root=tmp_path).get_stats("run-a")

    assert "error" in result


def test_malformed_config_is_skipped_during_discovery(make_run, tmp_path):
    make_run("broken", config_text="{not valid json")

    parser = ResultsParser(repo_root=tmp_path)

    assert parser.list_runs() == []
    assert parser.get_config("broken") is None


def test_missing_run_id_resolves_to_none(tmp_path):
    parser = ResultsParser(repo_root=tmp_path)

    assert parser.resolve_run_dir("does-not-exist") is None
    assert parser.get_config("does-not-exist") is None
    assert parser.get_summary("does-not-exist") is None
    assert parser.get_stats("does-not-exist") is None
    assert parser.get_population("does-not-exist") is None


def test_population_files_are_grouped_by_generation(make_run, tmp_path):
    make_run("run-a", populations={1: ["", "(1,0,0.05)"], 2: ["(2,0,-0.1)"]})

    data = ResultsParser(repo_root=tmp_path).get_population("run-a")

    assert sorted(data) == [1, 2]
    assert data[1][0]["Mutations"] is None
    assert data[2][0]["Mutations"] == "(2,0,-0.1)"


def test_node_modules_config_is_not_discovered(tmp_path):
    vendored = tmp_path / "node_modules" / "package"
    vendored.mkdir(parents=True)
    (vendored / "config.json").write_text(json.dumps({"simulation_mode": "stochastic"}))

    assert ResultsParser(repo_root=tmp_path).list_runs() == []


def test_clone_signature_keeps_legacy_labels_without_collision():
    assert render_clone_signature([]) == "ancestor"
    assert render_clone_signature([(0, 1)]) == "0"
    assert render_clone_signature([(3, 7), (1, 2)]) == "1,3"


def test_clone_signature_switches_to_id_type_on_collision():
    assert render_clone_signature([(0, 1), (0, 2)]) == "0:1,0:2"
    assert render_clone_signature([(3, 5), (0, 2), (0, 1)]) == "0:1,0:2,3"


def test_clone_signature_deduplicates_repeated_alleles():
    assert render_clone_signature([(0, 1), (0, 1)]) == "0"


def test_parse_clone_signature_round_trips_rendered_labels():
    assert parse_clone_signature("ancestor") == []
    assert parse_clone_signature("1,3") == [(1, None), (3, None)]
    assert parse_clone_signature("0:1,0:2,3") == [(0, 1), (0, 2), (3, None)]


def test_muller_parent_drops_newest_allele(tmp_path):
    parser = ResultsParser(repo_root=tmp_path)
    signatures = {"ancestor", "0", "0,1", "0,1,2"}

    assert parser._muller_parent_signature("0,1,2", signatures) == "0,1"
    assert parser._muller_parent_signature("0,1", signatures) == "0"
    assert parser._muller_parent_signature("0", signatures) == "ancestor"


def test_muller_parent_drops_all_alleles_from_one_division(tmp_path):
    """Alleles sharing a mutation id came from one division and drop together."""
    parser = ResultsParser(repo_root=tmp_path)
    signatures = {"ancestor", "7", "7,12:1,12:2"}

    assert parser._muller_parent_signature("7,12:1,12:2", signatures) == "7"


def test_muller_parent_does_not_drop_a_single_allele_from_one_division(tmp_path):
    parser = ResultsParser(repo_root=tmp_path)
    # Dropping only one of the two same-id alleles would fabricate the
    # unrelated "0" label; both alleles belong to the same event.
    signatures = {"ancestor", "0", "0:1,0:2"}

    assert parser._muller_parent_signature("0:1,0:2", signatures) == "ancestor"


def test_muller_parent_falls_back_to_ancestor_without_known_parent(tmp_path):
    parser = ResultsParser(repo_root=tmp_path)

    assert parser._muller_parent_signature("0,1", {"ancestor", "0,1"}) == "ancestor"
