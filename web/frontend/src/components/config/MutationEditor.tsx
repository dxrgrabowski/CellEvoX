import { Plus, Trash2 } from 'lucide-react';
import type { MutationType } from '../../types/simulation';
import { useConfigStore } from '../../stores';
import InlineNumberValue from './InlineNumberValue';
import SmoothSlider from './SmoothSlider';

interface Props { mutations: MutationType[] }

export default function MutationEditor({ mutations }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);

  const update = (idx: number, patch: Partial<MutationType>) => {
    const next = mutations.map((m, i) => i === idx ? { ...m, ...patch } : m);
    setConfig({ mutations: next });
  };

  const add = () => {
    const nextId = (mutations.length > 0 ? Math.max(...mutations.map(m => m.id)) : 0) + 1;
    setConfig({
      mutations: [...mutations, { id: nextId, is_driver: false, effect: 0, probability: 0.01 }]
    });
  };

  const remove = (idx: number) => {
    setConfig({ mutations: mutations.filter((_, i) => i !== idx) });
  };

  return (
    <div className="flex-col gap-16">
      {mutations.map((mut, idx) => (
        <div key={mut.id} className="card fade-up" style={{ position: 'relative', padding: '18px' }}>
          <div className="flex items-center justify-between" style={{ marginBottom: 14 }}>
            <div className="flex items-center gap-12">
              <span style={{ fontFamily: 'var(--font-mono)', color: 'var(--text-muted)', fontSize: '0.8rem' }}>
                #{mut.id}
              </span>
              <button
                id={`mut-${idx}-driver-toggle`}
                className={`badge ${mut.is_driver ? 'badge--driver' : 'badge--passenger'}`}
                style={{ cursor: 'pointer', border: 'none' }}
                onClick={() => update(idx, { is_driver: !mut.is_driver })}
              >
                {mut.is_driver ? '⚡ Driver' : '○ Passenger'}
              </button>
            </div>
            <button
              id={`mut-${idx}-remove`}
              className="btn btn--icon btn--danger tooltip-target tooltip-target--compact"
              onClick={() => remove(idx)}
              aria-label="Remove mutation"
              data-tooltip="Remove mutation"
            >
              <Trash2 size={14} />
            </button>
          </div>

          <div className="grid-2">
            {/* Effect */}
            <div className="field">
              <div className="field-label">
                Fitness Effect
                <InlineNumberValue
                  ariaLabel={`Mutation ${mut.id} fitness effect`}
                  value={mut.effect}
                  min={-0.5}
                  max={0.5}
                  decimals={4}
                  signed
                  color={mut.effect > 0 ? 'var(--positive)' : mut.effect < 0 ? 'var(--negative)' : 'var(--neutral)'}
                  onChange={value => update(idx, { effect: value })}
                />
              </div>
              <SmoothSlider
                id={`mut-${idx}-effect`}
                min={-0.5} max={0.5} step={0.001}
                variant="effect"
                aria-label={`Mutation ${mut.id} fitness effect slider`}
                value={mut.effect}
                onValueChange={value => update(idx, { effect: value })}
              />
              <span className="field-hint">Δ fitness per division event</span>
            </div>

            {/* Probability */}
            <div className="field">
              <div className="field-label">
                Probability
                <InlineNumberValue
                  ariaLabel={`Mutation ${mut.id} probability`}
                  value={mut.probability}
                  min={0.00001}
                  max={0.5}
                  decimals={5}
                  color="var(--accent)"
                  onChange={value => update(idx, { probability: value })}
                />
              </div>
              <SmoothSlider
                id={`mut-${idx}-prob`}
                min={0.00001} max={0.5} step={0.00001}
                aria-label={`Mutation ${mut.id} probability slider`}
                value={mut.probability}
                onValueChange={value => update(idx, { probability: value })}
              />
              <span className="field-hint">Per-cell per-step mutation chance</span>
            </div>
          </div>
        </div>
      ))}

      <button id="btn-add-mutation" className="btn btn--ghost" onClick={add}
        style={{ alignSelf: 'flex-start', borderStyle: 'dashed' }}>
        <Plus size={16} /> Add Mutation
      </button>

      {mutations.length === 0 && (
        <div style={{ textAlign: 'center', padding: '32px', color: 'var(--text-muted)', fontSize: '0.875rem' }}>
          No mutations defined. Add at least one to run a meaningful simulation.
        </div>
      )}
    </div>
  );
}
