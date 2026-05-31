import { ChevronDown, ChevronUp } from 'lucide-react';
import type { InputHTMLAttributes } from 'react';

interface NumberInputProps
  extends Omit<InputHTMLAttributes<HTMLInputElement>, 'type' | 'value' | 'onChange'> {
  value: number;
  onValueChange: (value: number) => void;
  inputClassName?: string;
}

function clampNumber(value: number, min?: number | string, max?: number | string) {
  const minValue = min === undefined ? undefined : Number(min);
  const maxValue = max === undefined ? undefined : Number(max);
  let next = value;
  if (minValue !== undefined && Number.isFinite(minValue)) next = Math.max(next, minValue);
  if (maxValue !== undefined && Number.isFinite(maxValue)) next = Math.min(next, maxValue);
  return next;
}

function stepPrecision(step?: number | string) {
  const stepValue = step === undefined ? '1' : String(step);
  if (stepValue.includes('e-')) return Number(stepValue.split('e-')[1]) || 0;
  return stepValue.includes('.') ? stepValue.split('.')[1].length : 0;
}

export default function NumberInput({
  value,
  onValueChange,
  inputClassName = '',
  step = 1,
  min,
  max,
  disabled,
  ...props
}: NumberInputProps) {
  const stepValue = Number(step) || 1;
  const precision = stepPrecision(step);

  const commit = (next: number) => {
    if (!Number.isFinite(next)) return;
    const clamped = clampNumber(next, min, max);
    onValueChange(Number(clamped.toFixed(Math.min(precision, 8))));
  };

  const adjust = (direction: 1 | -1) => {
    commit(Number(value) + direction * stepValue);
  };

  return (
    <div className={`number-input ${disabled ? 'number-input--disabled' : ''}`}>
      <input
        {...props}
        className={`input number-input__control ${inputClassName}`}
        type="number"
        min={min}
        max={max}
        step={step}
        value={value}
        disabled={disabled}
        onChange={e => commit(Number(e.target.value))}
      />
      <div className="number-input__steppers" aria-hidden={disabled ? true : undefined}>
        <button
          type="button"
          className="number-input__stepper"
          tabIndex={-1}
          disabled={disabled}
          onMouseDown={e => e.preventDefault()}
          onClick={() => adjust(1)}
          aria-label="Increase value"
        >
          <ChevronUp size={11} strokeWidth={2.4} />
        </button>
        <button
          type="button"
          className="number-input__stepper"
          tabIndex={-1}
          disabled={disabled}
          onMouseDown={e => e.preventDefault()}
          onClick={() => adjust(-1)}
          aria-label="Decrease value"
        >
          <ChevronDown size={11} strokeWidth={2.4} />
        </button>
      </div>
    </div>
  );
}
