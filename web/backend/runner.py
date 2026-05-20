"""
SimulationRunner — subprocess wrapper for ./bin/CellEvoX binary
"""
import asyncio
import json
import os
import signal
import tempfile
import time
from datetime import datetime
from pathlib import Path
from typing import AsyncIterator, Optional


class SimulationRunner:
    def __init__(self, repo_root: Path):
        self.repo_root = repo_root
        self.binary = repo_root / "CellEvoX" / "build" / "bin" / "CellEvoX"
        # fallback binary locations
        self._binary_candidates = [
            repo_root / "CellEvoX" / "build" / "bin" / "CellEvoX",
            repo_root / "build" / "bin" / "CellEvoX",
        ]
        self._process: Optional[asyncio.subprocess.Process] = None
        self._status = "idle"
        self._run_id: Optional[str] = None
        self._log_queue: asyncio.Queue = asyncio.Queue()
        self._start_time: Optional[float] = None
        self._config_snapshot: dict = {}

    def _find_binary(self) -> Optional[Path]:
        for candidate in self._binary_candidates:
            if candidate.exists() and os.access(candidate, os.X_OK):
                return candidate
        return None

    def is_running(self) -> bool:
        return self._process is not None and self._process.returncode is None

    def get_status(self) -> dict:
        elapsed = None
        if self._start_time:
            elapsed = round(time.time() - self._start_time, 1)
        return {
            "status": self._status,
            "run_id": self._run_id,
            "elapsed_seconds": elapsed,
        }

    async def start(self, config: dict) -> str:
        binary = self._find_binary()
        if binary is None:
            raise FileNotFoundError(
                "CellEvoX binary not found. Build the project first.\n"
                f"Searched: {[str(c) for c in self._binary_candidates]}"
            )

        # Generate timestamped run_id and inject into output_path
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self._run_id = ts
        if "output_path" not in config or not config["output_path"]:
            config["output_path"] = f"./output_{ts}"

        # Ensure output dir exists
        output_path = Path(config["output_path"])
        output_path.mkdir(parents=True, exist_ok=True)

        # Write config to output dir for reference
        config_path = output_path / "config.json"
        with open(config_path, "w") as f:
            json.dump(config, f, indent=2)

        self._config_snapshot = config
        self._status = "running"
        self._start_time = time.time()
        self._log_queue = asyncio.Queue()

        self._process = await asyncio.create_subprocess_exec(
            str(binary), "--config", str(config_path),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            cwd=str(self.repo_root),
        )

        # Start background reader
        asyncio.create_task(self._read_output())
        return self._run_id

    async def _read_output(self):
        assert self._process and self._process.stdout
        try:
            async for raw in self._process.stdout:
                line = raw.decode(errors="replace").rstrip()
                await self._log_queue.put(line)
            await self._process.wait()
            self._status = "finished" if self._process.returncode == 0 else "error"
        except Exception as e:
            self._status = "error"
            await self._log_queue.put(f"[runner error] {e}")
        finally:
            await self._log_queue.put(None)  # sentinel

    def stop(self):
        if self._process and self._process.returncode is None:
            try:
                self._process.send_signal(signal.SIGINT)
            except ProcessLookupError:
                pass
            self._status = "stopped"

    async def log_stream(self) -> AsyncIterator[str]:
        while True:
            line = await self._log_queue.get()
            if line is None:
                break
            yield line
