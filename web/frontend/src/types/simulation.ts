// ── SimulationConfig mirrors SimulationEngine.hpp ────────────────────────────

export type SimulationMode =
  | 'stochastic'
  | 'deterministic'
  | 'spatial_3d'
  | 'spatial_3d_density'
  | 'spatial_3d_capacity';

export interface MutationType {
  id: number;
  is_driver: boolean;
  effect: number;      // float, fitness delta
  probability: number; // per-cell per-step
}

export interface SimulationConfig {
  // Core
  seed: number;
  simulation_mode: SimulationMode;
  tau_step: number;
  // Population
  initial_population: number;
  env_capacity: number;
  steps: number;
  max_population_cutoff: number;
  // Output
  output_path: string;
  statistics_resolution: number;
  population_statistics_res: number;
  graveyard_pruning_interval: number;
  full_mutation_payload: boolean;
  verbosity: 0 | 1 | 2;
  phylogeny_num_cells_sampling: number;
  // Spatial 3D (only relevant for spatial modes)
  spatial_domain_size: number;
  max_local_density: number;
  sample_radius: number;
  spring_constant: number;
  mech_dt: number;
  mech_substeps: number;
  epsilon: number;
  // Mutations
  mutations: MutationType[];
}

export const DEFAULT_CONFIG: SimulationConfig = {
  seed: 42,
  simulation_mode: 'stochastic',
  tau_step: 0.005,
  initial_population: 1000,
  env_capacity: 10000,
  steps: 100000,
  max_population_cutoff: 0,
  output_path: './output',
  statistics_resolution: 10,
  population_statistics_res: 500,
  graveyard_pruning_interval: 500,
  full_mutation_payload: false,
  verbosity: 1,
  phylogeny_num_cells_sampling: 100,
  spatial_domain_size: 200.0,
  max_local_density: 8.0,
  sample_radius: 3.0,
  spring_constant: 0.5,
  mech_dt: 0.1,
  mech_substeps: 5,
  epsilon: 0.1,
  mutations: [
    { id: 1, is_driver: true, effect: 0.05, probability: 0.001 },
    { id: 2, is_driver: false, effect: -0.001, probability: 0.02 },
  ],
};

// ── API Response Types ────────────────────────────────────────────────────────

export interface SimulationStatus {
  status: 'idle' | 'running' | 'finished' | 'error' | 'stopped';
  run_id: string | null;
  elapsed_seconds: number | null;
}

export interface RunMeta {
  id: string;
  path: string;
  label: string;
  sim_mode: string;
  steps: number | null;
  has_stats: boolean;
  has_population: boolean;
  has_muller: boolean;
}

export interface RunSummaryData {
  run_id: string;
  path: string;
  config: Partial<SimulationConfig> & { mutations?: MutationType[]; stochastic?: boolean };
  final_tau?: number;
  final_population?: number;
  final_mean_fitness?: number;
  total_steps?: number;
}

export interface StatsData {
  columns: string[];
  rows: Record<string, (number | null)[]>;
}

export interface MullerData {
  populations: {
    Id: number[];
    Step: number[];
    Pop: number[];
  };
  parent_tree: {
    ParentId: number[];
    ChildId: number[];
  };
  driver_mutation_ids: number[];
  mutations: MutationType[];
  error?: string;
}
