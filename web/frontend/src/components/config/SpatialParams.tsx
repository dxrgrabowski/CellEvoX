import type { SimulationConfig } from '../../types/simulation';
import { useConfigStore } from '../../stores';

interface SliderFieldProps {
  id: string; label: string; hint: string;
  value: number; min: number; max: number; step: number;
  onChange: (v: number) => void;
}

function SliderField({ id, label, hint, value, min, max, step, onChange }: SliderFieldProps) {
  return (
    <div className="field">
      <label className="field-label" htmlFor={id}>
        {label}
        <span style={{ marginLeft: 8, color: 'var(--accent)', fontFamily: 'var(--font-mono)', fontSize: '0.85rem' }}>
          {value}
        </span>
      </label>
      <input id={id} className="slider" type="range" min={min} max={max} step={step}
        value={value} onChange={e => onChange(Number(e.target.value))} />
      <span className="field-hint">{hint}</span>
    </div>
  );
}

interface Props { config: SimulationConfig }

export default function SpatialParams({ config }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);

  return (
    <div className="flex-col gap-24">
      <div style={{
        padding: '10px 14px',
        background: 'rgba(124,58,237,0.1)',
        border: '1px solid rgba(124,58,237,0.25)',
        borderRadius: 'var(--radius-md)',
        fontSize: '0.82rem',
        color: '#a78bfa',
      }}>
        ⚡ These parameters are only used in <strong>3D Spatial</strong> simulation modes.
      </div>

      <div className="grid-2">
        <SliderField
          id="input-domain-size" label="Domain Size" hint="Spatial domain edge length"
          value={config.spatial_domain_size} min={10} max={2000} step={10}
          onChange={v => setConfig({ spatial_domain_size: v })} />

        <SliderField
          id="input-max-density" label="Max Local Density" hint="Max cell density per unit volume"
          value={config.max_local_density} min={1} max={100} step={0.5}
          onChange={v => setConfig({ max_local_density: v })} />

        <SliderField
          id="input-sample-radius" label="Sample Radius" hint="Neighbourhood radius for density sampling"
          value={config.sample_radius} min={0.5} max={50} step={0.5}
          onChange={v => setConfig({ sample_radius: v })} />

        <SliderField
          id="input-spring-const" label="Spring Constant" hint="Mechanical repulsion spring stiffness"
          value={config.spring_constant} min={0.01} max={10} step={0.01}
          onChange={v => setConfig({ spring_constant: v })} />

        <SliderField
          id="input-mech-dt" label="Mech. Δt" hint="Mechanical integration timestep"
          value={config.mech_dt} min={0.001} max={1} step={0.001}
          onChange={v => setConfig({ mech_dt: v })} />

        <div className="field">
          <label className="field-label" htmlFor="input-mech-substeps">
            Mech. Substeps
            <span style={{ marginLeft: 8, color: 'var(--accent)', fontFamily: 'var(--font-mono)', fontSize: '0.85rem' }}>
              {config.mech_substeps}
            </span>
          </label>
          <input id="input-mech-substeps" className="input" type="number" min={1} max={100}
            value={config.mech_substeps}
            onChange={e => setConfig({ mech_substeps: Number(e.target.value) })} />
          <span className="field-hint">Mechanical solver sub-iterations per tau step</span>
        </div>

        <SliderField
          id="input-epsilon" label="Division Epsilon (ε)" hint="Cell division jitter magnitude"
          value={config.epsilon} min={0.001} max={1} step={0.001}
          onChange={v => setConfig({ epsilon: v })} />
      </div>
    </div>
  );
}
