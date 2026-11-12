/**
 * @file index.ts
 * @description HubBLE device-report collector (Cloudflare Worker).
 *
 * Endpoints:
 *  - `POST /v1/reports`       public: submit a report (validated, rate-limited, de-duplicated);
 *  - `GET  /v1/stats`         public: aggregate statistics, no raw report maps;
 *  - `GET  /v1/reports/{id}`  admin (bearer ADMIN_TOKEN): one stored report;
 *  - `GET  /v1/export`        admin (bearer ADMIN_TOKEN): every report as NDJSON.
 *
 * Storage (Workers KV): `r:<id>` holds the report, with its {@link Summary} as key metadata; `rl:<hash>:<hour>` are
 * the rate-limit counters (they expire); `stats:cache` is the cached statistics document. See README.md for exactly
 * what is stored. The Worker never logs request bodies or addresses.
 */
import { computeStats, summarize } from './stats';
import type { Summary } from './stats';
import type { Env } from './storage';
import { validateReport } from './validate';

export type { Env, KvLike } from './storage';

/** Largest accepted request body, bytes. */
export const MAX_BODY_BYTES = 16 * 1024;
/** Reports accepted per client and hour (every POST counts, valid or not). */
export const RATE_LIMIT_PER_HOUR = 20;
/** Seconds the statistics document is cached. */
export const STATS_TTL_S = 300;

const HOUR_MS = 3_600_000;
const SALT_FALLBACK = 'hubble-reports-default-salt';
const ID_PATTERN = /^[0-9a-f]{24}$/;
const KV_PAGE = 1000;

/** Headers added to every response. The dongle's page is served from http://192.168.4.1 or http://hubble.local. */
const CORS: Record<string, string> = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type, Authorization',
  'Access-Control-Max-Age': '86400',
};

/** JSON response with the CORS headers. */
function json(body: unknown, status = 200, extra: Record<string, string> = {}): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8', ...CORS, ...extra },
  });
}

/** Error response `{ok:false, error:{code,message}}`. */
function fail(
  status: number,
  code: string,
  message: string,
  extra: Record<string, string> = {},
): Response {
  return json({ ok: false, error: { code, message } }, status, extra);
}

/** Lower-case hex SHA-256 of a string. */
async function sha256Hex(text: string): Promise<string> {
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

/** Copy with object keys sorted recursively, so equal content serialises equally. */
function canonical(v: unknown): unknown {
  if (Array.isArray(v)) return v.map(canonical);
  if (typeof v === 'object' && v !== null) {
    return Object.fromEntries(
      Object.entries(v)
        .sort(([a], [b]) => (a < b ? -1 : 1))
        .map(([k, x]) => [k, canonical(x)]),
    );
  }
  return v;
}

/**
 * Identity of a report for de-duplication: a hash of the `device` object (not the notes, not the firmware).
 *
 * @param device The validated `device` object.
 * @returns 24 hex characters.
 */
export async function reportId(device: unknown): Promise<string> {
  return (await sha256Hex(JSON.stringify(canonical(device)))).slice(0, 24);
}

/** Read the body up to MAX_BODY_BYTES; null if it is larger. */
async function readBody(req: Request): Promise<string | null> {
  const declared = Number(req.headers.get('Content-Length') ?? '0');
  if (declared > MAX_BODY_BYTES) return null;
  if (req.body === null) return '';
  const reader = req.body.getReader();
  const chunks: Uint8Array[] = [];
  let total = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > MAX_BODY_BYTES) {
      await reader.cancel();
      return null;
    }
    chunks.push(value);
  }
  const all = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) {
    all.set(c, at);
    at += c.byteLength;
  }
  return new TextDecoder().decode(all);
}

/**
 * Count this request against the client's hourly quota. The client is identified only by a salted hash of its
 * address, used as a counter key that expires; the address itself is never stored.
 *
 * @returns Seconds until the quota resets if it is exhausted, otherwise 0.
 */
async function rateLimited(req: Request, env: Env, now: number): Promise<number> {
  const ip = req.headers.get('CF-Connecting-IP') ?? 'unknown';
  const hash = (await sha256Hex(`${env.RATE_SALT ?? SALT_FALLBACK}|${ip}`)).slice(0, 24);
  const bucket = Math.floor(now / HOUR_MS);
  const key = `rl:${hash}:${String(bucket)}`;
  const used = Number((await env.REPORTS.get(key)) ?? '0');
  if (used >= RATE_LIMIT_PER_HOUR)
    return Math.max(1, Math.ceil(((bucket + 1) * HOUR_MS - now) / 1000));
  await env.REPORTS.put(key, String(used + 1), { expirationTtl: 2 * 3600 });
  return 0;
}

