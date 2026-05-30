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
            has_stats = self._find_stats_file(run_dir) is not None
            population_csv = self._find_population_files(run_dir, (".csv",))
            population_bin = self._find_population_files(run_dir, (".bin",))
            has_population = bool(population_csv or population_bin)
            run_id = self._dir_to_id(run_dir)
            runs.append({
                "id": run_id,
                "path": str(run_dir.relative_to(self.repo_root)),
                "label": run_dir.name,
                "sim_mode": config.get("simulation_mode", "stochastic" if config.get("stochastic") else "deterministic"),
                "steps": config.get("steps"),
                "has_stats": has_stats,
                "has_population": has_population,
                "has_muller": bool(population_csv),
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
        stats_file = self._find_stats_file(run_dir)
        summary = {
            "run_id": run_id,
            "path": str(run_dir.relative_to(self.repo_root)),
            "config": config,
        }
        if stats_file:
            try:
                df = pd.read_csv(stats_file)
                last = df.iloc[-1]
                summary.update({
                    "final_tau": float(self._row_value(last, ["Tau", "tau", "Generation", "generation"], 0)),
                    "final_population": int(self._row_value(last, ["TotalLivingCells", "total_living_cells"], 0)),
                    "final_mean_fitness": float(self._row_value(last, ["MeanFitness", "mean_fitness"], 0)),
                    "total_steps": len(df),
                })
            except Exception:
                pass
        return summary

    def get_stats(self, run_id: str) -> Optional[dict]:
        run_dir = self._id_to_dir(run_id)
        if run_dir is None:
            return None
        stats_file = self._find_stats_file(run_dir)
        if not stats_file:
            return None
        try:
            df = pd.read_csv(stats_file)
            # Normalize column names to camelCase
            df.columns = [c.strip() for c in df.columns]
            return {
                "columns": list(df.columns),
                "rows": self._jsonable_df(df).to_dict(orient="list"),
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
                results[gen] = self._jsonable_df(df).to_dict(orient="records")
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
                "populations": self._jsonable_df(populations_df).to_dict(orient="list"),
                "parent_tree": self._jsonable_df(parent_tree_df).to_dict(orient="list"),
                "driver_mutation_ids": list(driver_ids),
                "mutations": config.get("mutations", []),
            }
        except SystemExit as e:
            return {"error": f"Müller processing failed during import: {e}", "raw_available": True}
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

    def _find_stats_file(self, run_dir: Path) -> Optional[Path]:
        preferred = [
            run_dir / "statistics" / "generational_statistics.csv",
            run_dir / "generational_statistics.csv",
        ]
        for path in preferred:
            if path.exists():
                return path

        patterns = [
            "statistics*.csv",
            "statistics/*statistics*.csv",
            "statistics/*.csv",
        ]
        for pattern in patterns:
            files = sorted(
                p for p in run_dir.glob(pattern)
                if "memory" not in p.name.lower()
            )
            if files:
                return files[0]
        return None

    def _find_population_files(self, run_dir: Path, extensions: tuple[str, ...]) -> list[Path]:
        files: list[Path] = []
        for search_dir in (run_dir / "population_data", run_dir):
            if not search_dir.exists():
                continue
            for ext in extensions:
                files.extend(search_dir.glob(f"population_generation_*{ext}"))
        unique = list(dict.fromkeys(files))
        return sorted(unique, key=lambda p: self._extract_gen(p.name))

    def _row_value(self, row: pd.Series, names: list[str], default=0):
        normalized = {
            str(column).strip().lower().replace("_", ""): column
            for column in row.index
        }
        for name in names:
            column = normalized.get(name.strip().lower().replace("_", ""))
            if column is not None and pd.notna(row[column]):
                return row[column]
        return default

    def _jsonable_df(self, df: pd.DataFrame) -> pd.DataFrame:
        return df.replace([np.inf, -np.inf], np.nan).replace({np.nan: None})

    def _extract_gen(self, filename: str) -> int:
        import re
        m = re.search(r"(\d+)", filename)
        return int(m.group(1)) if m else 0
