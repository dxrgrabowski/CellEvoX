import { useQuery } from '@tanstack/react-query';
import {
  useCallback,
  useMemo,
  useRef,
  useState,
  type ComponentType,
  type CSSProperties,
  type MouseEvent as ReactMouseEvent,
} from 'react';
import PlotComponentModule from 'react-plotly.js';
import { getMullerData } from '../../api/client';
import ErrorBoundary from '../ErrorBoundary';
import type { MullerData } from '../../types/simulation';

interface Props { runId: string }
type MullerRows = { Id: unknown[]; Step: unknown[]; Pop: unknown[] };
type HoverTarget = number | 'other';
type HoverDetail = {
  title: string;
  meta: string;
  rows: Array<{
    key: string;
    rank: number;
    color: string;
    label: string;
    value: string;
    selected: boolean;
  }>;
};
type HoverInfo = {
  left: number;
  top: number;
  lineLeft: number;
  lineTop: number;
  lineHeight: number;
  detail: HoverDetail;
};
type PlotComponentProps = {
  data: unknown[];
  layout: Record<string, unknown>;
  config: Record<string, unknown>;
  style: CSSProperties;
  onRelayout?: (event: Readonly<Record<string, unknown>>) => void;
};
type ProcessedMuller =
  | { rowsEmpty: true }
  | {
      rowsEmpty: false;
      cloneIds: number[];
      steps: number[];
      traces: unknown[];
      hiddenCloneCount: number;
      renderedCloneCount: number;
      getHoverDetail: (step: number, selectedCloneId?: HoverTarget) => HoverDetail;
      getSelectedTarget: (step: number, yValue: number) => HoverTarget | undefined;
      totalPopulationMax: number;
    };

// Colour palette for clones
const CLONE_PALETTE = [
  '#00d4ff', '#7c3aed', '#10b981', '#f59e0b', '#ef4444',
  '#06b6d4', '#8b5cf6', '#34d399', '#fbbf24', '#f87171',
  '#0ea5e9', '#a78bfa', '#6ee7b7', '#fcd34d', '#fca5a5',
];
const MAX_RENDERED_CLONES = 120;
const OTHER_CLONE_COLOR = '#64748b';
const PLOT_MARGIN = { l: 60, r: 20, t: 20, b: 60 };
const PLOT_STYLE: CSSProperties = { width: '100%', minHeight: 400 };
const TOOLTIP_WIDTH = 380;
const TOOLTIP_MARGIN = 8;
const TOOLTIP_CURSOR_GAP = 18;

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

function formatPopulation(value: number): string {
  if (Math.abs(value - Math.round(value)) < 0.05) return Math.round(value).toLocaleString();
  return value.toFixed(1).replace(/\.0$/, '');
}

