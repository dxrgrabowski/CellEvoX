import { useQuery } from '@tanstack/react-query';
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip,
  ResponsiveContainer, Legend
} from 'recharts';
import { getRunStats } from '../../api/client';

interface Props { runId: string }

const SERIES = [
  { key: 'MeanFitness',        label: 'Mean Fitness',        color: '#00d4ff' },
  { key: 'FitnessVariance',    label: 'Fitness Variance',    color: '#7c3aed' },
  { key: 'MeanMutations',      label: 'Mean Mutations',      color: '#10b981' },
  { key: 'TotalLivingCells',   label: 'Population',          color: '#f59e0b' },
];

function normalizeColumn(value: string) {
  return value.toLowerCase().replace(/[_\s]/g, '');
}

function formatTime(value: number | string): string {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) return String(value);
  if (Math.abs(numeric - Math.round(numeric)) < 0.05) return Math.round(numeric).toLocaleString();
  return numeric.toFixed(1).replace(/\.0$/, '');
}

export default function StatChart({ runId }: Props) {
  const { data, isLoading, error } = useQuery({
    queryKey: ['stats', runId],
    queryFn: () => getRunStats(runId),
    enabled: !!runId,
  });

  if (isLoading) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--text-muted)' }}>Loading statistics…</div>
  );
  if (error || !data || !Array.isArray(data.columns) || !data.rows) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      ⚠ No statistics data found for this run.
    </div>
  );

  // Build chart data rows
  const timeCol =
    data.columns.find(c => normalizeColumn(c) === 'generation') ??
    data.columns.find(c => normalizeColumn(c) === 'tau') ??
    data.columns[0];
  const timeLabel = 'T';
  const nRows = data.rows[timeCol]?.length ?? 0;

  const chartData = Array.from({ length: nRows }, (_, i) => {
    const row: Record<string, number | null> = { time: data.rows[timeCol]?.[i] ?? i };
    SERIES.forEach(({ key }) => {
      const col = data.columns.find(c => normalizeColumn(c) === normalizeColumn(key));
      row[key] = col ? data.rows[col]?.[i] ?? null : null;
    });
    return row;
  });

  // Separate population (different Y scale)
  const fitSeries = SERIES.filter(s => s.key !== 'TotalLivingCells');

  const tooltipStyle = {
    background: 'var(--bg-elevated)',
    border: '1px solid var(--border)',
    borderRadius: '8px',
    fontFamily: 'var(--font-mono)',
    fontSize: '0.78rem',
  };

  return (
    <div className="flex-col gap-24">
      {/* Fitness Chart */}
      <div>
        <h4 style={{ marginBottom: 16, color: 'var(--text-secondary)' }}>Fitness & Mutations</h4>
        <ResponsiveContainer width="100%" height={280}>
          <LineChart data={chartData} margin={{ top: 5, right: 20, left: 0, bottom: 5 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.05)" />
            <XAxis dataKey="time" stroke="var(--text-muted)" tick={{ fontSize: 11, fontFamily: 'JetBrains Mono' }} tickFormatter={formatTime} label={{ value: timeLabel, position: 'insideBottom', offset: -5, fill: 'var(--text-muted)', fontSize: 11 }} />
            <YAxis stroke="var(--text-muted)" tick={{ fontSize: 11 }} />
            <Tooltip contentStyle={tooltipStyle} labelFormatter={(label) => `${timeLabel} ${formatTime(label as number | string)}`} />
            <Legend wrapperStyle={{ fontSize: '0.8rem' }} />
            {fitSeries.map(s => (
              <Line key={s.key} type="monotone" dataKey={s.key} name={s.label}
                stroke={s.color} dot={false} strokeWidth={2}
                connectNulls activeDot={{ r: 4 }} />
            ))}
          </LineChart>
        </ResponsiveContainer>
      </div>

      {/* Population Chart */}
      <div>
        <h4 style={{ marginBottom: 16, color: 'var(--text-secondary)' }}>Population Over Time</h4>
        <ResponsiveContainer width="100%" height={200}>
          <LineChart data={chartData} margin={{ top: 5, right: 20, left: 0, bottom: 5 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.05)" />
            <XAxis dataKey="time" stroke="var(--text-muted)" tick={{ fontSize: 11 }} tickFormatter={formatTime} />
            <YAxis stroke="var(--text-muted)" tick={{ fontSize: 11 }} />
            <Tooltip contentStyle={tooltipStyle} labelFormatter={(label) => `${timeLabel} ${formatTime(label as number | string)}`} />
            <Line type="monotone" dataKey="TotalLivingCells" name="Population"
              stroke="#f59e0b" dot={false} strokeWidth={2.5} />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}
