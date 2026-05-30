import { useQuery } from '@tanstack/react-query';
import { getRunSummary } from '../../api/client';
import type { RunSummaryData } from '../../types/simulation';

interface Props { runId: string }

function StatPill({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div className="summary-stat">
      <div className="summary-stat__label">{label}</div>
      <div className="summary-stat__value" style={{ color: color ?? 'var(--text-primary)' }}>
        {value}
      </div>
    </div>
  );
}

function formatMaybeNumber(value: number | undefined, digits: number): string {
  return value != null ? value.toFixed(digits) : '—';
}

function formatTimeValue(value: number | undefined): string {
  if (value == null) return '—';
  if (Math.abs(value - Math.round(value)) < 0.05) return Math.round(value).toLocaleString();
  return value.toFixed(1).replace(/\.0$/, '');
}

function formatNumber(value: number | undefined): string | undefined {
  return value != null ? value.toLocaleString() : undefined;
}

function formatBool(value: boolean | undefined): string | undefined {
  if (value == null) return undefined;
  return value ? 'Yes' : 'No';
}

function formatMode(cfg: RunSummaryData['config']): string | undefined {
  const mode = cfg.simulation_mode ?? (
    cfg.stochastic == null ? undefined : cfg.stochastic ? 'stochastic' : 'deterministic'
  );
  return mode?.replace(/_/g, ' ');
}

function formatCutoff(value: number | undefined): string | undefined {
  if (value == null) return undefined;
  return value === 0 ? 'Disabled' : value.toLocaleString();
}

export default function RunSummary({ runId }: Props) {
  const { data, isLoading, error } = useQuery<RunSummaryData>({
    queryKey: ['summary', runId],
    queryFn: () => getRunSummary(runId),
    enabled: !!runId,
  });

  if (isLoading) return <div style={{ color: 'var(--text-muted)', padding: 20 }}>Loading summary…</div>;
  if (error) return <div style={{ color: 'var(--negative)', padding: 20 }}>Summary could not be loaded.</div>;
  if (!data) return null;

  const cfg = data.config;
  const mutations = cfg?.mutations ?? [];
  const hasSpatialConfig = cfg.spatial_domain_size != null ||
    cfg.max_local_density != null ||
    cfg.sample_radius != null ||
    cfg.spring_constant != null ||
    cfg.mech_dt != null ||
    cfg.mech_substeps != null ||
    cfg.epsilon != null;
  const configSections = [
    {
      title: 'Core',
      rows: [
        ['Mode', formatMode(cfg)],
        ['Seed', formatNumber(cfg.seed)],
        ['T step', cfg.tau_step != null ? String(cfg.tau_step) : undefined],
      ],
    },
    {
      title: 'Population',
      rows: [
        ['Steps', formatNumber(cfg.steps)],
        ['Initial N', formatNumber(cfg.initial_population)],
        ['Capacity K', formatNumber(cfg.env_capacity)],
        ['Max cutoff', formatCutoff(cfg.max_population_cutoff)],
      ],
    },
    {
      title: 'Output',
      rows: [
        ['Stats resolution', formatNumber(cfg.statistics_resolution)],
        ['Population snapshot res', formatNumber(cfg.population_statistics_res)],
        ['Graveyard pruning', formatCutoff(cfg.graveyard_pruning_interval)],
        ['Full mutation payload', formatBool(cfg.full_mutation_payload)],
        ['Output path', cfg.output_path],
      ],
    },
    ...(hasSpatialConfig ? [{
      title: 'Spatial',
      rows: [
        ['Domain size', formatNumber(cfg.spatial_domain_size)],
        ['Max local density', formatNumber(cfg.max_local_density)],
        ['Sample radius', formatNumber(cfg.sample_radius)],
        ['Spring constant', formatNumber(cfg.spring_constant)],
        ['Mech dt', formatNumber(cfg.mech_dt)],
        ['Mech substeps', formatNumber(cfg.mech_substeps)],
        ['Epsilon', formatNumber(cfg.epsilon)],
      ],
    }] : []),
  ];

  return (
    <div className="flex-col gap-24">
      {/* Key metrics */}
      <div className="grid-4">
        <StatPill label="Final T"          value={formatTimeValue(data.final_tau)} color="var(--accent)" />
        <StatPill label="Final Population" value={data.final_population?.toLocaleString() ?? '—'} />
        <StatPill label="Mean Fitness"     value={formatMaybeNumber(data.final_mean_fitness, 4)} color="var(--positive)" />
        <StatPill label="Steps Recorded"   value={data.total_steps?.toLocaleString() ?? '—'} />
      </div>

      {/* Config summary */}
      {cfg && (
        <div className="summary-section">
          <h4 style={{ marginBottom: 16 }}>Simulation Configuration</h4>
          <div className="config-section-grid">
            {configSections.map(section => (
              <div className="config-group" key={section.title}>
                <div className="config-group__title">{section.title}</div>
                {section.rows.map(([label, value]) => value && (
                  <div
                    className="config-row tooltip-target tooltip-target--below tooltip-target--wide"
                    key={label}
                    data-tooltip={`${label}: ${value}`}
                  >
                    <span className="config-row__label">{label}</span>
                    <span className="config-row__value">{value}</span>
                  </div>
                ))}
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Mutation table */}
      <div className="summary-section">
        <h4 style={{ marginBottom: 16 }}>Mutation Profile</h4>
        {mutations.length > 0 ? (
          <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: '0.85rem', fontFamily: 'var(--font-mono)' }}>
            <thead>
              <tr style={{ color: 'var(--text-muted)', fontSize: '0.75rem' }}>
                {['ID', 'Type', 'Effect', 'Probability'].map(h => (
                  <th key={h} style={{ textAlign: 'left', padding: '8px 12px', borderBottom: '1px solid var(--border-subtle)' }}>{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {mutations.map((m) => (
                <tr key={m.id} style={{ borderBottom: '1px solid var(--border-subtle)' }}>
                  <td style={{ padding: '8px 12px' }}>{m.id}</td>
                  <td style={{ padding: '8px 12px' }}>
                    <span className={`badge ${m.is_driver ? 'badge--driver' : 'badge--passenger'}`}>
                      {m.is_driver ? 'Driver' : 'Passenger'}
                    </span>
                  </td>
                  <td style={{ padding: '8px 12px', color: m.effect > 0 ? 'var(--positive)' : m.effect < 0 ? 'var(--negative)' : 'var(--neutral)' }}>
                    {m.effect > 0 ? '+' : ''}{m.effect.toFixed(4)}
                  </td>
                  <td style={{ padding: '8px 12px' }}>{m.probability.toFixed(5)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        ) : (
          <p className="summary-empty">
            No mutation profile is stored in this run config.
          </p>
        )}
      </div>
    </div>
  );
}
