import type { SimulationConfig } from '../../types/simulation';
import { useConfigStore } from '../../stores';
import InlineNumberValue from './InlineNumberValue';
import NumberInput from './NumberInput';
import SmoothSlider from './SmoothSlider';

interface SliderFieldProps {
  id: string; label: string; hint: string;
  value: number; min: number; max: number; step: number;
  decimals: number;
  onChange: (v: number) => void;
}

function SliderField({ id, label, hint, value, min, max, step, decimals, onChange }: SliderFieldProps) {
  return (
    <div className="field">
      <div className="field-label">
        {label}
        <InlineNumberValue
          ariaLabel={`${label} value`}
          value={value}
          min={min}
          max={max}
          decimals={decimals}
          color="var(--accent)"
          onChange={onChange}
        />
      </div>
      <SmoothSlider
        id={id}
        min={min}
        max={max}
        step={step}
        value={value}
        aria-label={`${label} slider`}
        onValueChange={onChange}
      />
      <span className="field-hint">{hint}</span>
    </div>
  );
}

interface Props { config: SimulationConfig }

export default function SpatialParams({ config }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);
  const isCapacityMode = config.simulation_mode === 'spatial_3d_capacity';
  const isDensityMode =
    config.simulation_mode === 'spatial_3d_density' ||
    config.simulation_mode === 'spatial_3d';

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
        ⚡ {isCapacityMode ? (
          <>
            <strong>3D Spatial Capacity</strong> uses global carrying capacity from Population plus
            spatial mechanics below.
          </>
        ) : isDensityMode ? (
          <>
            <strong>3D Spatial Density</strong> uses local density controls and spatial mechanics below.
          </>
        ) : (
          <>These parameters are only used in <strong>3D Spatial</strong> simulation modes.</>
        )}
      </div>

      <div className="grid-2">
        <SliderField
          id="input-domain-size" label="Domain Size" hint="Spatial domain edge length"
          value={config.spatial_domain_size} min={10} max={2000} step={10}
          decimals={0}
          onChange={v => setConfig({ spatial_domain_size: v })} />

        {isDensityMode && (
          <>
            <SliderField
              id="input-max-density" label="Max Local Density" hint="Max cell density per unit volume"
              value={config.max_local_density} min={1} max={100} step={0.5}
              decimals={1}
              onChange={v => setConfig({ max_local_density: v })} />

            <SliderField
              id="input-sample-radius" label="Sample Radius" hint="Neighbourhood radius for density sampling"
              value={config.sample_radius} min={0.5} max={50} step={0.5}
              decimals={1}
              onChange={v => setConfig({ sample_radius: v })} />
          </>
        )}

        <SliderField
          id="input-spring-const" label="Spring Constant" hint="Mechanical repulsion spring stiffness"
          value={config.spring_constant} min={0.01} max={10} step={0.01}
          decimals={2}
          onChange={v => setConfig({ spring_constant: v })} />

        <SliderField
          id="input-mech-dt" label="Mech. Δt" hint="Mechanical integration timestep"
          value={config.mech_dt} min={0.001} max={1} step={0.001}
          decimals={3}
          onChange={v => setConfig({ mech_dt: v })} />

        <div className="field">
          <label className="field-label" htmlFor="input-mech-substeps">
            Mech. Substeps
            <span style={{ marginLeft: 8, color: 'var(--accent)', fontFamily: 'var(--font-mono)', fontSize: '0.85rem' }}>
              {config.mech_substeps}
            </span>
          </label>
          <NumberInput id="input-mech-substeps" min={1} max={100}
            value={config.mech_substeps}
            onValueChange={value => setConfig({ mech_substeps: value })} />
          <span className="field-hint">Mechanical solver sub-iterations per tau step</span>
        </div>

        <SliderField
          id="input-epsilon" label="Division Epsilon (ε)" hint="Cell division jitter magnitude"
          value={config.epsilon} min={0.001} max={1} step={0.001}
          decimals={3}
          onChange={v => setConfig({ epsilon: v })} />
      </div>
    </div>
  );
}
