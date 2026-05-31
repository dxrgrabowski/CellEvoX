import { useState, type CSSProperties, type InputHTMLAttributes } from 'react';

interface SmoothSliderProps
  extends Omit<InputHTMLAttributes<HTMLInputElement>, 'type' | 'value' | 'onChange' | 'min' | 'max' | 'step'> {
  value: number;
  min: number;
  max: number;
  step: number;
  variant?: 'standard' | 'effect';
  onValueChange: (value: number) => void;
}

function sliderRatio(value: number, min: number, max: number) {
  if (max === min) return 0;
  return Math.max(0, Math.min(100, ((value - min) / (max - min)) * 100));
}

export default function SmoothSlider({
  value,
  min,
  max,
  step,
  variant = 'standard',
  onValueChange,
  ...props
}: SmoothSliderProps) {
  const [dragging, setDragging] = useState(false);
  const ratio = sliderRatio(value, min, max);
  const style = { '--slider-ratio': `${ratio}%` } as CSSProperties;

  return (
    <div
      className={`smooth-slider smooth-slider--${variant} ${dragging ? 'is-dragging' : ''}`}
      style={style}
    >
      <div className="smooth-slider__track">
        <div className="smooth-slider__fill" />
      </div>
      <div className="smooth-slider__thumb" />
      <input
        {...props}
        className="smooth-slider__input"
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={event => onValueChange(Number(event.target.value))}
        onPointerDown={() => setDragging(true)}
        onPointerUp={() => setDragging(false)}
        onPointerCancel={() => setDragging(false)}
        onBlur={() => setDragging(false)}
      />
    </div>
  );
}
