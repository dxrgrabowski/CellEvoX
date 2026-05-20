import { create } from 'zustand';
import type { SimulationConfig, SimulationStatus } from '../types/simulation';
import { DEFAULT_CONFIG } from '../types/simulation';

// ── Config Store ──────────────────────────────────────────────────────────────
interface ConfigStore {
  config: SimulationConfig;
  setConfig: (patch: Partial<SimulationConfig>) => void;
  setFullConfig: (config: SimulationConfig) => void;
  resetConfig: () => void;
}

export const useConfigStore = create<ConfigStore>((set) => ({
  config: DEFAULT_CONFIG,
  setConfig: (patch) => set((s) => ({ config: { ...s.config, ...patch } })),
  setFullConfig: (config) => set({ config }),
  resetConfig: () => set({ config: DEFAULT_CONFIG }),
}));

// ── Simulation Status Store ───────────────────────────────────────────────────
interface SimStore {
  status: SimulationStatus;
  logs: string[];
  setStatus: (status: SimulationStatus) => void;
  appendLog: (line: string) => void;
  clearLogs: () => void;
}

export const useSimStore = create<SimStore>((set) => ({
  status: { status: 'idle', run_id: null, elapsed_seconds: null },
  logs: [],
  setStatus: (status) => set({ status }),
  appendLog: (line) => set((s) => ({ logs: [...s.logs.slice(-500), line] })),
  clearLogs: () => set({ logs: [] }),
}));

// ── Results Store ─────────────────────────────────────────────────────────────
interface ResultsStore {
  selectedRunId: string | null;
  activeTab: string;
  setSelectedRunId: (id: string | null) => void;
  setActiveTab: (tab: string) => void;
}

export const useResultsStore = create<ResultsStore>((set) => ({
  selectedRunId: null,
  activeTab: 'stats',
  setSelectedRunId: (id) => set({ selectedRunId: id, activeTab: 'stats' }),
  setActiveTab: (tab) => set({ activeTab: tab }),
}));
