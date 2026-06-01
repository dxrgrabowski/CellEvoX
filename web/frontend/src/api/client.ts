import axios from 'axios';
import type { SimulationConfig, SimulationStatus, RunMeta, RunSummaryData, StatsData, MullerData } from '../types/simulation';

const api = axios.create({ baseURL: '/api' });

// ── Simulation ────────────────────────────────────────────────────────────────
export const startSimulation = (config: Record<string, unknown>) =>
  api.post<{ status: string; run_id: string }>('/simulation/start', { config });

export interface BatchStartOptions {
  continueOnError?: boolean;
  maxParallel?: number;
  threadsPerRun?: number | null;
  postprocess?: 'full' | 'exports';
}

export const startSimulationBatch = (
  configs: Record<string, unknown>[],
  options: BatchStartOptions = {},
) =>
  api.post<{ status: string; run_id: string; batch_id: string; count: number; parallelism: number }>(
    '/simulation/batch/start',
    {
      configs,
      continue_on_error: options.continueOnError ?? false,
      max_parallel: options.maxParallel ?? 1,
      threads_per_run: options.threadsPerRun ?? null,
      postprocess: options.postprocess ?? 'exports',
    },
  );

export const stopSimulation = () =>
  api.post('/simulation/stop');

export const getSimulationStatus = () =>
  api.get<SimulationStatus>('/simulation/status').then(r => r.data);

// ── Results ───────────────────────────────────────────────────────────────────
export const listRuns = () =>
  api.get<RunMeta[]>('/results').then(r => r.data);

export const getRunConfig = (runId: string) =>
  api.get<SimulationConfig>(`/results/${encodeURIComponent(runId)}/config`).then(r => r.data);

export const getRunSummary = (runId: string) =>
  api.get<RunSummaryData>(`/results/${encodeURIComponent(runId)}/summary`).then(r => r.data);

export const getRunStats = (runId: string) =>
  api.get<StatsData>(`/results/${encodeURIComponent(runId)}/stats`).then(r => r.data);

export const getMullerData = (runId: string) =>
  api.get<MullerData>(`/results/${encodeURIComponent(runId)}/muller`).then(r => r.data);

export const analyzeRun = (runId: string, threadsPerRun?: number) =>
  api.post<{ status: string; run_id: string; analysis_id: string }>(
    `/results/${encodeURIComponent(runId)}/analyze`,
    { threads_per_run: threadsPerRun ?? null },
  );

// ── WebSocket log stream helper ───────────────────────────────────────────────
export function createLogSocket(
  onMessage: (line: string) => void,
  onDone: () => void,
): WebSocket {
  const wsUrl = `ws://${window.location.host}/ws/logs`;
  const ws = new WebSocket(wsUrl);
  ws.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data as string);
      if (msg.type === 'log') onMessage(msg.data as string);
      else if (msg.type === 'done') onDone();
    } catch { /* ignore */ }
  };
  return ws;
}
