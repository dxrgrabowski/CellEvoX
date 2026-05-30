import { useQuery } from '@tanstack/react-query';
import type { ComponentType, CSSProperties } from 'react';
import PlotComponentModule from 'react-plotly.js';
import { getMullerData } from '../../api/client';
import ErrorBoundary from '../ErrorBoundary';
import type { MullerData } from '../../types/simulation';

interface Props { runId: string }
type MullerRows = { Id: unknown[]; Step: unknown[]; Pop: unknown[] };
type PlotComponentProps = {
  data: unknown[];
  layout: Record<string, unknown>;
  config: Record<string, unknown>;
  style: CSSProperties;
};

// Colour palette for clones
const CLONE_PALETTE = [
  '#00d4ff', '#7c3aed', '#10b981', '#f59e0b', '#ef4444',
  '#06b6d4', '#8b5cf6', '#34d399', '#fbbf24', '#f87171',
  '#0ea5e9', '#a78bfa', '#6ee7b7', '#fcd34d', '#fca5a5',
];

function resolvePlotComponent(value: unknown): ComponentType<PlotComponentProps> | null {
  let current = value;
  for (let i = 0; i < 5; i += 1) {
    if (typeof current === 'function') return current as ComponentType<PlotComponentProps>;
    if (!current || typeof current !== 'object' || !('default' in current)) return null;
    current = (current as { default: unknown }).default;
  }
  return null;
}

const PlotComponent = resolvePlotComponent(PlotComponentModule);

function hasMullerRows(value: unknown): value is MullerRows {
  if (!value || typeof value !== 'object') return false;
  const rows = value as Record<string, unknown>;
  return Array.isArray(rows.Id) && Array.isArray(rows.Step) && Array.isArray(rows.Pop);
}

function formatGeneration(value: number): string {
  if (Math.abs(value - Math.round(value)) < 0.05) return Math.round(value).toLocaleString();
  return value.toFixed(1).replace(/\.0$/, '');
}

export default function MullerPlot({ runId }: Props) {
  const { data, isLoading, error } = useQuery<MullerData>({
    queryKey: ['muller', runId],
    queryFn: () => getMullerData(runId),
    enabled: !!runId,
  });

  if (isLoading) return (
    <div style={{ padding: 60, textAlign: 'center', color: 'var(--text-muted)' }}>
      <div style={{ fontSize: '2rem', marginBottom: 12 }}>⟳</div>
      Processing Müller data…
    </div>
  );

  if (error) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      Müller data request failed.
      <p style={{ color: 'var(--text-muted)', fontSize: '0.8rem', marginTop: 8 }}>
        {error instanceof Error ? error.message : 'The API returned an unexpected error.'}
      </p>
    </div>
  );

  if (!data || data.error) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      ⚠ {data?.error ?? 'No population data found for Müller plot.'}
      <p style={{ color: 'var(--text-muted)', fontSize: '0.8rem', marginTop: 8 }}>
        Make sure <code>full_mutation_payload</code> was enabled and population CSV files were generated.
      </p>
    </div>
  );

  // Group by clone Id and build stacked area traces
  if (!hasMullerRows(data.populations)) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      Invalid Müller data returned by API.
    </div>
  );

  const { Id, Step, Pop } = data.populations;
  const rowCount = Math.min(Id.length, Step.length, Pop.length);
  const rows = Array.from({ length: rowCount }, (_, i) => ({
    id: Number(Id[i]),
    step: Number(Step[i]),
    pop: Number(Pop[i]),
  })).filter(row =>
    Number.isFinite(row.id) &&
    Number.isFinite(row.step) &&
    Number.isFinite(row.pop)
  );

  if (rows.length === 0) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--text-muted)' }}>
      No plottable Müller rows were returned for this run.
    </div>
  );

  if (!PlotComponent) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      Müller plot renderer could not be loaded.
    </div>
  );

  const cloneIds = [...new Set(rows.map(row => row.id))].sort((a, b) => a - b);
  const steps = [...new Set(rows.map(row => row.step))].sort((a, b) => a - b);

  const traces = cloneIds.map((cloneId, idx) => {
    const idxMap = new Map<number, number>();
    rows.forEach(row => { if (row.id === cloneId) idxMap.set(row.step, row.pop); });
    const y = steps.map(s => idxMap.get(s) ?? 0);

    return {
      x: steps,
      y,
      name: `Clone ${cloneId}`,
      type: 'scatter' as const,
      mode: 'none' as const,
      fill: 'tonexty' as const,
      stackgroup: 'one',
      fillcolor: CLONE_PALETTE[idx % CLONE_PALETTE.length],
      line: { color: CLONE_PALETTE[idx % CLONE_PALETTE.length], width: 0 },
      opacity: 0.82,
      hovertemplate: `Recorded step %{x:,.0f}<br>Clone population %{y:,.0f}<extra>${cloneId}</extra>`,
    };
  });

  const layout = {
    paper_bgcolor: 'transparent',
    plot_bgcolor: 'rgba(255,255,255,0.02)',
    font: { family: 'JetBrains Mono, monospace', color: '#7fa8c0', size: 11 },
    xaxis: {
      title: { text: 'Recorded simulation step', font: { color: '#7fa8c0' } },
      gridcolor: 'rgba(255,255,255,0.05)',
      linecolor: 'rgba(0,212,255,0.2)',
      tickfont: { size: 10 },
      tickformat: ',.0f',
    },
    yaxis: {
      title: { text: 'Clone population', font: { color: '#7fa8c0' } },
      gridcolor: 'rgba(255,255,255,0.05)',
      linecolor: 'rgba(0,212,255,0.2)',
      tickfont: { size: 10 },
      tickformat: ',.0f',
    },
    legend: {
      bgcolor: 'rgba(13,21,32,0.8)',
      bordercolor: 'rgba(0,212,255,0.15)',
      borderwidth: 1,
      font: { size: 10 },
    },
    margin: { l: 60, r: 20, t: 20, b: 60 },
    hovermode: 'x unified' as const,
  };

  return (
    <div>
      <div style={{ marginBottom: 12, display: 'flex', gap: 12, alignItems: 'center' }}>
        <h4 style={{ color: 'var(--text-secondary)' }}>Müller Clone Evolution Plot</h4>
        <span style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>
          {cloneIds.length} clones · {steps.length} snapshots · steps {formatGeneration(steps[0])}–{formatGeneration(steps[steps.length - 1])}
        </span>
      </div>
      <ErrorBoundary>
        <PlotComponent
          data={traces}
          layout={layout}
          config={{
            responsive: true,
            displayModeBar: true,
            modeBarButtonsToRemove: ['lasso2d', 'select2d'],
            toImageButtonOptions: { format: 'png', filename: `muller_${runId}` },
          }}
          style={{ width: '100%', minHeight: 400 }}
        />
      </ErrorBoundary>
    </div>
  );
}
