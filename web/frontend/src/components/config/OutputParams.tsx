import type { SimulationConfig } from '../../types/simulation';
import { useConfigStore } from '../../stores';
import NumberInput from './NumberInput';

const VERBOSITY_LABELS = ['Off', 'Minimal', 'Full'] as const;
const VERBOSITY_DESCRIPTIONS = [
  'Suppresses simulation logs. Useful for batch runs where terminal output is noise.',
  'Shows warnings and key lifecycle messages without detailed per-run chatter.',
  'Shows detailed informational logs from the simulation engine.',
] as const;
const FULL_MUTATION_PAYLOAD_HELP =
  'When enabled, population snapshots include every mutation carried by each sampled cell. When off, snapshots keep only driver mutations.';

interface Props { config: SimulationConfig }

function formatTau(value: number) {
  if (!Number.isFinite(value)) return '--';
  return Number(value.toFixed(3)).toLocaleString();
}

export default function OutputParams({ config }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);
  const finalTau = config.steps * config.tau_step;

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
          <NumberInput id="input-stat-res" min={1}
            value={config.statistics_resolution}
            onValueChange={value => setConfig({ statistics_resolution: value })} />
          <span className="field-hint">Every N units of T; final T is {formatTau(finalTau)}. Controls generational_statistics.csv and memory_log.csv: population size, fitness moments, and mutation-count moments.</span>
        </div>

        <div className="field">
          <label className="field-label" htmlFor="input-pop-res">Population Snapshot Res.</label>
          <NumberInput id="input-pop-res" min={1}
            value={config.population_statistics_res}
            onValueChange={value => setConfig({ population_statistics_res: value })} />
          <span className="field-hint">Every N units of T; final T is {formatTau(finalTau)}. Writes population snapshots used for Results, Muller data, clone/mutation inspection, and CSV exports.</span>
        </div>

        <div className="field">
          <label className="field-label" htmlFor="input-graveyard">Graveyard Pruning Interval</label>
          <NumberInput id="input-graveyard" min={0}
            value={config.graveyard_pruning_interval}
            onValueChange={value => setConfig({ graveyard_pruning_interval: value })} />
          <span className="field-hint">Purge dead cells every N steps (0 = disabled)</span>
        </div>

        <div className="field">
          <label className="field-label" htmlFor="input-phylogeny">Phylogeny Cell Sampling</label>
          <NumberInput id="input-phylogeny" min={10} max={10000}
            value={config.phylogeny_num_cells_sampling}
            onValueChange={value => setConfig({ phylogeny_num_cells_sampling: value })} />
          <span className="field-hint">Cells sampled for phylogenetic tree reconstruction</span>
        </div>
      </div>

      {/* Verbosity */}
      <div className="field">
        <label
          className="field-label tooltip-target tooltip-target--wide"
          data-tooltip="Controls how much text the simulation writes to the live terminal and logs."
        >
          Verbosity
          <span className="field-info" aria-hidden="true">i</span>
        </label>
        <div style={{ display: 'flex', gap: '8px' }}>
          {VERBOSITY_LABELS.map((label, i) => (
            <button
              key={i}
              id={`verbosity-${i}`}
              type="button"
              className="tooltip-target tooltip-target--wide"
              data-tooltip={VERBOSITY_DESCRIPTIONS[i]}
              aria-label={`Verbosity ${label}: ${VERBOSITY_DESCRIPTIONS[i]}`}
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
        <span className="field-hint">Controls how much detail appears in live terminal output.</span>
      </div>

      {/* Toggles */}
      <div style={{ display: 'flex', gap: '24px', flexWrap: 'wrap' }}>
        <label
          className="toggle-wrapper tooltip-target tooltip-target--wide"
          id="toggle-full-payload"
          data-tooltip={FULL_MUTATION_PAYLOAD_HELP}
        >
          <span style={{ fontSize: '0.875rem', color: 'var(--text-secondary)' }}>
            Full Mutation Payload
            <span className="field-info" aria-hidden="true">i</span>
          </span>
          <span className="toggle">
            <input type="checkbox" checked={config.full_mutation_payload}
              aria-label={`Full Mutation Payload. ${FULL_MUTATION_PAYLOAD_HELP}`}
              onChange={e => setConfig({ full_mutation_payload: e.target.checked })} />
            <span className="toggle-track" />
            <span className="toggle-thumb" />
          </span>
        </label>
        <span className="field-hint" style={{ flexBasis: '100%' }}>
          Stores full mutation lists in population snapshots for deeper clone and mutation inspection.
        </span>
      </div>
    </div>
  );
}
