import { useQuery } from '@tanstack/react-query';
import { useResultsStore } from '../../stores';
import { listRuns } from '../../api/client';
import type { RunMeta } from '../../types/simulation';
import { FolderOpen } from 'lucide-react';

const MODE_LABELS: Record<string, string> = {
  stochastic: 'Stochastic',
  deterministic: 'Deterministic',
  spatial_3d: 'Spatial 3D',
  spatial_3d_density: '3D density',
  spatial_3d_capacity: '3D capacity',
};

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

function formatCompactNumber(value: number | null | undefined): string | null {
  if (value == null) return null;
  const abs = Math.abs(value);
  if (abs >= 1_000_000) {
    return `${(value / 1_000_000).toFixed(abs >= 10_000_000 ? 0 : 1).replace(/\.0$/, '')}M`;
  }
  if (abs >= 1_000) {
    return `${(value / 1_000).toFixed(abs >= 100_000 ? 0 : 1).replace(/\.0$/, '')}k`;
  }
  return value.toLocaleString();
}

function formatRunDate(label: string): string {
  const match = label.match(/^(\d{4})-(\d{2})-(\d{2})_(\d{2})-(\d{2})/);
  if (!match) return label;
  const monthIndex = Number(match[2]) - 1;
  const month = MONTHS[monthIndex] ?? match[2];
  return `${month} ${match[3]} ${match[4]}:${match[5]}`;
}

function parentPath(path: string): string {
  const parts = path.split('/');
  return parts.length > 1 ? parts.slice(0, -1).join(' / ') : path;
}

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
    <div className="run-list">
      {runs.map((run: RunMeta) => (
        <button
          key={run.id}
          id={`run-item-${run.id.slice(0, 16)}`}
          className={`run-card ${selectedRunId === run.id ? 'is-selected' : ''}`}
          onClick={() => setSelectedRunId(run.id)}
          title={run.path}
        >
          <div className="run-card__main">
            <div className="run-card__topline">
              <span className="run-card__date">{formatRunDate(run.label)}</span>
              <span className="run-card__mode">{MODE_LABELS[run.sim_mode] ?? run.sim_mode.replace(/_/g, ' ')}</span>
            </div>
            <div className="run-card__path">{parentPath(run.path)}</div>
          </div>
          <div className="run-card__meta">
            {run.steps && (
              <span className="run-card__steps" title={`${run.steps.toLocaleString()} steps`}>
                {formatCompactNumber(run.steps)} steps
              </span>
            )}
            <div className="run-card__badges">
              {run.has_stats && <span className="badge badge--finished">Stats</span>}
              {run.has_muller && <span className="badge badge--driver">Müller</span>}
              {!run.has_stats && !run.has_muller && (
                <span className="badge badge--idle">Config only</span>
              )}
              {run.has_population && !run.has_muller && (
                <span className="badge badge--idle">Population</span>
              )}
            </div>
          </div>
        </button>
      ))}
    </div>
  );
}
