/**
 * @file stats.ts
 * @description Per-report summaries (stored as KV key metadata) and the public aggregate statistics computed from
 * them. Nothing here touches the raw report map: the summaries are tiny and carry no free text.
 */
import type { Report } from './validate';

/** Report kinds, as in the schema. */
export type Kind = 'keyboard' | 'pointer' | 'unknown';

/**
 * Compact description of one report, kept as KV metadata (limit 1 KB). Short names keep it small.
 * Property meanings: k device key, l display label, c kind, f firmware version, p parse result (absent = not
 * reported), b boot fallback used (absent = not reported), m latency [avg, p95, max] in us (absent = no samples).
 */
export interface Summary {
  k: string;
  l: string;
  c: Kind;
  f: string;
  p?: number;
  b?: 0 | 1;
  m?: [number, number, number];
}

/** Hex of a 16-bit value, 4 digits. */
const hex4 = (n: number): string => n.toString(16).padStart(4, '0');

/**
 * Stable key of a device model: the PnP vendor/product when the device reports them, else its advertised name and
 * model string (lower-cased), else "unknown".
 *
 * @param r A validated report.
 * @returns The device key.
 */
export function deviceKey(r: Report): string {
  const pnp = r.device.pnp;
  if (pnp && pnp.vendor_id !== undefined && pnp.product_id !== undefined) {
    return `pnp:${hex4(pnp.vendor_id)}:${hex4(pnp.product_id)}`;
  }
  const name = (r.device.adv_name ?? '').trim().toLowerCase();
  const model = (r.device.model ?? '').trim().toLowerCase();
  return name === '' && model === '' ? 'unknown' : `name:${name}|${model}`;
}

/**
 * Human-readable label of a device: "Manufacturer Model", else the model, else the advertised name, else the key.
 *
 * @param r A validated report.
 * @param key Its device key.
 * @returns The label (at most 64 characters).
 */
export function deviceLabel(r: Report, key: string): string {
  const parts = [r.device.manufacturer, r.device.model]
    .map((s) => (s ?? '').trim())
    .filter((s) => s !== '');
  const label = parts.length > 0 ? parts.join(' ') : (r.device.adv_name ?? '').trim() || key;
  return label.slice(0, 64);
}

/**
 * Summarise a report for the statistics.
 *
 * @param r A validated report.
 * @returns The summary.
 */
export function summarize(r: Report): Summary {
  const key = deviceKey(r);
  const s: Summary = { k: key, l: deviceLabel(r, key), c: r.device.kind, f: r.firmware.version };
  const parse = r.device.parse;
  if (parse?.result !== undefined) s.p = parse.result;
  if (parse?.boot_fallback !== undefined) s.b = parse.boot_fallback ? 1 : 0;
  const lat = r.device.latency_us;
  if (
    lat &&
    (lat.count ?? 0) > 0 &&
    lat.avg !== undefined &&
    lat.p95 !== undefined &&
    lat.max !== undefined
  ) {
    s.m = [lat.avg, lat.p95, lat.max];
  }
  return s;
}

/** Median of a list (mean of the two middle values for an even count, rounded); null for an empty list. */
export function median(values: number[]): number | null {
  if (values.length === 0) return null;
  const v = [...values].sort((a, b) => a - b);
  const mid = v.length >> 1;
  return v.length % 2 === 1
    ? (v[mid] as number)
    : Math.round(((v[mid - 1] as number) + (v[mid] as number)) / 2);
}

/** 95th percentile by the nearest-rank method; null for an empty list. */
export function p95(values: number[]): number | null {
  if (values.length === 0) return null;
  const v = [...values].sort((a, b) => a - b);
  return v[Math.max(0, Math.ceil(0.95 * v.length) - 1)] as number;
}

/** Fraction rounded to 3 decimals; null when there is nothing to divide by. */
function share(n: number, of: number): number | null {
  return of === 0 ? null : Math.round((n / of) * 1000) / 1000;
}

/** Median and 95th percentile of one latency figure across the reports of a device. */
export interface Spread {
  median: number | null;
  p95: number | null;
}

