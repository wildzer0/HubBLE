/**
 * @file usbLink.ts
 * @description Talks to the dongle over USB with WebHID, so the web app works without any Wi-Fi. It mirrors the firmware
 * protocol (`firmware/components/usb_link/include/ulink_proto.h`): 64-byte HID reports carry a byte stream, the stream carries
 * frames, and the frames carry the same HTTP API and the same events the Wi-Fi connection has. Pure codec functions are
 * exported for tests; {@link UsbLink} wraps a WebHID device (or any {@link HidLike}).
 *
 * WebHID needs a secure context and a browser that has it (Chrome, Edge); the page is therefore served from the project's
 * HTTPS site, not from the dongle's own http address.
 */
import type { SocketLike } from './ws';

/** Vendor and usage the dongle's configuration interface is found by (see usb_hid.c). */
export const LINK_VENDOR_ID = 0x303a;
export const LINK_USAGE_PAGE = 0xff00;
export const LINK_USAGE = 0x0001;

/** Size of one report, and the stream bytes it carries. */
export const REPORT_SIZE = 64;
export const REPORT_DATA = REPORT_SIZE - 1;
/** Largest frame payload, and the body chunk the host sends per frame. */
export const MAX_PAYLOAD = 1100;
export const BODY_CHUNK = 1024;
/** Protocol version. */
export const PROTO = 1;

/** Frame types (same numbers as the firmware). */
export const T = {
  REQ_HEAD: 0x01,
  REQ_BODY: 0x02,
  RES_HEAD: 0x11,
  RES_BODY: 0x12,
  RES_END: 0x13,
  EVENT: 0x21,
  LIVE: 0x31,
  ACK: 0x41,
  HELLO: 0x51,
  INFO: 0x52,
  ERROR: 0x7f,
} as const;

/**
 * Pack stream bytes into one report: byte 0 is the number of valid bytes, the rest is zero padding.
 *
 * @param data Up to {@link REPORT_DATA} bytes.
 * @returns A {@link REPORT_SIZE}-byte report.
 */
export function packReport(data: Uint8Array): Uint8Array {
  const out = new Uint8Array(REPORT_SIZE);
  const n = Math.min(data.length, REPORT_DATA);
  out[0] = n;
  out.set(data.subarray(0, n), 1);
  return out;
}

/**
 * Encode one frame: `[length low][length high][type][payload]`.
 *
 * @param type Frame type.
 * @param payload Payload (at most {@link MAX_PAYLOAD} bytes).
 * @returns The frame bytes.
 */
export function encodeFrame(type: number, payload: Uint8Array = new Uint8Array(0)): Uint8Array {
  if (payload.length > MAX_PAYLOAD) throw new RangeError('frame payload too large');
  const out = new Uint8Array(3 + payload.length);
  out[0] = payload.length & 0xff;
  out[1] = (payload.length >> 8) & 0xff;
  out[2] = type;
  out.set(payload, 3);
  return out;
}

/** Split stream bytes into reports. */
export function toReports(stream: Uint8Array): Uint8Array[] {
  const reports: Uint8Array[] = [];
  for (let i = 0; i < stream.length; i += REPORT_DATA) {
    reports.push(packReport(stream.subarray(i, i + REPORT_DATA)));
  }
  return reports;
}

/** A complete frame. */
export interface Frame {
  /** Frame type. */
  type: number;
  /** Payload bytes. */
  payload: Uint8Array;
}

/** Incremental frame parser fed with the valid bytes of each report. */
export class FrameParser {
  private buf = new Uint8Array(3 + MAX_PAYLOAD);
  private have = 0;
  /** Oversized frames dropped while resynchronising. */
  errors = 0;

