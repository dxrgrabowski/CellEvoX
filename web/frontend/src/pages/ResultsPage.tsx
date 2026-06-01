import { useEffect, useMemo, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { useResultsStore } from '../stores';
import RunSelector from '../components/results/RunSelector';
import StatChart from '../components/results/StatChart';
import MullerPlot from '../components/results/MullerPlot';
import RunSummary from '../components/results/RunSummary';
import { analyzeRun, getSimulationStatus, listRuns } from '../api/client';
import { BarChart2, CheckCircle2, Loader, PlayCircle, RefreshCw } from 'lucide-react';

const TABS = [
  { id: 'summary', label: 'Summary' },
  { id: 'stats',   label: 'Statistics' },
  { id: 'muller',  label: 'Müller Plot' },
];

function getErrorMessage(error: unknown): string {
  if (typeof error === 'object' && error !== null && 'response' in error) {
    const response = (error as { response?: { data?: { detail?: unknown } } }).response;
    if (typeof response?.data?.detail === 'string') return response.data.detail;
  }
  return 'Analysis could not be started';
}

export default function ResultsPage() {
  const { selectedRunId, activeTab, setActiveTab } = useResultsStore();
  const queryClient = useQueryClient();
  const [analysisRunId, setAnalysisRunId] = useState<string | null>(null);
  const [analysisMessage, setAnalysisMessage] = useState<string | null>(null);

  const { data: runs = [] } = useQuery({
    queryKey: ['runs'],
    queryFn: listRuns,
    refetchInterval: 10000,
  });

  const statusQuery = useQuery({
    queryKey: ['simulation-status'],
    queryFn: getSimulationStatus,
    refetchInterval: (query) => query.state.data?.status === 'running' ? 2000 : false,
  });

  const selectedRun = useMemo(
    () => runs.find(run => run.id === selectedRunId) ?? null,
    [runs, selectedRunId],
  );

  const analysisNeeded = Boolean(
    selectedRun && (!selectedRun.has_stats || (selectedRun.has_population && !selectedRun.has_muller)),
  );
  const runnerBusy = statusQuery.data?.status === 'running';
  const currentAnalysisRunning = Boolean(
    analysisRunId && statusQuery.data?.run_id === analysisRunId && statusQuery.data?.status === 'running',
  );

  const analyzeMutation = useMutation({
    mutationFn: (runId: string) => analyzeRun(runId),
    onSuccess: async (response) => {
      setAnalysisRunId(response.data.analysis_id);
      setAnalysisMessage('Analysis started');
      await statusQuery.refetch();
    },
    onError: (error) => {
      setAnalysisMessage(getErrorMessage(error));
    },
  });

  useEffect(() => {
    if (!analysisRunId || statusQuery.data?.run_id !== analysisRunId) return;
    if (statusQuery.data.status === 'running') return;

    void queryClient.invalidateQueries({ queryKey: ['runs'] });
    if (selectedRunId) {
      void queryClient.invalidateQueries({ queryKey: ['summary', selectedRunId] });
      void queryClient.invalidateQueries({ queryKey: ['stats', selectedRunId] });
      void queryClient.invalidateQueries({ queryKey: ['muller', selectedRunId] });
    }
    const nextAnalysisMessage = statusQuery.data.status === 'finished'
      ? 'Analysis complete'
      : `Analysis ${statusQuery.data.status}`;
    const timeout = window.setTimeout(() => {
      setAnalysisMessage(nextAnalysisMessage);
      setAnalysisRunId(null);
    }, 0);
    return () => window.clearTimeout(timeout);
  }, [analysisRunId, queryClient, selectedRunId, statusQuery.data]);

  const handleAnalyzeSelected = () => {
    if (!selectedRunId || !analysisNeeded || runnerBusy || analyzeMutation.isPending) return;
    setAnalysisMessage(null);
    analyzeMutation.mutate(selectedRunId);
  };

  const actionDisabled = !analysisNeeded || runnerBusy || analyzeMutation.isPending;
  const actionLabel = currentAnalysisRunning
    ? 'Analyzing'
    : runnerBusy
      ? 'Runner Busy'
      : analysisNeeded
        ? 'Analyze Run'
        : 'Analysis Ready';

  return (
    <div className="page-content fade-up">
      <div className="results-page-header">
        <h1>Results</h1>
        <p style={{ color: 'var(--text-muted)', marginTop: 4, fontSize: '0.9rem' }}>
          Browse and analyse completed simulation runs
        </p>
      </div>

      <div className="results-layout">
        {/* Run list */}
        <div className="results-sidebar">
          <div style={{ marginBottom: 12 }}>
            <h4 style={{ color: 'var(--text-secondary)' }}>Simulation Runs</h4>
          </div>
          <RunSelector />
        </div>

        {/* Detail panel */}
        <div className="card results-detail">
          {!selectedRunId ? (
            <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', minHeight: 400, gap: 16 }}>
              <BarChart2 size={48} style={{ color: 'var(--text-muted)' }} />
              <p style={{ color: 'var(--text-muted)' }}>Select a simulation run to view results</p>
            </div>
          ) : (
            <>
              {selectedRun && (
                <div className="results-actionbar">
                  <div className="results-actionbar__status">
                    {analysisNeeded ? (
                      <RefreshCw size={17} style={{ color: 'var(--warning)' }} />
                    ) : (
                      <CheckCircle2 size={17} style={{ color: 'var(--positive)' }} />
                    )}
                    <div>
                      <div className="results-actionbar__title">
                        {analysisNeeded ? 'Post-processing needed' : 'Post-processing available'}
                      </div>
                      <div className="results-actionbar__meta">
                        {analysisMessage ?? selectedRun.path}
                      </div>
                    </div>
                  </div>
                  <button
                    className="btn btn--primary"
                    onClick={handleAnalyzeSelected}
                    disabled={actionDisabled}
                  >
                    {analyzeMutation.isPending || currentAnalysisRunning
                      ? <><Loader size={16} className="spin" /> {actionLabel}</>
                      : <><PlayCircle size={16} /> {actionLabel}</>}
                  </button>
                </div>
              )}

              {/* Tabs */}
              <div className="tabs">
                {TABS.map(({ id, label }) => (
                  <button
                    key={id}
                    id={`tab-${id}`}
                    className={`tab ${activeTab === id ? 'active' : ''}`}
                    onClick={() => setActiveTab(id)}
                  >
                    {label}
                  </button>
                ))}
              </div>

              {/* Tab content */}
              {activeTab === 'summary' && <RunSummary runId={selectedRunId} />}
              {activeTab === 'stats'   && <StatChart  runId={selectedRunId} />}
              {activeTab === 'muller'  && <MullerPlot runId={selectedRunId} />}
            </>
          )}
        </div>
      </div>
    </div>
  );
}
