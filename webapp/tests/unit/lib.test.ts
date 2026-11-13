import { describe, expect, it } from 'vitest';
import { parseBackup } from '../../src/lib/backup';
import { describeInput, keyName } from '../../src/lib/inputFormat';
import { qrSvg, wifiEscape, wifiPayload } from '../../src/lib/wifiqr';

describe('wifi QR', () => {
  it('escapes the special characters of the WIFI: format', () => {
    expect(wifiEscape('a;b,c:d"e\\f')).toBe('a\\;b\\,c\\:d\\"e\\\\f');
    expect(wifiPayload('HubBLE-A1B2', 'p;w')).toBe('WIFI:T:WPA;S:HubBLE-A1B2;P:p\\;w;;');
  });
  it('renders a square SVG with a quiet zone', () => {
    const svg = qrSvg('http://hubble.local');
    expect(svg).toMatch(/^<svg /);
    const vb = /viewBox="0 0 (\d+) (\d+)"/.exec(svg);
    expect(vb?.[1]).toBe(vb?.[2]);
    expect(Number(vb?.[1])).toBeGreaterThan(8); // at least version 1 (21) is not expected here, +8 quiet
    expect(svg).toContain('<path d="M');
  });
});

describe('backup parsing', () => {
  const good = {
    format: 'hubble-backup',
    version: 1,
    settings: { require_confirmation: true, ap_boot_window_s: 300, ap_idle_timeout_s: 180 },
  };
  it('accepts a valid backup', () => {
    // An old backup without the update channel gets the defaults.
    expect(parseBackup(JSON.stringify(good))?.settings).toEqual({
      ...good.settings,
      update_channel: 'stable',
      sta_configured: false,
      remote_wakeup: true,
      portal_auto: true,
      led_brightness: 25,
      status_led: false,
      shift_hold_ms: 3000,
      filter_enabled: false,
      filter_bounce_ms: 50,
      filter_hold_ms: 0,
    });
    const pre = { ...good, settings: { ...good.settings, update_channel: 'prerelease' } };
    expect(parseBackup(JSON.stringify(pre))?.settings.update_channel).toBe('prerelease');
  });
  it('rejects garbage, wrong markers and missing fields', () => {
    expect(parseBackup('not json')).toBeNull();
    expect(parseBackup('{}')).toBeNull();
    expect(parseBackup(JSON.stringify({ ...good, version: 2 }))).toBeNull();
    expect(
      parseBackup(JSON.stringify({ ...good, settings: { require_confirmation: 1 } })),
    ).toBeNull();
  });
});

describe('input description', () => {
  it('names keys', () => {
    expect([0x04, 0x1e, 0x27, 0x3a, 0x28, 0x99].map(keyName)).toEqual([
      'A',
      '1',
      '0',
      'F1',
      'Enter',
      '0x99',
    ]);
  });
  it('describes keys with modifiers and releases', () => {
    expect(describeInput({ slot: 0, kind: 'key', modifiers: 0x22, keys: [4, 5] })).toBe(
      'LShift + RShift + A + B',
    );
    expect(describeInput({ slot: 0, kind: 'key' })).toBe('(released)');
  });
  it('describes mouse motion, wheel and buttons', () => {
    expect(describeInput({ slot: 1, kind: 'mouse', dx: 5, dy: -3, buttons: 1 })).toBe(
      'dx=5 dy=-3 [Left]',
    );
    expect(describeInput({ slot: 1, kind: 'mouse', wheel: -1, pan: 2, buttons: 6 })).toBe(
      'wheel=-1 pan=2 [Right, Middle]',
    );
    expect(describeInput({ slot: 1, kind: 'mouse' })).toBe('(idle)');
  });
  it('describes consumer usages', () => {
    expect(describeInput({ slot: 0, kind: 'consumer', usage: 0xe9 })).toBe('usage 0x00e9');
  });
});
