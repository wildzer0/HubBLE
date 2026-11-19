import { describe, expect, it } from 'vitest';
import { Touchpad } from '../../src/lib/touchpad';
import type { Point } from '../../src/lib/touchpad';

const p = (id: number, x: number, y: number): Point => ({ id, x, y });

describe('Touchpad', () => {
  it('a short tap with one finger is a left click and sends no movement', () => {
    const pad = new Touchpad();
    pad.start([p(1, 100, 100)], 0);
    expect(pad.move([p(1, 102, 101)])).toEqual([]);
    expect(pad.end([], 120)).toEqual([{ kind: 'click', button: 'left' }]);
  });

  it('a long press is not a click', () => {
    const pad = new Touchpad();
    pad.start([p(1, 0, 0)], 0);
    expect(pad.end([], 600)).toEqual([]);
  });

  it('one finger dragging moves the pointer with the gain, and is not a click afterwards', () => {
    const pad = new Touchpad({ gain: 2 });
    pad.start([p(1, 0, 0)], 0);
    const a = pad.move([p(1, 20, 0)]);
    expect(a).toEqual([{ kind: 'move', dx: 40, dy: 0 }]);
    expect(pad.end([], 100)).toEqual([]);
  });

  it('keeps the fractional part so slow movement is not lost', () => {
    const pad = new Touchpad({ gain: 0.5, tapSlop: 0 });
    pad.start([p(1, 0, 0)], 0);
    expect(pad.move([p(1, 1, 0)])).toEqual([]); // 0.5
    expect(pad.move([p(1, 2, 0)])).toEqual([{ kind: 'move', dx: 1, dy: 0 }]); // 1.0 in total
  });

  it('two fingers dragging scroll (content follows the fingers by default)', () => {
    const pad = new Touchpad({ scrollGain: 1 });
    pad.start([p(1, 0, 0)], 0);
    pad.start([p(1, 0, 0), p(2, 50, 0)], 5);
    const down = pad.move([p(1, 0, 40), p(2, 50, 40)]);
    expect(down).toEqual([{ kind: 'scroll', dx: 0, dy: 40 }]);
    expect(pad.end([p(2, 50, 40)], 90)).toEqual([]);
    expect(pad.end([], 95)).toEqual([]);
  });

  it('traditional scroll direction can be chosen', () => {
    const pad = new Touchpad({ scrollGain: 1, naturalScroll: false });
    pad.start([p(1, 0, 0), p(2, 50, 0)], 0);
    expect(pad.move([p(1, 0, 30), p(2, 50, 30)])).toEqual([{ kind: 'scroll', dx: 0, dy: -30 }]);
  });

  it('a short two-finger tap is a right click', () => {
    const pad = new Touchpad();
    pad.start([p(1, 0, 0)], 0);
    pad.start([p(1, 0, 0), p(2, 40, 0)], 10);
    expect(pad.end([p(2, 40, 0)], 100)).toEqual([]);
    expect(pad.end([], 110)).toEqual([{ kind: 'click', button: 'right' }]);
  });

  it('starts fresh after all fingers left', () => {
    const pad = new Touchpad({ gain: 1 });
    pad.start([p(1, 0, 0)], 0);
    pad.move([p(1, 50, 0)]);
    pad.end([], 100);
    pad.start([p(2, 10, 10)], 1000);
    expect(pad.end([], 1100)).toEqual([{ kind: 'click', button: 'left' }]);
  });
});
