import type { SimulationConfig, SimulationMode } from '../../types/simulation';
import { useConfigStore } from '../../stores';
import InlineNumberValue from './InlineNumberValue';
import NumberInput from './NumberInput';
import SmoothSlider from './SmoothSlider';

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
          {MODES.map(m => {
            const isSelected = config.simulation_mode === m.value;
            const isWip = m.status === 'wip';

            return (
              <button
                key={m.value}
                type="button"
                disabled={isWip}
                onClick={() => {
                  if (!isWip) setConfig({ simulation_mode: m.value });
                }}
                className={`mode-card ${isSelected ? 'is-selected' : ''} ${isWip ? 'mode-card--wip' : ''}`}
                style={{
                  padding: '10px 14px',
                  borderRadius: 'var(--radius-md)',
                  border: `1px solid ${isSelected && !isWip ? 'var(--accent)' : 'var(--border)'}`,
                  background: isWip
                    ? 'rgba(148,163,184,0.08)'
                    : isSelected ? 'var(--accent-dim)' : 'var(--bg-input)',
                  color: isWip
                    ? 'var(--text-muted)'
                    : isSelected ? 'var(--accent)' : 'var(--text-secondary)',
                  cursor: isWip ? 'not-allowed' : 'pointer',
                  textAlign: 'left',
                  transition: 'all var(--transition)',
                  fontFamily: 'var(--font-sans)',
                }}
              >
                {isWip && <span className="mode-card__ribbon">Work in progress</span>}
                <div style={{ fontWeight: 600, fontSize: '0.85rem' }}>{m.label}</div>
                <div style={{ fontSize: '0.72rem', color: 'var(--text-muted)', marginTop: 2 }}>{m.desc}</div>
              </button>
            );
          })}
        </div>
      </div>

      <div className="grid-2">
        {/* Seed */}
        <div className="field">
          <label className="field-label">Random Seed</label>
          <NumberInput
            id="input-seed"
            min={0}
            value={config.seed}
            onValueChange={value => setConfig({ seed: value })}
          />
          <span className="field-hint">RNG seed for reproducibility</span>
        </div>

        {/* Tau Step */}
        <div className="field">
          <div className="field-label">
            Tau Step
            <InlineNumberValue
              ariaLabel="Tau Step value"
              value={config.tau_step}
              min={0.0001}
              max={1}
              decimals={4}
              color="var(--accent)"
              onChange={value => setConfig({ tau_step: value })}
            />
          </div>
          <SmoothSlider
            id="input-tau-step"
            min={0.0001} max={1} step={0.0001}
            value={config.tau_step}
            aria-label="Tau Step slider"
            onValueChange={value => setConfig({ tau_step: value })}
          />
          <span className="field-hint">Time step size (smaller = more accurate, slower)</span>
        </div>
      </div>
    </div>
  );
}