  /**
   * Feed stream bytes.
   *
   * @param data Stream bytes.
   * @returns The frames completed by these bytes.
   */
  feed(data: Uint8Array): Frame[] {
    const out: Frame[] = [];
    for (const b of data) {
      this.buf[this.have++] = b;
      if (this.have < 3) continue;
      const len = (this.buf[0] ?? 0) | ((this.buf[1] ?? 0) << 8);
      if (len > MAX_PAYLOAD) {
        this.errors++;
        this.have = 0;
        continue;
      }
      if (this.have === 3 + len) {
        out.push({ type: this.buf[2] ?? 0, payload: this.buf.slice(3, 3 + len) });
        this.have = 0;
      }
    }
    return out;
  }

  /**
   * Feed one report as received (byte 0 is the count of valid bytes).
   *
   * @param report The report bytes.
   * @returns The frames completed by it.
   */
  feedReport(report: Uint8Array): Frame[] {
    const n = Math.min(report[0] ?? 0, REPORT_DATA, Math.max(0, report.length - 1));
    return this.feed(report.subarray(1, 1 + n));
  }
}

/** The part of a WebHID device the link uses (lets tests supply a fake). */
export interface HidLike {
  /** Opened. */
  opened: boolean;
  open(): Promise<void>;
  close(): Promise<void>;
  /** Send an output report (no report ID: 0). */
  sendReport(reportId: number, data: Uint8Array): Promise<void>;
  /** Listen for input reports; returns a function that removes the listener. */
  onReport(cb: (data: Uint8Array) => void): () => void;
  /** Listen for the device being unplugged. */
  onDisconnect(cb: () => void): () => void;
}

/** Answer of the dongle to the handshake. */
export interface LinkInfo {
  proto: number;
  fw: string;
  board: string;
  /** The configuration window is open: requests are answered. */
  open: boolean;
}

/** Response of a request over the link. */
export interface LinkResponse {
  status: number;
  contentType: string;
  body: Uint8Array;
}

/** Error of the link itself (not of the API). */
export class LinkError extends Error {}

const enc = new TextEncoder();
const dec = new TextDecoder();

/** Heartbeat period; the dongle treats the host as gone after 15 s of silence. */
export const HEARTBEAT_MS = 5000;

/** The link over a {@link HidLike} device. */
export class UsbLink {
  private parser = new FrameParser();
  private waiters: ((f: Frame) => boolean)[] = [];
  private chain: Promise<unknown> = Promise.resolve();
  private nextId = 1;
  private offReport: (() => void) | null = null;
  private offDisc: (() => void) | null = null;
  private heartbeat: ReturnType<typeof setInterval> | null = null;
  private eventListener: ((json: string) => void) | null = null;
  private closeListener: (() => void) | null = null;
  /** Set once the handshake succeeded. */
  info: LinkInfo | null = null;

  /**
   * Create a link over a device.
   *
   * @param hid The (opened or openable) HID device.
   */
  constructor(private readonly hid: HidLike) {}

  /**
   * Open the device and run the handshake.
   *
   * @returns What the dongle says about itself.
   */
  async connect(): Promise<LinkInfo> {
    if (!this.hid.opened) await this.hid.open();
    this.offReport = this.hid.onReport((data) => {
      for (const f of this.parser.feedReport(data)) this.dispatch(f);
    });
    this.offDisc = this.hid.onDisconnect(() => {
      this.teardown();
      this.closeListener?.();
    });
    const reply = this.waitFor((f) => f.type === T.INFO, 3000);
    await this.sendFrame(T.HELLO, enc.encode(JSON.stringify({ proto: PROTO })));
    const f = await reply;
    const info = JSON.parse(dec.decode(f.payload)) as LinkInfo;
    if (info.proto !== PROTO) throw new LinkError(`Unsupported protocol ${String(info.proto)}`);
    this.info = info;
    // The dongle counts the host as present for a few seconds after its last frame: keep talking.
    this.heartbeat = setInterval(() => {
      void this.sendFrame(T.HELLO, enc.encode(JSON.stringify({ proto: PROTO }))).catch(
        () => undefined,
      );
    }, HEARTBEAT_MS);
    return info;
  }