function nearestStep(value: number, steps: number[]): number {
  return steps.reduce((closest, step) =>
    Math.abs(step - value) < Math.abs(closest - value) ? step : closest
  , steps[0]);
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

function numericValue(value: unknown): number | null {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : null;
}

function rangeFromRelayout(
  event: Readonly<Record<string, unknown>>,
  axis: 'xaxis' | 'yaxis',
): [number, number] | null {
  const direct = event[`${axis}.range`];
  if (Array.isArray(direct)) {
    const start = numericValue(direct[0]);
    const end = numericValue(direct[1]);
    if (start != null && end != null) return [start, end];
  }

  const start = numericValue(event[`${axis}.range[0]`]);
  const end = numericValue(event[`${axis}.range[1]`]);
  return start != null && end != null ? [start, end] : null;
}

function plotGeometry(rect: DOMRect) {
  const width = Math.max(1, rect.width - PLOT_MARGIN.l - PLOT_MARGIN.r);
  const height = Math.max(1, rect.height - PLOT_MARGIN.t - PLOT_MARGIN.b);
  return {
    left: PLOT_MARGIN.l,
    top: PLOT_MARGIN.t,
    width,
    height,
    right: PLOT_MARGIN.l + width,
    bottom: PLOT_MARGIN.t + height,
  };
}

function floatingTooltipPosition(
  cursorX: number,
  cursorY: number,
  containerWidth: number,
  containerHeight: number,
  tooltipWidth: number,
  tooltipHeight: number,
) {
  const width = Math.min(tooltipWidth, Math.max(160, containerWidth - TOOLTIP_MARGIN * 2));
  const hasRoomRight = cursorX + TOOLTIP_CURSOR_GAP + width <= containerWidth - TOOLTIP_MARGIN;
  const preferredLeft = hasRoomRight
    ? cursorX + TOOLTIP_CURSOR_GAP
    : cursorX - width - TOOLTIP_CURSOR_GAP;

  return {
    width,
    left: clamp(preferredLeft, TOOLTIP_MARGIN, containerWidth - width - TOOLTIP_MARGIN),
    top: clamp(
      cursorY - tooltipHeight / 2,
      TOOLTIP_MARGIN,
      containerHeight - tooltipHeight - TOOLTIP_MARGIN,
    ),
  };
}

export default function MullerPlot({ runId }: Props) {
  const plotAreaRef = useRef<HTMLDivElement>(null);
  const axisRangeRef = useRef<{ x?: [number, number]; y?: [number, number] }>({});
  const [hoverInfo, setHoverInfo] = useState<HoverInfo | null>(null);
  const { data, isLoading, error } = useQuery<MullerData>({
    queryKey: ['muller', runId],
    queryFn: () => getMullerData(runId),
    enabled: !!runId,
  });

  const processed = useMemo<ProcessedMuller | null>(() => {
    if (!data || data.error || !hasMullerRows(data.populations)) return null;

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

    if (rows.length === 0) return { rowsEmpty: true };

    const cloneIds = [...new Set(rows.map(row => row.id))].sort((a, b) => a - b);
    const steps = [...new Set(rows.map(row => row.step))].sort((a, b) => a - b);
    const stepClonePop = new Map<number, Map<number, number>>();
    const cloneScores = new Map<number, { peak: number; total: number }>();

    rows.forEach(row => {
      let clones = stepClonePop.get(row.step);
      if (!clones) {
        clones = new Map<number, number>();
        stepClonePop.set(row.step, clones);
      }
      const nextPop = (clones.get(row.id) ?? 0) + row.pop;
      clones.set(row.id, nextPop);

      const score = cloneScores.get(row.id) ?? { peak: 0, total: 0 };
      score.peak = Math.max(score.peak, nextPop);
      score.total += Math.max(row.pop, 0);
      cloneScores.set(row.id, score);
    });

    const rankedCloneIds = [...cloneIds].sort((a, b) => {
      const aScore = cloneScores.get(a) ?? { peak: 0, total: 0 };
      const bScore = cloneScores.get(b) ?? { peak: 0, total: 0 };
      return bScore.peak - aScore.peak || bScore.total - aScore.total || a - b;
    });
    const visibleCloneIds = rankedCloneIds.slice(0, MAX_RENDERED_CLONES);
    const hiddenCloneIds = rankedCloneIds.slice(MAX_RENDERED_CLONES);
    const hiddenCloneSet = new Set(hiddenCloneIds);
    const traceTargets: HoverTarget[] = [...visibleCloneIds];
    if (hiddenCloneIds.length > 0) traceTargets.push('other');
    const visibleCloneIndex = new Map(visibleCloneIds.map((cloneId, idx) => [cloneId, idx]));
    const cloneColor = (cloneId: number) => {
      const idx = visibleCloneIndex.get(cloneId);
      if (idx == null) return OTHER_CLONE_COLOR;
      return CLONE_PALETTE[idx % CLONE_PALETTE.length];
    };
    const populationForTarget = (target: HoverTarget, step: number) => {
      const clones = stepClonePop.get(step) ?? new Map<number, number>();
      if (target !== 'other') return Math.max(clones.get(target) ?? 0, 0);
      return [...clones.entries()]
        .filter(([cloneId]) => hiddenCloneSet.has(cloneId))
        .reduce((sum, [, pop]) => sum + Math.max(pop, 0), 0);
    };
    const totalPopByStep = new Map(steps.map(step => {
      const clones = stepClonePop.get(step) ?? new Map<number, number>();
      return [step, [...clones.values()].reduce((sum, pop) => sum + Math.max(pop, 0), 0)] as const;
    }));
    const totalPopulationMax = Math.max(1, ...totalPopByStep.values());

    const buildHoverDetail = (step: number, selectedCloneId?: HoverTarget): HoverDetail => {
      const clones = stepClonePop.get(step) ?? new Map<number, number>();
      const active = [...clones.entries()]
        .filter(([, pop]) => pop > 0)
        .sort(([aId, aPop], [bId, bPop]) => bPop - aPop || aId - bId);
      const totalPop = active.reduce((sum, [, pop]) => sum + pop, 0);
      let displayed = active.slice(0, 11);
      const selected = typeof selectedCloneId === 'number'
        ? active.find(([cloneId]) => cloneId === selectedCloneId)
        : undefined;

      if (selected && !displayed.some(([cloneId]) => cloneId === selectedCloneId)) {
        displayed = [...active.slice(0, 10), selected]
          .sort(([aId, aPop], [bId, bPop]) => bPop - aPop || aId - bId);
      }

      const displayedIds = new Set(displayed.map(([cloneId]) => cloneId));
      const other = active.filter(([cloneId]) => !displayedIds.has(cloneId));
      const hoverRows: HoverDetail['rows'] = displayed.map(([cloneId, pop], idx) => ({
        key: `clone-${cloneId}`,
        rank: idx + 1,
        color: cloneColor(cloneId),
        label: `Clone ${cloneId}`,
        value: formatPopulation(pop),
        selected: selectedCloneId === cloneId,
      }));

      if (other.length > 0) {
        const otherPop = other.reduce((sum, [, pop]) => sum + pop, 0);
        hoverRows.push({
          key: 'other',
          rank: 12,
          color: OTHER_CLONE_COLOR,
          label: `Other (${other.length} clones)`,
          value: formatPopulation(otherPop),
          selected: selectedCloneId === 'other',
        });
      }

      return {
        title: `Step ${formatGeneration(step)}`,
        meta: `${active.length.toLocaleString()} active clones · total ${formatPopulation(totalPop)}`,
        rows: hoverRows,
      };
    };

    const traces = visibleCloneIds.map((cloneId, idx) => ({
      x: steps,
      y: steps.map(s => stepClonePop.get(s)?.get(cloneId) ?? 0),
      name: `Clone ${cloneId}`,
      type: 'scatter' as const,
      mode: 'none' as const,
      fill: 'tonexty' as const,
      stackgroup: 'one',
      hoveron: 'fills' as const,
      fillcolor: CLONE_PALETTE[idx % CLONE_PALETTE.length],
      line: { color: CLONE_PALETTE[idx % CLONE_PALETTE.length], width: 0 },
      opacity: 0.82,
      hoverinfo: 'skip' as const,
      showlegend: false,
    }));

    if (hiddenCloneIds.length > 0) {
      traces.push({
        x: steps,
        y: steps.map(step => {
          const clones = stepClonePop.get(step) ?? new Map<number, number>();
          return [...clones.entries()]
            .filter(([cloneId]) => hiddenCloneSet.has(cloneId))
            .reduce((sum, [, pop]) => sum + Math.max(pop, 0), 0);
        }),
        name: `Other (${hiddenCloneIds.length} clones)`,
        type: 'scatter' as const,
        mode: 'none' as const,
        fill: 'tonexty' as const,
        stackgroup: 'one',
        hoveron: 'fills' as const,
        fillcolor: OTHER_CLONE_COLOR,
        line: { color: OTHER_CLONE_COLOR, width: 0 },
        opacity: 0.45,
        hoverinfo: 'skip' as const,
        showlegend: false,
      });
    }

    return {
      rowsEmpty: false,
      cloneIds,
      steps,
      traces,
      hiddenCloneCount: hiddenCloneIds.length,
      renderedCloneCount: visibleCloneIds.length,
      getHoverDetail: buildHoverDetail,
      getSelectedTarget: (step: number, yValue: number) => {
        let cumulative = 0;
        for (const target of traceTargets) {
          const population = populationForTarget(target, step);
          if (population <= 0) continue;
          cumulative += population;
          if (yValue <= cumulative) return target;
        }
        return undefined;
      },
      totalPopulationMax,
    };
  }, [data]);

  const handlePlotMouseMove = useCallback((event: ReactMouseEvent<HTMLDivElement>) => {
    if (!processed || processed.rowsEmpty) return;
    const rect = plotAreaRef.current?.getBoundingClientRect();
    if (!rect) return;

    const geometry = plotGeometry(rect);
    const relativeX = event.clientX - rect.left;
    const relativeY = event.clientY - rect.top;
    if (
      relativeX < geometry.left ||
      relativeX > geometry.right ||
      relativeY < geometry.top ||
      relativeY > geometry.bottom
    ) {
      setHoverInfo(null);
      return;
    }

    const xRatio = (relativeX - geometry.left) / geometry.width;
    const xRange = axisRangeRef.current.x ?? [
      processed.steps[0],
      processed.steps[processed.steps.length - 1],
    ];
    const minStep = xRange[0];
    const maxStep = xRange[1];
    const step = nearestStep(minStep + xRatio * (maxStep - minStep), processed.steps);
    const stepRatio = maxStep === minStep ? 0 : (step - minStep) / (maxStep - minStep);
    const yRatio = 1 - ((relativeY - geometry.top) / geometry.height);
    const yRange = axisRangeRef.current.y ?? [0, processed.totalPopulationMax];
    const yValue = yRange[0] + clamp(yRatio, 0, 1) * (yRange[1] - yRange[0]);
    const target = processed.getSelectedTarget(step, yValue);
    const detail = processed.getHoverDetail(step, target);
    const tooltipHeight = Math.min(320, 72 + detail.rows.length * 24);
    const lineLeft = geometry.left + clamp(stepRatio, 0, 1) * geometry.width;
    const position = floatingTooltipPosition(
      relativeX,
      relativeY,
      rect.width,
      rect.height,
      TOOLTIP_WIDTH,
      tooltipHeight,
    );

    setHoverInfo({
      left: position.left,
      top: position.top,
      lineLeft,
      lineTop: geometry.top,
      lineHeight: geometry.height,
      detail,
    });
  }, [processed]);

  const handlePlotUnhover = useCallback(() => {
    setHoverInfo(null);
  }, []);

  const handlePlotRelayout = useCallback((event: Readonly<Record<string, unknown>>) => {
    const nextRanges = { ...axisRangeRef.current };
    const xRange = rangeFromRelayout(event, 'xaxis');
    const yRange = rangeFromRelayout(event, 'yaxis');

    if (event['xaxis.autorange']) delete nextRanges.x;
    else if (xRange) nextRanges.x = xRange;

    if (event['yaxis.autorange']) delete nextRanges.y;
    else if (yRange) nextRanges.y = yRange;

    axisRangeRef.current = nextRanges;
  }, []);

  const layout = useMemo(() => ({
    uirevision: runId,
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
    showlegend: false,
    hoverlabel: {
      align: 'left' as const,
      bgcolor: 'rgba(13,21,32,0.96)',
      bordercolor: 'rgba(0,212,255,0.35)',
      font: { family: 'JetBrains Mono, monospace', size: 11, color: '#e8f4f8' },
    },
    margin: PLOT_MARGIN,
    hovermode: false,
  }), [runId]);

  const plotConfig = useMemo(() => ({
    responsive: true,
    displayModeBar: true,
    modeBarButtonsToRemove: ['lasso2d', 'select2d'],
    toImageButtonOptions: { format: 'png', filename: `muller_${runId}` },
  }), [runId]);

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

  if (!processed) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      Invalid Müller data returned by API.
    </div>
  );

  if (processed.rowsEmpty) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--text-muted)' }}>
      No plottable Müller rows were returned for this run.
    </div>
  );

  if (!PlotComponent) return (
    <div style={{ padding: 40, textAlign: 'center', color: 'var(--negative)' }}>
      Müller plot renderer could not be loaded.
    </div>
  );
  const chart = processed;

  return (
    <div>
      <div style={{ marginBottom: 12, display: 'flex', gap: 12, alignItems: 'center' }}>
        <h4 style={{ color: 'var(--text-secondary)' }}>Müller Clone Evolution Plot</h4>
        <span style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>
          {chart.cloneIds.length} clones · {chart.steps.length} snapshots · steps {formatGeneration(chart.steps[0])}–{formatGeneration(chart.steps[chart.steps.length - 1])}
          {chart.hiddenCloneCount > 0 && (
            <> · rendering top {chart.renderedCloneCount} + Other</>
          )}
        </span>
      </div>
      <ErrorBoundary>
        <div
          ref={plotAreaRef}
          style={{ position: 'relative' }}
          onMouseMove={handlePlotMouseMove}
          onMouseLeave={handlePlotUnhover}
        >
          <PlotComponent
            data={chart.traces}
            layout={layout}
            config={plotConfig}
            style={PLOT_STYLE}
            onRelayout={handlePlotRelayout}
          />
          {hoverInfo && (
            <>
              <div
                style={{
                  position: 'absolute',
                  left: hoverInfo.lineLeft,
                  top: hoverInfo.lineTop,
                  zIndex: 4,
                  width: 1,
                  height: hoverInfo.lineHeight,
                  background: 'rgba(232,244,248,0.55)',
                  boxShadow: '0 0 0 1px rgba(0,212,255,0.16)',
                  pointerEvents: 'none',
                }}
              />
              <div
                style={{
                  position: 'absolute',
                  left: hoverInfo.left,
                  top: hoverInfo.top,
                  zIndex: 5,
                  width: TOOLTIP_WIDTH,
                  maxWidth: 'calc(100% - 16px)',
                  padding: '10px 12px',
                  border: '1px solid rgba(0,212,255,0.35)',
                  borderRadius: 6,
                  background: 'rgba(13,21,32,0.96)',
                  color: '#e8f4f8',
                  fontFamily: 'JetBrains Mono, monospace',
                  fontSize: 11,
                  lineHeight: 1.45,
                  boxShadow: '0 18px 45px rgba(0,0,0,0.35)',
                  pointerEvents: 'none',
                  transition: 'left 120ms ease-out, top 120ms ease-out',
                }}
              >
                <div style={{ fontWeight: 700, marginBottom: 2 }}>{hoverInfo.detail.title}</div>
                <div style={{ color: '#9fb8c9', marginBottom: 8 }}>{hoverInfo.detail.meta}</div>
                <div style={{ display: 'grid', gap: 3 }}>
                  {hoverInfo.detail.rows.map(row => (
                    <div
                      key={row.key}
                      style={{
                        display: 'grid',
                        gridTemplateColumns: '24px 32px 14px minmax(0, 1fr) auto',
                        alignItems: 'center',
                        columnGap: 4,
                      }}
                    >
                      <span style={{ color: row.selected ? '#ffffff' : 'transparent', fontWeight: 700 }}>
                        -&gt;
                      </span>
                      <span style={{ color: '#9fb8c9', textAlign: 'right' }}>{row.rank}.</span>
                      <span style={{ color: row.color }}>■</span>
                      <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                        {row.label}
                      </span>
                      <span style={{ color: '#ffffff', fontWeight: row.selected ? 700 : 500 }}>
                        {row.value}
                      </span>
                    </div>
                  ))}
                </div>
              </div>
            </>
          )}
        </div>
      </ErrorBoundary>
    </div>
  );
}
