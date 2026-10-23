"""
SimulationRunner - subprocess wrapper for ./bin/CellEvoX binary
"""
import asyncio
import json
import os
import re
import signal
import tempfile
import time
from datetime import datetime
from pathlib import Path
from typing import AsyncIterator, Optional, Sequence, TextIO


class SimulationRunner:
    _stdout_chunk_size = 8192
    _max_buffered_log_bytes = 32768
    _progress_log_interval_seconds = 15.0
    _valid_postprocess_modes = {"full", "exports", "csv"}
    _ansi_escape_re = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    _progress_re = re.compile(
        r"Progress:\s*\[[^\]]*\]\s*"
        r"(?P<percent>\d+)%\s*.*?"
        r"(?P<steps>\d+)\s+steps remaining,\s*"
        r"~(?P<eta>[0-9.]+)s left\s+"
        r"(?P<cells>\d+)\s+cells"
    )

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
        self._log_file: Optional[TextIO] = None
        self._log_file_path: Optional[Path] = None
        self._progress_log_state: dict[str, dict[str, object]] = {}
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
            "log_path": str(self._log_file_path) if self._log_file_path else None,
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
        self._progress_log_state = {}
        self._stop_requested = False
        self._process = None
        self._processes = {}
        self._open_persistent_log(resolved_run_dir / "logs", f"{analysis_id}.log")

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
        self._progress_log_state = {}
        self._stop_requested = False
        self._process = None
        self._processes = {}
        self._open_persistent_log(
            self._default_log_dir(configs),
            f"{self._run_id}.log",
        )

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

    def _default_log_dir(self, configs: Sequence[dict]) -> Path:
        output_bases = [
            self._resolve_output_base(config.get("output_path"))
            for config in configs
            if config.get("output_path")
        ]
        if not output_bases:
            return self.repo_root / "output_logs"

        try:
            common_output = Path(os.path.commonpath([str(path) for path in output_bases]))
        except ValueError:
            return self.repo_root / "output_logs"

        export_root = None
        for candidate in (common_output, common_output.parent):
            if candidate.name.endswith("_exports"):
                export_root = candidate
                break

        if export_root is not None:
            prefix = export_root.name[: -len("_exports")]
            return export_root.with_name(f"{prefix}_logs")

        if common_output == self.repo_root or not common_output.name:
            return self.repo_root / "output_logs"

        return common_output.with_name(f"{common_output.name}_logs")

    def _open_persistent_log(self, log_dir: Path, filename: str) -> None:
        self._close_persistent_log()
        self._log_file_path = None
        log_dir.mkdir(parents=True, exist_ok=True)
        log_path = self._unique_log_path(log_dir / filename)
        self._log_file = log_path.open("w", encoding="utf-8", buffering=1)
        self._log_file_path = log_path

    def _unique_log_path(self, path: Path) -> Path:
        if not path.exists():
            return path

        suffix = path.suffix or ".log"
        stem = path.name[: -len(path.suffix)] if path.suffix else path.name
        for index in range(2, 1000):
            candidate = path.with_name(f"{stem}_{index}{suffix}")
            if not candidate.exists():
                return candidate
        raise FileExistsError(f"Could not allocate unique log file path under {path.parent}")

    def _close_persistent_log(self) -> None:
        if self._log_file is not None:
            self._log_file.close()
            self._log_file = None

    async def _emit_log(
        self,
        line: str,
        persistent_line: str | None = None,
        write_persistent: bool = True,
    ) -> None:
        if persistent_line is None:
            persistent_line = line
        if self._log_file is not None and write_persistent and persistent_line:
            self._log_file.write(f"{persistent_line}\n")
            self._log_file.flush()
        await self._log_queue.put(line)

    def _strip_ansi(self, line: str) -> str:
        return self._ansi_escape_re.sub("", line)

    def _progress_key(self, prefix: str, process: asyncio.subprocess.Process) -> str:
        return f"{prefix or 'run'}:{process.pid}"

    def _should_persist_progress(self, key: str, percent: int | None, cells: int | None) -> bool:
        now = time.monotonic()
        state = self._progress_log_state.get(key)
        if not state:
            return True

        last_percent = state.get("percent")
        if percent is not None and percent != last_percent:
            return True

        last_cells = state.get("cells")
        if isinstance(cells, int) and isinstance(last_cells, int) and abs(cells - last_cells) >= 10000:
            return True

        last_time = state.get("last_time")
        return not isinstance(last_time, float) or now - last_time >= self._progress_log_interval_seconds

    def _read_process_rss_kib(self, pid: int | None) -> int | None:
        if not pid:
            return None
        status_path = Path(f"/proc/{pid}/status")
        try:
            with status_path.open(encoding="utf-8") as handle:
                for line in handle:
                    if line.startswith("VmRSS:"):
                        parts = line.split()
                        if len(parts) >= 2:
                            return int(parts[1])
        except (FileNotFoundError, ProcessLookupError, PermissionError, ValueError):
            return None
        return None

    def _format_count(self, value: int | str) -> str:
        try:
            return f"{int(value):,}"
        except (TypeError, ValueError):
            return str(value)

    def _format_rss(self, rss_kib: int | None) -> str:
        if rss_kib is None:
            return "n/a"
        rss_mib = rss_kib / 1024
        if rss_mib >= 1024:
            return f"{rss_mib / 1024:.2f} GiB"
        return f"{rss_mib:.1f} MiB"

    def _format_duration(self, seconds: float) -> str:
        seconds_i = max(0, int(round(seconds)))
        hours, rem = divmod(seconds_i, 3600)
        minutes, secs = divmod(rem, 60)
        if hours:
            return f"{hours} h {minutes:02d} min {secs:02d} s"
        if minutes:
            return f"{minutes} min {secs:02d} s"
        return f"{secs} s"

    def _remember_progress(
        self,
        key: str,
        percent: int | None,
        cells: int | None,
        rss_kib: int | None,
    ) -> None:
        state = self._progress_log_state.setdefault(key, {})
        state["last_time"] = time.monotonic()
        state["percent"] = percent
        state["cells"] = cells
        if rss_kib is not None:
            peak = state.get("peak_rss_kib")
            if not isinstance(peak, int) or rss_kib > peak:
                state["peak_rss_kib"] = rss_kib

    def _persistent_process_line(
        self,
        display_line: str,
        prefix: str,
        process: asyncio.subprocess.Process,
    ) -> str | None:
        clean_line = self._strip_ansi(display_line)
        if "Progress:" not in clean_line:
            return clean_line

        match = self._progress_re.search(clean_line)
        percent = int(match.group("percent")) if match else None
        cells = int(match.group("cells")) if match else None
        key = self._progress_key(prefix, process)
        if not self._should_persist_progress(key, percent, cells):
            return None

        rss_kib = self._read_process_rss_kib(process.pid)
        self._remember_progress(key, percent, cells, rss_kib)
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        prefix_text = prefix if prefix else ""
        rss_text = self._format_rss(rss_kib)
        if match:
            eta = self._format_duration(float(match.group("eta")))
            steps = self._format_count(match.group("steps"))
            cells_text = self._format_count(match.group("cells"))
            return (
                f"{prefix_text}[progress] at={timestamp} | "
                f"progress={match.group('percent')}% | "
                f"remaining={steps} steps | "
                f"ETA={eta} | "
                f"population={cells_text} cells | "
                f"RSS={rss_text}"
            )

        progress_text = clean_line[len(prefix_text):].strip() if prefix_text and clean_line.startswith(prefix_text) else clean_line
        return f"{prefix_text}[progress] at={timestamp} | RSS={rss_text} | {progress_text}"

    async def _emit_process_exit_summary(
        self,
        prefix: str,
        process: asyncio.subprocess.Process,
        started_at: float,
        return_code: int,
    ) -> None:
        key = self._progress_key(prefix, process)
        state = self._progress_log_state.get(key, {})
        peak = state.get("peak_rss_kib")
        peak_rss = peak if isinstance(peak, int) else None
        await self._emit_log(
            f"{prefix}[runner] Process exited: code={return_code} | "
            f"wall={self._format_duration(time.monotonic() - started_at)} | "
            f"peak RSS seen={self._format_rss(peak_rss)}"
        )

    async def _run_analysis(
        self,
        binary: Path,
        run_dir: Path,
        analysis_id: str,
        threads_per_run: int | None,
    ):
        process_key = f"analysis:{analysis_id}"
        try:
            await self._emit_log(f"[runner] Persistent log: {self._log_file_path}")
            await self._emit_log(f"[runner] Using binary: {binary}")
            await self._emit_log(f"[runner] Analyzing run directory: {run_dir}")
            if threads_per_run:
                await self._emit_log(f"[runner] TBB threads for analysis: {threads_per_run}")

            args = [str(binary), "--analyze", str(run_dir)]
            if threads_per_run:
                args.extend(["--threads", str(threads_per_run)])

            env = os.environ.copy()
            if threads_per_run:
                env["CELLEVOX_TBB_THREADS"] = str(threads_per_run)

            process_started_at = time.monotonic()
            process = await asyncio.create_subprocess_exec(
                *args,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                cwd=str(self.repo_root),
                env=env,
            )
            self._process = process
            self._processes[process_key] = process
            await self._emit_log(f"[analysis] [runner] Started process pid={process.pid}")

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
            await self._emit_process_exit_summary("[analysis] ", process, process_started_at, return_code)
            if return_code == 0:
                self._batch_completed = 1
            else:
                self._batch_failures = 1
                await self._emit_log(f"[runner] Analysis exited with code {return_code}")

            if self._stop_requested:
                self._status = "stopped"
            else:
                self._status = "finished" if return_code == 0 else "error"
        except asyncio.CancelledError:
            self._status = "stopped"
            await self._emit_log("[runner] Analysis cancelled")
        except Exception as e:
            self._status = "error"
            self._batch_failures = 1
            await self._emit_log(f"[runner error] {e}")
        finally:
            self._process = None
            self._processes.clear()
            if self._log_file_path:
                await self._emit_log(f"[runner] Persistent log saved to: {self._log_file_path}")
            self._close_persistent_log()
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
            await self._emit_log(f"[runner] Persistent log: {self._log_file_path}")
            await self._emit_log(f"[runner] Using binary: {binary}")
            await self._emit_log(f"[runner] Post-processing mode: {postprocess}")
            if threads_per_run:
                await self._emit_log(
                    f"[runner] TBB threads per simulation: {threads_per_run}"
                )
            if total > 1:
                await self._emit_log(
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
                        await self._emit_log(f"[runner error] {exc}")
                        if not continue_on_error:
                            stop_launching = True
                        return

                    self._batch_completed += 1
                    if return_code != 0:
                        failures += 1
                        self._batch_failures = failures
                        await self._emit_log(
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
            await self._emit_log("[runner] Batch cancelled")
        except Exception as e:
            self._status = "error"
            await self._emit_log(f"[runner error] {e}")
        finally:
            self._process = None
            self._processes.clear()
            if self._log_file_path:
                await self._emit_log(f"[runner] Persistent log saved to: {self._log_file_path}")
            self._close_persistent_log()
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
        await self._emit_log(f"{prefix}[runner] Launch config: {config_path}")

        args = [str(binary), "--config", str(config_path)]
        if threads_per_run:
            args.extend(["--threads", str(threads_per_run)])
        if postprocess != "full":
            args.extend(["--postprocess", postprocess])

        env = os.environ.copy()
        if threads_per_run:
            env["CELLEVOX_TBB_THREADS"] = str(threads_per_run)

        process_started_at = time.monotonic()
        process = await asyncio.create_subprocess_exec(
            *args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            cwd=str(self.repo_root),
            env=env,
        )
        self._process = process
        self._processes[run_label] = process
        await self._emit_log(f"{prefix}[runner] Started process pid={process.pid}")

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

        return_code = await process.wait()
        await self._emit_process_exit_summary(prefix, process, process_started_at, return_code)
        return return_code

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
                await self._emit_log_line(raw_line, prefix, process)

            if len(buffer) > self._max_buffered_log_bytes:
                await self._emit_log_line(
                    buffer[:self._max_buffered_log_bytes] + b" ... [continued]",
                    prefix,
                    process,
                )
                buffer = b""

        if buffer:
            await self._emit_log_line(buffer, prefix, process)

    async def _emit_log_line(
        self,
        raw_line: bytes,
        prefix: str,
        process: asyncio.subprocess.Process,
    ):
        line = raw_line.decode(errors="replace").strip()
        if line:
            display_line = f"{prefix}{line}" if prefix else line
            persistent_line = self._persistent_process_line(display_line, prefix, process)
            await self._emit_log(
                display_line,
                persistent_line=persistent_line,
                write_persistent=persistent_line is not None,
            )

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
