import { describe, expect, it } from 'vitest';
import { computeStats, deviceKey, median, p95, summarize } from '../src/stats';
import { sample } from './samples';

describe('statistics maths', () => {
  it('median handles odd, even and empty lists', () => {
    expect(median([5, 1, 3])).toBe(3);
    expect(median([4, 1, 3, 2])).toBe(3); // (2 + 3) / 2 = 2.5 rounds to 3
    expect(median([10, 20])).toBe(15);
    expect(median([])).toBeNull();
  });

  it('p95 is the nearest rank', () => {
    expect(p95([1])).toBe(1);
    expect(p95(Array.from({ length: 20 }, (_, i) => i + 1))).toBe(19);
    expect(p95(Array.from({ length: 100 }, (_, i) => i + 1))).toBe(95);
    expect(p95([])).toBeNull();
  });

  it('device keys prefer the PnP id, then name and model', () => {
    expect(deviceKey(sample())).toBe('pnp:05ac:820a');
    expect(deviceKey(sample({ pnp: null, adv_name: ' OBLIO ', model: 'B68' }))).toBe(
      'name:oblio|b68',
    );
    expect(deviceKey(sample({ pnp: null, adv_name: null, model: null }))).toBe('unknown');
  });

  it('summaries drop the report map and the notes', () => {
    const s = summarize(sample());
    expect(JSON.stringify(s)).not.toContain('05010906');
    expect(s).toEqual({
      k: 'pnp:05ac:820a',
      l: 'Yunzii B68',
      c: 'keyboard',
      f: '1.1.7',
      p: 0,
      b: 0,
      m: [800, 1500, 1900],
    });
    expect(JSON.stringify(s).length).toBeLessThan(1024);
  });

  it('aggregates per device with medians and parse errors', () => {
    const lat = (avg: number, p: number, max: number) => ({ count: 10, avg, p95: p, max });
    const reports = [
      sample({ latency_us: lat(600, 1000, 1500), report_map_hex: '01' }),
      sample({ latency_us: lat(800, 1200, 1700), report_map_hex: '02' }),
      sample({
        latency_us: lat(1000, 2000, 3000),
        report_map_hex: '03',
        parse: { result: 3, boot_fallback: true },
      }),
      sample({
        pnp: null,
        adv_name: 'EM06',
        model: null,
        manufacturer: null,
        kind: 'pointer',
        latency_us: { count: 0 },
        report_map_hex: '04',
      }),
    ];
    const stats = computeStats(reports.map(summarize), new Date('2026-11-12T12:00:00Z'));
    expect(stats.total).toBe(4);
    expect(stats.by_kind).toEqual({ keyboard: 3, pointer: 1, unknown: 0 });
    expect(stats.by_firmware).toEqual({ '1.1.7': 4 });
    expect(stats.generated_at).toBe('2026-11-12T12:00:00.000Z');
    // Parse info exists for all four; one of them has an error.
    expect(stats.parse_error_share).toBe(0.25);
    const b68 = stats.devices[0];
    expect(b68?.key).toBe('pnp:05ac:820a');
    expect(b68?.reports).toBe(3);
    expect(b68?.parse_ok_share).toBeCloseTo(0.667, 3);
    expect(b68?.boot_fallback_share).toBeCloseTo(0.333, 3);
    expect(b68?.latency_us?.avg).toEqual({ median: 800, p95: 1000 });
    expect(b68?.latency_us?.p95.median).toBe(1200);
    expect(b68?.latency_us?.max.median).toBe(1700);
    expect(b68?.odd).toEqual([{ result: 3, reports: 1 }]);
    const em06 = stats.devices[1];
    expect(em06?.latency_us).toBeNull(); // no samples
    expect(stats.odd_devices).toEqual(['pnp:05ac:820a']);
  });

  it('is well-formed with no reports', () => {
    const s = computeStats([], new Date(0));
    expect(s.total).toBe(0);
    expect(s.parse_error_share).toBeNull();
    expect(s.devices).toEqual([]);
  });
});
