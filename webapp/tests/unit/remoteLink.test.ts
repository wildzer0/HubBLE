import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { BTN, RemoteLink } from '../../src/lib/remoteLink';

type Msg = Record<string, unknown>;

describe('RemoteLink', () => {
  let sent: Msg[];
  let link: RemoteLink;
  const last = () => sent[sent.length - 1];
  beforeEach(() => {
    vi.useFakeTimers();
    sent = [];
    link = new RemoteLink((m) => {
      sent.push(m);
      return true;
    });
  });
  afterEach(() => {
    link.dispose();
    vi.useRealTimers();
  });

  it('coalesces moves into one message per frame', () => {
    link.move(3, 1);
    link.move(2, 2);
    link.scroll(0, 4);
    expect(sent).toEqual([]);
    vi.advanceTimersByTime(20);
    expect(sent).toEqual([
      { event: 'remote.move', dx: 5, dy: 3 },
      { event: 'remote.scroll', dx: 0, dy: 4 },
    ]);
  });

  it('a click presses and releases the button', () => {
    link.click(BTN.left);
    expect(sent).toEqual([{ event: 'remote.buttons', b: 1 }]);
    vi.advanceTimersByTime(50);
    expect(last()).toEqual({ event: 'remote.buttons', b: 0 });
  });

  it('two quick clicks both complete', () => {
    link.click(BTN.left);
    vi.advanceTimersByTime(10);
    link.click(BTN.left);
    vi.advanceTimersByTime(100);
    expect(sent.filter((m) => m.b === 1).length).toBeGreaterThanOrEqual(2);
    expect(last()).toEqual({ event: 'remote.buttons', b: 0 });
  });

  it('a held button is repeated until released, and owners combine', () => {
    link.hold('pad', BTN.left);
    link.hold('screen', BTN.right);
    expect(last()).toEqual({ event: 'remote.buttons', b: 3 });
    sent.length = 0;
    vi.advanceTimersByTime(1100);
    expect(sent.filter((m) => m.b === 3).length).toBe(2);
    link.release('pad');
    expect(last()).toEqual({ event: 'remote.buttons', b: 2 });
    link.release('screen');
    sent.length = 0;
    vi.advanceTimersByTime(2000);
    expect(sent).toEqual([]); // nothing is held: no repeat
  });

  it('a key is held at least the minimum time and repeated while down', () => {
    link.keyDown(0x2a, 0);
    link.keyUp();
    expect(sent).toEqual([{ event: 'remote.key', m: 0, k: [0x2a] }]);
    vi.advanceTimersByTime(40);
    expect(last()).toEqual({ event: 'remote.key', m: 0, k: [] });
  });

  it('a key stays down while held and its repeat keeps it alive', () => {
    link.keyDown(0x50, 0x04);
    sent.length = 0;
    vi.advanceTimersByTime(1100);
    expect(sent.filter((m) => (m.k as number[]).length === 1).length).toBe(2);
    link.keyUp();
    vi.advanceTimersByTime(40);
    expect(last()).toEqual({ event: 'remote.key', m: 0, k: [] });
  });

  it('a modifier alone is sent with no key', () => {
    link.tapKey(0, 0x08);
    expect(sent[0]).toEqual({ event: 'remote.key', m: 0x08, k: [] });
  });

  it('long text is split into frames that fit, with the modifiers', () => {
    link.type('a'.repeat(95), 0x01);
    expect(sent.map((m) => (m.t as string).length)).toEqual([40, 40, 15]);
    expect(sent.every((m) => m.m === 1)).toBe(true);
  });

  it('dispose releases everything that is held', () => {
    link.hold('pad', BTN.left);
    link.keyDown(0x04, 0);
    sent.length = 0;
    link.dispose();
    expect(sent).toContainEqual({ event: 'remote.buttons', b: 0 });
    expect(sent).toContainEqual({ event: 'remote.key', m: 0, k: [] });
  });
});
