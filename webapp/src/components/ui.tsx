/**
 * @file ui.tsx
 * @description Small presentational building blocks shared by every page: icons, a labelled switch, a number
 * field with a unit, a status pill and an alert box. They carry no logic, only markup and class names (see
 * `style.css`), so the pages stay short and look the same.
 */
import type { ComponentChildren } from 'preact';
import type { SecurityLevel } from '../api/types';

/** Names of the available icons. */
export type IconName =
  | 'devices'
  | 'update'
  | 'settings'
  | 'system'
  | 'keyboard'
  | 'mouse'
  | 'chevron'
  | 'back'
  | 'lock'
  | 'plus'
  | 'wifi'
  | 'bolt';

/** SVG path data (24x24, stroked) of each icon. */
const PATHS: Record<IconName, string> = {
  devices: 'M3 6h18v11H3zM8 20h8M12 17v3',
  update: 'M12 4v11m0 0-4-4m4 4 4-4M5 20h14',
  settings: 'M4 7h9m4 0h3M4 17h3m4 0h9M15 4v6M9 14v6',
  system: 'M5 4h14v6H5zM5 14h14v6H5zM8 7h.01M8 17h.01',
  keyboard: 'M3 6h18v12H3zM7 10h.01M11 10h.01M15 10h.01M7 14h10',
  mouse: 'M7 4h10v16H7zM12 4v6',
  chevron: 'm9 6 6 6-6 6',
  back: 'm15 6-6 6 6 6',
  lock: 'M6 11h12v9H6zM8 11V8a4 4 0 0 1 8 0v3',
  plus: 'M12 5v14M5 12h14',
  wifi: 'M2 9a15 15 0 0 1 20 0M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0M12 19.5h.01',
  bolt: 'M13 3 5 14h6l-1 7 8-11h-6z',
};

/**
 * Decorative icon (hidden from assistive technology; always next to a text label).
 *
 * @param props.name Icon to draw.
 * @returns The SVG element.
 */
export function Icon({ name }: { name: IconName }) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
      stroke-linecap="round"
      stroke-linejoin="round"
      aria-hidden="true"
    >
      <path d={PATHS[name]} />
    </svg>
  );
}

/**
 * A setting toggled by a switch, with an optional explanation under the label.
 *
 * @param props.label Setting name.
 * @param props.hint Optional explanation.
 * @param props.checked Current value.
 * @param props.onChange Called with the new value.
 * @returns The row element.
 */
export function SwitchRow({
  label,
  hint,
  checked,
  onChange,
}: {
  label: string;
  hint?: string;
  checked: boolean;
  onChange: (v: boolean) => void;
}) {
  return (
    <label class="switch-row">
      <span class="text">
        <b>{label}</b>
        {hint && <span class="hint">{hint}</span>}
      </span>
      <input
        type="checkbox"
        role="switch"
        class="switch"
        checked={checked}
        onChange={(e) => {
          onChange(e.currentTarget.checked);
        }}
      />
    </label>
  );
}

/**
 * A number input with a unit shown inside the field and an optional explanation.
 *
 * @param props.label Field name.
 * @param props.unit Unit text (for example "s").
 * @param props.value Current value.
 * @param props.min Lowest allowed value.
 * @param props.max Highest allowed value.
 * @param props.step Step of the input (default 1).
 * @param props.hint Optional explanation.
 * @param props.id Element id (used by tests and labels).
 * @param props.onChange Called with the typed number (NaN when empty).
 * @returns The field element.
 */
export function NumberField({
  label,
  unit,
  value,
  min,
  max,
  step,
  hint,
  id,
  onChange,
}: {
  label: string;
  unit: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  hint?: string;
  id: string;
  onChange: (v: number) => void;
}) {
  const bad = !Number.isFinite(value) || value < min || value > max;
  return (
    <label class="field">
      <span>{label}</span>
      <span class="suffix">
        <input
          id={id}
          class="input"
          type="number"
          inputMode="decimal"
          min={min}
          max={max}
          step={step ?? 1}
          value={value}
          aria-invalid={bad}
          onInput={(e) => {
            onChange(e.currentTarget.valueAsNumber);
          }}
        />
        <em>{unit}</em>
      </span>
      {hint && <span class="hint">{hint}</span>}
    </label>
  );
}

/**
 * Message box.
 *
 * @param props.kind Colour: info (default), ok, warn or bad.
 * @param props.role ARIA role (`alert` for errors, `status` for results).
 * @param props.children Content.
 * @returns The box element.
 */
export function Alert({
  kind,
  role,
  testId,
  children,
}: {
  kind?: 'ok' | 'warn' | 'bad';
  role?: 'alert' | 'status';
  testId?: string;
  children: ComponentChildren;
}) {
  return (
    <div class={`alert ${kind ?? ''}`} role={role} data-testid={testId}>
      {children}
    </div>
  );
}

/**
 * Coloured status pill.
 *
 * @param props.tone ok, warn, bad or off.
 * @param props.children Text.
 * @returns The pill element.
 */
export function Pill({
  tone,
  testId,
  children,
}: {
  tone: 'ok' | 'warn' | 'bad' | 'off';
  testId?: string;
  children: ComponentChildren;
}) {
  return (
    <span class={`pill ${tone}`} data-testid={testId}>
      {children}
    </span>
  );
}

/** Pill tone of each security level. */
export const SECURITY_TONE: Record<SecurityLevel, 'ok' | 'warn' | 'bad' | 'off'> = {
  green: 'ok',
  yellow: 'warn',
  red: 'bad',
  unknown: 'off',
};

/**
 * Signal strength drawn as four bars.
 *
 * @param props.rssi Signal in dBm.
 * @param props.label Accessible name ("Signal strength").
 * @returns The bars element.
 */
export function SignalBars({ rssi, label }: { rssi: number; label: string }) {
  const level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  return (
    <span class="bars" role="img" aria-label={`${label}: ${String(level)}/4`}>
      {[1, 2, 3, 4].map((n) => (
        <i key={n} class={n <= level ? 'on' : ''} />
      ))}
    </span>
  );
}
