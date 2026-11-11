/**
 * @file handlers.ts
 * @description Framework-independent mock of the firmware REST API. Used by the
 * Vite dev-server plugin (dev + Playwright) and by unit tests, so the web app
 * can be developed and tested without hardware (CLAUDE.md §3.7).
 */
import type { ApiErrorBody, Status } from '../api/types';

/** Result of a mocked request. */
export interface MockResponse {
  /** HTTP status code. */
  status: number;
  /** JSON-serialisable body. */
  body: Status | ApiErrorBody;
}

/** Fixed fake status; realistic values, deterministic for tests. */
export const MOCK_STATUS: Status = {
  version: '0.1.0-mock',
  board: 's3_zero',
  uptime_s: 1234,
  usb_state: 'active',
  ap_timeout_s: 180,
  setup_required: true,
};

/**
 * Answer a request the way the firmware would.
 *
 * @param method HTTP method.
 * @param path Request path without query string.
 * @returns The mocked response (404 error body for unknown routes).
 */
export function handleMock(method: string, path: string): MockResponse {
  if (method === 'GET' && path === '/api/v1/status') {
    return { status: 200, body: MOCK_STATUS };
  }
  return {
    status: 404,
    body: { error: { code: 'not_found', message: `No route ${method} ${path}` } },
  };
}
