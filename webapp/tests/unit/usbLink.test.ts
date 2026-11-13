import { describe, expect, it } from 'vitest';
import {
  BODY_CHUNK,
  encodeFrame,
  FrameParser,
  packReport,
  T,
  toReports,
  UsbLink,
  REPORT_DATA,
  REPORT_SIZE,
} from '../../src/api/usbLink';
import type { Frame, HidLike } from '../../src/api/usbLink';

const enc = new TextEncoder();
const dec = new TextDecoder();

describe('framing (same vectors as firmware/test/unit/test_ulink_proto.c)', () => {
  it('encodes headers and reports', () => {
    expect([...encodeFrame(T.HELLO, new Uint8Array(0x105)).subarray(0, 3)]).toEqual([
      0x05, 0x01, 0x51,
    ]);
    const r = packReport(Uint8Array.of(1, 2, 3));
    expect(r.length).toBe(REPORT_SIZE);
    expect([...r.subarray(0, 5)]).toEqual([3, 1, 2, 3, 0]);
  });
  it('reassembles a frame split across reports', () => {
    const payload = Uint8Array.from({ length: 100 }, (_, i) => i);
    const reports = toReports(encodeFrame(T.REQ_BODY, payload));
    expect(reports.length).toBe(2);
    const p = new FrameParser();
    expect(p.feedReport(reports[0] as Uint8Array)).toEqual([]);
    const f = p.feedReport(reports[1] as Uint8Array);
    expect(f.length).toBe(1);
    expect(f[0]?.type).toBe(T.REQ_BODY);
    expect([...(f[0]?.payload ?? [])]).toEqual([...payload]);
  });
  it('parses two frames in one stream and an empty frame', () => {
    const s = new Uint8Array([...encodeFrame(T.ACK), ...encodeFrame(T.RES_END)]);
    const frames = new FrameParser().feed(s);
    expect(frames.map((f) => f.type)).toEqual([T.ACK, T.RES_END]);
  });
  it('resynchronises after an oversized frame and clamps a lying report count', () => {
    const p = new FrameParser();
    expect(p.feed(Uint8Array.of(0xff, 0xff, 0x01))).toEqual([]);
    expect(p.errors).toBe(1);
    expect(p.feed(encodeFrame(T.ACK)).length).toBe(1);
    const lying = new Uint8Array(REPORT_SIZE);
    lying[0] = 0xff;
    lying.set([100, 0, T.RES_BODY], 1);
    const q = new FrameParser();
    expect(q.feedReport(lying)).toEqual([]);
  });
});

/** A fake dongle behind the HidLike interface, speaking the firmware side of the protocol. */
function fakeDongle(open = true) {
  const parser = new FrameParser();
  let onReport: ((d: Uint8Array) => void) | null = null;
  const received: { head: Record<string, unknown>; body: number }[] = [];
  let current: { head: Record<string, unknown>; body: number; remaining: number } | null = null;
  const send = (type: number, payload: Uint8Array = new Uint8Array(0)) => {
    for (const r of toReports(encodeFrame(type, payload))) onReport?.(r);
  };
  const respond = (head: Record<string, unknown>, bodyLen: number) => {
    const json = JSON.stringify({ path: head.path, got: bodyLen });
    send(
      T.RES_HEAD,
      enc.encode(JSON.stringify({ id: head.id, status: 200, ct: 'application/json' })),
    );
    send(T.RES_BODY, enc.encode(json));
    send(T.RES_END);
  };
  const handle = (f: Frame) => {
    if (f.type === T.HELLO) {
      send(T.INFO, enc.encode(JSON.stringify({ proto: 1, fw: '1.3.0', board: 's3_zero', open })));
    } else if (f.type === T.REQ_HEAD) {
      const head = JSON.parse(dec.decode(f.payload)) as Record<string, unknown>;
      const len = Number(head.len);
      current = { head, body: 0, remaining: len };
      if (len === 0) respond(head, 0);
      else send(T.ACK);
    } else if (f.type === T.REQ_BODY && current) {
      current.body += f.payload.length;
      current.remaining -= f.payload.length;
      if (current.remaining > 0) send(T.ACK);
      else {
        received.push({ head: current.head, body: current.body });
        respond(current.head, current.body);
      }
    }
  };
  const hid: HidLike = {
    opened: false,
    open: () => {
      hid.opened = true;
      return Promise.resolve();
    },
    close: () => Promise.resolve(),
    sendReport: (_id, data) => {
      // Deliver asynchronously, like real USB.
      for (const f of parser.feedReport(data))
        queueMicrotask(() => {
          handle(f);
        });
      return Promise.resolve();
    },
    onReport: (cb) => {
      onReport = cb;
      return () => {
        onReport = null;
      };
    },
    onDisconnect: () => () => undefined,
  };
  return {
    hid,
    received,
    push: (type: number, p: Uint8Array) => {
      send(type, p);
    },
  };
}