  /**
   * Run `cb` when the link goes away (device unplugged).
   *
   * @param cb Listener.
   */
  onClose(cb: () => void): void {
    this.closeListener = cb;
  }

  /** Close the device. */
  async disconnect(): Promise<void> {
    this.teardown();
    await this.hid.close();
  }

  private teardown(): void {
    this.offReport?.();
    this.offDisc?.();
    if (this.heartbeat) clearInterval(this.heartbeat);
    this.offReport = this.offDisc = this.heartbeat = null;
    this.waiters = [];
  }

  private dispatch(f: Frame): void {
    if (f.type === T.EVENT) {
      this.eventListener?.(dec.decode(f.payload));
      return;
    }
    this.waiters = this.waiters.filter((w) => !w(f));
  }

  private waitFor(match: (f: Frame) => boolean, timeoutMs: number): Promise<Frame> {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.waiters = this.waiters.filter((x) => x !== w);
        reject(new LinkError('The dongle did not answer'));
      }, timeoutMs);
      const w = (f: Frame): boolean => {
        if (!match(f)) return false;
        clearTimeout(timer);
        resolve(f);
        return true;
      };
      this.waiters.push(w);
    });
  }

  private async sendFrame(type: number, payload?: Uint8Array): Promise<void> {
    for (const r of toReports(encodeFrame(type, payload))) {
      await this.hid.sendReport(0, r);
    }
  }

  /**
   * Perform an API request. Requests are serialised: the dongle serves one at a time.
   *
   * @param method HTTP method.
   * @param path API path, e.g. `/api/v1/status`.
   * @param body Optional body.
   * @param contentType Content type of the body (default JSON).
   * @param onProgress Called with the fraction of the body sent (uploads).
   * @returns The response.
   */
  request(
    method: string,
    path: string,
    body?: Uint8Array | string,
    contentType = 'application/json',
    onProgress?: (fraction: number) => void,
  ): Promise<LinkResponse> {
    const run = () => this.doRequest(method, path, body, contentType, onProgress);
    const p = this.chain.then(run, run);
    this.chain = p.catch(() => undefined);
    return p;
  }

  private async doRequest(
    method: string,
    path: string,
    body: Uint8Array | string | undefined,
    contentType: string,
    onProgress?: (fraction: number) => void,
  ): Promise<LinkResponse> {
    const id = this.nextId++ & 0x7fffffff;
    const bytes = typeof body === 'string' ? enc.encode(body) : (body ?? new Uint8Array(0));
    const res = this.collectResponse(id);
    const head = { id, method, path, ct: contentType, len: bytes.length };
    const ack = bytes.length > 0 ? this.waitFor((f) => f.type === T.ACK, 10000) : null;
    await this.sendFrame(T.REQ_HEAD, enc.encode(JSON.stringify(head)));
    if (ack) await ack;
    for (let off = 0; off < bytes.length; off += BODY_CHUNK) {
      const chunk = bytes.subarray(off, off + BODY_CHUNK);
      const last = off + BODY_CHUNK >= bytes.length;
      // Wait for the acknowledgement before the next chunk (the dongle's window is one chunk); after the last
      // chunk the answer is the response itself.
      const next = last ? null : this.waitFor((f) => f.type === T.ACK, 10000);
      await this.sendFrame(T.REQ_BODY, chunk);
      if (next) await next;
      onProgress?.(Math.min(1, (off + chunk.length) / bytes.length));
    }
    return res;
  }

  private collectResponse(id: number): Promise<LinkResponse> {
    return new Promise((resolve, reject) => {
      let status = 0;
      let contentType = '';
      const parts: Uint8Array[] = [];
      const timer = setTimeout(() => {
        this.waiters = this.waiters.filter((x) => x !== w);
        reject(new LinkError('The dongle did not finish the response'));
      }, 120000);
      const w = (f: Frame): boolean => {
        if (f.type === T.RES_HEAD) {
          const h = JSON.parse(dec.decode(f.payload)) as {
            id: number;
            status: number;
            ct?: string;
          };
          if (h.id !== id) return false;
          status = h.status;
          contentType = h.ct ?? '';
          return false;
        }
        if (f.type === T.RES_BODY) {
          parts.push(f.payload);
          return false;
        }
        if (f.type === T.RES_END) {
          clearTimeout(timer);
          const total = parts.reduce((n, p) => n + p.length, 0);
          const body = new Uint8Array(total);
          let o = 0;
          for (const p of parts) {
            body.set(p, o);
            o += p.length;
          }
          resolve({ status, contentType, body });
          return true;
        }
        return false;
      };
      this.waiters.push(w);
    });
  }

  /**
   * A socket-like object for the live events (same JSON as the WebSocket), so the existing live-connection code works.
   *
   * @returns The socket adapter.
   */
  socket(): SocketLike {
    const sock: SocketLike = {
      onopen: null,
      onmessage: null,
      onclose: null,
      send: (data) => {
        void this.sendFrame(T.LIVE, enc.encode(data)).catch(() => undefined);
      },
      close: () => {
        this.eventListener = null;
      },
    };
    this.eventListener = (json) => sock.onmessage?.(json);
    this.closeListener = () => sock.onclose?.();
    queueMicrotask(() => sock.onopen?.());
    return sock;
  }
}

