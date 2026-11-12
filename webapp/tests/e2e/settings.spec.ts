import { expect, test } from '@playwright/test';
import { resetMock } from './helpers';

test.beforeEach(({ request }) => resetMock(request));

test('settings validate, save and persist', async ({ page }) => {
  await page.goto('/#/settings');
  const box = page.getByLabel('Require physical confirmation');
  await expect(box).toBeChecked();
  await box.uncheck();
  await page.getByLabel('On after power-up (s)').fill('10');
  await expect(page.getByRole('button', { name: 'Save settings' })).toBeDisabled();
  await page.getByLabel('On after power-up (s)').fill('600');
  await page.getByRole('button', { name: 'Save settings' }).click();
  await expect(page.getByText('Settings saved.')).toBeVisible();

  await page.reload();
  await expect(page.getByLabel('Require physical confirmation')).not.toBeChecked();
  await expect(page.getByLabel('On after power-up (s)')).toHaveValue('600');
});

test('backup downloads settings and restore applies them', async ({ page }) => {
  await page.goto('/#/system');
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Download backup' }).click();
  expect((await download).suggestedFilename()).toBe('hubble-backup.json');

  const good = {
    format: 'hubble-backup',
    version: 1,
    settings: { require_confirmation: false, ap_boot_window_s: 120, ap_idle_timeout_s: 90 },
  };
  await page.getByLabel('Restore from file').setInputFiles({
    name: 'b.json',
    mimeType: 'application/json',
    buffer: Buffer.from(JSON.stringify(good)),
  });
  await expect(page.getByText('Settings restored.')).toBeVisible();

  await page.getByLabel('Restore from file').setInputFiles({
    name: 'bad.json',
    mimeType: 'application/json',
    buffer: Buffer.from('{"hello":1}'),
  });
  await expect(page.getByText('This is not a valid HubBLE backup file.')).toBeVisible();

  await page.goto('/#/settings');
  await expect(page.getByLabel('On after power-up (s)')).toHaveValue('120');
});

test('factory reset needs a confirmation and restarts the wizard', async ({ page }) => {
  await page.goto('/#/system');
  await page.getByRole('button', { name: 'Factory reset', exact: true }).click();
  await page.getByRole('button', { name: /Confirm: Factory reset/ }).click();
  await expect(page.getByText('Command sent.')).toBeVisible();
  await page.goto('/#/dashboard');
  await expect(page).toHaveURL(/#\/setup$/);
});
