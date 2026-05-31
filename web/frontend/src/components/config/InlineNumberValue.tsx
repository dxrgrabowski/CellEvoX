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
  const displayValue = editing ? draft : formatInlineValue(value, decimals, signed);

  const commit = (raw: string) => {
    const next = Number(raw.trim().replace(',', '.'));
    if (!Number.isFinite(next)) return false;
    const clamped = Math.max(min, Math.min(max, next));
    onChange(Number(clamped.toFixed(decimals)));
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
        setDraft(formatInlineValue(value, decimals, signed));
        e.currentTarget.select();
      }}
      onChange={e => {
        setDraft(e.target.value);
        commit(e.target.value);
      }}
      onBlur={() => {
        setEditing(false);
        if (!commit(draft)) setDraft(formatInlineValue(value, decimals, signed));
      }}
      onKeyDown={e => {
        if (e.key === 'Enter') e.currentTarget.blur();
        if (e.key === 'Escape') {
          setDraft(formatInlineValue(value, decimals, signed));
          e.currentTarget.blur();
        }
      }}
    />
  );
}