/** True if the request carries the admin bearer token (compared in constant time). */
async function isAdmin(req: Request, env: Env): Promise<boolean> {
  if (!env.ADMIN_TOKEN) return false;
  const given = /^Bearer (.+)$/.exec(req.headers.get('Authorization') ?? '')?.[1] ?? '';
  const [a, b] = await Promise.all([sha256Hex(given), sha256Hex(env.ADMIN_TOKEN)]);
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

/** POST /v1/reports. */
async function postReport(req: Request, env: Env, now: number): Promise<Response> {
  const wait = await rateLimited(req, env, now);
  if (wait > 0) {
    return fail(429, 'rate_limited', 'Too many reports from this client; try again later.', {
      'Retry-After': String(wait),
    });
  }
  const text = await readBody(req);
  if (text === null)
    return fail(413, 'too_large', `The report must be at most ${String(MAX_BODY_BYTES)} bytes.`);
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return fail(400, 'invalid_json', 'The body is not valid JSON.');
  }
  const v = validateReport(parsed);
  if (!v.ok) return fail(400, 'invalid_report', v.error);

  const id = await reportId(v.report.device);
  if ((await env.REPORTS.get(`r:${id}`)) !== null) return json({ ok: true, id, duplicate: true });
  await env.REPORTS.put(`r:${id}`, JSON.stringify(v.report), { metadata: summarize(v.report) });
  await env.REPORTS.delete('stats:cache');
  return json({ ok: true, id }, 201);
}

/** All summaries, read from the key metadata (no per-report reads). */
async function allSummaries(env: Env): Promise<Summary[]> {
  const out: Summary[] = [];
  let cursor: string | undefined;
  for (;;) {
    const page = await env.REPORTS.list<Summary>({
      prefix: 'r:',
      limit: KV_PAGE,
      ...(cursor ? { cursor } : {}),
    });
    for (const k of page.keys) if (k.metadata) out.push(k.metadata);
    if (page.list_complete || !page.cursor) return out;
    cursor = page.cursor;
  }
}

/** GET /v1/stats. */
async function getStats(env: Env, now: number): Promise<Response> {
  const headers = { 'Cache-Control': `public, max-age=${String(STATS_TTL_S)}` };
  const cached = await env.REPORTS.get('stats:cache');
  if (cached !== null)
    return new Response(cached, {
      headers: { 'Content-Type': 'application/json; charset=utf-8', ...CORS, ...headers },
    });
  const body = JSON.stringify(computeStats(await allSummaries(env), new Date(now)));
  await env.REPORTS.put('stats:cache', body, { expirationTtl: STATS_TTL_S });
  return new Response(body, {
    headers: { 'Content-Type': 'application/json; charset=utf-8', ...CORS, ...headers },
  });
}

/** GET /v1/export: every report, one JSON document per line, streamed page by page. */
function exportAll(env: Env): Response {
  const encoder = new TextEncoder();
  let cursor: string | undefined;
  let queue: string[] = [];
  let done = false;
  const stream = new ReadableStream<Uint8Array>({
    async pull(controller) {
      while (queue.length === 0 && !done) {
        const page = await env.REPORTS.list({
          prefix: 'r:',
          limit: KV_PAGE,
          ...(cursor ? { cursor } : {}),
        });
        queue = page.keys.map((k) => k.name);
        cursor = page.cursor;
        done = page.list_complete || !page.cursor;
      }
      const name = queue.shift();
      if (name === undefined) {
        controller.close();
        return;
      }
      const value = await env.REPORTS.get(name);
      if (value !== null) controller.enqueue(encoder.encode(`${value}\n`));
    },
  });
  return new Response(stream, { headers: { 'Content-Type': 'application/x-ndjson', ...CORS } });
}

/**
 * Handle one request. Pure with respect to its arguments (time is injected), so it is directly testable.
 *
 * @param req The request.
 * @param env Bindings.
 * @param now Current time in ms (injectable for tests).
 * @returns The response.
 */
export async function handle(req: Request, env: Env, now: number = Date.now()): Promise<Response> {
  const url = new URL(req.url);
  const path = url.pathname;
  if (req.method === 'OPTIONS') return new Response(null, { status: 204, headers: CORS });

  if (path === '/v1/reports') {
    if (req.method !== 'POST')
      return fail(405, 'method_not_allowed', 'Use POST.', { Allow: 'POST, OPTIONS' });
    return postReport(req, env, now);
  }
  if (path === '/v1/stats') {
    if (req.method !== 'GET')
      return fail(405, 'method_not_allowed', 'Use GET.', { Allow: 'GET, OPTIONS' });
    return getStats(env, now);
  }
  const single = /^\/v1\/reports\/([^/]+)$/.exec(path);
  if (single || path === '/v1/export') {
    if (req.method !== 'GET')
      return fail(405, 'method_not_allowed', 'Use GET.', { Allow: 'GET, OPTIONS' });
    if (!(await isAdmin(req, env)))
      return fail(401, 'unauthorized', 'Admin token required.', { 'WWW-Authenticate': 'Bearer' });
    if (path === '/v1/export') return exportAll(env);
    const id = single?.[1] ?? '';
    const stored = ID_PATTERN.test(id) ? await env.REPORTS.get(`r:${id}`) : null;
    if (stored === null) return fail(404, 'not_found', 'No such report.');
    return new Response(stored, {
      headers: { 'Content-Type': 'application/json; charset=utf-8', ...CORS },
    });
  }
  return fail(404, 'not_found', 'Unknown endpoint.');
}

export default {
  fetch: (req: Request, env: Env): Promise<Response> => handle(req, env),
};
