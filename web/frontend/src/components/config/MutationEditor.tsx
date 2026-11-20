import { Plus, Trash2 } from 'lucide-react';
import type { MutationCountModel, MutationEffectMode, MutationType } from '../../types/simulation';
import { useConfigStore } from '../../stores';
import InlineNumberValue from './InlineNumberValue';
import SmoothSlider from './SmoothSlider';

interface Props { mutations: MutationType[] }

const EFFECT_MODES: { value: MutationEffectMode; label: string }[] = [
  { value: 'constant', label: 'Constant' },
  { value: 'gamma', label: 'Gamma' },
];

const COUNT_MODELS: { value: MutationCountModel; label: string }[] = [
  { value: 'bernoulli', label: 'Bernoulli' },
  { value: 'poisson', label: 'Poisson' },
];

const GAMMA_DEFAULTS = {
  effect_mean: 0.05,
  effect_std: 0.02,
  effect_limit: 0.2,
  effect_direction: 'positive' as const,
};

const DEFAULT_PROBABILITY = 0.01;
const DEFAULT_LAMBDA = 0.01;

export default function MutationEditor({ mutations }: Props) {
  const setConfig = useConfigStore(s => s.setConfig);

  const update = (idx: number, patch: Partial<MutationType>) => {
    const next = mutations.map((m, i) => i === idx ? { ...m, ...patch } : m);
    setConfig({ mutations: next });
  };

  const add = () => {
    const nextId = (mutations.length > 0 ? Math.max(...mutations.map(m => m.id)) : 0) + 1;
    setConfig({
      mutations: [...mutations, {
        id: nextId,
        is_driver: false,
        effect: 0,
        probability: DEFAULT_PROBABILITY,
        count_model: 'bernoulli',
        effect_mode: 'constant',
      }]
    });
  };

  const remove = (idx: number) => {
    setConfig({ mutations: mutations.filter((_, i) => i !== idx) });
  };

  return (
    <div className="flex-col gap-16">
      {mutations.map((mut, idx) => {
        const effectMode = mut.effect_mode ?? 'constant';
        const countModel = mut.count_model ?? 'bernoulli';
        const effectMean = mut.effect_mean ?? GAMMA_DEFAULTS.effect_mean;
        const effectStd = mut.effect_std ?? GAMMA_DEFAULTS.effect_std;
        const effectLimit = mut.effect_limit ?? GAMMA_DEFAULTS.effect_limit;
        const effectDirection = mut.effect_direction ?? GAMMA_DEFAULTS.effect_direction;

        return (
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

            {/* Effect mode */}
            <div className="flex items-center justify-between" style={{ marginBottom: 14 }}>
              <span className="field-label" style={{ margin: 0 }}>Fitness Effect Mode</span>
              <div className="flex gap-8">
                {EFFECT_MODES.map(mode => {
                  const isSelected = effectMode === mode.value;
                  return (
                    <button
                      key={mode.value}
                      id={`mut-${idx}-effect-mode-${mode.value}`}
                      type="button"
                      className={`badge ${isSelected ? 'badge--driver' : 'badge--passenger'}`}
                      style={{ cursor: 'pointer', border: 'none' }}
                      onClick={() => update(idx, {
                        effect_mode: mode.value,
                        ...(mode.value === 'gamma'
                          ? {
                              effect_mean: mut.effect_mean ?? GAMMA_DEFAULTS.effect_mean,
                              effect_std: mut.effect_std ?? GAMMA_DEFAULTS.effect_std,
                              effect_limit: mut.effect_limit ?? GAMMA_DEFAULTS.effect_limit,
                              effect_direction: mut.effect_direction ?? GAMMA_DEFAULTS.effect_direction,
                            }
                          : { effect: mut.effect ?? 0 }),
                      })}
                    >
                      {mode.label}
                    </button>
                  );
                })}
              </div>
            </div>

            {/* Count model */}
            <div className="flex items-center justify-between" style={{ marginBottom: 14 }}>
              <span className="field-label" style={{ margin: 0 }}>Mutations per Division</span>
              <div className="flex gap-8">
                {COUNT_MODELS.map(model => {
                  const isSelected = countModel === model.value;
                  return (
                    <button
                      key={model.value}
                      id={`mut-${idx}-count-model-${model.value}`}
                      type="button"
                      className={`badge ${isSelected ? 'badge--driver' : 'badge--passenger'}`}
                      style={{ cursor: 'pointer', border: 'none' }}
                      onClick={() => update(idx, {
                        count_model: model.value,
                        ...(model.value === 'poisson'
                          ? { lambda: mut.lambda ?? DEFAULT_LAMBDA, probability: undefined }
                          : { probability: mut.probability ?? DEFAULT_PROBABILITY, lambda: undefined }),
                      })}
                    >
                      {model.label}
                    </button>
                  );
                })}
              </div>
            </div>

            <div className="grid-2">
              {/* Effect or mean */}
              {effectMode === 'constant' ? (
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
              ) : (
                <div className="field">
                  <div className="field-label">
                    Effect Mean (μ)
                    <InlineNumberValue
                      ariaLabel={`Mutation ${mut.id} effect mean`}
                      value={effectMean}
                      min={0.0001}
                      max={0.5}
                      decimals={4}
                      color="var(--accent)"
                      onChange={value => update(idx, {
                        effect_mean: value,
                        effect_limit: Math.max(effectLimit, value),
                      })}
                    />
                  </div>
                  <SmoothSlider
                    id={`mut-${idx}-effect-mean`}
                    min={0.0001} max={0.5} step={0.0001}
                    aria-label={`Mutation ${mut.id} effect mean slider`}
                    value={effectMean}
                    onValueChange={value => update(idx, {
                      effect_mean: value,
                      effect_limit: Math.max(effectLimit, value),
                    })}
                  />
                  <span className="field-hint">Requested mean of |Δ fitness|</span>
                </div>
              )}

              {/* Probability (Bernoulli) or Lambda (Poisson) */}
              {countModel === 'bernoulli' ? (
                <div className="field">
                  <div className="field-label">
                    Probability
                    <InlineNumberValue
                      ariaLabel={`Mutation ${mut.id} probability`}
                      value={mut.probability ?? DEFAULT_PROBABILITY}
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
                    value={mut.probability ?? DEFAULT_PROBABILITY}
                    onValueChange={value => update(idx, { probability: value })}
                  />
                  <span className="field-hint">Per-cell per-step mutation chance</span>
                </div>
              ) : (
                <div className="field">
                  <div className="field-label">
                    Lambda (λ)
                    <InlineNumberValue
                      ariaLabel={`Mutation ${mut.id} lambda`}
                      value={mut.lambda ?? DEFAULT_LAMBDA}
                      min={0}
                      max={10}
                      decimals={4}
                      color="var(--accent)"
                      onChange={value => update(idx, { lambda: value })}
                    />
                  </div>
                  <SmoothSlider
                    id={`mut-${idx}-lambda`}
                    min={0} max={10} step={0.0001}
                    aria-label={`Mutation ${mut.id} lambda slider`}
                    value={mut.lambda ?? DEFAULT_LAMBDA}
                    onValueChange={value => update(idx, { lambda: value })}
                  />
                  <span className="field-hint">Mean mutation events per division (Poisson)</span>
                </div>
              )}
            </div>

            {effectMode === 'gamma' && (
              <>
                <div className="grid-2" style={{ marginTop: 14 }}>
                  {/* Standard deviation */}
                  <div className="field">
                    <div className="field-label">
                      Effect Std (σ)
                      <InlineNumberValue
                        ariaLabel={`Mutation ${mut.id} effect standard deviation`}
                        value={effectStd}
                        min={0.0001}
                        max={0.5}
                        decimals={4}
                        color="var(--accent)"
                        onChange={value => update(idx, { effect_std: value })}
                      />
                    </div>
                    <SmoothSlider
                      id={`mut-${idx}-effect-std`}
                      min={0.0001} max={0.5} step={0.0001}
                      aria-label={`Mutation ${mut.id} effect standard deviation slider`}
                      value={effectStd}
                      onValueChange={value => update(idx, { effect_std: value })}
                    />
                    <span className="field-hint">Standard deviation of |Δ fitness|</span>
                  </div>

                  {/* Limit */}
                  <div className="field">
                    <div className="field-label">
                      Effect Limit (L)
                      <InlineNumberValue
                        ariaLabel={`Mutation ${mut.id} effect limit`}
                        value={effectLimit}
                        min={Math.max(0.0001, effectMean)}
                        max={0.9999}
                        decimals={4}
                        color="var(--accent)"
                        onChange={value => update(idx, { effect_limit: value })}
                      />
                    </div>
                    <SmoothSlider
                      id={`mut-${idx}-effect-limit`}
                      min={Math.max(0.0001, effectMean)} max={0.9999} step={0.0001}
                      aria-label={`Mutation ${mut.id} effect limit slider`}
                      value={effectLimit}
                      onValueChange={value => update(idx, { effect_limit: value })}
                    />
                    <span className="field-hint">Hard cap on |Δ fitness|; must be ≥ μ</span>
                  </div>
                </div>

                <div className="field" style={{ marginTop: 14 }}>
                  <label className="field-label">Effect Direction</label>
                  <div className="flex gap-8">
                    {(['positive', 'negative'] as const).map(direction => {
                      const isSelected = effectDirection === direction;
                      return (
                        <button
                          key={direction}
                          id={`mut-${idx}-effect-direction-${direction}`}
                          type="button"
                          className={`badge ${isSelected ? 'badge--driver' : 'badge--passenger'}`}
                          style={{ cursor: 'pointer', border: 'none' }}
                          onClick={() => update(idx, { effect_direction: direction })}
                        >
                          {direction === 'positive' ? '+ Beneficial' : '− Deleterious'}
                        </button>
                      );
                    })}
                  </div>
                  <span className="field-hint">
                    Gamma samples are clamped at the limit, so the realized mean can be lower than μ.
                  </span>
                </div>
              </>
            )}
          </div>
        );
      })}

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
