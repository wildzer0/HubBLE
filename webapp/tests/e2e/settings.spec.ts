import { expect, test } from '@playwright/test';
import { resetMock } from './helpers';

test.beforeEach(({ request }) => resetMock(request));

test('settings validate, save and persist', async ({ page }) => {
  await page.goto('/#/settings');
  const box = page.getByLabel('Require physical confirmation');
  await expect(box).toBeChecked();
  await box.uncheck();
  await page.getByLabel('On after power-up').fill('10');
  await expect(page.getByRole('button', { name: 'Save settings' })).toBeDisabled();
  await page.getByLabel('On after power-up').fill('600');
  await page.getByRole('button', { name: 'Save settings' }).click();
  await expect(page.getByText('Settings saved.')).toBeVisible();

  await page.reload();
  await expect(page.getByLabel('Require physical confirmation')).not.toBeChecked();
  await expect(page.getByLabel('On after power-up')).toHaveValue('600');
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
  await expect(page.getByLabel('On after power-up')).toHaveValue('120');
});

test('factory reset needs a confirmation and restarts the wizard', async ({ page }) => {
  await page.goto('/#/system');
  await page.getByRole('button', { name: 'Factory reset', exact: true }).click();
  await page.getByRole('button', { name: /Confirm: Factory reset/ }).click();
  await expect(page.getByText('Command sent.')).toBeVisible();
  await page.goto('/#/dashboard');
  await expect(page).toHaveURL(/#\/setup$/);
});

test('power and LED settings validate and persist', async ({ page }) => {
  await page.goto('/#/settings');
  const wake = page.getByLabel('Wake the computer from sleep with the devices');
  await expect(wake).toBeChecked();
  await wake.uncheck();
  await page.getByLabel(/LED brightness/).fill('150');
  await expect(page.getByRole('button', { name: 'Save settings' })).toBeDisabled();
  await page.getByLabel(/LED brightness/).fill('60');
  await page.getByLabel('Hold Ctrl and Alt for').fill('5');
  await page.getByLabel('Show dim green while a device is connected').check();
  await page.getByRole('button', { name: 'Save settings' }).click();
  await expect(page.getByText('Settings saved.')).toBeVisible();

  await page.reload();
  await expect(page.getByLabel('Wake the computer from sleep with the devices')).not.toBeChecked();
  await expect(page.getByLabel(/LED brightness/)).toHaveValue('60');
  await expect(page.getByLabel('Hold Ctrl and Alt for')).toHaveValue('5');
  await expect(page.getByLabel('Show dim green while a device is connected')).toBeChecked();
});

test('the typing filter is off by default and its times appear when it is turned on', async ({
  page,
}) => {
  await page.goto('/#/settings');
  const sw = page.getByLabel('Filter keys and mouse buttons');
  await expect(sw).not.toBeChecked();
  await expect(page.locator('#filter-bounce')).toHaveCount(0);
  await sw.check();
  await expect(page.locator('#filter-bounce')).toHaveValue('50');
  await page.locator('#filter-hold').fill('120');
  await page.getByRole('button', { name: 'Save settings' }).click();
  await expect(page.getByText('Settings saved.')).toBeVisible();
  await page.reload();
  await expect(page.getByLabel('Filter keys and mouse buttons')).toBeChecked();
  await expect(page.locator('#filter-hold')).toHaveValue('120');
});
