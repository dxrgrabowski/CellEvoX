import { useState, useRef } from 'react';
import { useNavigate } from 'react-router-dom';
import { Settings, Layers, Database, Dna, FileDown, FileUp, RotateCcw, Play } from 'lucide-react';
import { useConfigStore } from '../stores';
import CoreParams from '../components/config/CoreParams';
import PopulationParams from '../components/config/PopulationParams';
import SpatialParams from '../components/config/SpatialParams';
import MutationEditor from '../components/config/MutationEditor';
import OutputParams from '../components/config/OutputParams';

const SECTIONS = [
  { id: 'core',       label: 'Core',       icon: Settings, comp: CoreParams       },
  { id: 'population', label: 'Population', icon: Layers,   comp: PopulationParams },
  { id: 'mutations',  label: 'Mutations',  icon: Dna,      comp: MutationEditor   },
  { id: 'output',     label: 'Output',     icon: Database, comp: OutputParams      },
];

export default function ConfigPage() {
  const { config, setFullConfig, resetConfig } = useConfigStore();
  const [activeSection, setActiveSection] = useState('core');
  const fileRef = useRef<HTMLInputElement>(null);
  const navigate = useNavigate();

  const isSpatial =
    config.simulation_mode === 'spatial_3d_density' ||
    config.simulation_mode === 'spatial_3d_capacity';

  const exportJson = () => {
    const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'cellevox_config.json';
    a.click();
  };

  const importJson = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
      try { setFullConfig(JSON.parse(ev.target!.result as string)); }
      catch { alert('Invalid JSON config file'); }
    };
    reader.readAsText(file);
    e.target.value = '';
  };

  // Build preview JSON, strip spatial fields when not in spatial mode
  const previewJson: Record<string, unknown> = { ...config };
  if (!isSpatial) {
    const {
      spatial_domain_size, max_local_density, sample_radius,
      spring_constant, mech_dt, mech_substeps, epsilon,
      ...rest
    } = previewJson as Record<string, unknown>;
    void spatial_domain_size; void max_local_density; void sample_radius;
    void spring_constant; void mech_dt; void mech_substeps; void epsilon;
    Object.keys(previewJson).forEach(k => delete previewJson[k]);
    Object.assign(previewJson, rest);
  }

  return (
    <div className="page-content fade-up">
      {/* Page header */}
      <div className="flex items-center justify-between" style={{ marginBottom: '28px' }}>
        <div>
          <h1>Configuration</h1>
          <p style={{ color: 'var(--text-muted)', marginTop: 4, fontSize: '0.9rem' }}>
            Set up simulation parameters and mutation profile
          </p>
        </div>
        <div className="flex gap-8">
          <button id="btn-reset-config" className="btn btn--ghost" onClick={resetConfig}>
            <RotateCcw size={15} /> Reset
          </button>
          <button id="btn-import-config" className="btn btn--ghost" onClick={() => fileRef.current?.click()}>
            <FileUp size={15} /> Import JSON
          </button>
          <input ref={fileRef} type="file" accept=".json" hidden onChange={importJson} />
          <button id="btn-export-config" className="btn btn--ghost" onClick={exportJson}>
            <FileDown size={15} /> Export JSON
          </button>
          <button id="btn-go-run" className="btn btn--primary" onClick={() => navigate('/run')}>
            <Play size={15} /> Run Simulation
          </button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '240px 1fr 340px', gap: '24px', alignItems: 'start' }}>
        {/* Left nav */}
        <div className="flex-col gap-8" style={{ position: 'sticky', top: '80px' }}>
          {SECTIONS.map(({ id, label, icon: Icon }) => (
            <button
              key={id}
              id={`nav-${id}`}
              onClick={() => setActiveSection(id)}
              style={{
                display: 'flex', alignItems: 'center', gap: '10px',
                padding: '10px 14px', borderRadius: 'var(--radius-md)',
                border: `1px solid ${activeSection === id ? 'var(--accent)' : 'transparent'}`,
                background: activeSection === id ? 'var(--accent-dim)' : 'transparent',
                color: activeSection === id ? 'var(--accent)' : 'var(--text-secondary)',
                cursor: 'pointer', textAlign: 'left', width: '100%',
                fontSize: '0.9rem', fontWeight: 500,
                fontFamily: 'var(--font-sans)',
                transition: 'all var(--transition)',
              }}
            >
              <Icon size={16} strokeWidth={activeSection === id ? 2.5 : 2} />
              {label}
            </button>
          ))}

          {/* Spatial 3D nav item */}
          <button
            id="nav-spatial"
            onClick={() => setActiveSection('spatial')}
            style={{
              display: 'flex', alignItems: 'center', gap: '10px',
              padding: '10px 14px', borderRadius: 'var(--radius-md)',
              border: `1px solid ${
                activeSection === 'spatial' ? 'rgba(124,58,237,0.5)'
                : isSpatial ? 'rgba(124,58,237,0.2)' : 'transparent'
              }`,
              background: activeSection === 'spatial' ? 'var(--purple-dim)' : 'transparent',
              color: activeSection === 'spatial' ? '#a78bfa'
                : isSpatial ? 'rgba(167,139,250,0.6)' : 'var(--text-muted)',
              cursor: 'pointer', textAlign: 'left', width: '100%',
              fontSize: '0.9rem', fontWeight: 500,
              fontFamily: 'var(--font-sans)',
              transition: 'all var(--transition)',
              opacity: isSpatial ? 1 : 0.45,
            }}
          >
            <span style={{ fontSize: '16px' }}>⬡</span>
            Spatial 3D
            {!isSpatial && <span style={{ fontSize: '0.7rem', marginLeft: 'auto' }}>inactive</span>}
          </button>
        </div>

        {/* Main form panel */}
        <div className="card" style={{ minHeight: '400px' }}>
          <div className="section-header">
            <div className="icon">
              {activeSection === 'spatial'
                ? <span>⬡</span>
                : (() => { const S = SECTIONS.find(s => s.id === activeSection); return S ? <S.icon size={16} /> : null; })()
              }
            </div>
            <h3>
              {activeSection === 'spatial'
                ? 'Spatial 3D Parameters'
                : SECTIONS.find(s => s.id === activeSection)?.label}
            </h3>
          </div>

          {activeSection === 'spatial'    ? <SpatialParams config={config} /> :
           activeSection === 'mutations'  ? <MutationEditor mutations={config.mutations} /> :
           activeSection === 'output'     ? <OutputParams config={config} /> :
           activeSection === 'population' ? <PopulationParams config={config} /> :
                                            <CoreParams config={config} />}
        </div>

        {/* Live JSON preview */}
        <div style={{ position: 'sticky', top: '80px' }}>
          <div className="card">
            <div className="section-header">
              <h4 style={{ margin: 0 }}>Live JSON Preview</h4>
            </div>
            <pre className="json-preview">
              {JSON.stringify(previewJson, null, 2)}
            </pre>
          </div>
        </div>
      </div>
    </div>
  );
}
