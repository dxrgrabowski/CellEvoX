import { useQuery } from '@tanstack/react-query';
import Plot from 'react-plotly.js';
import { getMullerData } from '../../api/client';

interface Props { runId: string }

// Colour palette for clones
const CLONE_PALETTE = [
  '#00d4ff', '#7c3aed', '#10b981', '#f59e0b', '#ef4444',
  '#06b6d4', '#8b5cf6', '#34d399', '#fbbf24', '#f87171',
  '#0ea5e9', '#a78bfa', '#6ee7b7', '#fcd34d', '#fca5a5',
];

export default function MullerPlot({ runId }: Props) {
  const { data, isLoading } = useQuery({
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

  if (!data || data.error) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      ⚠ {data?.error ?? 'No population data found for Müller plot.'}
      <p style={{ color: 'var(--text-muted)', fontSize: '0.8rem', marginTop: 8 }}>
        Make sure <code>full_mutation_payload</code> was enabled and population CSV files were generated.
      </p>
    </div>
  );

  // Group by clone Id and build stacked area traces
  const { Id, Step, Pop } = data.populations;
  const cloneIds = [...new Set(Id)].sort((a, b) => a - b);
  const steps = [...new Set(Step)].sort((a, b) => a - b);

  const traces = cloneIds.map((cloneId, idx) => {
    const idxMap = new Map<number, number>();
    Id.forEach((id, i) => { if (id === cloneId) idxMap.set(Step[i], Pop[i]); });
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
    };
  });

  const layout = {
    paper_bgcolor: 'transparent',
    plot_bgcolor: 'rgba(255,255,255,0.02)',
    font: { family: 'JetBrains Mono, monospace', color: '#7fa8c0', size: 11 },
    xaxis: {
      title: { text: 'Simulation Step (τ)', font: { color: '#7fa8c0' } },
      gridcolor: 'rgba(255,255,255,0.05)',
      linecolor: 'rgba(0,212,255,0.2)',
      tickfont: { size: 10 },
    },
    yaxis: {
      title: { text: 'Relative Clone Population', font: { color: '#7fa8c0' } },
      gridcolor: 'rgba(255,255,255,0.05)',
      linecolor: 'rgba(0,212,255,0.2)',
      tickfont: { size: 10 },
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
          {cloneIds.length} clones · {steps.length} time points · Interactive (zoom/pan enabled)
        </span>
      </div>
      <Plot
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
    </div>
  );
}
