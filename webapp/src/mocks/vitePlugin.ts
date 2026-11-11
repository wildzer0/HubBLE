/**
 * @file vitePlugin.ts
 * @description Vite dev-server plugin exposing the mock API under `/api/`.
 * Only active for `vite` / `vite preview`; never part of the production bundle.
 */
import type { Plugin } from 'vite';
import { handleMock } from './handlers.ts';

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
        const { status, body } = handleMock(req.method ?? 'GET', path);
        res.statusCode = status;
        res.setHeader('Content-Type', 'application/json');
        res.end(JSON.stringify(body));
      });
    },
  };
}
