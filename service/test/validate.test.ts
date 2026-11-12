import { describe, expect, it } from 'vitest';
import { validateReport } from '../src/validate';
import { sample } from './samples';

describe('validateReport', () => {
  it('accepts a complete report', () => {
    expect(validateReport(sample()).ok).toBe(true);
  });

  it('accepts the smallest legal report and nulls for optional fields', () => {
    const minimal = {
      format: 'hubble-device-report',
      version: 1,
      firmware: { version: '1.0.0', board: 's3_zero' },
      device: { kind: 'unknown', report_map_hex: '' },
    };
    expect(validateReport(minimal).ok).toBe(true);
    const nulls = sample(
      { adv_name: null, appearance: null, manufacturer: null, model: null, pnp: null },
      { notes: null },
    );
    expect(validateReport(nulls).ok).toBe(true);
  });

  const bad: [string, unknown, RegExp][] = [
    ['not an object', 'text', /report: must be an object/],
    ['an array', [], /report: must be an object/],
    ['wrong format', { ...sample(), format: 'other' }, /report\.format/],
    ['wrong version', { ...sample(), version: 2 }, /report\.version/],
    ['version as a string', { ...sample(), version: '1' }, /report\.version/],
    [
      'missing firmware',
      (({ firmware, ...r }) => (void firmware, r))(sample()),
      /report\.firmware: required/,
    ],
    ['extra top-level property', { ...sample(), extra: 1 }, /report\.extra: unknown property/],
    [
      'extra device property',
      sample({ mac: 'aa' } as never),
      /report\.device\.mac: unknown property/,
    ],
    [
      'extra nested property',
      sample({ latency_us: { count: 1, avg: 1, p95: 1, max: 1, min: 0 } as never }),
      /latency_us\.min/,
    ],
    ['bad kind', sample({ kind: 'gamepad' as never }), /report\.device\.kind/],
    ['non-string name', sample({ adv_name: 5 as never }), /adv_name: must be a string/],
    ['long name', sample({ adv_name: 'x'.repeat(33) }), /adv_name: longer than 32/],
    [
      'odd hex length',
      sample({ report_map_hex: '050' }),
      /report_map_hex: must be an even number of hex digits/,
    ],
    ['non-hex', sample({ report_map_hex: '05zz' }), /report_map_hex/],
    [
      'oversized hex',
      sample({ report_map_hex: '00'.repeat(1025) }),
      /report_map_hex: longer than 2048/,
    ],
    ['float appearance', sample({ appearance: 1.5 }), /appearance: must be an integer/],
    ['appearance out of range', sample({ appearance: 70000 }), /appearance: must be between/],
    ['bad pnp', sample({ pnp: { vendor_id: 70000 } }), /pnp\.vendor_id/],
    [
      'non-boolean',
      sample({ battery_reported: 'yes' as never }),
      /battery_reported: must be a boolean/,
    ],
    ['negative latency', sample({ latency_us: { count: -1 } }), /latency_us\.count/],
    ['bad security', sample({ security: 'blue' as never }), /security: must be/],
    ['negative interval', sample({ connection: { interval_ms: -1 } }), /interval_ms/],
    ['long notes', { ...sample(), notes: 'n'.repeat(501) }, /report\.notes: longer than 500/],
    ['notes not a string', { ...sample(), notes: 3 }, /report\.notes: must be a string/],
  ];
  it.each(bad)('rejects %s', (_name, input, message) => {
    const v = validateReport(input);
    expect(v.ok).toBe(false);
    if (!v.ok) expect(v.error).toMatch(message);
  });
});
