import { describe, expect, it } from 'vitest';
import { handleMock, MOCK_STATUS } from '../../src/mocks/handlers';

describe('mock API', () => {
  it('serves /api/v1/status', () => {
    expect(handleMock('GET', '/api/v1/status')).toEqual({ status: 200, body: MOCK_STATUS });
  });
  it('returns the standard error body for unknown routes', () => {
    const r = handleMock('GET', '/api/v1/nope');
    expect(r.status).toBe(404);
    expect(r.body).toEqual({ error: { code: 'not_found', message: 'No route GET /api/v1/nope' } });
  });
});
