import { create } from 'zustand';
import type { BatchQueueItem, MutationCountModel, SimulationConfig, SimulationStatus } from '../types/simulation';
import { DEFAULT_CONFIG, DEFAULT_MUTATION_LAMBDA, DEFAULT_MUTATION_PROBABILITY } from '../types/simulation';
import { createBatchQueueItem } from '../utils/batchQueue';

// ── Config Store ──────────────────────────────────────────────────────────────
type ImportedSimulationConfig = Partial<SimulationConfig> & {
  stochastic?: boolean;
};

interface ConfigStore {
  config: SimulationConfig;
  inactiveMutationCountValues: ReadonlyMap<number, number>;
  setConfig: (patch: Partial<SimulationConfig>) => void;
  setMutationCountModel: (id: number, model: MutationCountModel) => void;
  setFullConfig: (config: ImportedSimulationConfig) => void;
  resetConfig: () => void;
}

const SUPPORTED_MODES: readonly string[] = ['stochastic', 'spatial_3d_capacity'];

function normalizeConfig(config: ImportedSimulationConfig): SimulationConfig {
  if (config.stochastic === false) {
    throw new Error(
      "Unsupported 'stochastic: false': the deterministic RK4 mode has been removed. " +
      "Use 'simulation_mode': 'stochastic' or 'spatial_3d_capacity'.",
    );
  }
  if (config.simulation_mode !== undefined && !SUPPORTED_MODES.includes(config.simulation_mode)) {
    throw new Error(
      `Unsupported 'simulation_mode': '${String(config.simulation_mode)}'. ` +
      "Supported modes: stochastic, spatial_3d_capacity.",
    );
  }
  const { stochastic: _legacyStochastic, ...rest } = config;
  void _legacyStochastic;
  return { ...DEFAULT_CONFIG, ...rest };
}

export const useConfigStore = create<ConfigStore>((set) => ({
  config: DEFAULT_CONFIG,
  inactiveMutationCountValues: new Map(),
  setConfig: (patch) => set((state) => {
    const config = { ...state.config, ...patch };
    if (!patch.mutations || state.inactiveMutationCountValues.size === 0) return { config };

    const ids = new Set(patch.mutations.map(mutation => mutation.id));
    return {
      config,
      inactiveMutationCountValues: new Map(
        [...state.inactiveMutationCountValues].filter(([id]) => ids.has(id)),
      ),
    };
  }),
  setMutationCountModel: (id, model) => set((state) => {
    const mutation = state.config.mutations.find(entry => entry.id === id);
    if (!mutation || (mutation.count_model ?? 'bernoulli') === model) return state;

    // Keep the inactive parameter outside the serialized configuration.
    const { probability, lambda, ...fields } = mutation;
    const inactiveMutationCountValues = new Map(state.inactiveMutationCountValues);
    const savedValue = inactiveMutationCountValues.get(id);
    const nextMutation = model === 'poisson'
      ? { ...fields, count_model: model, lambda: savedValue ?? DEFAULT_MUTATION_LAMBDA }
      : { ...fields, count_model: model, probability: savedValue ?? DEFAULT_MUTATION_PROBABILITY };

    const previousValue = model === 'poisson' ? probability : lambda;
    if (previousValue === undefined) {
      inactiveMutationCountValues.delete(id);
    } else {
      inactiveMutationCountValues.set(id, previousValue);
    }

    return {
      config: {
        ...state.config,
        mutations: state.config.mutations.map(entry => entry === mutation ? nextMutation : entry),
      },
      inactiveMutationCountValues,
    };
  }),
  setFullConfig: (config) => set({ config: normalizeConfig(config), inactiveMutationCountValues: new Map() }),
  resetConfig: () => set({ config: DEFAULT_CONFIG, inactiveMutationCountValues: new Map() }),
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
