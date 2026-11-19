import { describe, expect, it } from 'vitest';
import { Touchpad } from '../../src/lib/touchpad';
import type { Point } from '../../src/lib/touchpad';

const p = (id: number, x: number, y: number): Point => ({ id, x, y });

describe('Touchpad', () => {
  it('a short tap with one finger is a left click and sends no movement', () => {
    const pad = new Touchpad();
    pad.start([p(1, 100, 100)], 0);
    expect(pad.move([p(1, 102, 101)], 10)).toEqual([]);
    expect(pad.end([], 120)).toEqual([{ kind: 'click', button: 'left' }]);
  });

  it('a long press is not a click', () => {
    const pad = new Touchpad();
    pad.start([p(1, 0, 0)], 0);
    expect(pad.end([], 600)).toEqual([]);
  });

  it('a slow drag moves with the base gain and is not a click afterwards', () => {
    const pad = new Touchpad({ gain: 2, accel: 0 });
    pad.start([p(1, 0, 0)], 0);
    expect(pad.move([p(1, 20, 0)], 100)).toEqual([{ kind: 'move', dx: 40, dy: 0 }]);
    expect(pad.end([], 200)).toEqual([]);
  });

  it('a fast finger moves the pointer further than a slow one over the same distance', () => {
    const slow = new Touchpad({ gain: 2, accel: 2, maxBoost: 6 });
    const fast = new Touchpad({ gain: 2, accel: 2, maxBoost: 6 });
    slow.start([p(1, 0, 0)], 0);
    fast.start([p(1, 0, 0)], 0);
    const s = slow.move([p(1, 30, 0)], 300); // 0.1 px/ms
    const f = fast.move([p(1, 30, 0)], 15); // 2 px/ms
    expect(s[0]).toMatchObject({ kind: 'move' });
    expect((f[0] as { dx: number }).dx).toBeGreaterThan((s[0] as { dx: number }).dx * 2);
  });

  it('the speed boost is capped', () => {
    const pad = new Touchpad({ gain: 1, accel: 100, maxBoost: 3 });
    pad.start([p(1, 0, 0)], 0);
    expect(pad.move([p(1, 100, 0)], 1)).toEqual([{ kind: 'move', dx: 300, dy: 0 }]);
  });

  it('keeps the fractional part so slow movement is not lost', () => {
    const pad = new Touchpad({ gain: 0.5, accel: 0, tapSlop: 0 });
    pad.start([p(1, 0, 0)], 0);
    expect(pad.move([p(1, 1, 0)], 100)).toEqual([]); // 0.5
    expect(pad.move([p(1, 2, 0)], 200)).toEqual([{ kind: 'move', dx: 1, dy: 0 }]); // 1.0 in total
  });

  it('two fingers dragging scroll (content follows the fingers by default)', () => {
    const pad = new Touchpad({ scrollGain: 1 });
    pad.start([p(1, 0, 0)], 0);
    pad.start([p(1, 0, 0), p(2, 50, 0)], 5);
    const down = pad.move([p(1, 0, 40), p(2, 50, 40)], 1000);
    expect(down).toHaveLength(1);
    expect(down[0]).toMatchObject({ kind: 'scroll', dx: 0 });
    expect((down[0] as { dy: number }).dy).toBeGreaterThan(0);
    expect(pad.end([p(2, 50, 40)], 1090)).toEqual([]);
    expect(pad.end([], 1095)).toEqual([]);
  });

  it('traditional scroll direction can be chosen', () => {
    const pad = new Touchpad({ scrollGain: 1, naturalScroll: false });
    pad.start([p(1, 0, 0), p(2, 50, 0)], 0);
    const a = pad.move([p(1, 0, 30), p(2, 50, 30)], 1000);
    expect((a[0] as { dy: number }).dy).toBeLessThan(0);
  });

  it('a short two-finger tap is a right click', () => {
    const pad = new Touchpad();
    pad.start([p(1, 0, 0)], 0);
    pad.start([p(1, 0, 0), p(2, 40, 0)], 10);
    expect(pad.end([p(2, 40, 0)], 100)).toEqual([]);
    expect(pad.end([], 110)).toEqual([{ kind: 'click', button: 'right' }]);
  });

  it('a touch right after a tap holds the left button until the finger is lifted (drag)', () => {
    const pad = new Touchpad({ gain: 1, accel: 0 });
    pad.start([p(1, 0, 0)], 0);
    expect(pad.end([], 80)).toEqual([{ kind: 'click', button: 'left' }]);
    expect(pad.start([p(2, 5, 5)], 200)).toEqual([{ kind: 'button', down: true }]);
    expect(pad.move([p(2, 25, 5)], 300)).toEqual([{ kind: 'move', dx: 20, dy: 0 }]);
    expect(pad.end([], 400)).toEqual([{ kind: 'button', down: false }]);
  });

  it('a second quick tap is a double click: press and release without a separate click', () => {
    const pad = new Touchpad();
    pad.start([p(1, 0, 0)], 0);
    pad.end([], 60);
    expect(pad.start([p(2, 0, 0)], 150)).toEqual([{ kind: 'button', down: true }]);
    expect(pad.end([], 210)).toEqual([{ kind: 'button', down: false }]);
  });

  it('a touch long after a tap is a normal touch again', () => {
    const pad = new Touchpad({ dragMs: 300 });
    pad.start([p(1, 0, 0)], 0);
    pad.end([], 60);
    expect(pad.start([p(2, 0, 0)], 900)).toEqual([]);
    expect(pad.end([], 960)).toEqual([{ kind: 'click', button: 'left' }]);
  });

  it('starts fresh after all fingers left', () => {
    const pad = new Touchpad({ gain: 1 });
    pad.start([p(1, 0, 0)], 0);
    pad.move([p(1, 50, 0)], 50);
    pad.end([], 100);
    pad.start([p(2, 10, 10)], 1000);
    expect(pad.end([], 1100)).toEqual([{ kind: 'click', button: 'left' }]);
  });
});
