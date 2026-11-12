import { expect, test } from '@playwright/test';
import { resetMock } from './helpers';

test.beforeEach(({ request }) => resetMock(request));

test('shows the dongle status from the mock API', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'HubBLE' })).toBeVisible();
  await expect(page.getByTestId('version')).toHaveText('0.2.0-mock');
  await expect(page.getByText('Switches off in 180 s')).toBeVisible();
});

test('switches language to Italian', async ({ page }) => {
  await page.goto('/#/settings');
  await page.getByRole('combobox', { name: 'Language' }).selectOption('it');
  await expect(page.getByRole('heading', { name: 'Impostazioni' })).toBeVisible();
  await page.getByRole('link', { name: 'Dispositivi' }).click();
  await expect(page.getByRole('heading', { name: 'Dispositivi', level: 2 })).toBeVisible();
});

test('navigation and live connection', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText('Live connection')).toBeVisible();
  await page.getByRole('link', { name: 'System' }).click();
  await expect(page.getByRole('heading', { name: 'System', level: 2 })).toBeVisible();
  await page.getByRole('link', { name: 'Update' }).click();
  await expect(page).toHaveURL(/#\/update$/);
});