describe('UsbLink against a fake dongle', () => {
  it('does the handshake', async () => {
    const d = fakeDongle();
    const link = new UsbLink(d.hid);
    const info = await link.connect();
    expect(info).toEqual({ proto: 1, fw: '1.3.0', board: 's3_zero', open: true });
    await link.disconnect();
  });
  it('performs a GET and a POST with a multi-chunk body, reporting progress', async () => {
    const d = fakeDongle();
    const link = new UsbLink(d.hid);
    await link.connect();
    const get = await link.request('GET', '/api/v1/status');
    expect(get.status).toBe(200);
    expect(JSON.parse(dec.decode(get.body))).toEqual({ path: '/api/v1/status', got: 0 });
    const body = new Uint8Array(BODY_CHUNK * 2 + 10);
    const progress: number[] = [];
    const post = await link.request(
      'POST',
      '/api/v1/ota/upload',
      body,
      'application/octet-stream',
      (f) => progress.push(f),
    );
    expect(JSON.parse(dec.decode(post.body))).toEqual({
      path: '/api/v1/ota/upload',
      got: body.length,
    });
    expect(progress.length).toBe(3);
    expect(progress[2]).toBe(1);
    await link.disconnect();
  });
  it('serialises concurrent requests', async () => {
    const d = fakeDongle();
    const link = new UsbLink(d.hid);
    await link.connect();
    const [a, b] = await Promise.all([
      link.request('GET', '/api/v1/a'),
      link.request('GET', '/api/v1/b'),
    ]);
    expect((JSON.parse(dec.decode(a.body)) as { path: string }).path).toBe('/api/v1/a');
    expect((JSON.parse(dec.decode(b.body)) as { path: string }).path).toBe('/api/v1/b');
    await link.disconnect();
  });
  it('delivers events through the socket adapter and sends live messages', async () => {
    const d = fakeDongle();
    const link = new UsbLink(d.hid);
    await link.connect();
    const sock = link.socket();
    const got: string[] = [];
    sock.onmessage = (m) => got.push(m);
    d.push(T.EVENT, enc.encode('{"event":"log.line","data":{"text":"hi"}}'));
    expect(got).toEqual(['{"event":"log.line","data":{"text":"hi"}}']);
    sock.send('{"event":"hello"}'); // must not throw
    await link.disconnect();
  });
  it('reports a closed configuration window', async () => {
    const link = new UsbLink(fakeDongle(false).hid);
    expect((await link.connect()).open).toBe(false);
    await link.disconnect();
  });
});

describe('transmission', () => {
  it('never interleaves the reports of two frames sent at the same time', async () => {
    const sent: Uint8Array[] = [];
    const hid: HidLike = {
      opened: true,
      open: () => Promise.resolve(),
      close: () => Promise.resolve(),
      // A slow device: every report takes a turn of the event loop, so concurrent senders would interleave.
      sendReport: async (_id, data) => {
        await new Promise((r) => setTimeout(r, 1));
        sent.push(data);
      },
      onReport: () => () => undefined,
      onDisconnect: () => () => undefined,
    };
    const link = new UsbLink(hid);
    const body = new Uint8Array(BODY_CHUNK).fill(7);
    // A request body chunk and a heartbeat, started together.
    const a = (link as unknown as { sendFrame(t: number, p: Uint8Array): Promise<void> }).sendFrame(
      T.REQ_BODY,
      body,
    );
    const b = (link as unknown as { sendFrame(t: number, p: Uint8Array): Promise<void> }).sendFrame(
      T.HELLO,
      enc.encode('{"proto":1}'),
    );
    await Promise.all([a, b]);
    const p = new FrameParser();
    const frames = sent.flatMap((r) => p.feedReport(r));
    expect(frames.map((f) => f.type)).toEqual([T.REQ_BODY, T.HELLO]);
    expect(frames[0]?.payload.length).toBe(BODY_CHUNK);
    expect(p.errors).toBe(0);
  });
});

describe('chunk size', () => {
  it('is within one frame', () => {
    expect(BODY_CHUNK).toBeLessThanOrEqual(1100);
    expect(REPORT_DATA).toBe(63);
  });
});
