/**
 * @file BatteryGauge.tsx
 * @description Battery indicator: a battery icon filled to the reported level, the percentage as text and a colour
 * (green, amber at or below 30 %, red at or below the low-battery threshold). The number is always shown, so the
 * colour is never the only carrier of the information (accessibility).
 */
import { LOW_BATTERY_PCT } from '../lib/battery';
import { useT } from '../i18n/context';

/** Level at or below which the gauge turns amber. */
const WARN_BATTERY_PCT = 30;
/** Width in SVG units of the fillable area of the icon. */
const FILL_WIDTH = 20;

/**
 * Colour class for a level.
 *
 * @param pct Battery percentage.
 * @returns `bad`, `warn` or `ok`.
 */
function tone(pct: number): 'ok' | 'warn' | 'bad' {
  if (pct <= LOW_BATTERY_PCT) return 'bad';
  if (pct <= WARN_BATTERY_PCT) return 'warn';
  return 'ok';
}

/**
 * Render the gauge.
 *
 * - `pct`: Battery percentage, or null when the device does not report one.
 * - `large`: Bigger icon and text (device page).
 * @returns The gauge element.
 */
export function BatteryGauge({ pct, large = false }: { pct: number | null; large?: boolean }) {
  const t = useT();
  if (pct === null) return <span class="muted">–</span>;
  const level = Math.max(0, Math.min(100, pct));
  return (
    <span class={`battery ${tone(level)}${large ? ' large' : ''}`} data-testid="battery">
      <svg viewBox="0 0 26 12" aria-hidden="true" focusable="false">
        <rect
          x="0.75"
          y="0.75"
          width="22.5"
          height="10.5"
          rx="2"
          fill="none"
          stroke="currentColor"
        />
        <rect x="24" y="4" width="2" height="4" rx="0.5" fill="currentColor" />
        <rect
          x="2"
          y="2"
          width={(FILL_WIDTH * level) / 100}
          height="8"
          rx="1"
          fill="currentColor"
        />
      </svg>
      <span>{t('device.battery_value', { pct: level })}</span>
    </span>
  );
}
