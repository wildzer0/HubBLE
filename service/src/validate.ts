/**
 * @file validate.ts
 * @description Strict validation of a device report against `docs/api/device-report.schema.json` (written by hand
 * so the Worker needs no schema library). Unknown properties, wrong types and out-of-range values are all errors.
 * The module is pure: it neither reads nor writes anything.
 */

/** A validated report (the same shape as the schema; optional fields may be null or absent). */
export interface Report {
  format: 'hubble-device-report';
  version: 1;
  firmware: { version: string; board: string };
  device: {
    kind: 'keyboard' | 'pointer' | 'unknown';
    adv_name?: string | null;
    appearance?: number | null;
    manufacturer?: string | null;
    model?: string | null;
    fw_revision?: string | null;
    pnp?: {
      vendor_id_source?: number;
      vendor_id?: number;
      product_id?: number;
      version?: number;
    } | null;
    report_map_hex: string;
    parse?: {
      result?: number;
      fields?: number;
      boot_fallback?: boolean;
      primary_page?: number;
      primary_usage?: number;
      has_pointer?: boolean;
    };
    security?: 'green' | 'yellow' | 'red' | 'unknown';
    connection?: { interval_ms?: number; latency?: number; timeout_ms?: number };
    battery_reported?: boolean;
    latency_us?: { count?: number; avg?: number; p95?: number; max?: number };
  };
  notes?: string | null;
}

/** Result of {@link validateReport}. */
export type Validation = { ok: true; report: Report } | { ok: false; error: string };

/** Largest report map accepted, in hex characters (schema: maxLength 2048). */
export const MAX_HEX_CHARS = 2048;

type Obj = Record<string, unknown>;

/** Error thrown internally and turned into a {@link Validation} failure. */
class Invalid extends Error {}

const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);

/** Fail unless @p v is an object whose keys are all in @p allowed and which has every key in @p required. */
function object(
  v: unknown,
  path: string,
  allowed: readonly string[],
  required: readonly string[],
): Obj {
  if (!isObj(v)) throw new Invalid(`${path}: must be an object`);
  for (const k of Object.keys(v)) {
    if (!allowed.includes(k)) throw new Invalid(`${path}.${k}: unknown property`);
  }
  for (const k of required) {
    if (!(k in v)) throw new Invalid(`${path}.${k}: required`);
  }
  return v;
}

/** String of at most @p max characters; @p nullable also allows null. */
function str(v: unknown, path: string, max: number): void {
  if (typeof v !== 'string') throw new Invalid(`${path}: must be a string`);
  if (v.length > max) throw new Invalid(`${path}: longer than ${String(max)} characters`);
}

/** Integer within [min, max]. */
function int(v: unknown, path: string, min: number, max = Number.MAX_SAFE_INTEGER): void {
  if (typeof v !== 'number' || !Number.isInteger(v))
    throw new Invalid(`${path}: must be an integer`);
  if (v < min || v > max)
    throw new Invalid(`${path}: must be between ${String(min)} and ${String(max)}`);
}

/** Optional property: absent is fine, otherwise run @p check on it. */
function opt(o: Obj, key: string, path: string, check: (v: unknown, p: string) => void): void {
  if (key in o) check(o[key], `${path}.${key}`);
}

/** Like {@link opt} but null is accepted too. */
function optNull(o: Obj, key: string, path: string, check: (v: unknown, p: string) => void): void {
  if (key in o && o[key] !== null) check(o[key], `${path}.${key}`);
}

/**
 * Validate an untrusted value as a device report.
 *
 * @param input Parsed JSON.
 * @returns The same object typed as {@link Report}, or the first problem found (with its path).
 */
