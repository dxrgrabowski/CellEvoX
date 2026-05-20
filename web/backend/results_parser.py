"""
ResultsParser — reads CellEvoX output directories and returns structured data.
Imports processing functions from existing Python scripts (no duplication).
"""
import csv
import glob
import json
import os
import sys
from pathlib import Path
from typing import Optional

import pandas as pd
import numpy as np


class ResultsParser:
    # Directories scanned for run outputs (relative to repo root)
    SCAN_DIRS = ["output*", "build/**/", "build/"]

    def __init__(self, repo_root: Path):
        self.repo_root = repo_root
        self._scripts_dir = repo_root / "CellEvoX" / "scripts"

    # ── Run Discovery ──────────────────────────────────────────────────────────

    def _find_run_dirs(self) -> list[Path]:
        """
        Recursively find directories that contain a config.json.
        Scans the entire repo root but excludes build artefacts without config.
        """
        found = []
        for p in self.repo_root.rglob("config.json"):
            # Skip node_modules and .git
            parts = p.parts
            if any(x in parts for x in ["node_modules", ".git", ".venv"]):
                continue
            found.append(p.parent)
        return found

    def list_runs(self) -> list[dict]:
        runs = []
        for run_dir in self._find_run_dirs():
            config = self._load_json(run_dir / "config.json")
            if config is None:
                continue
            has_stats = bool(list(run_dir.glob("statistics*.csv")))
            has_population = bool(
                list(run_dir.glob("population_generation_*.csv"))
                or (run_dir / "population_data").exists()
            )
            run_id = self._dir_to_id(run_dir)
            runs.append({
                "id": run_id,
                "path": str(run_dir.relative_to(self.repo_root)),
                "label": run_dir.name,
                "sim_mode": config.get("simulation_mode", "stochastic" if config.get("stochastic") else "deterministic"),
                "steps": config.get("steps"),
                "has_stats": has_stats,
                "has_population": has_population,
                "has_muller": has_population,
            })
        # Sort newest first (by path descending)
        runs.sort(key=lambda r: r["path"], reverse=True)
        return runs

    def _dir_to_id(self, path: Path) -> str:
        """Convert absolute path to a URL-safe run ID (base64-like relative path)."""
        rel = str(path.relative_to(self.repo_root))
        return rel.replace("/", "__").replace("\\", "__")

    def _id_to_dir(self, run_id: str) -> Optional[Path]:
        rel = run_id.replace("__", "/")
        candidate = self.repo_root / rel
        if candidate.is_dir():
            return candidate
        return None

    # ── Data Accessors ─────────────────────────────────────────────────────────

    def get_config(self, run_id: str) -> Optional[dict]:
        run_dir = self._id_to_dir(run_id)
        if run_dir is None:
            return None
        return self._load_json(run_dir / "config.json")

    def get_summary(self, run_id: str) -> Optional[dict]:
        run_dir = self._id_to_dir(run_id)
        if run_dir is None:
            return None
        config = self._load_json(run_dir / "config.json")
        if config is None:
            return None

        # Try to read stats CSV for summary numbers
        stats_files = list(run_dir.glob("statistics*.csv"))
        summary = {
            "run_id": run_id,
            "path": str(run_dir.relative_to(self.repo_root)),
            "config": config,
        }
        if stats_files:
            try:
                df = pd.read_csv(stats_files[0])
                last = df.iloc[-1]
                summary.update({
                    "final_tau": float(last.get("Tau", last.get("tau", 0))),
                    "final_population": int(last.get("TotalLivingCells", last.get("total_living_cells", 0))),
                    "final_mean_fitness": float(last.get("MeanFitness", last.get("mean_fitness", 0))),
                    "total_steps": len(df),
                })
            except Exception:
                pass
        return summary

    def get_stats(self, run_id: str) -> Optional[dict]:
        run_dir = self._id_to_dir(run_id)
        if run_dir is None:
            return None
        stats_files = list(run_dir.glob("statistics*.csv"))
        if not stats_files:
            return None
        try:
            df = pd.read_csv(stats_files[0])
            # Normalize column names to camelCase
            df.columns = [c.strip() for c in df.columns]
            return {
                "columns": list(df.columns),
                "rows": df.replace({float("nan"): None}).to_dict(orient="list"),
            }
        except Exception as e:
            return {"error": str(e)}

    def get_population(self, run_id: str, generation: Optional[int] = None) -> Optional[dict]:
        run_dir = self._id_to_dir(run_id)
        if run_dir is None:
            return None

        pop_dir = run_dir / "population_data"
        search_dir = pop_dir if pop_dir.exists() else run_dir

        if generation is not None:
            pattern = f"population_generation_{generation}.csv"
            files = list(search_dir.glob(pattern))
        else:
            files = sorted(search_dir.glob("population_generation_*.csv"))

        if not files:
            return None

        results = {}
        for f in files[:20]:  # cap at 20 for performance
            gen = self._extract_gen(f.name)
            try:
                df = pd.read_csv(f)
                results[gen] = df.replace({float("nan"): None}).to_dict(orient="records")
            except Exception:
                pass
        return results

    def get_muller_data(self, run_id: str) -> Optional[dict]:
        """
        Returns Müller plot data as JSON suitable for Plotly.js stacked area chart.
        Reuses build_pyfish_data() logic from the existing plot_muller.py script.
        """
        run_dir = self._id_to_dir(run_id)
        if run_dir is None:
            return None

        config_path = run_dir / "config.json"
        if not config_path.exists():
            return None

        try:
            sys.path.insert(0, str(self._scripts_dir))
            # Import existing processing functions — no code duplication
            from plot_muller import build_pyfish_data, get_driver_mutation_type_ids

            config = self._load_json(config_path)
            driver_ids = get_driver_mutation_type_ids(str(config_path))

            populations_df, parent_tree_df = build_pyfish_data(str(run_dir))

            # Convert to JSON-serialisable format for Plotly.js
            return {
                "populations": populations_df.to_dict(orient="list"),
                "parent_tree": parent_tree_df.to_dict(orient="list"),
                "driver_mutation_ids": list(driver_ids),
                "mutations": config.get("mutations", []),
            }
        except ImportError as e:
            return {"error": f"pyfish not installed: {e}", "raw_available": True}
        except Exception as e:
            return {"error": str(e)}

    # ── Helpers ────────────────────────────────────────────────────────────────

    def _load_json(self, path: Path) -> Optional[dict]:
        try:
            with open(path) as f:
                return json.load(f)
        except Exception:
            return None

    def _extract_gen(self, filename: str) -> int:
        import re
        m = re.search(r"(\d+)", filename)
        return int(m.group(1)) if m else 0
