import { expect, test } from '@playwright/test';

test('shows the dongle status from the mock API', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'HubBLE' })).toBeVisible();
  await expect(page.getByTestId('version')).toHaveText('0.1.0-mock');
});

test('switches language to Italian', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('combobox').selectOption('it');
  await expect(page.getByRole('heading', { name: 'Stato del dongle' })).toBeVisible();
});
