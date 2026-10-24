import type { SimulationConfig } from '../types/simulation';

const SPATIAL_FIELDS = [
  'spatial_domain_size',
  'spring_constant',
  'mech_dt',
  'mech_substeps',
  'epsilon',
] as const;

export function buildConfigPayload(config: SimulationConfig): Record<string, unknown> {
  const payload: Record<string, unknown> = { ...config };
  const isSpatial = config.simulation_mode === 'spatial_3d_capacity';

  if (!isSpatial) {
    SPATIAL_FIELDS.forEach(field => delete payload[field]);
  }

  return payload;
}
