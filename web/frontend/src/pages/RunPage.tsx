import { useCallback, useEffect, useMemo, useRef, useState, type ChangeEvent } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import {
  Activity,
  Cpu,
  FileDown,
  FileUp,
  Layers,
  ListChecks,
  Loader,
  Minus,
  Plus,
  Play,
  Rows3,
  SlidersHorizontal,
  Square,
  Terminal,
  Trash2,
  type LucideIcon,
} from 'lucide-react';
import { useBatchQueueStore, useConfigStore, useSimStore } from '../stores';
import {
  startSimulationBatch,
  stopSimulation,
  getSimulationStatus,
  createLogSocket,
} from '../api/client';
import { buildConfigPayload } from '../utils/configPayload';
import { describeBatchConfig, extractBatchImportItems } from '../utils/batchQueue';

function formatElapsed(s: number | null) {
  if (s === null) return '--';
  if (s < 60) return `${s}s`;
  return `${Math.floor(s / 60)}m ${s % 60}s`;
}

function getErrorMessage(error: unknown): string {
  if (typeof error === 'object' && error !== null && 'response' in error) {
    const response = (error as { response?: { data?: { detail?: unknown } } }).response;
    if (typeof response?.data?.detail === 'string') return response.data.detail;
  }
  return 'Failed to start simulation';
}

function formatCompactTotal(value: number) {
  if (value >= 1_000_000) return `${Number((value / 1_000_000).toFixed(1))}M`;
  if (value >= 1_000) return `${Number((value / 1_000).toFixed(1))}k`;
  return value.toLocaleString();
}

function totalNumber(items: Record<string, unknown>[], field: string) {
  return items.reduce((sum, config) => {
    const value = config[field];
    return sum + (typeof value === 'number' && Number.isFinite(value) ? value : 0);
  }, 0);
}

function totalGenerations(items: Record<string, unknown>[]) {
  return items.reduce((sum, config) => {
    const steps = config.steps;
    const tau = config.tau_step;
    if (typeof steps !== 'number' || typeof tau !== 'number') return sum;
    return sum + steps * tau;
  }, 0);
}

type PostprocessMode = 'exports' | 'full';

type StepperControlProps = {
  label: string;
  icon: LucideIcon;
  value: number;
  min: number;
  max: number;
  disabled: boolean;
  onChange: (value: number) => void;
  suffix?: string;
};

type LogGroup = {
  id: string;
  label: string;
  subtitle: string;
  lines: string[];
  kind: 'runner' | 'batch';
  index: number;
};

const BATCH_LOG_PATTERN = /^\[batch\s+(\d+)\/(\d+)\]\s?(.*)$/;

function clampNumber(value: number, min: number, max: number) {
  if (!Number.isFinite(value)) return min;
  return Math.min(max, Math.max(min, Math.round(value)));
}

