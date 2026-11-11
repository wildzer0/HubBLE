import { beforeEach, describe, expect, it } from 'vitest';
import {
  handleMock,
  MOCK_SLOT_BYTES,
  MOCK_UPLOAD_VERSION,
  resetMock,
  type MockRequest,
} from '../../src/mocks/handlers';

/** Build a request with the headers the real client sends for state-changing calls. */
function post(path: string, json?: unknown, over: Partial<MockRequest> = {}): MockRequest {
  return {
    method: 'POST',
    path,
    headers: { 'x-hubble': '1', 'content-type': 'application/json' },
    json,
    bodyBytes: 0,
    ...over,
  };
}
const get = (path: string): MockRequest => ({ method: 'GET', path, headers: {}, bodyBytes: 0 });

describe('mock API', () => {
  beforeEach(resetMock);

  it('serves the read-only endpoints', () => {
    for (const p of ['/api/v1/status', '/api/v1/ota/status', '/api/v1/diagnostics']) {
      expect(handleMock(get(p)).status).toBe(200);
    }
    expect(handleMock(get('/api/v1/logs')).text).toContain('HubBLE');
  });

  it('returns the standard error body for unknown routes', () => {
    const r = handleMock(get('/api/v1/nope'));
    expect(r.status).toBe(404);
    expect(r.json).toEqual({ error: { code: 'not_found', message: 'No route GET /api/v1/nope' } });
  });

  it('rejects state-changing calls without the CSRF header or JSON type', () => {
    expect(handleMock(post('/api/v1/system/reboot', {}, { headers: {} })).status).toBe(403);
    const wrongType = { 'x-hubble': '1', 'content-type': 'text/plain' };
    expect(handleMock(post('/api/v1/system/reboot', {}, { headers: wrongType })).status).toBe(415);
    expect(handleMock(post('/api/v1/system/reboot', {})).status).toBe(200);
  });

  it('validates and applies the Wi-Fi password', () => {
    expect(handleMock(post('/api/v1/settings/wifi-password', { password: 'short' })).status).toBe(
      400,
    );
    expect(
      handleMock(post('/api/v1/settings/wifi-password', { password: 'hubble-setup' })).status,
    ).toBe(400);
    expect(
      handleMock(post('/api/v1/settings/wifi-password', { password: 'a-good-password' })).status,
    ).toBe(200);
    expect(handleMock(get('/api/v1/status')).json).toMatchObject({ setup_required: false });
  });

  it('accepts a plausible upload and reports the new version', () => {
    const headers = { 'x-hubble': '1', 'content-type': 'application/octet-stream' };
    const ok = handleMock(post('/api/v1/ota/upload', undefined, { headers, bodyBytes: 200_000 }));
    expect(ok.json).toMatchObject({ ok: true, version: MOCK_UPLOAD_VERSION });
    expect(handleMock(get('/api/v1/status')).json).toMatchObject({ version: MOCK_UPLOAD_VERSION });
  });

  it('rejects implausible uploads like the firmware does', () => {
    const headers = { 'x-hubble': '1', 'content-type': 'application/octet-stream' };
    expect(
      handleMock(post('/api/v1/ota/upload', undefined, { headers, bodyBytes: 10 })).status,
    ).toBe(400);
    const big = { headers, bodyBytes: MOCK_SLOT_BYTES + 1 };
    expect(handleMock(post('/api/v1/ota/upload', undefined, big)).status).toBe(413);
    expect(handleMock(post('/api/v1/ota/upload', undefined, { bodyBytes: 5000 })).status).toBe(415);
  });
});
