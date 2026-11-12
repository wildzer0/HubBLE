import { expect, test } from '@playwright/test';
import { resetMock } from './helpers';

test('first run: forced redirect to the wizard, password, QR sticker', async ({
  page,
  request,
}) => {
  await resetMock(request, false);
  await page.goto('/#/dashboard');
  await expect(page).toHaveURL(/#\/setup$/);
  await expect(page.getByText('Step 1 of 3')).toBeVisible();

  await page.getByLabel('New password').fill('hubble-setup');
  await expect(page.getByRole('button', { name: 'Change password' })).toBeDisabled();
  await page.getByLabel('New password').fill('a-good-password');
  await page.getByRole('button', { name: 'Change password' }).click();

  await expect(page.getByText('Step 2 of 3')).toBeVisible();
  const sticker = page.getByTestId('sticker');
  await expect(sticker.locator('svg')).toHaveCount(2);
  await expect(sticker).toContainText('HubBLE-A1B2');
  await expect(page.getByRole('link', { name: 'Pair the first device' })).toBeVisible();

  await page.getByRole('link', { name: 'Skip for now' }).click();
  await expect(page).toHaveURL(/#\/dashboard$/);
  await expect(page.getByRole('heading', { name: 'Dongle status' })).toBeVisible();
});

test('after setup the dashboard is no longer redirected', async ({ page, request }) => {
  await resetMock(request, true);
  await page.goto('/#/dashboard');
  await expect(page).toHaveURL(/#\/dashboard$/);
});
