"""
Console entrypoint for running several CellEvoX configs with the same runner
used by the FastAPI backend.

Examples:
  python3 web/backend/run_batch.py param_set_1_stochastic.json param_set_2_stochastic.json
  python3 web/backend/run_batch.py --manifest batch_manifest.json
"""
import argparse
import asyncio
import json
import sys
from pathlib import Path
from typing import Any

from runner import SimulationRunner


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def configs_from_manifest(data: Any, manifest_dir: Path) -> list[dict]:
    if isinstance(data, list):
        return [item for item in data if isinstance(item, dict)]

    if not isinstance(data, dict):
        raise ValueError("Batch manifest must be an object or an array")

    runs = data.get("runs")
    if runs is None:
        return [data]
    if not isinstance(runs, list):
        raise ValueError("Batch manifest field 'runs' must be an array")

    configs: list[dict] = []
    for index, item in enumerate(runs, start=1):
        if not isinstance(item, dict):
            raise ValueError(f"Run #{index} must be an object")
        if isinstance(item.get("config"), dict):
            configs.append(item["config"])
            continue
        if isinstance(item.get("path"), str):
            config_path = Path(item["path"]).expanduser()
            if not config_path.is_absolute():
                config_path = manifest_dir / config_path
            config = load_json(config_path)
            if not isinstance(config, dict):
                raise ValueError(f"Config file must contain a JSON object: {config_path}")
            configs.append(config)
            continue
        configs.append(item)
    return configs


def load_configs(config_paths: list[str], manifest_path: str | None) -> list[dict]:
    configs: list[dict] = []

    if manifest_path:
        manifest = Path(manifest_path).expanduser().resolve()
        configs.extend(configs_from_manifest(load_json(manifest), manifest.parent))

    for raw_path in config_paths:
        path = Path(raw_path).expanduser().resolve()
        data = load_json(path)
        configs.extend(configs_from_manifest(data, path.parent))

    if not configs:
        raise ValueError("Provide at least one config JSON file or --manifest")
    return configs


async def run(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).expanduser().resolve()
    configs = load_configs(args.configs, args.manifest)
    runner = SimulationRunner(repo_root=repo_root)
    run_id = await runner.start_many(configs, continue_on_error=args.continue_on_error)
    print(f"[runner] Started {run_id} with {len(configs)} config(s)")

    async for line in runner.log_stream():
        print(line, flush=True)

    status = runner.get_status()
    return 0 if status["status"] == "finished" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Run one or more CellEvoX simulation configs.")
    parser.add_argument("configs", nargs="*", help="Config JSON files or manifest JSON files")
    parser.add_argument("--manifest", help="Batch manifest JSON with a 'runs' array")
    parser.add_argument("--continue-on-error", action="store_true", help="Continue after a failed run")
    parser.add_argument(
        "--repo-root",
        default=str(Path(__file__).resolve().parents[2]),
        help="Repository root containing the CellEvoX build and output directories",
    )
    args = parser.parse_args()

    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        print(f"[runner error] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
