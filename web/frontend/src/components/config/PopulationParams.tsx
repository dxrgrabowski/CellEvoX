import type { SimulationConfig } from '../../types/simulation';
import { useConfigStore } from '../../stores';

interface Props { config: SimulationConfig }

export default function PopulationParams({ config }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);

  return (
    <div className="flex-col gap-16">
      <div className="grid-2">
        <div className="field">
          <label className="field-label">Initial Population</label>
          <input id="input-initial-pop" className="input" type="number" min={1}
            value={config.initial_population}
            onChange={e => setConfig({ initial_population: Number(e.target.value) })} />
          <span className="field-hint">Starting number of cells (N₀)</span>
        </div>

        <div className="field">
          <label className="field-label">Environment Capacity (K)</label>
          <input id="input-env-capacity" className="input" type="number" min={1}
            value={config.env_capacity}
            onChange={e => setConfig({ env_capacity: Number(e.target.value) })} />
          <span className="field-hint">Carrying capacity of the environment</span>
        </div>

        <div className="field">
          <label className="field-label">Simulation Steps</label>
          <input id="input-steps" className="input" type="number" min={1000}
            value={config.steps}
            onChange={e => setConfig({ steps: Number(e.target.value) })} />
          <span className="field-hint">Total number of tau steps to run</span>
        </div>

        <div className="field">
          <label className="field-label">
            Max Population Cutoff
            {config.max_population_cutoff === 0 && (
              <span style={{ marginLeft: 8, color: 'var(--neutral)', fontSize: '0.72rem' }}>disabled</span>
            )}
          </label>
          <input id="input-max-pop" className="input" type="number" min={0}
            value={config.max_population_cutoff}
            onChange={e => setConfig({ max_population_cutoff: Number(e.target.value) })} />
          <span className="field-hint">Stop when N ≥ this value (0 = disabled)</span>
        </div>
      </div>
    </div>
  );
}
