import { useCallback, useEffect, useRef, useState, type ChangeEvent } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { FileUp, Loader, Plus, Play, Square, Terminal, Trash2 } from 'lucide-react';
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
  const [error, setError] = useState<string | null>(null);
  const location = useLocation();
  const navigate = useNavigate();

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
    } catch {
      setError('Invalid batch JSON file');
    }
  };

  const addCurrentConfigToBatch = () => {
    addConfigToQueue(buildConfigPayload(config));
  };

  const handleStartBatch = useCallback(async () => {
    if (batchItems.length === 0) return;
    setError(null);
    clearLogs();
    try {
      const res = await startSimulationBatch(batchItems.map(item => item.config));
      setStatus({
        status: 'running',
        run_id: res.data.batch_id,
        current_run_id: null,
        run_index: 0,
        run_total: res.data.count,
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
  }, [appendLog, batchItems, clearLogs, setStatus]);

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
  const batchProgress = status.run_total && status.run_total > 1
    ? `${status.run_index ?? 0}/${status.run_total}`
    : batchItems.length > 0 ? `${batchItems.length} queued` : 'empty';

  return (
    <div className="page-content page-content--wide fade-up">
      <div className="flex items-center justify-between" style={{ marginBottom: '28px' }}>
        <div>
          <h1>Run Queue</h1>
          <p style={{ color: 'var(--text-muted)', marginTop: 4, fontSize: '0.9rem' }}>
            Launch and monitor queued simulations
          </p>
        </div>
        <span className={`badge badge--${status.status}`} style={{ fontSize: '0.85rem', padding: '6px 14px' }}>
          {status.status}
        </span>
      </div>

      {/* Stats row */}
      <div className="grid-4 fade-up-1" style={{ marginBottom: 24 }}>
        {[
          { label: 'Queued', value: batchItems.length },
          { label: 'Total Steps', value: formatCompactTotal(totalSteps) },
          { label: 'Generations', value: formatCompactTotal(totalGenerationEstimate) },
          { label: status.run_total && status.run_total > 1 ? 'Batch' : 'Elapsed', value: status.run_total && status.run_total > 1 ? batchProgress : formatElapsed(status.elapsed_seconds) },
        ].map(({ label, value }) => (
          <div key={label} className="card" style={{ padding: '16px 20px' }}>
            <div style={{ color: 'var(--text-muted)', fontSize: '0.75rem', textTransform: 'uppercase', letterSpacing: '0.06em' }}>
              {label}
            </div>
            <div style={{ fontFamily: 'var(--font-mono)', fontSize: '1.1rem', marginTop: 4, color: 'var(--text-primary)' }}>
              {String(value)}
            </div>
          </div>
        ))}
      </div>

      {/* Queue launch */}
      <div className={`card fade-up-2 ${isRunning ? 'pulse-border' : ''}`} style={{ marginBottom: 24 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
          {isRunning ? (
            <button id="btn-stop-sim" className="btn btn--danger btn--lg" onClick={handleStop}>
              <Square size={18} /> Stop Simulation
            </button>
          ) : (
            <button id="btn-start-sim" className="btn btn--primary btn--lg" onClick={handleStartBatch}
              disabled={status.status === 'running' || batchItems.length === 0}>
              {status.status === 'running'
                ? <><Loader size={18} className="spin" /> Running…</>
                : <><Play size={18} /> Run Queue</>}
            </button>
          )}
          {status.run_id && (
            <span style={{ fontFamily: 'var(--font-mono)', fontSize: '0.8rem', color: 'var(--text-muted)' }}>
              Run: {status.run_id}
            </span>
          )}
          {error && (
            <span style={{ color: 'var(--negative)', fontSize: '0.875rem' }}>⚠ {error}</span>
          )}
        </div>
      </div>

      {/* Batch queue */}
      <div className="card fade-up-2" style={{ marginBottom: 24 }}>
        <div className="section-header">
          <h3>Batch Queue</h3>
          <div style={{ marginLeft: 'auto', display: 'flex', gap: 8 }}>
            <button className="btn btn--ghost" style={{ padding: '6px 12px', fontSize: '0.82rem' }}
              onClick={addCurrentConfigToBatch} disabled={isRunning}>
              <Plus size={15} /> Add Current
            </button>
            <button className="btn btn--ghost" style={{ padding: '6px 12px', fontSize: '0.82rem' }}
              onClick={() => batchFileRef.current?.click()} disabled={isRunning}>
              <FileUp size={15} /> Import JSON
            </button>
            <input ref={batchFileRef} type="file" accept=".json" multiple hidden onChange={handleBatchFiles} />
            <button className="btn btn--primary" style={{ padding: '6px 12px', fontSize: '0.82rem' }}
              onClick={handleStartBatch} disabled={isRunning || batchItems.length === 0}>
              <Play size={15} /> Run Batch
            </button>
          </div>
        </div>
        {batchItems.length === 0 ? (
          <div style={{ color: 'var(--text-muted)', fontSize: '0.86rem' }}>
            No queued configs.
          </div>
        ) : (
          <div className="flex-col gap-8">
            {batchItems.map((item, index) => {
              const description = describeBatchConfig(item.config);
              return (
                <div
                  key={item.id}
                  style={{
                    display: 'grid',
                    gridTemplateColumns: '42px minmax(0, 1fr) auto',
                    alignItems: 'center',
                    gap: 14,
                    padding: '12px 12px',
                    border: '1px solid var(--border-subtle)',
                    borderRadius: 'var(--radius-sm)',
                    background: 'rgba(255,255,255,0.025)',
                  }}
                >
                  <span style={{ color: 'var(--text-muted)', fontFamily: 'var(--font-mono)', fontSize: '0.78rem' }}>
                    #{index + 1}
                  </span>
                  <div style={{ minWidth: 0 }}>
                    <div style={{
                      overflow: 'hidden',
                      textOverflow: 'ellipsis',
                      whiteSpace: 'nowrap',
                      color: 'var(--text-primary)',
                      fontSize: '0.9rem',
                      fontWeight: 600,
                    }}>
                      {description.title}
                    </div>
                    <div style={{
                      overflow: 'hidden',
                      marginTop: 2,
                      color: 'var(--text-muted)',
                      fontFamily: 'var(--font-mono)',
                      fontSize: '0.72rem',
                      textOverflow: 'ellipsis',
                      whiteSpace: 'nowrap',
                    }}>
                      {description.details.join(' | ')} | {description.mutationSummary}
                    </div>
                    {description.mutationTokens.length > 0 && (
                      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 8 }}>
                        {description.mutationTokens.map(token => (
                          <span
                            key={`${token.isDriver ? 'driver' : 'passenger'}-${token.id}`}
                            className={`batch-mutation-chip ${token.isDriver ? 'batch-mutation-chip--driver' : 'batch-mutation-chip--passenger'}`}
                          >
                            <span className="batch-mutation-chip__kind">
                              {token.isDriver ? '⚡ D' : 'P'}
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
            <button className="btn btn--ghost" style={{ alignSelf: 'flex-start', padding: '6px 12px', fontSize: '0.82rem' }}
              onClick={clearBatchQueue} disabled={isRunning}>
              Clear Batch
            </button>
          </div>
        )}
      </div>

      {/* Terminal */}
      <div className="card fade-up-3">
        <div className="section-header">
          <div className="icon"><Terminal size={16} /></div>
          <h3>Live Output</h3>
          <div style={{ marginLeft: 'auto', display: 'flex', gap: 8 }}>
            <button className="btn btn--ghost" style={{ padding: '4px 10px', fontSize: '0.8rem' }}
              onClick={clearLogs}>Clear</button>
          </div>
        </div>
        <div id="terminal-output" className="terminal" ref={logRef}>
          {logs.length === 0 ? (
            <span style={{ color: 'var(--text-muted)' }}>
              {isRunning ? '⟳ Connecting...' : '$ Awaiting simulation launch...'}
              {isRunning && <span className="terminal-cursor" />}
            </span>
          ) : (
            logs.map((line, i) => (
              <div key={i} className="terminal-line">{line}</div>
            ))
          )}
          {isRunning && logs.length > 0 && <div className="terminal-cursor" />}
        </div>
      </div>
    </div>
  );
}
