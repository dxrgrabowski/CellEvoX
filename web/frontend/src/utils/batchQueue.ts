import type { BatchQueueItem, MutationType } from '../types/simulation';

interface BatchImportItem {
  name?: string;
  config: Record<string, unknown>;
}

interface BatchMutationToken {
  id: number;
  isDriver: boolean;
  effect: string;
  probability: string;
  effectDirection: 'positive' | 'negative' | 'neutral';
}

interface BatchConfigDescription {
  title: string;
  details: string[];
  mutationSummary: string;
  mutationTokens: BatchMutationToken[];
  mutationOverflowCount: number;
}

const MODE_LABELS: Record<string, string> = {
  stochastic: 'Stochastic tau-leap',
  deterministic: 'Deterministic RK4',
  spatial_3d: '3D spatial density',
  spatial_3d_density: '3D spatial density',
  spatial_3d_capacity: '3D spatial capacity',
};

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function asNumber(value: unknown): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function formatCompactNumber(value: number | null, decimals = 1) {
  if (value === null) return '--';
  const abs = Math.abs(value);
  if (abs >= 1_000_000) return `${Number((value / 1_000_000).toFixed(decimals))}M`;
  if (abs >= 1_000) return `${Number((value / 1_000).toFixed(decimals))}k`;
  return Number(value.toFixed(abs < 10 && value % 1 !== 0 ? decimals : 0)).toLocaleString();
}

function formatProbability(value: number | null) {
  if (value === null) return 'p?';
  if (value < 0.001) return `p=${value.toExponential(1)}`;
  return `p=${Number(value.toFixed(5))}`;
}

function formatEffect(value: number | null) {
  if (value === null) return '?';
  const sign = value > 0 ? '+' : '';
  return `${sign}${Number(value.toFixed(4))}`;
}

function effectDirection(value: number): BatchMutationToken['effectDirection'] {
  if (value > 0) return 'positive';
  if (value < 0) return 'negative';
  return 'neutral';
}

function readMutations(config: Record<string, unknown>): MutationType[] {
  const rawMutations = config.mutations;
  if (!Array.isArray(rawMutations)) return [];
  return rawMutations.filter(isRecord).map((mutation, index) => ({
    id: asNumber(mutation.id) ?? index + 1,
    is_driver: Boolean(mutation.is_driver),
    effect: asNumber(mutation.effect) ?? 0,
    probability: asNumber(mutation.probability) ?? 0,
  }));
}

export function describeBatchConfig(config: Record<string, unknown>): BatchConfigDescription {
  const mode = typeof config.simulation_mode === 'string'
    ? config.simulation_mode
    : config.stochastic === false ? 'deterministic' : 'stochastic';
  const modeLabel = MODE_LABELS[mode] ?? mode.replace(/_/g, ' ');
  const steps = asNumber(config.steps);
  const tauStep = asNumber(config.tau_step);
  const generations = steps !== null && tauStep !== null ? steps * tauStep : null;
  const initialPopulation = asNumber(config.initial_population);
  const capacity = asNumber(config.env_capacity);
  const cutoff = asNumber(config.max_population_cutoff);
  const mutations = readMutations(config);
  const driverCount = mutations.filter(mutation => mutation.is_driver).length;
  const passengerCount = mutations.length - driverCount;

  const details = [
    `${formatCompactNumber(steps)} steps`,
    tauStep !== null ? `tau ${Number(tauStep.toFixed(5))}` : 'tau --',
    initialPopulation !== null ? `N0 ${formatCompactNumber(initialPopulation)}` : null,
    capacity !== null ? `K ${formatCompactNumber(capacity)}` : null,
    cutoff && cutoff > 0 ? `cutoff ${formatCompactNumber(cutoff)}` : null,
  ].filter((detail): detail is string => Boolean(detail));

  const visibleMutationLimit = 6;
  const mutationTokens = mutations.slice(0, visibleMutationLimit).map(mutation => ({
    id: mutation.id,
    isDriver: mutation.is_driver,
    effect: formatEffect(mutation.effect),
    probability: formatProbability(mutation.probability),
    effectDirection: effectDirection(mutation.effect),
  }));
  const mutationOverflowCount = Math.max(0, mutations.length - mutationTokens.length);

  return {
    title: `${modeLabel} | ${formatCompactNumber(generations)} generations`,
    details,
    mutationSummary: mutations.length === 0
      ? 'No mutations'
      : `${driverCount} driver, ${passengerCount} passenger`,
    mutationTokens,
    mutationOverflowCount,
  };
}

export function createBatchQueueItem(config: Record<string, unknown>, name?: string): BatchQueueItem {
  const snapshot = JSON.parse(JSON.stringify(config)) as Record<string, unknown>;
  return {
    id: `${Date.now()}-${Math.random().toString(36).slice(2)}`,
    name: name || describeBatchConfig(snapshot).title,
    config: snapshot,
    created_at: new Date().toISOString(),
  };
}

export function extractBatchImportItems(raw: unknown, fallbackName: string): BatchImportItem[] {
  if (Array.isArray(raw)) {
    return raw.filter(isRecord).map((config, index) => ({
      name: `${fallbackName} #${index + 1}`,
      config,
    }));
  }

  if (!isRecord(raw)) return [];

  if (Array.isArray(raw.runs)) {
    return raw.runs.filter(isRecord).flatMap((run, index) => {
      const name = typeof run.name === 'string' ? run.name : `${fallbackName} #${index + 1}`;
      if (isRecord(run.config)) return [{ name, config: run.config }];
      if (typeof run.path === 'string') return [];
      return [{ name, config: run }];
    });
  }

  if (isRecord(raw.config)) return [{ name: fallbackName, config: raw.config }];
  return [{ name: fallbackName, config: raw }];
}
