"""
SimulationRunner - subprocess wrapper for ./bin/CellEvoX binary
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
    _valid_postprocess_modes = {"full", "exports", "csv"}

    def __init__(self, repo_root: Path):
        self.repo_root = repo_root
        self.binary = repo_root / "build" / "bin" / "CellEvoX"
        # fallback binary locations
        self._binary_candidates = [
            repo_root / "build" / "bin" / "CellEvoX",
            repo_root / "CellEvoX" / "build" / "bin" / "CellEvoX",
        ]
        self._process: Optional[asyncio.subprocess.Process] = None
        self._processes: dict[str, asyncio.subprocess.Process] = {}
        self._task: Optional[asyncio.Task] = None
        self._status = "idle"
        self._run_id: Optional[str] = None
        self._current_run_id: Optional[str] = None
        self._batch_index = 0
        self._batch_completed = 0
        self._batch_failures = 0
        self._batch_total = 0
        self._batch_parallelism = 1
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
        process_running = any(process.returncode is None for process in self._processes.values())
        if self._process is not None and self._process.returncode is None:
            process_running = True
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
            "run_completed": self._batch_completed,
            "run_failures": self._batch_failures,
            "run_total": self._batch_total,
            "parallelism": self._batch_parallelism,
            "active_runs": len([p for p in self._processes.values() if p.returncode is None]),
            "elapsed_seconds": elapsed,
        }

    async def start(
        self,
        config: dict,
        threads_per_run: int | None = None,
        postprocess: str = "full",
    ) -> str:
        return await self.start_many(
            [config],
            threads_per_run=threads_per_run,
            postprocess=postprocess,
        )

    async def analyze(self, run_dir: Path, threads_per_run: int | None = None) -> str:
        if self.is_running():
            raise RuntimeError("Simulation or analysis already running")
        if threads_per_run is not None and threads_per_run < 1:
            raise ValueError("threads_per_run must be at least 1")

        resolved_run_dir = run_dir.expanduser().resolve()
        if not resolved_run_dir.is_dir():
            raise FileNotFoundError(f"Run directory not found: {resolved_run_dir}")

        binary = self._find_binary()
        if binary is None:
            raise FileNotFoundError(
                "CellEvoX binary not found. Build the project first.\n"
                f"Searched: {[str(c) for c in self._binary_candidates]}"
            )

        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        analysis_id = f"analysis_{ts}"
        self._run_id = analysis_id
        self._current_run_id = resolved_run_dir.name
        self._batch_index = 1
        self._batch_completed = 0
        self._batch_failures = 0
        self._batch_total = 1
        self._batch_parallelism = 1
        self._config_snapshot = {}
        self._status = "running"
        self._start_time = time.time()
        self._log_queue = asyncio.Queue()
        self._stop_requested = False
        self._process = None
        self._processes = {}

        self._task = asyncio.create_task(
            self._run_analysis(
                binary=binary,
                run_dir=resolved_run_dir,
                analysis_id=analysis_id,
                threads_per_run=threads_per_run,
            )
        )
        return analysis_id

    async def start_many(
        self,
        configs: Sequence[dict],
        continue_on_error: bool = False,
        max_parallel: int = 1,
        threads_per_run: int | None = None,
        postprocess: str = "full",
    ) -> str:
        if self.is_running():
            raise RuntimeError("Simulation already running")
        if not configs:
            raise ValueError("At least one simulation config is required")
        if max_parallel < 1:
            raise ValueError("max_parallel must be at least 1")
        if threads_per_run is not None and threads_per_run < 1:
            raise ValueError("threads_per_run must be at least 1")

        normalized_postprocess = self._normalize_postprocess(postprocess)

        binary = self._find_binary()
        if binary is None:
            raise FileNotFoundError(
                "CellEvoX binary not found. Build the project first.\n"
                f"Searched: {[str(c) for c in self._binary_candidates]}"
            )

        total = len(configs)
        effective_parallel = min(max_parallel, total)
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self._run_id = ts if total == 1 else f"batch_{ts}"
        self._current_run_id = None
        self._batch_index = 0
        self._batch_completed = 0
        self._batch_failures = 0
        self._batch_total = total
        self._batch_parallelism = effective_parallel
        self._config_snapshot = dict(configs[0])
        self._status = "running"
        self._start_time = time.time()
        self._log_queue = asyncio.Queue()
        self._stop_requested = False
        self._process = None
        self._processes = {}

        self._task = asyncio.create_task(
            self._run_many(
                binary=binary,
                configs=[dict(config) for config in configs],
                batch_id=self._run_id,
                continue_on_error=continue_on_error,
                max_parallel=effective_parallel,
                threads_per_run=threads_per_run,
                postprocess=normalized_postprocess,
            )
        )
        return self._run_id

    def _normalize_postprocess(self, postprocess: str) -> str:
        normalized = postprocess.lower().strip()
        if normalized not in self._valid_postprocess_modes:
            raise ValueError("postprocess must be one of: full, exports, csv")
        if normalized == "csv":
            return "exports"
        return normalized

    def _resolve_output_base(self, output_path: object) -> Path:
        raw = str(output_path or "./output")
        path = Path(raw).expanduser()
        if not path.is_absolute():
            path = self.repo_root / path
        return path

    def _write_launch_config(self, config: dict, run_label: str, isolate_output: bool = False) -> Path:
        prepared = dict(config)
        if "output_path" not in prepared or not prepared["output_path"]:
            prepared["output_path"] = "./output" if isolate_output else f"./output_{run_label}"

        output_base = self._resolve_output_base(prepared["output_path"])
        if isolate_output:
            output_base = output_base / run_label
            prepared["output_path"] = str(output_base)
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

    async def _run_analysis(
        self,
        binary: Path,
        run_dir: Path,
        analysis_id: str,
        threads_per_run: int | None,
    ):
        process_key = f"analysis:{analysis_id}"
        try:
            await self._log_queue.put(f"[runner] Using binary: {binary}")
            await self._log_queue.put(f"[runner] Analyzing run directory: {run_dir}")
            if threads_per_run:
                await self._log_queue.put(f"[runner] TBB threads for analysis: {threads_per_run}")

            args = [str(binary), "--analyze", str(run_dir)]
            if threads_per_run:
                args.extend(["--threads", str(threads_per_run)])

            env = os.environ.copy()
            if threads_per_run:
                env["CELLEVOX_TBB_THREADS"] = str(threads_per_run)

            process = await asyncio.create_subprocess_exec(
                *args,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                cwd=str(self.repo_root),
                env=env,
            )
            self._process = process
            self._processes[process_key] = process

            try:
                await self._stream_process_output(process, "[analysis] ")
            except Exception:
                if process.returncode is None:
                    process.terminate()
                    try:
                        await asyncio.wait_for(process.wait(), timeout=5)
                    except asyncio.TimeoutError:
                        process.kill()
                        await process.wait()
                raise
            finally:
                self._processes.pop(process_key, None)

            return_code = await process.wait()
            if return_code == 0:
                self._batch_completed = 1
            else:
                self._batch_failures = 1
                await self._log_queue.put(f"[runner] Analysis exited with code {return_code}")

            if self._stop_requested:
                self._status = "stopped"
            else:
                self._status = "finished" if return_code == 0 else "error"
        except asyncio.CancelledError:
            self._status = "stopped"
            await self._log_queue.put("[runner] Analysis cancelled")
        except Exception as e:
            self._status = "error"
            self._batch_failures = 1
            await self._log_queue.put(f"[runner error] {e}")
        finally:
            self._process = None
            self._processes.clear()
            await self._log_queue.put(None)

    async def _run_many(
        self,
        binary: Path,
        configs: Sequence[dict],
        batch_id: str,
        continue_on_error: bool,
        max_parallel: int,
        threads_per_run: int | None,
        postprocess: str,
    ):
        failures = 0
        stop_launching = False
        semaphore = asyncio.Semaphore(max_parallel)
        total = len(configs)
        isolate_output = total > 1

        try:
            await self._log_queue.put(f"[runner] Using binary: {binary}")
            await self._log_queue.put(f"[runner] Post-processing mode: {postprocess}")
            if threads_per_run:
                await self._log_queue.put(
                    f"[runner] TBB threads per simulation: {threads_per_run}"
                )
            if total > 1:
                await self._log_queue.put(
                    f"[runner] Starting batch {batch_id} with {total} simulation(s), "
                    f"parallelism={max_parallel}"
                )

            async def run_index(index: int, config: dict):
                nonlocal failures, stop_launching
                async with semaphore:
                    if self._stop_requested:
                        return
                    if stop_launching and not continue_on_error:
                        return

                    self._batch_index = index
                    run_label = batch_id if total == 1 else f"{batch_id}_{index:03d}"
                    self._current_run_id = run_label
                    try:
                        return_code = await self._run_one(
                            binary=binary,
                            config=config,
                            run_label=run_label,
                            index=index,
                            total=total,
                            isolate_output=isolate_output,
                            threads_per_run=threads_per_run,
                            postprocess=postprocess,
                        )
                    except Exception as exc:
                        failures += 1
                        self._batch_failures = failures
                        await self._log_queue.put(f"[runner error] {exc}")
                        if not continue_on_error:
                            stop_launching = True
                        return

                    self._batch_completed += 1
                    if return_code != 0:
                        failures += 1
                        self._batch_failures = failures
                        await self._log_queue.put(
                            f"[runner] Run {index}/{total} exited with code {return_code}"
                        )
                        if not continue_on_error:
                            stop_launching = True

            tasks = [
                asyncio.create_task(run_index(index, config))
                for index, config in enumerate(configs, start=1)
            ]
            await asyncio.gather(*tasks)

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
            self._processes.clear()
            await self._log_queue.put(None)  # sentinel

    async def _run_one(
        self,
        binary: Path,
        config: dict,
        run_label: str,
        index: int,
        total: int,
        isolate_output: bool,
        threads_per_run: int | None,
        postprocess: str,
    ) -> int:
        config_path = self._write_launch_config(config, run_label, isolate_output=isolate_output)
        prefix = f"[batch {index}/{total}] " if total > 1 else ""
        await self._log_queue.put(f"{prefix}[runner] Launch config: {config_path}")

        args = [str(binary), "--config", str(config_path)]
        if threads_per_run:
            args.extend(["--threads", str(threads_per_run)])
        if postprocess != "full":
            args.extend(["--postprocess", postprocess])

        env = os.environ.copy()
        if threads_per_run:
            env["CELLEVOX_TBB_THREADS"] = str(threads_per_run)

        process = await asyncio.create_subprocess_exec(
            *args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            cwd=str(self.repo_root),
            env=env,
        )
        self._process = process
        self._processes[run_label] = process

        try:
            await self._stream_process_output(process, prefix)
        except Exception:
            if process.returncode is None:
                process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), timeout=5)
                except asyncio.TimeoutError:
                    process.kill()
                    await process.wait()
            raise
        finally:
            self._processes.pop(run_label, None)

        return await process.wait()

    async def _stream_process_output(self, process: asyncio.subprocess.Process, prefix: str):
        assert process.stdout
        buffer = b""

        while True:
            chunk = await process.stdout.read(self._stdout_chunk_size)
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
        processes = list(self._processes.values())
        if self._process and self._process.returncode is None and self._process not in processes:
            processes.append(self._process)

        for process in processes:
            if process.returncode is not None:
                continue
            try:
                process.send_signal(signal.SIGINT)
            except ProcessLookupError:
                pass

        if not processes and self._task and not self._task.done():
            self._task.cancel()
        self._status = "stopped"

    async def log_stream(self) -> AsyncIterator[str]:
        while True:
            line = await self._log_queue.get()
            if line is None:
                break
            yield line
