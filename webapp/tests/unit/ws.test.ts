import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { HEARTBEAT_MS, HubbleSocket, parseMessage, RECONNECT_MS } from '../../src/api/ws';
import type { SocketLike } from '../../src/api/ws';

/** A controllable fake socket. */
class FakeSocket implements SocketLike {
  onopen: (() => void) | null = null;
  onmessage: ((data: string) => void) | null = null;
  onclose: (() => void) | null = null;
  sent: string[] = [];
  send(d: string): void {
    this.sent.push(d);
  }
  close(): void {
    this.onclose?.();
  }
}

describe('parseMessage', () => {
  it('parses valid events and rejects junk', () => {
    expect(parseMessage('{"event":"log.line","data":{"text":"x"}}')).toEqual({
      event: 'log.line',
      data: { text: 'x' },
    });
    expect(parseMessage('{"event":"ap.timeout"}')).toEqual({ event: 'ap.timeout', data: null });
    expect(parseMessage('not json')).toBeNull();
    expect(parseMessage('{"data":1}')).toBeNull();
    expect(parseMessage('42')).toBeNull();
  });
});

describe('HubbleSocket', () => {
  let socks: FakeSocket[];
  const factory = () => {
    const s = new FakeSocket();
    socks.push(s);
    return s;
  };

  beforeEach(() => {
    vi.useFakeTimers();
    socks = [];
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it('sends hello on open and then every heartbeat period', () => {
    const states: boolean[] = [];
    const c = new HubbleSocket(
      'ws://x',
      factory,
      () => undefined,
      (s) => states.push(s),
    );
    c.start();
    socks[0]?.onopen?.();
    expect(socks[0]?.sent).toEqual(['{"event":"hello"}']);
    vi.advanceTimersByTime(HEARTBEAT_MS * 2);
    expect(socks[0]?.sent).toHaveLength(3);
    expect(states).toEqual([true]);
    c.stop();
  });

  it('delivers messages and reconnects after a close', () => {
    const got: string[] = [];
    const c = new HubbleSocket('ws://x', factory, (m) => got.push(m.event));
    c.start();
    socks[0]?.onopen?.();
    socks[0]?.onmessage?.('{"event":"system.status","data":{}}');
    expect(got).toEqual(['system.status']);
    socks[0]?.onclose?.();
    vi.advanceTimersByTime(RECONNECT_MS);
    expect(socks).toHaveLength(2);
    c.stop();
  });

  it('does not reconnect after stop()', () => {
    const c = new HubbleSocket('ws://x', factory, () => undefined);
    c.start();
    c.stop();
    vi.advanceTimersByTime(RECONNECT_MS * 3);
    expect(socks).toHaveLength(1);
  });
});
