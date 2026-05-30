import { useEffect, useRef, useState } from 'react';
import { Play, Square, Loader, Terminal } from 'lucide-react';
import { useConfigStore, useSimStore } from '../stores';
import { startSimulation, stopSimulation, getSimulationStatus, createLogSocket } from '../api/client';

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

export default function RunPage() {
  const config = useConfigStore(s => s.config);
  const { status, logs, setStatus, appendLog, clearLogs } = useSimStore();
  const logRef = useRef<HTMLDivElement>(null);
  const wsRef = useRef<WebSocket | null>(null);
  const [error, setError] = useState<string | null>(null);

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

  const handleStart = async () => {
    setError(null);
    clearLogs();
    try {
      const res = await startSimulation(config);
      setStatus({ status: 'running', run_id: res.data.run_id, elapsed_seconds: 0 });

      // Open WebSocket for live logs
      wsRef.current?.close();
      wsRef.current = createLogSocket(
        (line) => appendLog(line),
        () => setStatus({ ...status, status: 'finished' }),
      );
    } catch (e: unknown) {
      setError(getErrorMessage(e));
      setStatus({ status: 'error', run_id: null, elapsed_seconds: null });
    }
  };

  const handleStop = async () => {
    await stopSimulation();
    wsRef.current?.close();
    setStatus({ status: 'stopped', run_id: status.run_id, elapsed_seconds: status.elapsed_seconds });
  };

  const isRunning = status.status === 'running';

  return (
    <div className="page-content fade-up">
      <div className="flex items-center justify-between" style={{ marginBottom: '28px' }}>
        <div>
          <h1>Run Simulation</h1>
          <p style={{ color: 'var(--text-muted)', marginTop: 4, fontSize: '0.9rem' }}>
            Launch and monitor your configured simulation
          </p>
        </div>
        <span className={`badge badge--${status.status}`} style={{ fontSize: '0.85rem', padding: '6px 14px' }}>
          {status.status}
        </span>
      </div>

      {/* Stats row */}
      <div className="grid-4 fade-up-1" style={{ marginBottom: 24 }}>
        {[
          { label: 'Mode', value: config.simulation_mode.replace(/_/g, ' ') },
          { label: 'Steps', value: config.steps.toLocaleString() },
          { label: 'τ Step', value: config.tau_step },
          { label: 'Elapsed', value: formatElapsed(status.elapsed_seconds) },
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

      {/* Launch button */}
      <div className={`card fade-up-2 ${isRunning ? 'pulse-border' : ''}`} style={{ marginBottom: 24 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
          {isRunning ? (
            <button id="btn-stop-sim" className="btn btn--danger btn--lg" onClick={handleStop}>
              <Square size={18} /> Stop Simulation
            </button>
          ) : (
            <button id="btn-start-sim" className="btn btn--primary btn--lg" onClick={handleStart}
              disabled={status.status === 'running'}>
              {status.status === 'running'
                ? <><Loader size={18} className="spin" /> Running…</>
                : <><Play size={18} /> Launch Simulation</>}
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