export function validateReport(input: unknown): Validation {
  try {
    const r = object(
      input,
      'report',
      ['format', 'version', 'firmware', 'device', 'notes'],
      ['format', 'version', 'firmware', 'device'],
    );
    if (r.format !== 'hubble-device-report')
      throw new Invalid('report.format: must be "hubble-device-report"');
    if (r.version !== 1) throw new Invalid('report.version: must be 1');

    const fw = object(r.firmware, 'report.firmware', ['version', 'board'], ['version', 'board']);
    str(fw.version, 'report.firmware.version', 32);
    str(fw.board, 'report.firmware.board', 32);

    const d = object(
      r.device,
      'report.device',
      [
        'kind',
        'adv_name',
        'appearance',
        'manufacturer',
        'model',
        'fw_revision',
        'pnp',
        'report_map_hex',
        'parse',
        'security',
        'connection',
        'battery_reported',
        'latency_us',
      ],
      ['kind', 'report_map_hex'],
    );
    if (d.kind !== 'keyboard' && d.kind !== 'pointer' && d.kind !== 'unknown') {
      throw new Invalid('report.device.kind: must be keyboard, pointer or unknown');
    }
    optNull(d, 'adv_name', 'report.device', (v, p) => {
      str(v, p, 32);
    });
    optNull(d, 'appearance', 'report.device', (v, p) => {
      int(v, p, 0, 65535);
    });
    optNull(d, 'manufacturer', 'report.device', (v, p) => {
      str(v, p, 48);
    });
    optNull(d, 'model', 'report.device', (v, p) => {
      str(v, p, 48);
    });
    optNull(d, 'fw_revision', 'report.device', (v, p) => {
      str(v, p, 48);
    });
    optNull(d, 'pnp', 'report.device', (v, p) => {
      const o = object(v, p, ['vendor_id_source', 'vendor_id', 'product_id', 'version'], []);
      opt(o, 'vendor_id_source', p, (x, q) => {
        int(x, q, 0, 255);
      });
      opt(o, 'vendor_id', p, (x, q) => {
        int(x, q, 0, 65535);
      });
      opt(o, 'product_id', p, (x, q) => {
        int(x, q, 0, 65535);
      });
      opt(o, 'version', p, (x, q) => {
        int(x, q, 0, 65535);
      });
    });
    str(d.report_map_hex, 'report.device.report_map_hex', MAX_HEX_CHARS);
    if (!/^([0-9a-fA-F]{2})*$/.test(d.report_map_hex as string)) {
      throw new Invalid('report.device.report_map_hex: must be an even number of hex digits');
    }
    if ('parse' in d) {
      const p = 'report.device.parse';
      const o = object(
        d.parse,
        p,
        ['result', 'fields', 'boot_fallback', 'primary_page', 'primary_usage', 'has_pointer'],
        [],
      );
      opt(o, 'result', p, (x, q) => {
        int(x, q, Number.MIN_SAFE_INTEGER);
      });
      opt(o, 'fields', p, (x, q) => {
        int(x, q, 0);
      });
      opt(o, 'boot_fallback', p, bool);
      opt(o, 'primary_page', p, (x, q) => {
        int(x, q, 0, 65535);
      });
      opt(o, 'primary_usage', p, (x, q) => {
        int(x, q, 0, 65535);
      });
      opt(o, 'has_pointer', p, bool);
    }
    opt(d, 'security', 'report.device', (v, p) => {
      if (v !== 'green' && v !== 'yellow' && v !== 'red' && v !== 'unknown') {
        throw new Invalid(`${p}: must be green, yellow, red or unknown`);
      }
    });
    if ('connection' in d) {
      const p = 'report.device.connection';
      const o = object(d.connection, p, ['interval_ms', 'latency', 'timeout_ms'], []);
      opt(o, 'interval_ms', p, (x, q) => {
        if (typeof x !== 'number' || !Number.isFinite(x) || x < 0)
          throw new Invalid(`${q}: must be a number >= 0`);
      });
      opt(o, 'latency', p, (x, q) => {
        int(x, q, 0);
      });
      opt(o, 'timeout_ms', p, (x, q) => {
        int(x, q, 0);
      });
    }
    opt(d, 'battery_reported', 'report.device', bool);
    if ('latency_us' in d) {
      const p = 'report.device.latency_us';
      const o = object(d.latency_us, p, ['count', 'avg', 'p95', 'max'], []);
      for (const k of ['count', 'avg', 'p95', 'max'])
        opt(o, k, p, (x, q) => {
          int(x, q, 0);
        });
    }
    optNull(r, 'notes', 'report', (v, p) => {
      str(v, p, 500);
    });
    return { ok: true, report: input as Report };
  } catch (e) {
    if (e instanceof Invalid) return { ok: false, error: e.message };
    throw e;
  }
}

/** Boolean check for {@link opt}. */
function bool(v: unknown, path: string): void {
  if (typeof v !== 'boolean') throw new Invalid(`${path}: must be a boolean`);
}
