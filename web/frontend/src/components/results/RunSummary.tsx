import { useQuery } from '@tanstack/react-query';
import { getRunSummary } from '../../api/client';
import type { RunSummaryData } from '../../types/simulation';

interface Props { runId: string }

function StatPill({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div className="card" style={{ padding: '16px 20px' }}>
      <div style={{ color: 'var(--text-muted)', fontSize: '0.72rem', textTransform: 'uppercase', letterSpacing: '0.06em' }}>
        {label}
      </div>
      <div style={{
        fontFamily: 'var(--font-mono)', fontSize: '1.2rem', marginTop: 6,
        color: color ?? 'var(--text-primary)', fontWeight: 500,
      }}>
        {value}
      </div>
    </div>
  );
}

export default function RunSummary({ runId }: Props) {
  const { data, isLoading } = useQuery<RunSummaryData>({
    queryKey: ['summary', runId],
    queryFn: () => getRunSummary(runId),
    enabled: !!runId,
  });

  if (isLoading) return <div style={{ color: 'var(--text-muted)', padding: 20 }}>Loading summary…</div>;
  if (!data) return null;

  const cfg = data.config;
  const mutations = cfg?.mutations ?? [];

  return (
    <div className="flex-col gap-24">
      {/* Key metrics */}
      <div className="grid-4">
        <StatPill label="Final τ"         value={data.final_tau != null ? data.final_tau.toFixed(1) : '—'} color="var(--accent)" />
        <StatPill label="Final Population" value={data.final_population?.toLocaleString() ?? '—'} />
        <StatPill label="Mean Fitness"     value={data.final_mean_fitness?.toFixed(4) ?? '—'} color="var(--positive)" />
        <StatPill label="Steps Recorded"   value={data.total_steps?.toLocaleString() ?? '—'} />
      </div>

      {/* Config summary */}
      {cfg && (
        <div className="card">
          <h4 style={{ marginBottom: 16 }}>Simulation Configuration</h4>
          <div className="grid-2" style={{ gap: '8px 24px' }}>
            {[
              ['Mode', cfg.simulation_mode?.replace(/_/g, ' ')],
              ['Seed', String(cfg.seed)],
              ['τ Step', String(cfg.tau_step)],
              ['Steps', cfg.steps?.toLocaleString()],
              ['Initial N', cfg.initial_population?.toLocaleString()],
              ['Capacity K', cfg.env_capacity?.toLocaleString()],
            ].map(([k, v]) => v && (
              <div key={k} style={{ display: 'flex', justifyContent: 'space-between', borderBottom: '1px solid var(--border-subtle)', paddingBottom: 6 }}>
                <span style={{ color: 'var(--text-muted)', fontSize: '0.82rem' }}>{k}</span>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '0.82rem' }}>{v}</span>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Mutation table */}
      {mutations.length > 0 && (
        <div className="card">
          <h4 style={{ marginBottom: 16 }}>Mutation Profile</h4>
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
        </div>
      )}
    </div>
  );
}
