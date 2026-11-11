/**
 * @file vitePlugin.ts
 * @description Vite dev-server plugin exposing the mock API under `/api/`.
 * Only active for `vite` / `vite preview`; never part of the production bundle.
 */
import type { Plugin } from 'vite';
import { handleMock } from './handlers.ts';

/** Largest body kept in memory for JSON parsing; larger bodies (uploads) are only counted. */
const MAX_JSON_BYTES = 4096;

/**
 * Create the mock API plugin.
 *
 * @returns A Vite plugin installing a connect middleware for `/api/*`.
 */
export function mockApiPlugin(): Plugin {
  return {
    name: 'hubble-mock-api',
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const url = req.url ?? '';
        if (!url.startsWith('/api/')) {
          next();
          return;
        }
        const path = url.split('?')[0] ?? url;
        const chunks: Buffer[] = [];
        let bytes = 0;
        req.on('data', (c: Buffer) => {
          bytes += c.length;
          if (bytes <= MAX_JSON_BYTES) chunks.push(c);
        });
        req.on('end', () => {
          let json: unknown;
          if (bytes > 0 && bytes <= MAX_JSON_BYTES) {
            try {
              json = JSON.parse(Buffer.concat(chunks).toString('utf8'));
            } catch {
              json = undefined; /* Not JSON (e.g. a binary upload): the handler decides. */
            }
          }
          const headers: Record<string, string | undefined> = {};
          for (const [k, v] of Object.entries(req.headers)) {
            headers[k] = Array.isArray(v) ? v.join(',') : v;
          }
          const r = handleMock({
            method: req.method ?? 'GET',
            path,
            headers,
            json,
            bodyBytes: bytes,
          });
          res.statusCode = r.status;
          if (r.text !== undefined) {
            res.setHeader('Content-Type', 'text/plain; charset=utf-8');
            res.end(r.text);
          } else {
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify(r.json));
          }
        });
      });
    },
  };
}
