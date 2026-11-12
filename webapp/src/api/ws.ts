/**
 * @file ws.ts
 * @description WebSocket client for `/api/v1/ws`: JSON events, a `hello` heartbeat that keeps the dongle's
 * Wi-Fi AP alive while the app is open (CLAUDE.md §3.5), and automatic reconnection.
 */
import type { WsMessage } from './types';

/** The subset of WebSocket behaviour the client needs; lets tests and the dev mock stand in for a socket. */
export interface SocketLike {
  /** Called when the connection opens. */
  onopen: (() => void) | null;
  /** Called for each text message. */
  onmessage: ((data: string) => void) | null;
  /** Called when the connection closes or fails. */
  onclose: (() => void) | null;
  /** Send a text message. */
  send(data: string): void;
  /** Close the connection. */
  close(): void;
}

/** Creates a socket for a URL. */
export type SocketFactory = (url: string) => SocketLike;

/** Heartbeat period; the firmware treats a heartbeat older than 15 s as "web app closed". */
export const HEARTBEAT_MS = 5000;
/** Delay before reconnecting after a close. */
export const RECONNECT_MS = 2000;

/**
 * Wrap the browser WebSocket in the {@link SocketLike} interface.
 *
 * @param url WebSocket URL.
 * @returns A socket adapter.
 */
export const browserSocketFactory: SocketFactory = (url) => {
  const ws = new WebSocket(url);
  const sock: SocketLike = {
    onopen: null,
    onmessage: null,
    onclose: null,
    send: (d) => {
      ws.send(d);
    },
    close: () => {
      ws.close();
    },
  };
  ws.onopen = () => sock.onopen?.();
  ws.onmessage = (e: MessageEvent<unknown>) => {
    if (typeof e.data === 'string') sock.onmessage?.(e.data);
  };
  ws.onclose = () => sock.onclose?.();
  return sock;
};

/**
 * Parse a server message.
 *
 * @param text Raw text frame.
 * @returns The message, or null if it is not a valid `{event, data}` object.
 */
export function parseMessage(text: string): WsMessage | null {
  try {
    const v: unknown = JSON.parse(text);
    if (typeof v === 'object' && v !== null && 'event' in v && typeof v.event === 'string') {
      return { event: v.event, data: 'data' in v ? v.data : null };
    }
  } catch {
    /* Ignore malformed frames: the server never sends them, a proxy might. */
  }
  return null;
}

/** Self-reconnecting WebSocket client. */
export class HubbleSocket {
  private sock: SocketLike | null = null;
  private heartbeat: ReturnType<typeof setInterval> | null = null;
  private retry: ReturnType<typeof setTimeout> | null = null;
  private stopped = true;

  /**
   * @param url WebSocket URL.
   * @param factory Socket factory (browser or mock).
   * @param onMessage Called for every valid server message.
   * @param onState Called with true when connected and false when disconnected.
   */
  constructor(
    private readonly url: string,
    private readonly factory: SocketFactory,
    private readonly onMessage: (m: WsMessage) => void,
    private readonly onState: (connected: boolean) => void = () => undefined,
  ) {}

  /** Open the connection (and keep it open until {@link stop}). */
  start(): void {
    this.stopped = false;
    this.connect();
  }

  /** Close the connection and stop reconnecting. */
  stop(): void {
    this.stopped = true;
    this.cleanup();
    this.sock?.close();
    this.sock = null;
  }

  private connect(): void {
    const s = this.factory(this.url);
    this.sock = s;
    s.onopen = () => {
      this.onState(true);
      this.sendHello();
      this.heartbeat = setInterval(() => {
        this.sendHello();
      }, HEARTBEAT_MS);
    };
    s.onmessage = (text) => {
      const m = parseMessage(text);
      if (m) this.onMessage(m);
    };
    s.onclose = () => {
      this.cleanup();
      this.onState(false);
      if (!this.stopped) {
        this.retry = setTimeout(() => {
          this.connect();
        }, RECONNECT_MS);
      }
    };
  }

  /**
   * Send a client event (e.g. `input.subscribe`) if the socket is open.
   *
   * @param event Event name.
   * @returns True if the frame was handed to the socket.
   */
  sendEvent(event: string): boolean {
    if (!this.sock) return false;
    this.sock.send(JSON.stringify({ event }));
    return true;
  }

  private sendHello(): void {
    this.sock?.send(JSON.stringify({ event: 'hello' }));
  }

  private cleanup(): void {
    if (this.heartbeat) clearInterval(this.heartbeat);
    if (this.retry) clearTimeout(this.retry);
    this.heartbeat = null;
    this.retry = null;
  }
}
