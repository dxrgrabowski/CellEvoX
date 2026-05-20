import { useQuery } from '@tanstack/react-query';
import { useResultsStore } from '../../stores';
import { listRuns } from '../../api/client';
import type { RunMeta } from '../../types/simulation';
import { FolderOpen } from 'lucide-react';

const MODE_ICONS: Record<string, string> = {
  stochastic: '🎲',
  deterministic: '📐',
  spatial_3d_density: '🌐',
  spatial_3d_capacity: '🌐',
};

export default function RunSelector() {
  const { selectedRunId, setSelectedRunId } = useResultsStore();
  const { data: runs = [], isLoading } = useQuery({
    queryKey: ['runs'],
    queryFn: listRuns,
    refetchInterval: 10000,
  });

  if (isLoading) return (
    <div className="card" style={{ textAlign: 'center', padding: 40, color: 'var(--text-muted)' }}>
      <div className="spin" style={{ display: 'inline-block', marginBottom: 8 }}>⟳</div>
      <br />Loading runs...
    </div>
  );

  if (runs.length === 0) return (
    <div className="card" style={{ textAlign: 'center', padding: 40 }}>
      <FolderOpen size={32} style={{ color: 'var(--text-muted)', marginBottom: 12 }} />
      <p style={{ color: 'var(--text-muted)' }}>No simulation runs found in repository.</p>
    </div>
  );

  return (
    <div className="flex-col gap-8">
      {runs.map((run: RunMeta) => (
        <button
          key={run.id}
          id={`run-item-${run.id.slice(0, 16)}`}
          onClick={() => setSelectedRunId(run.id)}
          style={{
            display: 'block', width: '100%', textAlign: 'left', cursor: 'pointer',
            background: selectedRunId === run.id ? 'var(--accent-dim)' : 'var(--bg-card)',
            border: `1px solid ${selectedRunId === run.id ? 'var(--accent)' : 'var(--border)'}`,
            borderRadius: 'var(--radius-md)', padding: '12px 16px',
            fontFamily: 'var(--font-sans)', transition: 'all var(--transition)',
          }}
        >
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-8">
              <span style={{ fontSize: '1.1rem' }}>{MODE_ICONS[run.sim_mode] ?? '◉'}</span>
              <div>
                <div style={{
                  fontWeight: 600, fontSize: '0.85rem',
                  color: selectedRunId === run.id ? 'var(--accent)' : 'var(--text-primary)',
                }}>
                  {run.label}
                </div>
                <div style={{ fontFamily: 'var(--font-mono)', fontSize: '0.7rem', color: 'var(--text-muted)', marginTop: 2 }}>
                  {run.path}
                </div>
              </div>
            </div>
            <div className="flex-col gap-8" style={{ alignItems: 'flex-end' }}>
              {run.steps && (
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: '0.72rem', color: 'var(--text-muted)' }}>
                  {run.steps.toLocaleString()} steps
                </span>
              )}
              <div className="flex gap-8">
                {run.has_stats && <span className="badge badge--finished" style={{ fontSize: '0.65rem', padding: '2px 7px' }}>stats</span>}
                {run.has_muller && <span className="badge badge--driver" style={{ fontSize: '0.65rem', padding: '2px 7px' }}>müller</span>}
              </div>
            </div>
          </div>
        </button>
      ))}
    </div>
  );
}
