/**
 * @file handlers.ts
 * @description Framework-independent mock of the firmware REST API. Used by the Vite dev-server plugin
 * (dev + Playwright) and by unit tests, so the web app can be developed and tested without hardware
 * (CLAUDE.md §3.7). It reproduces the firmware's CSRF guard and validation rules.
 */
import type { Diagnostics, OtaStatus, Status } from '../api/types';
import { passwordProblem } from '../lib/password.ts';

/** A request as seen by the mock. */
export interface MockRequest {
  /** HTTP method. */
  method: string;
  /** Path without query string. */
  path: string;
  /** Lower-cased request headers. */
  headers: Record<string, string | undefined>;
  /** Parsed JSON body, if any. */
  json?: unknown;
  /** Total request body size in bytes (used by the upload endpoint). */
  bodyBytes: number;
}

/** Result of a mocked request. */
export interface MockResponse {
  /** HTTP status code. */
  status: number;
  /** JSON body (mutually exclusive with `text`). */
  json?: unknown;
  /** Plain text body, e.g. the log download. */
  text?: string;
}

/** Version the mocked dongle reports as running. */
export const MOCK_VERSION = '0.2.0-mock';
/** Version the mock "installs" on upload. */
export const MOCK_UPLOAD_VERSION = '0.2.1-rc.1-mock';
/** Slot size of the S3-Zero; uploads above this are rejected like the firmware does. */
export const MOCK_SLOT_BYTES = 0x1e0000;
/** Uploads smaller than this cannot be a real image. */
export const MOCK_MIN_IMAGE_BYTES = 1024;

/** Initial mock state; also what {@link resetMock} restores. */
function initialState() {
  return { setupDone: false, version: MOCK_VERSION };
}
let state = initialState();

/** Reset the mutable mock state (tests). */
export function resetMock(): void {
  state = initialState();
}

/** Fixed fake status; realistic values, deterministic for tests. */
export function mockStatus(): Status {
  return {
    version: state.version,
    board: 's3_zero',
    uptime_s: 1234,
    usb_state: 'detached',
    ap_timeout_s: 180,
    setup_required: !state.setupDone,
  };
}

/** Fake OTA status with a pending-verify image and a failed previous attempt. */
function mockOta(): OtaStatus {
  return {
    running: { slot: 'ota_0', version: state.version },
    other: { slot: 'ota_1', version: '0.1.1' },
    pending_verify: false,
    rollback_happened: false,
    boot_failures: 0,
    last_result: `running ${state.version}, self-test passed`,
  };
}

/** Fake diagnostics. */
function mockDiagnostics(): Diagnostics {
  return {
    uptime_s: 1234,
    reset_reason: 'power-on',
    heap: { free: 142_000, min_free: 120_500, largest_block: 98_304 },
    psram: { total: 2_097_152, free: 2_000_000 },
    partitions: {
      running: { label: 'ota_0', address: 0x20000, size: MOCK_SLOT_BYTES },
      next_update: { label: 'ota_1', address: 0x200000, size: MOCK_SLOT_BYTES },
    },
    boot_failures: 0,
    usb_state: 'detached',
    ble: null,
    wifi: { ssid: 'HubBLE-A1B2', clients: 1 },
  };
}

/** Standard error response. */
function err(status: number, code: string, message: string): MockResponse {
  return { status, json: { error: { code, message } } };
}

/**
 * Apply the firmware's CSRF guard to a state-changing request.
 *
 * @param req Request.
 * @param expectedCt Required content type prefix.
 * @returns An error response, or null if the request passes.
 */
function guard(req: MockRequest, expectedCt: string): MockResponse | null {
  if (req.headers['x-hubble'] !== '1') {
    return err(403, 'forbidden', 'Missing X-HubBLE header or cross-origin request');
  }
  if (!(req.headers['content-type'] ?? '').toLowerCase().startsWith(expectedCt)) {
    return err(415, 'bad_content_type', 'Unexpected Content-Type');
  }
  return null;
}

/**
 * Answer a request the way the firmware would.
 *
 * @param req The request.
 * @returns The mocked response (404 error body for unknown routes).
 */
export function handleMock(req: MockRequest): MockResponse {
  const { method, path } = req;
  if (method === 'GET') {
    switch (path) {
      case '/api/v1/status':
        return { status: 200, json: mockStatus() };
      case '/api/v1/ota/status':
        return { status: 200, json: mockOta() };
      case '/api/v1/diagnostics':
        return { status: 200, json: mockDiagnostics() };
      case '/api/v1/logs':
        return {
          status: 200,
          text: 'I (100) hubble: HubBLE firmware (mock)\nI (200) web: HTTP server up\n',
        };
    }
  }
  if (method === 'POST') {
    switch (path) {
      case '/api/v1/settings/wifi-password': {
        const bad = guard(req, 'application/json');
        if (bad) return bad;
        const pw = (req.json as { password?: unknown } | undefined)?.password;
        if (typeof pw !== 'string' || passwordProblem(pw) !== null) {
          return err(
            400,
            'invalid_password',
            'Password must be 10-63 printable characters and not the default',
          );
        }
        state.setupDone = true;
        return { status: 200, json: { ok: true } };
      }
      case '/api/v1/system/reboot':
      case '/api/v1/system/reboot-download': {
        const bad = guard(req, 'application/json');
        return bad ?? { status: 200, json: { ok: true } };
      }
      case '/api/v1/ota/upload': {
        const bad = guard(req, 'application/octet-stream');
        if (bad) return bad;
        if (req.bodyBytes > MOCK_SLOT_BYTES) return err(413, 'bad_size', 'bad size');
        if (req.bodyBytes < MOCK_MIN_IMAGE_BYTES)
          return err(400, 'bad_image', 'not a valid ESP32-S3 image');
        state.version = MOCK_UPLOAD_VERSION;
        return { status: 200, json: { ok: true, version: MOCK_UPLOAD_VERSION, reboot: true } };
      }
    }
  }
  return err(404, 'not_found', `No route ${method} ${path}`);
}
