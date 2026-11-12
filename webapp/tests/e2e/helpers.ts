/**
 * @file helpers.ts
 * @description Shared Playwright helpers for the mock API (state lives in the dev server).
 */
import type { APIRequestContext } from '@playwright/test';

/** HTTP headers of a state-changing call, like the real client sends. */
const HEADERS = { 'X-HubBLE': '1', 'Content-Type': 'application/json' };

/**
 * Reset the mock dongle to its initial state.
 *
 * @param request Playwright request context.
 * @param setupDone False to simulate a brand-new dongle (the wizard is shown).
 */
export async function resetMock(request: APIRequestContext, setupDone = true): Promise<void> {
  await request.post('/api/v1/mock/reset', { headers: HEADERS, data: { setupDone } });
}

/**
 * Do what the user would do on the device being paired (type the passkey, click, hold Shift).
 *
 * @param request Playwright request context.
 * @param action `advance`, `fail` or `timeout`.
 */
export async function mockPairing(
  request: APIRequestContext,
  action: 'advance' | 'fail' | 'timeout',
): Promise<void> {
  await request.post('/api/v1/mock/pairing', { headers: HEADERS, data: { action } });
}

/**
 * Choose what the mocked "check for updates" finds, and optionally advance a waiting install.
 *
 * @param request Playwright request context.
 * @param scenario `available`, `downgrade`, `up_to_date`, `wifi_failed` or `confirm`.
 * @param advance True to move a confirm/download step forward.
 */
export async function mockOta(
  request: APIRequestContext,
  scenario?: 'available' | 'downgrade' | 'up_to_date' | 'wifi_failed' | 'confirm',
  advance = false,
): Promise<void> {
  await request.post('/api/v1/mock/ota', { headers: HEADERS, data: { scenario, advance } });
}

/**
 * Save the Wi-Fi network used for updates (as the Settings page does).
 *
 * @param request Playwright request context.
 */
export async function saveSta(request: APIRequestContext): Promise<void> {
  await request.put('/api/v1/settings/sta', {
    headers: HEADERS,
    data: { ssid: 'HomeNet', password: 'secret-pass' },
  });
}
