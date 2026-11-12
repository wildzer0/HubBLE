import { expect, test } from '@playwright/test';
import { resetMock } from './helpers';

test.beforeEach(({ request }) => resetMock(request));

test('dashboard shows a card per device with badge, battery and latency', async ({ page }) => {
  await page.goto('/');
  const cards = page.getByTestId('device-card');
  await expect(cards).toHaveCount(2);
  await expect(cards.first()).toContainText('YUNZII B68');
  await expect(cards.first()).toContainText('Verified (passkey)');
  await expect(cards.first()).toContainText('87 %');
  await expect(cards.first()).toContainText('0.8 ms average');
  await expect(cards.nth(1)).toContainText('Verified (challenge)');
});

test('device detail: rename, stats, report map export, forget', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('link', { name: 'YUNZII B68' }).click();
  await expect(page.getByTestId('interval')).toHaveText('7.5 ms');
  await expect(page.getByTestId('lat-avg')).toHaveText('0.78 ms');

  await page.getByLabel('Name').fill('Desk keyboard');
  await page.getByRole('button', { name: 'Rename' }).click();
  await expect(page.getByText('Name saved.')).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Desk keyboard', level: 2 })).toBeVisible();

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Export report map' }).click();
  expect((await download).suggestedFilename()).toBe('report-map-c0ffee000001.hex');
  await expect(page.getByText(/Report map exported/)).toBeVisible();

  await page.getByRole('button', { name: 'Forget device', exact: true }).click();
  await page.getByRole('button', { name: /Confirm: Forget device/ }).click();
  await expect(page).toHaveURL(/#\/dashboard$/);
  await expect(page.getByTestId('device-card')).toHaveCount(1);
});

test('input tester streams events only while enabled', async ({ page }) => {
  await page.goto('/#/tester');
  await expect(page.getByTestId('events').locator('li')).toHaveCount(0);
  await page.getByRole('button', { name: 'Start' }).click();
  await expect(page.getByTestId('events')).toContainText('LShift + A', { timeout: 5000 });
  await page.getByRole('button', { name: 'Stop' }).click();
  await expect(page.getByTestId('events').locator('li')).toHaveCount(0);
});
