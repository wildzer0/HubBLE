/**
 * @file capture.spec.ts
 * @description Generates the screenshots used by docs/user-guide.md from the mock API (`npm run screenshots`).
 * Not part of the normal e2e run: it writes files under docs/img/.
 */
import { test } from '@playwright/test';
import path from 'node:path';
import { mockOta, mockPairing, resetMock, saveSta } from '../e2e/helpers';

/** Output folder of the screenshots. */
const OUT = path.resolve(import.meta.dirname, '../../../docs/img');

/**
 * Save a screenshot of the page body.
 *
 * @param page Playwright page.
 * @param name File name without extension.
 */
async function shot(page: import('@playwright/test').Page, name: string): Promise<void> {
  await page.screenshot({ path: path.join(OUT, `${name}.png`), fullPage: true });
}

test.use({ colorScheme: 'light' });

test('capture the user guide screenshots', async ({ page, request }) => {
  // First run: wizard, password, QR sticker.
  await resetMock(request, false);
  await page.goto('/#/setup');
  await page.getByText('Step 1 of 3').waitFor();
  await shot(page, 'setup-password');
  await page.getByLabel('New password').fill('a-good-password');
  await page.getByRole('button', { name: 'Change password' }).click();
  await page.getByTestId('sticker').waitFor();
  await shot(page, 'setup-sticker');

  // Dashboard with two devices.
  await resetMock(request, true);
  await page.goto('/#/dashboard');
  await page.getByTestId('device-card').first().waitFor();
  await shot(page, 'dashboard');

  // Pairing: passkey (keyboard) and quarantine challenge (mouse).
  await page.goto('/#/pair');
  await page.getByRole('button', { name: 'Scan for devices' }).click();
  await page.getByTestId('scan-result').first().waitFor();
  await shot(page, 'pair-scan');
  await page.getByRole('button', { name: 'Pair with Mock Keyboard' }).click();
  await page.getByTestId('passkey').waitFor();
  await shot(page, 'pair-passkey');
  await mockPairing(request, 'fail');
  await page.getByRole('button', { name: 'Scan for devices' }).click();
  await page.getByRole('button', { name: 'Pair with Mock Mouse' }).click();
  await page.getByTestId('challenge').waitFor();
  await mockPairing(request, 'advance');
  await mockPairing(request, 'advance');
  await page.getByText('2 of 6').waitFor();
  await shot(page, 'pair-challenge');
  await page.getByRole('button', { name: 'Cancel' }).click();

  // Device detail, input tester, settings.
  await resetMock(request, true);
  await page.goto('/#/device/c0ffee000001');
  await page.getByTestId('interval').waitFor();
  await shot(page, 'device-detail');
  await page.goto('/#/tester');
  await page.getByRole('button', { name: 'Start' }).click();
  await page.getByTestId('events').getByText('LShift + A').first().waitFor({ timeout: 5000 });
  await shot(page, 'tester');
  await page.goto('/#/settings');
  await page.getByLabel('Require physical confirmation').waitFor();
  await shot(page, 'settings');
  await page.getByRole('button', { name: 'Search networks' }).click();
  await page.getByTestId('network').first().waitFor();
  await page.getByTestId('wifi-picker').screenshot({ path: path.join(OUT, 'settings-wifi.png') });

  // Update: an update found on GitHub.
  await saveSta(request);
  await mockOta(request, 'available');
  await page.goto('/#/update');
  await page.getByRole('button', { name: 'Check for updates' }).click();
  await page.getByTestId('release').waitFor();
  await shot(page, 'update');

  // System.
  await page.goto('/#/system');
  await page.getByTestId('heap').waitFor();
  await shot(page, 'system');
});
