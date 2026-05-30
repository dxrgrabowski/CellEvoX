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
from typing import AsyncIterator, Optional, Sequence


class SimulationRunner:
    _stdout_chunk_size = 8192
    _max_buffered_log_bytes = 32768

    def __init__(self, repo_root: Path):
        self.repo_root = repo_root
        self.binary = repo_root / "build" / "bin" / "CellEvoX"
        # fallback binary locations
        self._binary_candidates = [
            repo_root / "build" / "bin" / "CellEvoX",
            repo_root / "CellEvoX" / "build" / "bin" / "CellEvoX",
        ]
        self._process: Optional[asyncio.subprocess.Process] = None
        self._task: Optional[asyncio.Task] = None
        self._status = "idle"
        self._run_id: Optional[str] = None
        self._current_run_id: Optional[str] = None
        self._batch_index = 0
        self._batch_total = 0
        self._log_queue: asyncio.Queue = asyncio.Queue()
        self._start_time: Optional[float] = None
        self._config_snapshot: dict = {}
        self._stop_requested = False

    def _find_binary(self) -> Optional[Path]:
        candidates = []
        env_binary = os.environ.get("CELLEVOX_BINARY")
        if env_binary:
            candidates.append(Path(env_binary).expanduser())
        candidates.extend(self._binary_candidates)

        executable_candidates = []
        for candidate in candidates:
            if candidate.exists() and os.access(candidate, os.X_OK):
                executable_candidates.append(candidate)

        if not executable_candidates:
            return None

        # Prefer the freshest build when both historical build locations exist.
        return max(set(executable_candidates), key=lambda p: p.stat().st_mtime)

    def is_running(self) -> bool:
        process_running = self._process is not None and self._process.returncode is None
        task_running = self._task is not None and not self._task.done()
        return process_running or task_running

    def get_status(self) -> dict:
        elapsed = None
        if self._start_time:
            elapsed = round(time.time() - self._start_time, 1)
        return {
            "status": self._status,
            "run_id": self._run_id,
            "current_run_id": self._current_run_id,
            "run_index": self._batch_index,
            "run_total": self._batch_total,
            "elapsed_seconds": elapsed,
        }

    async def start(self, config: dict) -> str:
        return await self.start_many([config])

    async def start_many(self, configs: Sequence[dict], continue_on_error: bool = False) -> str:
        if self.is_running():
            raise RuntimeError("Simulation already running")
        if not configs:
            raise ValueError("At least one simulation config is required")

        binary = self._find_binary()
        if binary is None:
            raise FileNotFoundError(
                "CellEvoX binary not found. Build the project first.\n"
                f"Searched: {[str(c) for c in self._binary_candidates]}"
            )

        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self._run_id = ts if len(configs) == 1 else f"batch_{ts}"
        self._current_run_id = None
        self._batch_index = 0
        self._batch_total = len(configs)
        self._config_snapshot = dict(configs[0])
        self._status = "running"
        self._start_time = time.time()
        self._log_queue = asyncio.Queue()
        self._stop_requested = False

        self._task = asyncio.create_task(
            self._run_many(
                binary=binary,
                configs=[dict(config) for config in configs],
                batch_id=self._run_id,
                continue_on_error=continue_on_error,
            )
        )
        return self._run_id

    def _resolve_output_base(self, output_path: object) -> Path:
        raw = str(output_path or "./output")
        path = Path(raw).expanduser()
        if not path.is_absolute():
            path = self.repo_root / path
        return path

    def _write_launch_config(self, config: dict, run_label: str) -> Path:
        prepared = dict(config)
        if "output_path" not in prepared or not prepared["output_path"]:
            prepared["output_path"] = f"./output_{run_label}"

        output_base = self._resolve_output_base(prepared["output_path"])
        output_base.mkdir(parents=True, exist_ok=True)

        launch_dir = self.repo_root / ".cellevox_launch_configs"
        launch_dir.mkdir(parents=True, exist_ok=True)
        config_path = launch_dir / f"{run_label}.json"

        fd, tmp_name = tempfile.mkstemp(
            prefix=f".{run_label}.",
            suffix=".tmp",
            dir=launch_dir,
            text=True,
        )
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                json.dump(prepared, handle, indent=2)
                handle.write("\n")
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(tmp_name, config_path)
        except Exception:
            try:
                os.unlink(tmp_name)
            except FileNotFoundError:
                pass
            raise

        if config_path.stat().st_size == 0:
            raise RuntimeError(f"Launch config was written empty: {config_path}")
        return config_path.resolve()

    async def _run_many(
        self,
        binary: Path,
        configs: Sequence[dict],
        batch_id: str,
        continue_on_error: bool,
    ):
        failures = 0
        try:
            total = len(configs)
            await self._log_queue.put(
                f"[runner] Using binary: {binary}"
            )
            if total > 1:
                await self._log_queue.put(
                    f"[runner] Starting batch {batch_id} with {total} simulations"
                )

            for index, config in enumerate(configs, start=1):
                if self._stop_requested:
                    break

                self._batch_index = index
                run_label = batch_id if total == 1 else f"{batch_id}_{index:03d}"
                self._current_run_id = run_label
                try:
                    return_code = await self._run_one(binary, config, run_label, index, total)
                except Exception as exc:
                    failures += 1
                    await self._log_queue.put(f"[runner error] {exc}")
                    if not continue_on_error:
                        break
                    continue

                if return_code != 0:
                    failures += 1
                    await self._log_queue.put(
                        f"[runner] Run {index}/{total} exited with code {return_code}"
                    )
                    if not continue_on_error:
                        break

            if self._stop_requested:
                self._status = "stopped"
            else:
                self._status = "finished" if failures == 0 else "error"
        except asyncio.CancelledError:
            self._status = "stopped"
            await self._log_queue.put("[runner] Batch cancelled")
        except Exception as e:
            self._status = "error"
            await self._log_queue.put(f"[runner error] {e}")
        finally:
            self._process = None
            await self._log_queue.put(None)  # sentinel

    async def _run_one(
        self,
        binary: Path,
        config: dict,
        run_label: str,
        index: int,
        total: int,
    ) -> int:
        config_path = self._write_launch_config(config, run_label)
        prefix = f"[batch {index}/{total}] " if total > 1 else ""
        await self._log_queue.put(f"{prefix}[runner] Launch config: {config_path}")

        self._process = await asyncio.create_subprocess_exec(
            str(binary), "--config", str(config_path),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            cwd=str(self.repo_root),
        )

        try:
            await self._stream_process_output(prefix)
        except Exception:
            if self._process.returncode is None:
                self._process.terminate()
                try:
                    await asyncio.wait_for(self._process.wait(), timeout=5)
                except asyncio.TimeoutError:
                    self._process.kill()
                    await self._process.wait()
            raise
        return await self._process.wait()

    async def _stream_process_output(self, prefix: str):
        assert self._process and self._process.stdout
        buffer = b""

        while True:
            chunk = await self._process.stdout.read(self._stdout_chunk_size)
            if not chunk:
                break

            buffer += chunk.replace(b"\r", b"\n")
            while b"\n" in buffer:
                raw_line, buffer = buffer.split(b"\n", 1)
                await self._emit_log_line(raw_line, prefix)

            if len(buffer) > self._max_buffered_log_bytes:
                await self._emit_log_line(
                    buffer[:self._max_buffered_log_bytes] + b" ... [continued]",
                    prefix,
                )
                buffer = b""

        if buffer:
            await self._emit_log_line(buffer, prefix)

    async def _emit_log_line(self, raw_line: bytes, prefix: str):
        line = raw_line.decode(errors="replace").strip()
        if line:
            await self._log_queue.put(f"{prefix}{line}" if prefix else line)

    def stop(self):
        self._stop_requested = True
        if self._process and self._process.returncode is None:
            try:
                self._process.send_signal(signal.SIGINT)
            except ProcessLookupError:
                pass
        elif self._task and not self._task.done():
            self._task.cancel()
        self._status = "stopped"

    async def log_stream(self) -> AsyncIterator[str]:
        while True:
            line = await self._log_queue.get()
            if line is None:
                break
            yield line
