import { describe, expect, it } from 'vitest';
import { en } from '../../src/i18n/en';
import { it as itTable } from '../../src/i18n/it';
import { pickLocale, translate } from '../../src/i18n';
import { parseRoute, routeParam } from '../../src/app';

describe('i18n', () => {
  it('has the same keys in English and Italian', () => {
    expect(Object.keys(itTable).sort()).toEqual(Object.keys(en).sort());
  });
  it('uses the same placeholders in both languages', () => {
    const ph = (s: string) => (s.match(/\{\w+\}/g) ?? []).sort();
    for (const k of Object.keys(en) as (keyof typeof en)[]) {
      expect(ph(itTable[k]), k).toEqual(ph(en[k]));
    }
  });
  it('picks Italian for it-* tags and English otherwise', () => {
    expect(pickLocale('it-IT')).toBe('it');
    expect(pickLocale('de')).toBe('en');
    expect(pickLocale(undefined)).toBe('en');
  });
  it('translates and substitutes placeholders', () => {
    expect(translate('it', 'status.heading')).toBe('Stato del dongle');
    expect(translate('en', 'status.ap_timeout', { s: 42 })).toBe('Switches off in 42 s');
    expect(translate('en', 'update.failed', {})).toContain('{message}');
  });
});

describe('routing', () => {
  it('maps hashes to routes and falls back to the dashboard', () => {
    expect(parseRoute('#/update')).toBe('update');
    expect(parseRoute('#system')).toBe('system');
    expect(parseRoute('#/nope')).toBe('dashboard');
    expect(parseRoute('')).toBe('dashboard');
    expect(parseRoute('#/device/aabbccddeeff')).toBe('device');
    expect(routeParam('#/device/aabbccddeeff')).toBe('aabbccddeeff');
    expect(routeParam('#/system')).toBe('');
  });
});
