/** Report builders shared by the tests. */
import type { Report } from '../src/validate';

/** A complete, valid report; @p over replaces top-level or device fields. */
export function sample(
  device: Partial<Report['device']> = {},
  extra: Partial<Report> = {},
): Report {
  return {
    format: 'hubble-device-report',
    version: 1,
    firmware: { version: '1.1.7', board: 's3_zero' },
    device: {
      kind: 'keyboard',
      adv_name: 'OBLIO',
      appearance: 961,
      manufacturer: 'Yunzii',
      model: 'B68',
      fw_revision: '1.0',
      pnp: { vendor_id_source: 2, vendor_id: 0x05ac, product_id: 0x820a, version: 1 },
      report_map_hex: '05010906a101',
      parse: {
        result: 0,
        fields: 12,
        boot_fallback: false,
        primary_page: 1,
        primary_usage: 6,
        has_pointer: true,
      },
      security: 'green',
      connection: { interval_ms: 7.5, latency: 0, timeout_ms: 2000 },
      battery_reported: true,
      latency_us: { count: 100, avg: 800, p95: 1500, max: 1900 },
      ...device,
    },
    ...extra,
  };
}

/** POST request carrying @p body (an object is stringified). */
export function post(
  body: unknown,
  ip = '203.0.113.7',
  headers: Record<string, string> = {},
): Request {
  return new Request('https://reports.example/v1/reports', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'CF-Connecting-IP': ip, ...headers },
    body: typeof body === 'string' ? body : JSON.stringify(body),
  });
}

/** GET request with an optional bearer token. */
export function get(path: string, token?: string): Request {
  return new Request(`https://reports.example${path}`, {
    method: 'GET',
    ...(token ? { headers: { Authorization: `Bearer ${token}` } } : {}),
  });
}
