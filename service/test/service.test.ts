import { beforeEach, describe, expect, it } from 'vitest';
import { MAX_BODY_BYTES, RATE_LIMIT_PER_HOUR, handle, reportId } from '../src/index';
import type { Env } from '../src/index';
import { FakeKv } from './fakekv';
import { get, post, sample } from './samples';

const NOW = Date.UTC(2026, 10, 12, 12, 30);
let kv: FakeKv;
let env: Env;

beforeEach(() => {
  kv = new FakeKv();
  env = { REPORTS: kv, ADMIN_TOKEN: 'secret-token', RATE_SALT: 'salt' };
});

const call = (req: Request, now = NOW) => handle(req, env, now);
const body = async (r: Response) => (await r.json()) as Record<string, any>;

describe('POST /v1/reports', () => {
  it('stores a valid report and returns its id', async () => {
    const res = await call(post(sample()));
    expect(res.status).toBe(201);
    const j = await body(res);
    expect(j.ok).toBe(true);
    expect(j.id).toMatch(/^[0-9a-f]{24}$/);
    expect(kv.reportKeys()).toEqual([`r:${j.id}`]);
    expect(JSON.parse(kv.data.get(`r:${j.id}`)?.value ?? '{}').device.model).toBe('B68');
    // The key has no TTL: reports are kept until the maintainer deletes them.
    expect(kv.data.get(`r:${j.id}`)?.ttl).toBeUndefined();
  });

  it('answers CORS on every response', async () => {
    const res = await call(post(sample()));
    expect(res.headers.get('Access-Control-Allow-Origin')).toBe('*');
    const err = await call(post('{'));
    expect(err.headers.get('Access-Control-Allow-Origin')).toBe('*');
  });

  it('rejects bad JSON, invalid reports and the wrong method', async () => {
    const notJson = await call(post('{nope'));
    expect(notJson.status).toBe(400);
    expect((await body(notJson)).error.code).toBe('invalid_json');

    const wrong = await call(post({ ...sample(), version: 2 }));
    expect(wrong.status).toBe(400);
    const j = await body(wrong);
    expect(j.error.code).toBe('invalid_report');
    expect(j.error.message).toMatch(/report\.version/);

    const extra = await call(post({ ...sample(), mac: 'aa:bb' }));
    expect(extra.status).toBe(400);

    const badHex = await call(post(sample({ report_map_hex: 'zz' })));
    expect(badHex.status).toBe(400);

    const nonString = await call(post(sample({ adv_name: 7 as never })));
    expect(nonString.status).toBe(400);

    const method = await call(new Request('https://reports.example/v1/reports', { method: 'PUT' }));
    expect(method.status).toBe(405);
    expect(kv.reportKeys()).toEqual([]);
  });

  it('rejects an oversize body, declared or not', async () => {
    const big = JSON.stringify({ ...sample(), notes: 'x'.repeat(MAX_BODY_BYTES) });
    const res = await call(post(big));
    expect(res.status).toBe(413);
    expect((await body(res)).error.code).toBe('too_large');
    // Without a Content-Length header the streamed body is still capped.
    const chunked = new Request('https://reports.example/v1/reports', {
      method: 'POST',
      headers: { 'CF-Connecting-IP': '198.51.100.1' },
      body: new ReadableStream({
        start(c) {
          c.enqueue(new TextEncoder().encode('x'.repeat(MAX_BODY_BYTES + 1)));
          c.close();
        },
      }),
      duplex: 'half',
    } as RequestInit);
    expect((await call(chunked)).status).toBe(413);
  });

  it('de-duplicates identical device data, ignoring the notes', async () => {
    const first = await body(await call(post(sample(), '203.0.113.1')));
    const again = await call(post({ ...sample(), notes: 'typed by someone else' }, '203.0.113.2'));
    expect(again.status).toBe(200);
    const j = await body(again);
    expect(j.id).toBe(first.id);
    expect(j.duplicate).toBe(true);
    expect(kv.reportKeys()).toHaveLength(1);
    // A different device is a new report.
    const other = await call(post(sample({ model: 'B87' }), '203.0.113.3'));
    expect(other.status).toBe(201);
    expect(kv.reportKeys()).toHaveLength(2);
  });

  it('computes the id from the device object only', async () => {
    const a = sample();
    const b = { ...sample(), firmware: { version: '9.9.9', board: 'other' }, notes: 'x' };
    expect(await reportId(a.device)).toBe(await reportId(b.device));
  });

  it('rate-limits per client, counting invalid requests too, and resets next hour', async () => {
    for (let i = 0; i < RATE_LIMIT_PER_HOUR; i++) {
      expect(
        (await call(post(i % 2 ? '{' : sample({ model: `M${String(i)}` }), '192.0.2.9'))).status,
      ).not.toBe(429);
    }
    const blocked = await call(post(sample(), '192.0.2.9'));
    expect(blocked.status).toBe(429);
    expect((await body(blocked)).error.code).toBe('rate_limited');
    expect(Number(blocked.headers.get('Retry-After'))).toBeGreaterThan(0);
    // Another client is not affected.
    expect((await call(post(sample({ model: 'X' }), '192.0.2.10'))).status).toBe(201);
    // The next hour starts a new counter.
    expect((await call(post(sample({ model: 'Y' }), '192.0.2.9'), NOW + 3_600_000)).status).toBe(
      201,
    );
  });

  it('never stores the client address', async () => {
    await call(post(sample(), '198.51.100.77'));
    const everything = JSON.stringify([...kv.data.entries()]);
    expect(everything).not.toContain('198.51.100.77');
    const counters = [...kv.data.keys()].filter((k) => k.startsWith('rl:'));
    expect(counters).toHaveLength(1);
    expect(kv.data.get(counters[0] as string)?.ttl).toBe(7200); // the counter expires
  });
});

