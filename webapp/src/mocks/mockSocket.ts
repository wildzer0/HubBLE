/**
 * @file mockSocket.ts
 * @description Fake WebSocket for development without hardware: behaves like `/api/v1/ws` by pushing
 * `system.status` and `log.line` events every second. Loaded only in dev builds.
 */
import type { SocketFactory, SocketLike } from '../api/ws';

/** Push period of the fake events. */
const PUSH_MS = 1000;
/** Delay before the fake connection "opens". */
const OPEN_DELAY_MS = 30;

/** Mock socket factory (see {@link SocketFactory}). */
export const mockSocketFactory: SocketFactory = () => {
  let timer: ReturnType<typeof setInterval> | null = null;
  let uptime = 1234;
  const sock: SocketLike = {
    onopen: null,
    onmessage: null,
    onclose: null,
    send: () => undefined, // heartbeats need no reply
    close: () => {
      if (timer) clearInterval(timer);
      timer = null;
      setTimeout(() => sock.onclose?.(), 0);
    },
  };
  setTimeout(() => {
    sock.onopen?.();
    timer = setInterval(() => {
      uptime += 1;
      sock.onmessage?.(
        JSON.stringify({
          event: 'system.status',
          data: { uptime_s: uptime, heap_free: 142000, ap_timeout_s: 120 },
        }),
      );
      sock.onmessage?.(
        JSON.stringify({
          event: 'log.line',
          data: { text: `I (${String(uptime * 1000)}) mock: tick\n` },
        }),
      );
    }, PUSH_MS);
  }, OPEN_DELAY_MS);
  return sock;
};
