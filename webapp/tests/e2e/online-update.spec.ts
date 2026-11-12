import { expect, test } from '@playwright/test';
import { mockOta, resetMock, saveSta } from './helpers';

test.beforeEach(({ request }) => resetMock(request));

test('without a saved network the page asks for one and hides the check button', async ({
  page,
}) => {
  await page.goto('/#/update');
  await expect(page.getByText(/save a Wi-Fi network with internet access/)).toBeVisible();
  await expect(page.getByRole('button', { name: 'Check for updates' })).toHaveCount(0);
});

test('Wi-Fi credentials are write-only: saved, never shown again, can be forgotten', async ({
  page,
}) => {
  await page.goto('/#/settings');
  await expect(page.getByTestId('sta-state')).toHaveText('No network saved.');
  await page.getByLabel('Network name').fill('HomeNet');
  await page.getByLabel(/Password \(empty/).fill('secret-pass');
  await page.getByRole('button', { name: 'Save network' }).click();
  await expect(page.getByText('Network saved.')).toBeVisible();
  await expect(page.getByTestId('sta-state')).toHaveText('A network is saved.');
  await expect(page.getByLabel('Network name')).toHaveValue('');
  await expect(page.getByLabel(/Password \(empty/)).toHaveValue('');
  await page.getByRole('button', { name: 'Forget network' }).click();
  await expect(page.getByTestId('sta-state')).toHaveText('No network saved.');
});

test('check, release details and install of a newer signed version', async ({ page, request }) => {
  await saveSta(request);
  await page.goto('/#/update');
  await page.getByRole('button', { name: 'Check for updates' }).click();
  const rel = page.getByTestId('release');
  await expect(rel).toContainText('Version 0.7.0 is available');
  await expect(rel).toContainText('signed');
  await expect(rel.getByRole('link', { name: 'Release notes' })).toHaveAttribute(
    'href',
    'https://example.org/releases/notes',
  );
  await rel.getByRole('button', { name: 'Install 0.7.0' }).click();
  await expect(page.getByText('Installed. The dongle is restarting…')).toBeVisible();
});

test('the pre-release channel offers the release candidate', async ({ page, request }) => {
  await saveSta(request);
  await page.goto('/#/update');
  await page.getByLabel('Channel').selectOption('prerelease');
  await page.getByRole('button', { name: 'Check for updates' }).click();
  await expect(page.getByTestId('release')).toContainText('0.8.0-rc.1');
});

test('an older release cannot be installed without the explicit confirmation', async ({
  page,
  request,
}) => {
  await saveSta(request);
  await mockOta(request, 'downgrade');
  await page.goto('/#/update');
  await page.getByRole('button', { name: 'Check for updates' }).click();
  const install = page.getByRole('button', { name: 'Install 0.1.0' });
  await expect(page.getByText(/OLDER version/)).toBeVisible();
  await expect(install).toBeDisabled();
  await page.getByLabel(/I want to install version 0.1.0 anyway/).check();
  await expect(install).toBeEnabled();
  await install.click();
  await expect(page.getByText('Installed. The dongle is restarting…')).toBeVisible();
});

test('physical confirmation, then download progress, then installed', async ({ page, request }) => {
  await saveSta(request);
  await mockOta(request, 'confirm');
  await page.goto('/#/update');
  await page.getByRole('button', { name: 'Check for updates' }).click();
  await page.getByRole('button', { name: 'Install 0.7.0' }).click();
  await expect(page.getByText(/Hold both Shift keys for 3 seconds/).first()).toBeVisible();
  await mockOta(request, undefined, true);
  await expect(page.getByRole('progressbar')).toBeVisible();
  await expect(page.getByText('Uploading… 50%')).toBeVisible();
  await mockOta(request, undefined, true);
  await expect(page.getByText('Installed. The dongle is restarting…')).toBeVisible();
});

test('errors and the up-to-date case are explained', async ({ page, request }) => {
  await saveSta(request);
  await mockOta(request, 'wifi_failed');
  await page.goto('/#/update');
  await page.getByRole('button', { name: 'Check for updates' }).click();
  await expect(page.getByText(/Could not join the Wi-Fi network/)).toBeVisible();
  await mockOta(request, 'up_to_date');
  await page.getByRole('button', { name: 'Check for updates' }).click();
  await expect(page.getByText('You are running the newest version.')).toBeVisible();
});
