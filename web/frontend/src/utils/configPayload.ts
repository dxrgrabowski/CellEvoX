import type { SimulationConfig } from '../types/simulation';

const SPATIAL_FIELDS = [
  'spatial_domain_size',
  'max_local_density',
  'sample_radius',
  'spring_constant',
  'mech_dt',
  'mech_substeps',
  'epsilon',
] as const;

const DENSITY_ONLY_FIELDS = [
  'max_local_density',
  'sample_radius',
] as const;

export function buildConfigPayload(config: SimulationConfig): Record<string, unknown> {
  const payload: Record<string, unknown> = { ...config };
  const isDensityMode =
    config.simulation_mode === 'spatial_3d_density' ||
    config.simulation_mode === 'spatial_3d';
  const isCapacityMode = config.simulation_mode === 'spatial_3d_capacity';
  const isSpatial = isDensityMode || isCapacityMode;

  if (!isSpatial) {
    SPATIAL_FIELDS.forEach(field => delete payload[field]);
  } else if (isCapacityMode) {
    DENSITY_ONLY_FIELDS.forEach(field => delete payload[field]);
  }

  return payload;
}