/** Public statistics of one device model. */
export interface DeviceStats {
  key: string;
  label: string;
  reports: number;
  kinds: Record<Kind, number>;
  firmware_versions: Record<string, number>;
  /** Share of reports (that say) whose report map parsed without error. */
  parse_ok_share: number | null;
  /** Share of reports (that say) where the dongle had to fall back to the boot keyboard. */
  boot_fallback_share: number | null;
  /** Latency the dongle added, in microseconds; null when no report had samples. */
  latency_us: { reports: number; avg: Spread; p95: Spread; max: Spread } | null;
  /** Parse results other than 0, with how many reports had each. */
  odd: { result: number; reports: number }[];
}

/** The public statistics document (`GET /v1/stats`). */
export interface Stats {
  generated_at: string;
  total: number;
  by_kind: Record<Kind, number>;
  by_firmware: Record<string, number>;
  parse_error_share: number | null;
  boot_fallback_share: number | null;
  devices: DeviceStats[];
  /** Keys of the devices that have at least one report with a parse error: the cases worth looking at. */
  odd_devices: string[];
}

/** Count occurrences of each value. */
function tally<T extends string | number>(items: T[]): Record<string, number> {
  const out: Record<string, number> = {};
  for (const i of items) out[String(i)] = (out[String(i)] ?? 0) + 1;
  return out;
}

/** Counts per kind with all three kinds present. */
function kinds(items: Summary[]): Record<Kind, number> {
  const t = tally(items.map((s) => s.c));
  return { keyboard: t.keyboard ?? 0, pointer: t.pointer ?? 0, unknown: t.unknown ?? 0 };
}

/** Spread of the latency figure at @p idx (0 avg, 1 p95, 2 max). */
function spread(items: Summary[], idx: 0 | 1 | 2): Spread {
  const v = items.flatMap((s) => (s.m ? [s.m[idx]] : []));
  return { median: median(v), p95: p95(v) };
}

/**
 * Aggregate the summaries into the public statistics.
 *
 * @param all One summary per stored report.
 * @param now Time of the computation.
 * @returns The statistics document.
 */
export function computeStats(all: Summary[], now: Date): Stats {
  const byKey = new Map<string, Summary[]>();
  for (const s of all) byKey.set(s.k, [...(byKey.get(s.k) ?? []), s]);

  const parsed = all.filter((s) => s.p !== undefined);
  const fb = all.filter((s) => s.b !== undefined);
  const devices: DeviceStats[] = [...byKey.entries()].map(([key, items]) => {
    const withParse = items.filter((s) => s.p !== undefined);
    const withFb = items.filter((s) => s.b !== undefined);
    const withLat = items.filter((s) => s.m !== undefined);
    const odd = Object.entries(tally(withParse.filter((s) => s.p !== 0).map((s) => s.p as number)))
      .map(([result, reports]) => ({ result: Number(result), reports }))
      .sort((a, b) => a.result - b.result);
    return {
      key,
      label: (items[0] as Summary).l,
      reports: items.length,
      kinds: kinds(items),
      firmware_versions: tally(items.map((s) => s.f)),
      parse_ok_share: share(withParse.filter((s) => s.p === 0).length, withParse.length),
      boot_fallback_share: share(withFb.filter((s) => s.b === 1).length, withFb.length),
      latency_us:
        withLat.length === 0
          ? null
          : {
              reports: withLat.length,
              avg: spread(withLat, 0),
              p95: spread(withLat, 1),
              max: spread(withLat, 2),
            },
      odd,
    };
  });
  devices.sort((a, b) => b.reports - a.reports || a.key.localeCompare(b.key));

  return {
    generated_at: now.toISOString(),
    total: all.length,
    by_kind: kinds(all),
    by_firmware: tally(all.map((s) => s.f)),
    parse_error_share: share(parsed.filter((s) => s.p !== 0).length, parsed.length),
    boot_fallback_share: share(fb.filter((s) => s.b === 1).length, fb.length),
    devices,
    odd_devices: devices.filter((d) => d.odd.length > 0).map((d) => d.key),
  };
}
