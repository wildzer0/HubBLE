/**
 * @file live.ts
 * @description Shared live connection: one WebSocket for the whole app, with subscribe/unsubscribe for
 * pages that want events (logs, OTA progress).
 */
import type { WsMessage } from './types';
import { browserSocketFactory, HubbleSocket } from './ws';
import type { SocketFactory } from './ws';

type Listener = (m: WsMessage) => void;
type StateListener = (connected: boolean) => void;

const listeners = new Set<Listener>();
const stateListeners = new Set<StateListener>();
let socket: HubbleSocket | null = null;
let connected = false;

/**
 * Start the shared connection (idempotent).
 *
 * @param factory Socket factory; defaults to the browser WebSocket. The dev server passes a mock.
 */
export function startLive(factory: SocketFactory = browserSocketFactory): void {
  if (socket) return;
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new HubbleSocket(
    `${proto}://${location.host}/api/v1/ws`,
    factory,
    (m) => {
      listeners.forEach((l) => {
        l(m);
      });
    },
    (c) => {
      connected = c;
      stateListeners.forEach((l) => {
        l(c);
      });
    },
  );
  socket.start();
}

/**
 * Subscribe to server events.
 *
 * @param l Listener.
 * @returns Function that removes the listener.
 */
export function subscribe(l: Listener): () => void {
  listeners.add(l);
  return () => {
    listeners.delete(l);
  };
}

/**
 * Subscribe to connection state changes; the listener is called immediately with the current state.
 *
 * @param l Listener.
 * @returns Function that removes the listener.
 */
export function subscribeState(l: StateListener): () => void {
  stateListeners.add(l);
  l(connected);
  return () => {
    stateListeners.delete(l);
  };
}
