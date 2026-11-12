import { expect, test } from '@playwright/test';
import { mockPairing, resetMock } from './helpers';

test.beforeEach(({ request }) => resetMock(request));

test('keyboard: passkey screen, physical confirmation, green result', async ({ page, request }) => {
  await page.goto('/#/pair');
  await page.getByRole('button', { name: 'Scan for devices' }).click();
  await page.getByRole('button', { name: 'Pair with Mock Keyboard' }).click();
  await expect(page.getByTestId('passkey')).toHaveText('123456');
  await expect(page.getByRole('timer')).toBeVisible();

  await mockPairing(request, 'advance'); // passkey typed
  await expect(page.getByTestId('confirm')).toBeVisible(); // a keyboard is already paired
  await mockPairing(request, 'advance'); // both Shifts held
  await expect(page.getByTestId('result-ok')).toContainText('Mock Keyboard is paired.');
  await expect(page.getByTestId('result-ok')).toContainText('Verified (passkey)');
});

test('mouse: quarantine challenge with live progress, yellow result', async ({ page, request }) => {
  await request.put('/api/v1/settings', {
    headers: { 'X-HubBLE': '1', 'Content-Type': 'application/json' },
    data: { require_confirmation: false },
  });
  await page.goto('/#/pair');
  await page.getByRole('button', { name: 'Scan for devices' }).click();
  await page.getByRole('button', { name: 'Pair with Mock Mouse' }).click();
  await expect(page.getByText(/connected but muted/)).toBeVisible();
  await expect(page.getByTestId('challenge').locator('li')).toHaveCount(6);
  await mockPairing(request, 'advance');
  await expect(page.getByText('1 of 6')).toBeVisible();
  for (let i = 0; i < 5; i++) await mockPairing(request, 'advance');
  await expect(page.getByTestId('result-ok')).toContainText('Verified (challenge)');
});

test('legacy-only device is rejected unless the override is ticked', async ({ page, request }) => {
  await page.goto('/#/pair');
  await page.getByRole('button', { name: 'Scan for devices' }).click();
  await page.getByRole('button', { name: 'Pair with Old Keyboard' }).click();
  await expect(page.getByTestId('result-fail')).toContainText('only supports legacy pairing');

  await page.getByLabel('Allow legacy pairing for this device').check();
  await expect(page.getByText(/Warning: legacy pairing/)).toBeVisible();
  await page.getByRole('button', { name: 'Scan for devices' }).click();
  await page.getByRole('button', { name: 'Pair with Old Keyboard' }).click();
  await expect(page.getByTestId('passkey')).toBeVisible();
  await mockPairing(request, 'advance');
  await mockPairing(request, 'advance');
  await expect(page.getByTestId('result-ok')).toContainText('Legacy pairing');
});

test('three failures lock pairing', async ({ page, request }) => {
  await page.goto('/#/pair');
  for (let i = 0; i < 3; i++) {
    await page.getByRole('button', { name: 'Scan for devices' }).click();
    await page.getByRole('button', { name: 'Pair with Mock Mouse' }).click();
    await expect(page.getByTestId('challenge')).toBeVisible();
    await mockPairing(request, 'fail');
    await expect(page.getByTestId('result-fail')).toContainText('The check failed');
  }
  await expect(page.getByTestId('locked')).toContainText('locked for 60 s');
  await expect(page.getByRole('button', { name: 'Scan for devices' })).toBeDisabled();
});

test('cancelling returns to the start', async ({ page }) => {
  await page.goto('/#/pair');
  await page.getByRole('button', { name: 'Scan for devices' }).click();
  await expect(page.getByText('Scanning for nearby HID devices…')).toBeVisible();
  await page.getByRole('button', { name: 'Cancel' }).click();
  await expect(page.getByRole('button', { name: 'Scan for devices' })).toBeVisible();
});
