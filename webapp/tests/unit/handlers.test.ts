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

describe('mock API: devices, pairing, settings', () => {
  beforeEach(() => {
    resetMock({ setupDone: true });
  });
  const send = (method: string, path: string, json?: unknown): ReturnType<typeof handleMock> =>
    handleMock({
      method,
      path,
      headers: { 'x-hubble': '1', 'content-type': 'application/json' },
      json,
      bodyBytes: 0,
    });

  it('lists, renames and forgets devices', () => {
    expect(handleMock(get('/api/v1/devices')).json).toMatchObject({ max: 4 });
    expect(send('PATCH', '/api/v1/devices/c0ffee000001', { name: 'X' }).status).toBe(200);
    expect(send('PATCH', '/api/v1/devices/c0ffee000001', { name: '' }).status).toBe(400);
    expect(handleMock(get('/api/v1/devices/c0ffee000001/report-map')).status).toBe(200);
    expect(send('DELETE', '/api/v1/devices/c0ffee000001').status).toBe(200);
    expect(handleMock(get('/api/v1/devices/c0ffee000001')).status).toBe(404);
  });

  it('runs the keyboard pairing path with confirmation', () => {
    send('POST', '/api/v1/pairing/scan');
    send('POST', '/api/v1/pairing/start', { address: 'aabbccddee01' });
    expect(handleMock(get('/api/v1/pairing/scan')).json).toMatchObject({ state: 'pairing' });
    send('POST', '/api/v1/mock/pairing', { action: 'advance' });
    expect(handleMock(get('/api/v1/pairing/scan')).json).toMatchObject({ state: 'confirm' });
    send('POST', '/api/v1/mock/pairing', { action: 'advance' });
    expect(handleMock(get('/api/v1/pairing/scan')).json).toMatchObject({ state: 'bonded' });
  });

  it('locks pairing after three failures with 429', () => {
    for (let i = 0; i < 3; i++) {
      send('POST', '/api/v1/pairing/scan');
      send('POST', '/api/v1/pairing/start', { address: 'aabbccddee02' });
      send('POST', '/api/v1/mock/pairing', { action: 'fail' });
    }
    expect(send('POST', '/api/v1/pairing/scan').status).toBe(429);
  });

  it('rejects out-of-range settings and bad backups', () => {
    expect(send('PUT', '/api/v1/settings', { ap_boot_window_s: 5 }).status).toBe(400);
    expect(send('PUT', '/api/v1/settings', { ap_boot_window_s: 60 }).status).toBe(200);
    expect(send('POST', '/api/v1/restore', { format: 'x' }).status).toBe(400);
    const b = handleMock(get('/api/v1/backup')).json;
    expect(send('POST', '/api/v1/restore', b).status).toBe(200);
  });

  it('factory reset restores the fresh state', () => {
    send('DELETE', '/api/v1/devices/c0ffee000001');
    send('POST', '/api/v1/system/factory-reset', {});
    expect(handleMock(get('/api/v1/status')).json).toMatchObject({ setup_required: true });
    expect(handleMock(get('/api/v1/devices')).json).toMatchObject({ devices: [{}, {}] });
  });
});

describe('mock API: online updates', () => {
  beforeEach(() => {
    resetMock({ setupDone: true });
  });
  const send = (method: string, path: string, json?: unknown): ReturnType<typeof handleMock> =>
    handleMock({
      method,
      path,
      headers: { 'x-hubble': '1', 'content-type': 'application/json' },
      json,
      bodyBytes: 0,
    });

  it('needs saved Wi-Fi credentials and never returns them', () => {
    expect(send('POST', '/api/v1/ota/check', {}).status).toBe(409);
    expect(send('PUT', '/api/v1/settings/sta', { ssid: 'Home', password: 'short' }).status).toBe(
      400,
    );
    const saved = send('PUT', '/api/v1/settings/sta', { ssid: 'Home', password: 'longenough' });
    expect(saved.status).toBe(200);
    expect(JSON.stringify(saved.json)).not.toContain('longenough');
    expect(JSON.stringify(saved.json)).not.toContain('Home');
    expect(send('POST', '/api/v1/ota/check', { channel: 'stable' }).status).toBe(200);
  });

  it('installs the found release and refuses a downgrade without confirmation', () => {
    send('PUT', '/api/v1/settings/sta', { ssid: 'Home', password: '' });
    send('POST', '/api/v1/mock/ota', { scenario: 'downgrade' });
    send('POST', '/api/v1/ota/check', { channel: 'stable' });
    expect(send('POST', '/api/v1/ota/install', {}).status).toBe(409);
    expect(send('POST', '/api/v1/ota/install', { allow_downgrade: true }).status).toBe(200);
    expect(handleMock(get('/api/v1/status')).json).toMatchObject({ version: '0.1.0' });
  });

  it('refuses to install before a check', () => {
    expect(send('POST', '/api/v1/ota/install', {}).status).toBe(409);
  });
});
