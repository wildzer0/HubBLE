/**
 * @file healthcheck.ts
 * @description The dongle health check: turns the data the dongle already reports (diagnostics, devices, firmware
 * status) into a list of plain checks with a verdict and a message key. Pure, so every rule is unit tested.
 */
import type { Device, Diagnostics, OtaStatus } from '../api/types';
import type { MessageKey } from '../i18n';
import { LOW_BATTERY_PCT } from './battery';

/** Verdict of one check. */
export type CheckLevel = 'ok' | 'warn' | 'bad';

/** One line of the report. */
export interface Check {
  /** Verdict. */
  level: CheckLevel;
  /** Translation key of the message. */
  key: MessageKey;
  /** Values for the message placeholders. */
  params: Record<string, string | number>;
}

/** Added latency (95th percentile) above which a device is flagged, microseconds (CLAUDE.md §1.1: 15 ms). */
export const LATENCY_LIMIT_US = 15000;
/** Connection interval above which a device is flagged, ms. */
export const INTERVAL_LIMIT_MS = 15;
/** Free internal RAM below which memory is flagged / critical, bytes. */
export const HEAP_WARN_BYTES = 60 * 1024;
export const HEAP_BAD_BYTES = 30 * 1024;
/** Chip temperature above which it is flagged / critical, Celsius. */
export const TEMP_WARN_C = 75;
export const TEMP_BAD_C = 85;
/** Bytes per KiB. */
const KIB = 1024;

/**
 * Run all the checks.
 *
 * @param diag Diagnostics.
 * @param devices Bonded devices.
 * @param ota Firmware status.
 * @returns The checks, most important first.
 */
export function runChecks(diag: Diagnostics, devices: readonly Device[], ota: OtaStatus): Check[] {
  const out: Check[] = [];
  const add = (
    level: CheckLevel,
    key: MessageKey,
    params: Record<string, string | number> = {},
  ) => {
    out.push({ level, key, params });
  };

  // USB: does the computer see the dongle?
  if (diag.usb_state === 'configured') add('ok', 'check.usb.ok');
  else if (diag.usb_state === 'suspended') add('warn', 'check.usb.warn');
  else add('bad', 'check.usb.bad');

  // Devices.
  if (devices.length === 0) add('warn', 'check.nodevices.warn');
  for (const d of devices) {
    const p = { name: d.name };
    if (!d.connected) {
      add('warn', 'check.connected.warn', p);
      continue;
    }
    if (!d.ready) {
      add('warn', 'check.ready.warn', p);
      continue;
    }
    add('ok', 'check.connected.ok', p);
    if (d.boot_fallback) add('warn', 'check.fallback.warn', p);
    if (d.security === 'red') add('warn', 'check.security.warn', p);
    if (d.latency_us.count > 0) {
      const ms = (d.latency_us.p95 / 1000).toFixed(1);
      add(
        d.latency_us.p95 <= LATENCY_LIMIT_US ? 'ok' : 'warn',
        d.latency_us.p95 <= LATENCY_LIMIT_US ? 'check.latency.ok' : 'check.latency.warn',
        { ...p, ms },
      );
    }
    if (d.connection.interval_ms > 0) {
      const ms = d.connection.interval_ms.toFixed(1);
      add(
        d.connection.interval_ms <= INTERVAL_LIMIT_MS ? 'ok' : 'warn',
        d.connection.interval_ms <= INTERVAL_LIMIT_MS ? 'check.interval.ok' : 'check.interval.warn',
        { ...p, ms },
      );
    }
    if (d.battery !== null) {
      add(
        d.battery <= LOW_BATTERY_PCT ? 'warn' : 'ok',
        d.battery <= LOW_BATTERY_PCT ? 'check.battery.warn' : 'check.battery.ok',
        { ...p, pct: d.battery },
      );
    }
  }

  // Memory and temperature.
  const kb = Math.round(diag.heap.free / KIB);
  if (diag.heap.free >= HEAP_WARN_BYTES) add('ok', 'check.memory.ok', { kb });
  else add(diag.heap.free >= HEAP_BAD_BYTES ? 'warn' : 'bad', 'check.memory.warn', { kb });
  if (diag.chip_temp_c !== undefined) {
    const c = diag.chip_temp_c.toFixed(1);
    if (diag.chip_temp_c > TEMP_BAD_C) add('bad', 'check.temp.bad', { c });
    else if (diag.chip_temp_c > TEMP_WARN_C) add('warn', 'check.temp.warn', { c });
    else add('ok', 'check.temp.ok', { c });
  }

  // Firmware health.
  if (diag.reset_reason === 'panic') add('warn', 'check.reset.panic.warn');
  else if (diag.reset_reason === 'brownout') add('warn', 'check.reset.brownout.warn');
  else add('ok', 'check.reset.ok', { reason: diag.reset_reason });
  if (ota.rollback_happened) add('warn', 'check.firmware.rollback.warn');
  if (ota.pending_verify) add('warn', 'check.firmware.pending.warn');
  if (diag.boot_failures > 0) add('warn', 'check.bootfail.warn', { n: diag.boot_failures });

  // Most important first, stable inside a level.
  const rank: Record<CheckLevel, number> = { bad: 0, warn: 1, ok: 2 };
  return out
    .map((c, i) => ({ c, i }))
    .sort((a, b) => rank[a.c.level] - rank[b.c.level] || a.i - b.i)
    .map((x) => x.c);
}

/**
 * Number of checks that are not fine.
 *
 * @param checks The checks.
 * @returns How many are `warn` or `bad`.
 */
export function issueCount(checks: readonly Check[]): number {
  return checks.filter((c) => c.level !== 'ok').length;
}