function numericConfigValue(config: Record<string, unknown>, field: string): number | null {
  const value = config[field];
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function validateResultArtifacts(items: Record<string, unknown>[]): string | null {
  for (let i = 0; i < items.length; i += 1) {
    const config = items[i];
    const steps = numericConfigValue(config, 'steps');
    const tauStep = numericConfigValue(config, 'tau_step');
    const statRes = numericConfigValue(config, 'statistics_resolution');
    const populationRes = numericConfigValue(config, 'population_statistics_res');

    if (steps == null || tauStep == null || statRes == null || populationRes == null) {
      return `Run #${i + 1}: missing numeric output settings`;
    }
    if (steps <= 0 || tauStep <= 0 || statRes <= 0 || populationRes <= 0) {
      return `Run #${i + 1}: output settings must be positive`;
    }

    const finalTau = steps * tauStep;
    const finalTauFloor = Math.floor(finalTau + 1e-9);
    if (finalTauFloor < statRes) {
      return `Run #${i + 1}: final T ${Number(finalTau.toFixed(3))} is below statistics resolution ${statRes}`;
    }
    if (finalTauFloor < populationRes) {
      return `Run #${i + 1}: final T ${Number(finalTau.toFixed(3))} is below population snapshot resolution ${populationRes}`;
    }
  }
  return null;
}

function groupLogLines(logs: string[]): LogGroup[] {
  const groups = new Map<string, LogGroup>();
  const ensureGroup = (id: string, group: Omit<LogGroup, 'lines'>) => {
    const existing = groups.get(id);
    if (existing) return existing;
    const next = { ...group, lines: [] };
    groups.set(id, next);
    return next;
  };

  logs.forEach((line) => {
    const batchMatch = line.match(BATCH_LOG_PATTERN);
    if (batchMatch) {
      const index = Number(batchMatch[1]);
      const total = Number(batchMatch[2]);
      const cleanedLine = batchMatch[3] || line;
      ensureGroup(`batch-${index}`, {
        id: `batch-${index}`,
        label: `Run ${index}`,
        subtitle: `batch ${index}/${total}`,
        kind: 'batch',
        index,
      }).lines.push(cleanedLine);
      return;
    }

    ensureGroup('runner', {
      id: 'runner',
      label: 'Runner',
      subtitle: 'orchestrator',
      kind: 'runner',
      index: 0,
    }).lines.push(line);
  });

  return Array.from(groups.values()).sort((a, b) => a.index - b.index);
}

function StepperControl({ label, icon: Icon, value, min, max, disabled, onChange, suffix }: StepperControlProps) {
  const commit = (nextValue: number) => onChange(clampNumber(nextValue, min, max));

  return (
    <div className="field run-stepper-field">
      <span className="field-label">{label}</span>
      <div className="run-stepper">
        <span className="run-stepper__icon"><Icon size={15} /></span>
        <button
          type="button"
          className="run-stepper__button tooltip-target tooltip-target--compact"
          aria-label={`Decrease ${label}`}
          data-tooltip="Decrease"
          onClick={() => commit(value - 1)}
          disabled={disabled || value <= min}
        >
          <Minus size={14} />
        </button>
        <input
          className="run-stepper__input"
          type="number"
          min={min}
          max={max}
          value={value}
          onChange={(event) => commit(Number(event.target.value))}
          disabled={disabled}
          aria-label={label}
        />
        {suffix && <span className="run-stepper__suffix">{suffix}</span>}
        <button
          type="button"
          className="run-stepper__button tooltip-target tooltip-target--compact"
          aria-label={`Increase ${label}`}
          data-tooltip="Increase"
          onClick={() => commit(value + 1)}
          disabled={disabled || value >= max}
        >
          <Plus size={14} />
        </button>
      </div>
    </div>
  );
}

export default function RunPage() {
  const config = useConfigStore(s => s.config);
  const batchItems = useBatchQueueStore(s => s.items);
  const addConfigToQueue = useBatchQueueStore(s => s.addConfig);
  const addItemsToQueue = useBatchQueueStore(s => s.addItems);
  const removeBatchItem = useBatchQueueStore(s => s.removeItem);
  const clearBatchQueue = useBatchQueueStore(s => s.clear);
  const { status, logs, setStatus, appendLog, clearLogs } = useSimStore();
  const logRef = useRef<HTMLDivElement>(null);
  const wsRef = useRef<WebSocket | null>(null);
  const autoRunHandledRef = useRef(false);
  const batchFileRef = useRef<HTMLInputElement>(null);
  const queuePulseTimerRef = useRef<number | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [postprocessMode, setPostprocessMode] = useState<PostprocessMode>('exports');
  const [maxParallel, setMaxParallel] = useState(1);
  const [threadsPerRun, setThreadsPerRun] = useState(1);
  const [queuePulse, setQueuePulse] = useState(false);
  const location = useLocation();
  const navigate = useNavigate();

  const triggerQueuePulse = useCallback(() => {
    if (queuePulseTimerRef.current !== null) {
      window.clearTimeout(queuePulseTimerRef.current);
    }
    setQueuePulse(false);
    window.requestAnimationFrame(() => setQueuePulse(true));
    queuePulseTimerRef.current = window.setTimeout(() => setQueuePulse(false), 560);
  }, []);

  useEffect(() => () => {
    if (queuePulseTimerRef.current !== null) {
      window.clearTimeout(queuePulseTimerRef.current);
    }
  }, []);

  // Auto-scroll terminal
  useEffect(() => {
    if (logRef.current) {
      logRef.current.scrollTop = logRef.current.scrollHeight;
    }
  }, [logs]);

  // Poll status while running
  useEffect(() => {
    if (status.status !== 'running') return;
    const iv = setInterval(async () => {
      const s = await getSimulationStatus();
      setStatus(s);
      if (s.status !== 'running') clearInterval(iv);
    }, 2000);
    return () => clearInterval(iv);
  }, [status.status, setStatus]);

  const handleBatchFiles = async (event: ChangeEvent<HTMLInputElement>) => {
    const files = Array.from(event.target.files ?? []);
    event.target.value = '';
    if (files.length === 0) return;

    setError(null);
    try {
      const imported = await Promise.all(files.map(async (file) => {
        const parsed = JSON.parse(await file.text()) as unknown;
        return extractBatchImportItems(parsed, file.name.replace(/\.json$/i, ''));
      }));
      const importedItems = imported.flat();
      if (importedItems.length === 0) {
        setError('No runnable configs found in imported JSON');
        return;
      }
      addItemsToQueue(importedItems);
      triggerQueuePulse();
    } catch {
      setError('Invalid batch JSON file');
    }
  };

  const exportBatchJson = () => {
    if (batchItems.length === 0) return;

    const manifest = {
      runs: batchItems.map(({ name, config }) => ({
        name,
        config,
      })),
    };
    const blob = new Blob([JSON.stringify(manifest, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'cellevox_batch_queue.json';
    a.click();
    window.setTimeout(() => URL.revokeObjectURL(url), 0);
  };

  const addCurrentConfigToBatch = () => {
    addConfigToQueue(buildConfigPayload(config));
    triggerQueuePulse();
  };

  const handleStartBatch = useCallback(async () => {
    if (batchItems.length === 0) return;
    const launchConfigs = batchItems.map(item => item.config);
    const artifactError = validateResultArtifacts(launchConfigs);
    if (artifactError) {
      setError(artifactError);
      return;
    }

    setError(null);
    clearLogs();
    try {
      const res = await startSimulationBatch(launchConfigs, {
        maxParallel,
        threadsPerRun,
        postprocess: postprocessMode,
      });
      setStatus({
        status: 'running',
        run_id: res.data.batch_id,
        current_run_id: null,
        run_index: 0,
        run_total: res.data.count,
        parallelism: res.data.parallelism,
        elapsed_seconds: 0,
      });

      wsRef.current?.close();
      wsRef.current = createLogSocket(
        (line) => appendLog(line),
        () => {
          getSimulationStatus()
            .then(setStatus)
            .catch(() => setStatus({ status: 'finished', run_id: res.data.batch_id, elapsed_seconds: null }));
        },
      );
    } catch (e: unknown) {
      setError(getErrorMessage(e));
      setStatus({ status: 'error', run_id: null, elapsed_seconds: null });
    }
  }, [appendLog, batchItems, clearLogs, maxParallel, postprocessMode, setStatus, threadsPerRun]);

  useEffect(() => {
    const state = location.state as { autoRunQueue?: boolean } | null;
    if (!state?.autoRunQueue || autoRunHandledRef.current) return;
    if (status.status === 'running' || batchItems.length === 0) return;
    autoRunHandledRef.current = true;
    navigate('/run', { replace: true });
    const timeout = window.setTimeout(() => {
      void handleStartBatch();
    }, 0);
    return () => window.clearTimeout(timeout);
  }, [batchItems.length, handleStartBatch, location.state, navigate, status.status]);

  const handleStop = async () => {
    await stopSimulation();
    wsRef.current?.close();
    setStatus({ ...status, status: 'stopped' });
  };

  const isRunning = status.status === 'running';
  const queuedConfigs = batchItems.map(item => item.config);
  const totalSteps = totalNumber(queuedConfigs, 'steps');
  const totalGenerationEstimate = totalGenerations(queuedConfigs);
  const logGroups = useMemo(() => groupLogLines(logs), [logs]);
  const hasSplitLogs = logGroups.some(group => group.kind === 'batch') && logGroups.length > 1;
  const batchProgress = status.run_total && status.run_total > 1
    ? `${status.run_index ?? 0}/${status.run_total}`
    : batchItems.length > 0 ? `${batchItems.length} queued` : 'empty';
  const analysisLabel = postprocessMode === 'exports' ? 'Exports' : 'Full';
  const activeWorkers = isRunning ? status.parallelism ?? maxParallel : maxParallel;
  const activeThreads = threadsPerRun;

  const renderTerminalLines = (lines: string[]) => {
    if (lines.length === 0) {
      return <div className="terminal-line terminal-line--muted">No output yet</div>;
    }

    return lines.map((line, index) => (
      <div key={`${index}-${line.slice(0, 12)}`} className="terminal-line">{line}</div>
    ));
  };

  return (
    <div className="page-content page-content--wide fade-up">
      <div className="run-page-header">
        <div>
          <h1>Run Queue</h1>
          <p>Launch and monitor queued simulations</p>
        </div>
        <div className="run-header-actions">
          <span className={`queue-count-pill ${queuePulse ? 'is-bumping' : ''}`}>Queue {batchItems.length}</span>
          <span className={`badge badge--${status.status}`}>{status.status}</span>
        </div>
      </div>

      <div className="run-metrics fade-up-1">
        <div className={`run-metric ${queuePulse ? 'is-bumping' : ''}`}>
          <span className="run-metric__icon"><ListChecks size={16} /></span>
          <span className="run-metric__label">Queued</span>
          <strong>{batchItems.length}</strong>
        </div>
        <div className="run-metric">
          <span className="run-metric__icon"><Rows3 size={16} /></span>
          <span className="run-metric__label">Total Steps</span>
          <strong>{formatCompactTotal(totalSteps)}</strong>
        </div>
        <div className="run-metric">
          <span className="run-metric__icon"><Activity size={16} /></span>
          <span className="run-metric__label">Generations</span>
          <strong>{formatCompactTotal(totalGenerationEstimate)}</strong>
        </div>
        <div className="run-metric">
          <span className="run-metric__icon"><Layers size={16} /></span>
          <span className="run-metric__label">{status.run_total && status.run_total > 1 ? 'Batch' : 'Elapsed'}</span>
          <strong>{status.run_total && status.run_total > 1 ? batchProgress : formatElapsed(status.elapsed_seconds)}</strong>
        </div>
      </div>

      <div className={`card run-launch-panel fade-up-2 ${isRunning ? 'pulse-border' : ''}`}>
        <div className="section-header run-section-header">
          <div className="icon"><SlidersHorizontal size={16} /></div>
          <div>
            <h3>Launch Controls</h3>
            <p>{analysisLabel} mode, {activeWorkers} parallel, {activeThreads} threads/run</p>
          </div>
        </div>

        <div className="run-control-grid">
          <div className="field run-mode-field">
            <span className="field-label">Analysis Mode</span>
            <div className="segmented-control">
              <button
                type="button"
                className={`segmented-control__item ${postprocessMode === 'exports' ? 'is-active' : ''}`}
                onClick={() => setPostprocessMode('exports')}
                disabled={isRunning}
              >
                Exports only
              </button>
              <button
                type="button"
                className={`segmented-control__item ${postprocessMode === 'full' ? 'is-active' : ''}`}
                onClick={() => setPostprocessMode('full')}
                disabled={isRunning}
              >
                Full
              </button>
            </div>
          </div>

          <StepperControl
            label="Parallel Runs"
            icon={Rows3}
            min={1}
            max={72}
            value={maxParallel}
            disabled={isRunning}
            onChange={setMaxParallel}
          />

          <StepperControl
            label="Threads / Run"
            icon={Cpu}
            min={1}
            max={64}
            value={threadsPerRun}
            disabled={isRunning}
            onChange={setThreadsPerRun}
          />

          <div className="run-options-summary run-options-summary--compact">
            <Activity size={16} />
            <span>{analysisLabel} · {maxParallel} x {threadsPerRun}</span>
          </div>
        </div>

        <div className="run-launch-actions">
          {isRunning ? (
            <button id="btn-stop-sim" className="btn btn--danger btn--lg" onClick={handleStop}>
              <Square size={18} /> Stop Simulation
            </button>
          ) : (
            <button
              id="btn-start-sim"
              className="btn btn--primary btn--lg"
              onClick={handleStartBatch}
              disabled={status.status === 'running' || batchItems.length === 0}
            >
              {status.status === 'running'
                ? <><Loader size={18} className="spin" /> Running...</>
                : <><Play size={18} /> Run Queue</>}
            </button>
          )}
          {status.run_id && (
            <span className="run-run-id">Run: {status.run_id}</span>
          )}
          {error && (
            <span className="run-error">! {error}</span>
          )}
        </div>
      </div>

      <div className="card run-queue-panel fade-up-2">
        <div className="section-header run-section-header">
          <div className="icon"><ListChecks size={16} /></div>
          <div>
            <h3>Batch Queue</h3>
            <p>{batchItems.length === 0 ? 'No configs queued' : `${batchItems.length} config${batchItems.length === 1 ? '' : 's'} ready`}</p>
          </div>
          <div className="queue-toolbar">
            <button
              className={`btn btn--ghost btn--compact ${queuePulse ? 'is-popping' : ''}`}
              onClick={addCurrentConfigToBatch}
              disabled={isRunning}
            >
              <Plus size={15} /> Add to Queue
            </button>
            <button className="btn btn--ghost btn--compact" onClick={() => batchFileRef.current?.click()} disabled={isRunning}>
              <FileUp size={15} /> Import JSON
            </button>
            <input ref={batchFileRef} type="file" accept=".json" multiple hidden onChange={handleBatchFiles} />
            <button className="btn btn--ghost btn--compact" onClick={exportBatchJson} disabled={isRunning || batchItems.length === 0}>
              <FileDown size={15} /> Export JSON
            </button>
            <button className="btn btn--primary btn--compact" onClick={handleStartBatch} disabled={isRunning || batchItems.length === 0}>
              <Play size={15} /> Run Batch
            </button>
          </div>
        </div>

        {batchItems.length === 0 ? (
          <div className="run-empty-state">
            <ListChecks size={18} />
            <span>No queued configs.</span>
          </div>
        ) : (
          <div className="run-queue-list">
            {batchItems.map((item, index) => {
              const description = describeBatchConfig(item.config);
              return (
                <div key={item.id} className="run-queue-item">
                  <span className="run-queue-item__index">#{index + 1}</span>
                  <div className="run-queue-item__body">
                    <div className="run-queue-item__title">{description.title}</div>
                    <div className="run-queue-item__details">
                      {description.details.join(' | ')} | {description.mutationSummary}
                    </div>
                    {description.mutationTokens.length > 0 && (
                      <div className="run-queue-item__chips">
                        {description.mutationTokens.map(token => (
                          <span
                            key={`${token.isDriver ? 'driver' : 'passenger'}-${token.id}`}
                            className={`batch-mutation-chip ${token.isDriver ? 'batch-mutation-chip--driver' : 'batch-mutation-chip--passenger'}`}
                          >
                            <span className="batch-mutation-chip__kind">
                              {token.isDriver ? 'D' : 'P'}
                            </span>
                            <span className="batch-mutation-chip__id">#{token.id}</span>
                            <span className={`batch-mutation-chip__effect batch-mutation-chip__effect--${token.effectDirection}`}>
                              {token.effect}
                            </span>
                            <span className="batch-mutation-chip__probability">
                              {token.probability}
                            </span>
                          </span>
                        ))}
                        {description.mutationOverflowCount > 0 && (
                          <span className="batch-mutation-chip batch-mutation-chip--overflow">
                            +{description.mutationOverflowCount} more
                          </span>
                        )}
                      </div>
                    )}
                  </div>
                  <button
                    className="btn btn--icon btn--danger tooltip-target tooltip-target--compact"
                    aria-label="Remove from batch"
                    data-tooltip="Remove from batch"
                    onClick={() => removeBatchItem(item.id)}
                    disabled={isRunning}
                  >
                    <Trash2 size={14} />
                  </button>
                </div>
              );
            })}
            <button className="btn btn--ghost btn--compact run-clear-queue" onClick={clearBatchQueue} disabled={isRunning}>
              <Trash2 size={14} /> Clear Batch
            </button>
          </div>
        )}
      </div>

      <div className="card run-output-card fade-up-3">
        <div className="section-header run-section-header">
          <div className="icon"><Terminal size={16} /></div>
          <div>
            <h3>Live Output</h3>
            <p>{hasSplitLogs ? `${logGroups.length} output streams` : `${logs.length} lines`}</p>
          </div>
          <div className="queue-toolbar">
            <button className="btn btn--ghost btn--compact" onClick={clearLogs}>Clear</button>
          </div>
        </div>

        {logs.length === 0 ? (
          <div id="terminal-output" className="terminal" ref={logRef}>
            <span className="terminal-line--muted">
              {isRunning ? 'Connecting...' : '$ Awaiting simulation launch...'}
              {isRunning && <span className="terminal-cursor" />}
            </span>
          </div>
        ) : hasSplitLogs ? (
          <div id="terminal-output" className="terminal-grid" ref={logRef}>
            {logGroups.map(group => (
              <div key={group.id} className={`terminal-panel terminal-panel--${group.kind}`}>
                <div className="terminal-panel__header">
                  <strong>{group.label}</strong>
                  <span>{group.subtitle} · {group.lines.length} lines</span>
                </div>
                <div className="terminal terminal--panel">
                  {renderTerminalLines(group.lines)}
                  {isRunning && <div className="terminal-cursor" />}
                </div>
              </div>
            ))}
          </div>
        ) : (
          <div id="terminal-output" className="terminal" ref={logRef}>
            {renderTerminalLines(logs)}
            {isRunning && <div className="terminal-cursor" />}
          </div>
        )}
      </div>
    </div>
  );
}