describe('CORS preflight', () => {
  it('answers OPTIONS with the allowed methods and headers', async () => {
    const res = await call(
      new Request('https://reports.example/v1/reports', { method: 'OPTIONS' }),
    );
    expect(res.status).toBe(204);
    expect(res.headers.get('Access-Control-Allow-Origin')).toBe('*');
    expect(res.headers.get('Access-Control-Allow-Methods')).toContain('POST');
    expect(res.headers.get('Access-Control-Allow-Headers')).toContain('Content-Type');
  });
});

describe('GET /v1/stats', () => {
  it('returns public aggregates without raw report maps and refreshes after a new report', async () => {
    await call(post(sample(), '1.1.1.1'));
    await call(post(sample({ model: 'B87', report_map_hex: 'abcdef0102' }), '1.1.1.2'));
    const res = await call(get('/v1/stats'));
    expect(res.status).toBe(200);
    expect(res.headers.get('Cache-Control')).toContain('max-age=300');
    const text = await res.text();
    expect(text).not.toContain('abcdef0102');
    expect(text).not.toContain('05010906a101');
    const stats = JSON.parse(text);
    expect(stats.total).toBe(2);
    // A new report invalidates the cache.
    await call(post(sample({ model: 'B99', report_map_hex: '00' }), '1.1.1.3'));
    expect((await body(await call(get('/v1/stats')))).total).toBe(3);
  });

  it('pages through more reports than one KV page', async () => {
    for (let i = 0; i < 7; i++)
      await call(post(sample({ report_map_hex: `0${String(i)}` }), `2.2.2.${String(i)}`));
    expect((await body(await call(get('/v1/stats')))).total).toBe(7);
  });
});

describe('admin endpoints', () => {
  it('require the bearer token', async () => {
    const id = (await body(await call(post(sample())))).id as string;
    for (const path of [`/v1/reports/${id}`, '/v1/export']) {
      expect((await call(get(path))).status).toBe(401);
      expect((await call(get(path, 'wrong'))).status).toBe(401);
    }
    env = { REPORTS: kv }; // no ADMIN_TOKEN configured: always denied
    expect((await call(get('/v1/export', ''))).status).toBe(401);
    expect((await call(get('/v1/export', 'undefined'))).status).toBe(401);
  });

  it('returns one report and 404 for unknown or malformed ids', async () => {
    const id = (await body(await call(post(sample())))).id as string;
    const ok = await call(get(`/v1/reports/${id}`, 'secret-token'));
    expect(ok.status).toBe(200);
    expect((await body(ok)).device.report_map_hex).toBe('05010906a101');
    expect((await call(get(`/v1/reports/${'0'.repeat(24)}`, 'secret-token'))).status).toBe(404);
    expect((await call(get('/v1/reports/../x', 'secret-token'))).status).toBe(404);
  });

  it('exports every report as NDJSON', async () => {
    for (let i = 0; i < 5; i++)
      await call(post(sample({ report_map_hex: `0${String(i)}` }), `3.3.3.${String(i)}`));
    const res = await call(get('/v1/export', 'secret-token'));
    expect(res.headers.get('Content-Type')).toBe('application/x-ndjson');
    const lines = (await res.text()).trim().split('\n');
    expect(lines).toHaveLength(5);
    expect(lines.map((l) => JSON.parse(l).device.report_map_hex).sort()).toEqual([
      '00',
      '01',
      '02',
      '03',
      '04',
    ]);
  });
});

describe('routing', () => {
  it('answers 404 for unknown paths', async () => {
    expect((await call(get('/nope'))).status).toBe(404);
  });
});
