import { describe, expect, it } from 'vitest';
import { MOD, Modifiers } from '../../src/lib/modifiers';

describe('Modifiers', () => {
  it('a tap arms a modifier for one key, which uses it up', () => {
    const m = new Modifiers();
    m.tap(MOD.ctrl);
    expect(m.get(MOD.ctrl)).toBe('once');
    expect(m.mask()).toBe(MOD.ctrl);
    m.consume();
    expect(m.mask()).toBe(0);
  });

  it('a second tap locks it, a third releases it', () => {
    const m = new Modifiers();
    m.tap(MOD.shift);
    m.tap(MOD.shift);
    expect(m.get(MOD.shift)).toBe('locked');
    m.consume();
    expect(m.mask()).toBe(MOD.shift); // locked survives a key
    m.tap(MOD.shift);
    expect(m.get(MOD.shift)).toBe('off');
    expect(m.any()).toBe(false);
  });

  it('combines several modifiers', () => {
    const m = new Modifiers();
    m.tap(MOD.ctrl);
    m.tap(MOD.alt);
    expect(m.mask()).toBe(MOD.ctrl | MOD.alt);
    expect(m.any()).toBe(true);
  });
});
