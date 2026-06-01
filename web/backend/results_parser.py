"""
ResultsParser — reads CellEvoX output directories and returns structured data.
"""
import csv
import glob
import json
import os
import re
from pathlib import Path
from typing import Optional

import pandas as pd
import numpy as np


class ResultsParser:
    # Directories scanned for run outputs (relative to repo root)
    SCAN_DIRS = ["output*", "build/**/", "build/"]
    MUTATION_RE = re.compile(r"\((\d+),(\d+)\)")

    def __init__(self, repo_root: Path):
        self.repo_root = repo_root
        self._scripts_dir = repo_root / "CellEvoX" / "scripts"
        self._muller_cache: dict[tuple, dict] = {}

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

    def resolve_run_dir(self, run_id: str) -> Optional[Path]:
        return self._id_to_dir(run_id)

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
        """
        run_dir = self._id_to_dir(run_id)
        if run_dir is None:
            return None

        config_path = run_dir / "config.json"
        if not config_path.exists():
            return None

        cache_key = self._muller_cache_key(run_dir)
        if cache_key in self._muller_cache:
            return self._muller_cache[cache_key]

        try:
            config = self._load_json(config_path) or {}
            driver_ids = self._driver_mutation_type_ids(config)
            populations_df, parent_tree_df = self._build_muller_web_data(run_dir, driver_ids)

            # Convert to JSON-serialisable format for Plotly.js
            result = {
                "populations": self._jsonable_df(populations_df).to_dict(orient="list"),
                "parent_tree": self._jsonable_df(parent_tree_df).to_dict(orient="list"),
                "driver_mutation_ids": sorted(driver_ids),
                "mutations": config.get("mutations", []),
            }
            self._muller_cache[cache_key] = result
            return result
        except Exception as e:
            return {"error": str(e)}

    # ── Helpers ────────────────────────────────────────────────────────────────

    def _driver_mutation_type_ids(self, config: dict) -> set[int]:
        driver_ids: set[int] = set()
        for mutation in config.get("mutations", []):
            if not mutation.get("is_driver", False):
                continue
            try:
                driver_ids.add(int(mutation.get("id")))
            except (TypeError, ValueError):
                continue
        return driver_ids

    def _build_muller_web_data(
        self,
        run_dir: Path,
        driver_type_ids: set[int],
    ) -> tuple[pd.DataFrame, pd.DataFrame]:
        files = self._find_population_files(run_dir, (".csv",))
        if not files:
            raise ValueError(f"No population_generation_*.csv files found in {run_dir}")

        clone_populations: dict[int, dict[str, int]] = {}
        all_signatures: set[str] = {"ancestor"}
        signature_cache: dict[str, str] = {}

        for path in files:
            generation = self._extract_gen(path.name)
            counts: dict[str, int] = {}
            with open(path, newline="") as f:
                for row in csv.DictReader(f):
                    signature = self._driver_signature(
                        row.get("Mutations", ""),
                        driver_type_ids,
                        signature_cache,
                    )
                    all_signatures.add(signature)
                    counts[signature] = counts.get(signature, 0) + 1
            clone_populations[generation] = counts

        if len(clone_populations) == 1:
            generation = next(iter(clone_populations))
            clone_populations[generation + 1] = dict(clone_populations[generation])

        generations = sorted(clone_populations)
        signature_order = sorted(all_signatures)
        signature_to_id = {signature: idx for idx, signature in enumerate(signature_order)}
        first_generation = generations[0]
        last_generation = generations[-1]

        population_rows = []
        for generation in generations:
            counts = clone_populations[generation]
            for signature in signature_order:
                count = counts.get(signature, 0)
                if count > 0 or generation == first_generation or generation == last_generation:
                    population_rows.append({
                        "Id": signature_to_id[signature],
                        "Step": generation,
                        "Pop": float(count),
                    })

        parent_rows = []
        for signature in signature_order:
            if signature == "ancestor":
                continue
            parent_signature = self._muller_parent_signature(signature, all_signatures)
            if parent_signature and parent_signature in signature_to_id:
                parent_rows.append({
                    "ParentId": signature_to_id[parent_signature],
                    "ChildId": signature_to_id[signature],
                })

        return (
            pd.DataFrame(population_rows, columns=["Id", "Step", "Pop"]),
            pd.DataFrame(parent_rows, columns=["ParentId", "ChildId"]),
        )

    def _driver_signature(
        self,
        mutations_value,
        driver_type_ids: set[int],
        signature_cache: dict[str, str],
    ) -> str:
        if not driver_type_ids or not mutations_value:
            return "ancestor"

        mutations_str = str(mutations_value).strip('"')
        if not mutations_str:
            return "ancestor"

        cached = signature_cache.get(mutations_str)
        if cached is not None:
            return cached

        driver_mutation_ids = []
        for match in self.MUTATION_RE.finditer(mutations_str):
            try:
                mutation_type_id = int(match.group(2))
            except ValueError:
                continue
            if mutation_type_id in driver_type_ids:
                driver_mutation_ids.append(int(match.group(1)))

        if driver_mutation_ids:
            signature = ",".join(str(mutation_id) for mutation_id in sorted(driver_mutation_ids))
        else:
            signature = "ancestor"

        signature_cache[mutations_str] = signature
        return signature

    def _muller_parent_signature(self, signature: str, all_signatures: set[str]) -> str:
        if signature == "ancestor":
            return ""
        if "," not in signature:
            return "ancestor"

        # Mutation IDs are monotonic in CellEvoX outputs, so the newest driver is
        # usually the final token. Try that parent first and keep a fallback for
        # older or hand-authored outputs.
        newest_parent = signature.rsplit(",", 1)[0]
        if newest_parent in all_signatures:
            return newest_parent

        parts = signature.split(",")
        for idx in range(len(parts)):
            candidate_parts = parts[:idx] + parts[idx + 1:]
            candidate = "ancestor" if not candidate_parts else ",".join(candidate_parts)
            if candidate in all_signatures:
                return candidate
        return "ancestor"

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

    def _muller_cache_key(self, run_dir: Path) -> tuple:
        files = [run_dir / "config.json", *self._find_population_files(run_dir, (".csv",))]
        stats = []
        for path in files:
            try:
                stat = path.stat()
                stats.append((str(path.relative_to(self.repo_root)), stat.st_mtime_ns, stat.st_size))
            except OSError:
                continue
        return tuple(stats)

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
