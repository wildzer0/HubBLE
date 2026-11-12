import { expect, test } from '@playwright/test';
import { resetMock } from './helpers';

test.beforeEach(({ request }) => resetMock(request));

test('system page shows diagnostics and live log lines', async ({ page }) => {
  await page.goto('/#/system');
  await expect(page.getByTestId('heap')).toHaveText('138.7 KiB');
  await expect(page.getByTestId('power')).toHaveText('Full speed');
  await expect(page.getByTestId('log')).toContainText('mock: tick', { timeout: 5000 });
});

test('reboot needs a confirmation', async ({ page }) => {
  await page.goto('/#/system');
  const sent = page.waitForRequest(
    (r) => r.url().endsWith('/api/v1/system/reboot') && r.method() === 'POST',
  );
  await page.getByRole('button', { name: 'Reboot', exact: true }).click();
  await page.getByRole('button', { name: /Confirm: Reboot$/ }).click();
  const req = await sent;
  expect(req.headers()['x-hubble']).toBe('1');
  await expect(page.getByText('Command sent.')).toBeVisible();
});

test('password form validates before sending and then succeeds', async ({ page }) => {
  await page.goto('/#/system');
  const save = page.getByRole('button', { name: 'Change password' });
  const field = page.getByLabel('New password');
  await field.fill('short');
  await expect(page.getByText('At least 10 characters are required.')).toBeVisible();
  await expect(save).toBeDisabled();
  await field.fill('hubble-setup');
  await expect(page.getByText('Choose a password different from the default one.')).toBeVisible();
  await field.fill('a-good-password');
  await save.click();
  await expect(page.getByRole('status').filter({ hasText: 'Password changed' })).toBeVisible();
});
