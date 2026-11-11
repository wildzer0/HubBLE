import { expect, test } from '@playwright/test';

test('update page shows both slots', async ({ page }) => {
  await page.goto('/#/update');
  await expect(page.getByTestId('running-version')).toContainText('0.2.0-mock');
  await expect(page.getByText('0.1.1')).toBeVisible();
});

test('uploading a firmware shows the installed version after the dongle returns', async ({
  page,
}) => {
  await page.goto('/#/update');
  await page.getByLabel('Firmware file (.bin)').setInputFiles({
    name: 'hubble.bin',
    mimeType: 'application/octet-stream',
    buffer: Buffer.alloc(200_000, 1),
  });
  await page.getByRole('button', { name: 'Install update' }).click();
  await expect(page.getByText(/installed\. The dongle is restarting/)).toBeVisible();
  await expect(page.getByText(/back online, running 0\.2\.1-rc\.1-mock/)).toBeVisible({
    timeout: 10_000,
  });
});

test('a rejected image shows an error', async ({ page }) => {
  await page.goto('/#/update');
  await page.getByLabel('Firmware file (.bin)').setInputFiles({
    name: 'tiny.bin',
    mimeType: 'application/octet-stream',
    buffer: Buffer.alloc(10, 1),
  });
  await page.getByRole('button', { name: 'Install update' }).click();
  await expect(page.getByRole('alert')).toContainText('not a valid ESP32-S3 image');
});
