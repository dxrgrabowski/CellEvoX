import { useResultsStore } from '../stores';
import RunSelector from '../components/results/RunSelector';
import StatChart from '../components/results/StatChart';
import MullerPlot from '../components/results/MullerPlot';
import RunSummary from '../components/results/RunSummary';
import { BarChart2 } from 'lucide-react';

const TABS = [
  { id: 'summary', label: 'Summary' },
  { id: 'stats',   label: 'Statistics' },
  { id: 'muller',  label: 'Müller Plot' },
];

export default function ResultsPage() {
  const { selectedRunId, activeTab, setActiveTab } = useResultsStore();

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
