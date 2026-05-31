import type { SimulationConfig } from '../../types/simulation';
import { useConfigStore } from '../../stores';
import NumberInput from './NumberInput';

interface Props { config: SimulationConfig }

function formatGenerations(value: number): string {
  if (!Number.isFinite(value)) return '--';
  if (Math.abs(value - Math.round(value)) < 0.005) return Math.round(value).toLocaleString();
  return value.toLocaleString(undefined, {
    maximumFractionDigits: value >= 100 ? 1 : 3,
  });
}

export default function PopulationParams({ config }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);
  const estimatedGenerations = config.steps * config.tau_step;

  return (
    <div className="flex-col gap-16">
      <div className="grid-2">
        <div className="field">
          <label className="field-label">Initial Population</label>
          <NumberInput id="input-initial-pop" min={1}
            value={config.initial_population}
            onValueChange={value => setConfig({ initial_population: value })} />
          <span className="field-hint">Starting number of cells (N₀)</span>
        </div>

        <div className="field">
          <label className="field-label">Environment Capacity (K)</label>
          <NumberInput id="input-env-capacity" min={1}
            value={config.env_capacity}
            onValueChange={value => setConfig({ env_capacity: value })} />
          <span className="field-hint">Carrying capacity of the environment</span>
        </div>

        <div className="field">
          <label className="field-label">
            Simulation Steps
            <span className="field-label-meta">
              ≈ {formatGenerations(estimatedGenerations)} generations
            </span>
          </label>
          <NumberInput id="input-steps" min={1000}
            value={config.steps}
            onValueChange={value => setConfig({ steps: value })} />
          <span className="field-hint">Total number of tau steps to run</span>
        </div>

        <div className="field">
          <label className="field-label">
            Max Population Cutoff
            {config.max_population_cutoff === 0 && (
              <span style={{ marginLeft: 8, color: 'var(--neutral)', fontSize: '0.72rem' }}>disabled</span>
            )}
          </label>
          <NumberInput id="input-max-pop" min={0}
            value={config.max_population_cutoff}
            onValueChange={value => setConfig({ max_population_cutoff: value })} />
          <span className="field-hint">Stop when N ≥ this value (0 = disabled)</span>
        </div>
      </div>
    </div>
  );
}
