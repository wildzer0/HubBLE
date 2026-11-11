import { describe, expect, it } from 'vitest';
import { en } from '../../src/i18n/en';
import { it as itTable } from '../../src/i18n/it';
import { pickLocale, translate } from '../../src/i18n';

describe('i18n', () => {
  it('has the same keys in English and Italian', () => {
    expect(Object.keys(itTable).sort()).toEqual(Object.keys(en).sort());
  });
  it('picks Italian for it-* tags and English otherwise', () => {
    expect(pickLocale('it-IT')).toBe('it');
    expect(pickLocale('de')).toBe('en');
    expect(pickLocale(undefined)).toBe('en');
  });
  it('translates', () => {
    expect(translate('it', 'status.heading')).toBe('Stato del dongle');
  });
});
