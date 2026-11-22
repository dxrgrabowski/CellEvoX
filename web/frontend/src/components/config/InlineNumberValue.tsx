import { useState } from 'react';

interface InlineNumberValueProps {
  ariaLabel: string;
  value: number;
  min: number;
  max: number;
  decimals: number;
  signed?: boolean;
  color: string;
  onChange: (value: number) => void;
}

function formatInlineValue(value: number, decimals: number, signed = false) {
  return `${signed && value > 0 ? '+' : ''}${value.toFixed(decimals)}`;
}

export default function InlineNumberValue({
  ariaLabel,
  value,
  min,
  max,
  decimals,
  signed,
  color,
  onChange,
}: InlineNumberValueProps) {
  const [draft, setDraft] = useState(() => formatInlineValue(value, decimals, signed));
  const [editing, setEditing] = useState(false);
  const formattedValue = formatInlineValue(value, decimals, signed);
  const displayValue = editing ? draft : formattedValue;

  const commit = (raw: string) => {
    const next = Number(raw.trim().replace(',', '.'));
    if (!Number.isFinite(next)) return false;
    const clamped = Math.max(min, Math.min(max, next));
    const rounded = Number(clamped.toFixed(decimals));
    if (rounded !== value) onChange(rounded);
    return true;
  };

  return (
    <input
      className="inline-number-input"
      type="text"
      inputMode="decimal"
      aria-label={ariaLabel}
      value={displayValue}
      style={{ color }}
      onFocus={e => {
        setEditing(true);
        setDraft(formattedValue);
        e.currentTarget.select();
      }}
      onChange={e => {
        setDraft(e.target.value);
        commit(e.target.value);
      }}
      onBlur={() => {
        setEditing(false);
        if (draft.trim() === formattedValue.trim()) return;
        if (!commit(draft)) setDraft(formattedValue);
      }}
      onKeyDown={e => {
        if (e.key === 'Enter') e.currentTarget.blur();
        if (e.key === 'Escape') {
          setDraft(formattedValue);
          e.currentTarget.blur();
        }
      }}
    />
  );
}
