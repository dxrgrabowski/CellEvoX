import type { SimulationConfig, SimulationMode } from '../../types/simulation';
import { useConfigStore } from '../../stores';

const MODES: { value: SimulationMode; label: string; desc: string; status?: 'wip' }[] = [
  { value: 'stochastic',          label: 'Stochastic τ-Leap',   desc: 'Gillespie-based, probabilistic' },
  { value: 'deterministic',       label: 'Deterministic RK4',   desc: 'Runge-Kutta 4th order', status: 'wip' },
  { value: 'spatial_3d_density',  label: '3D Spatial Density',  desc: 'Density-regulated 3D' },
  { value: 'spatial_3d_capacity', label: '3D Spatial Capacity', desc: 'Capacity-regulated 3D' },
];

interface Props { config: SimulationConfig }

export default function CoreParams({ config }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);

  return (
    <div className="flex-col gap-24">
      {/* Simulation Mode */}
      <div className="field">
        <label className="field-label">Simulation Mode</label>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '8px' }}>
          {MODES.map(m => (
            <button
              key={m.value}
              onClick={() => setConfig({
                simulation_mode: m.value,
              })}
              className={`mode-card ${config.simulation_mode === m.value ? 'is-selected' : ''} ${m.status === 'wip' ? 'mode-card--wip' : ''}`}
              style={{
                padding: '10px 14px',
                borderRadius: 'var(--radius-md)',
                border: `1px solid ${config.simulation_mode === m.value ? 'var(--accent)' : 'var(--border)'}`,
                background: config.simulation_mode === m.value ? 'var(--accent-dim)' : 'var(--bg-input)',
                color: config.simulation_mode === m.value ? 'var(--accent)' : 'var(--text-secondary)',
                cursor: 'pointer',
                textAlign: 'left',
                transition: 'all var(--transition)',
                fontFamily: 'var(--font-sans)',
              }}
            >
              {m.status === 'wip' && <span className="mode-card__ribbon">Work in progress</span>}
              <div style={{ fontWeight: 600, fontSize: '0.85rem' }}>{m.label}</div>
              <div style={{ fontSize: '0.72rem', color: 'var(--text-muted)', marginTop: 2 }}>{m.desc}</div>
            </button>
          ))}
        </div>
      </div>

      <div className="grid-2">
        {/* Seed */}
        <div className="field">
          <label className="field-label">Random Seed</label>
          <input
            id="input-seed"
            className="input"
            type="number"
            min={0}
            value={config.seed}
            onChange={e => setConfig({ seed: Number(e.target.value) })}
          />
          <span className="field-hint">RNG seed for reproducibility</span>
        </div>

        {/* Tau Step */}
        <div className="field">
          <label className="field-label" htmlFor="input-tau-step">Tau Step</label>
          <div className="slider-input-row">
            <input
              id="input-tau-step"
              className="slider"
              type="range"
              min={0.0001} max={1} step={0.0001}
              value={config.tau_step}
              onChange={e => setConfig({ tau_step: Number(e.target.value) })}
            />
            <input
              id="input-tau-step-number"
              className="input numeric-step-input"
              type="number"
              min={0.0001} max={1} step={0.0001}
              value={config.tau_step}
              onChange={e => {
                const next = Number(e.target.value);
                if (Number.isFinite(next)) setConfig({ tau_step: next });
              }}
            />
          </div>
          <span className="field-hint">Time step size (smaller = more accurate, slower)</span>
        </div>
      </div>
    </div>
  );
}
