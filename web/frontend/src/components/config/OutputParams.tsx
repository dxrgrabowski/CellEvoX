import type { SimulationConfig } from '../../types/simulation';
import { useConfigStore } from '../../stores';

const VERBOSITY_LABELS = ['Off', 'Minimal', 'Full'] as const;

interface Props { config: SimulationConfig }

export default function OutputParams({ config }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);

  return (
    <div className="flex-col gap-16">
      <div className="field">
        <label className="field-label" htmlFor="input-output-path">Output Directory</label>
        <input id="input-output-path" className="input" type="text"
          value={config.output_path}
          onChange={e => setConfig({ output_path: e.target.value })}
          placeholder="./output" />
        <span className="field-hint">Relative or absolute path for simulation results</span>
      </div>

      <div className="grid-2">
        <div className="field">
          <label className="field-label" htmlFor="input-stat-res">Statistics Resolution</label>
          <input id="input-stat-res" className="input" type="number" min={1}
            value={config.statistics_resolution}
            onChange={e => setConfig({ statistics_resolution: Number(e.target.value) })} />
          <span className="field-hint">Record fitness stats every N steps</span>
        </div>

        <div className="field">
          <label className="field-label" htmlFor="input-pop-res">Population Snapshot Res.</label>
          <input id="input-pop-res" className="input" type="number" min={1}
            value={config.population_statistics_res}
            onChange={e => setConfig({ population_statistics_res: Number(e.target.value) })} />
          <span className="field-hint">Record full population every N steps</span>
        </div>

        <div className="field">
          <label className="field-label" htmlFor="input-graveyard">Graveyard Pruning Interval</label>
          <input id="input-graveyard" className="input" type="number" min={0}
            value={config.graveyard_pruning_interval}
            onChange={e => setConfig({ graveyard_pruning_interval: Number(e.target.value) })} />
          <span className="field-hint">Purge dead cells every N steps (0 = disabled)</span>
        </div>

        <div className="field">
          <label className="field-label" htmlFor="input-phylogeny">Phylogeny Cell Sampling</label>
          <input id="input-phylogeny" className="input" type="number" min={10} max={10000}
            value={config.phylogeny_num_cells_sampling}
            onChange={e => setConfig({ phylogeny_num_cells_sampling: Number(e.target.value) })} />
          <span className="field-hint">Cells sampled for phylogenetic tree reconstruction</span>
        </div>
      </div>

      {/* Verbosity */}
      <div className="field">
        <label className="field-label">Verbosity</label>
        <div style={{ display: 'flex', gap: '8px' }}>
          {VERBOSITY_LABELS.map((label, i) => (
            <button
              key={i}
              id={`verbosity-${i}`}
              onClick={() => setConfig({ verbosity: i as 0 | 1 | 2 })}
              style={{
                padding: '8px 16px', borderRadius: 'var(--radius-md)',
                border: `1px solid ${config.verbosity === i ? 'var(--accent)' : 'var(--border)'}`,
                background: config.verbosity === i ? 'var(--accent-dim)' : 'var(--bg-input)',
                color: config.verbosity === i ? 'var(--accent)' : 'var(--text-muted)',
                cursor: 'pointer', fontFamily: 'var(--font-sans)',
                fontSize: '0.875rem', fontWeight: 500,
                transition: 'all var(--transition)',
              }}
            >{label}</button>
          ))}
        </div>
      </div>

      {/* Toggles */}
      <div style={{ display: 'flex', gap: '24px', flexWrap: 'wrap' }}>
        <label className="toggle-wrapper" id="toggle-full-payload">
          <span style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>
            Full Mutation Payload
          </span>
          <span className="toggle">
            <input type="checkbox" checked={config.full_mutation_payload}
              onChange={e => setConfig({ full_mutation_payload: e.target.checked })} />
            <span className="toggle-track" />
            <span className="toggle-thumb" />
          </span>
        </label>
      </div>
    </div>
  );
}