/** Minimal shape of the WebHID API used here. */
interface HidDeviceApi {
  opened: boolean;
  open(): Promise<void>;
  close(): Promise<void>;
  sendReport(reportId: number, data: BufferSource): Promise<void>;
  addEventListener(type: 'inputreport', cb: (e: { data: DataView }) => void): void;
  removeEventListener(type: 'inputreport', cb: (e: { data: DataView }) => void): void;
}
interface HidApi {
  requestDevice(opts: {
    filters: { vendorId: number; usagePage: number; usage: number }[];
  }): Promise<HidDeviceApi[]>;
  addEventListener(type: 'disconnect', cb: (e: { device: HidDeviceApi }) => void): void;
  removeEventListener(type: 'disconnect', cb: (e: { device: HidDeviceApi }) => void): void;
}

/**
 * Whether this browser can talk to the dongle over USB (WebHID in a secure context).
 *
 * @returns true if available.
 */
export function usbSupported(): boolean {
  return typeof navigator !== 'undefined' && 'hid' in navigator && globalThis.isSecureContext;
}

/**
 * Ask the user to pick the dongle (a browser prompt) and return a link over it. Must be called from a click.
 *
 * @returns The link, not yet connected.
 * @throws {LinkError} if no device was chosen.
 */
export async function requestUsbLink(): Promise<UsbLink> {
  const hid = (navigator as unknown as { hid: HidApi }).hid;
  const devices = await hid.requestDevice({
    filters: [{ vendorId: LINK_VENDOR_ID, usagePage: LINK_USAGE_PAGE, usage: LINK_USAGE }],
  });
  const d = devices[0];
  if (!d) throw new LinkError('No dongle selected');
  const like: HidLike = {
    get opened() {
      return d.opened;
    },
    open: () => d.open(),
    close: () => d.close(),
    sendReport: (id, data) => d.sendReport(id, data as unknown as BufferSource),
    onReport: (cb) => {
      const h = (e: { data: DataView }) => {
        cb(new Uint8Array(e.data.buffer, e.data.byteOffset, e.data.byteLength));
      };
      d.addEventListener('inputreport', h);
      return () => {
        d.removeEventListener('inputreport', h);
      };
    },
    onDisconnect: (cb) => {
      const h = (e: { device: HidDeviceApi }) => {
        if (e.device === d) cb();
      };
      hid.addEventListener('disconnect', h);
      return () => {
        hid.removeEventListener('disconnect', h);
      };
    },
  };
  return new UsbLink(like);
}
