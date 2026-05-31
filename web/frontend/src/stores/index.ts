import { create } from 'zustand';
import type { BatchQueueItem, SimulationConfig, SimulationMode, SimulationStatus } from '../types/simulation';
import { DEFAULT_CONFIG } from '../types/simulation';
import { createBatchQueueItem } from '../utils/batchQueue';

// ── Config Store ──────────────────────────────────────────────────────────────
type ImportedSimulationConfig = Partial<SimulationConfig> & {
  stochastic?: boolean;
};

interface ConfigStore {
  config: SimulationConfig;
  setConfig: (patch: Partial<SimulationConfig>) => void;
  setFullConfig: (config: ImportedSimulationConfig) => void;
  resetConfig: () => void;
}

function inferSimulationMode(config: ImportedSimulationConfig): SimulationMode {
  if (config.simulation_mode) return config.simulation_mode;
  if (config.stochastic === false) return 'deterministic';
  return DEFAULT_CONFIG.simulation_mode;
}

function normalizeConfig(config: ImportedSimulationConfig): SimulationConfig {
  const { stochastic: _legacyStochastic, ...rest } = config;
  void _legacyStochastic;
  return {
    ...DEFAULT_CONFIG,
    ...rest,
    simulation_mode: inferSimulationMode(config),
  };
}

export const useConfigStore = create<ConfigStore>((set) => ({
  config: DEFAULT_CONFIG,
  setConfig: (patch) => set((s) => ({ config: { ...s.config, ...patch } })),
  setFullConfig: (config) => set({ config: normalizeConfig(config) }),
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

// ── Batch Queue Store ────────────────────────────────────────────────────────
const BATCH_QUEUE_STORAGE_KEY = 'cellevox.batchQueue.v1';

interface BatchQueueStore {
  items: BatchQueueItem[];
  addConfig: (config: Record<string, unknown>, name?: string) => void;
  addItems: (items: Array<{ config: Record<string, unknown>; name?: string }>) => void;
  removeItem: (id: string) => void;
  clear: () => void;
}

function readStoredBatchQueue(): BatchQueueItem[] {
  if (typeof window === 'undefined') return [];
  try {
    const raw = window.localStorage.getItem(BATCH_QUEUE_STORAGE_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as unknown;
    if (!Array.isArray(parsed)) return [];
    return parsed.filter((item): item is BatchQueueItem => (
      typeof item === 'object' &&
      item !== null &&
      'id' in item &&
      'name' in item &&
      'config' in item &&
      'created_at' in item
    ));
  } catch {
    return [];
  }
}

function storeBatchQueue(items: BatchQueueItem[]) {
  if (typeof window === 'undefined') return;
  window.localStorage.setItem(BATCH_QUEUE_STORAGE_KEY, JSON.stringify(items));
}

export const useBatchQueueStore = create<BatchQueueStore>((set) => ({
  items: readStoredBatchQueue(),
  addConfig: (config, name) => set((state) => {
    const items = [...state.items, createBatchQueueItem(config, name)];
    storeBatchQueue(items);
    return { items };
  }),
  addItems: (incomingItems) => set((state) => {
    const items = [
      ...state.items,
      ...incomingItems.map(item => createBatchQueueItem(item.config, item.name)),
    ];
    storeBatchQueue(items);
    return { items };
  }),
  removeItem: (id) => set((state) => {
    const items = state.items.filter(item => item.id !== id);
    storeBatchQueue(items);
    return { items };
  }),
  clear: () => {
    storeBatchQueue([]);
    set({ items: [] });
  },
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
  activeTab: 'summary',
  setSelectedRunId: (id) => set({ selectedRunId: id, activeTab: 'summary' }),
  setActiveTab: (tab) => set({ activeTab: tab }),
}));
