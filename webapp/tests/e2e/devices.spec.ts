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

test('the compatibility report is shown before anything is shared and can be downloaded', async ({
  page,
}) => {
  await page.goto('/#/device/c0ffee000001');
  const card = page.getByTestId('report-card');
  await card.getByRole('button', { name: 'Prepare a report' }).click();
  const json = page.getByTestId('report-json');
  await expect(json).toContainText('hubble-device-report');
  await expect(json).toContainText('report_map_hex');
  await expect(json).not.toContainText('c0ffee000001'); // the device id (its address) is never part of it
  const download = page.waitForEvent('download');
  await card.getByRole('button', { name: 'Download the report' }).click();
  expect((await download).suggestedFilename()).toMatch(/^hubble-report-.*\.json$/);
});

test('input lock: lock from the dashboard, banner, unlock', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('locked-banner')).toHaveCount(0);
  await page.getByRole('button', { name: 'Lock input', exact: true }).click();
  await expect(page.getByTestId('locked-banner')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Lock input', exact: true })).toHaveCount(0);
  await page.getByRole('button', { name: 'Unlock input' }).click();
  await expect(page.getByTestId('locked-banner')).toHaveCount(0);
});

test('phone touchpad: off by default, switch on for a while, switch off', async ({ page }) => {
  await page.goto('/#/remote');
  await expect(page.getByTestId('touchpad')).toHaveCount(0);
  await page.getByTestId('remote-enable').click();
  await expect(page.getByTestId('touchpad')).toBeVisible();
  await expect(page.getByTestId('remote-timer')).toContainText('On');
  await page.getByTestId('remote-stop').click();
  await expect(page.getByTestId('touchpad')).toHaveCount(0);
});

test('phone touchpad is refused while the input is locked', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Lock input', exact: true }).click();
  await expect(page.getByTestId('locked-banner')).toBeVisible();
  await page.goto('/#/remote');
  await expect(page.getByTestId('remote-locked')).toBeVisible();
  await expect(page.getByTestId('remote-enable')).toHaveCount(0);
});

test('phone keyboard: sticky modifiers arm, lock and release', async ({ page }) => {
  await page.goto('/#/remote');
  await page.getByTestId('remote-enable').click();
  await page.getByRole('tab', { name: 'Keyboard' }).click();
  const ctrl = page.getByRole('button', { name: /^Ctrl/ });
  await expect(ctrl).toHaveAttribute('data-state', 'off');
  await ctrl.click();
  await expect(ctrl).toHaveAttribute('data-state', 'once');
  await expect(page.getByRole('status').filter({ hasText: 'Next key: Ctrl' })).toBeVisible();
  await ctrl.click();
  await expect(ctrl).toHaveAttribute('data-state', 'locked');
  await ctrl.click();
  await expect(ctrl).toHaveAttribute('data-state', 'off');
});

test('phone touchpad: the speed setting is remembered', async ({ page }) => {
  await page.goto('/#/remote');
  await page.getByTestId('remote-enable').click();
  await page.getByText('Settings').click();
  await page.getByTestId('remote-speed').fill('5');
  await page.reload();
  await page.getByText('Settings').click();
  await expect(page.getByTestId('remote-speed')).toHaveValue('5');
});

test('phone touchpad: the page cannot scroll or be pulled to refresh while the pad is used', async ({
  page,
}) => {
  await page.goto('/#/remote');
  await page.getByTestId('remote-enable').click();
  await expect(page.getByTestId('touchpad')).toBeVisible();
  await expect(page.locator('html')).toHaveClass(/remote-lock/);
  const overscroll = await page.evaluate(
    () => getComputedStyle(document.documentElement).overscrollBehaviorY,
  );
  expect(overscroll).toBe('none');
  const touchAction = await page
    .getByTestId('touchpad')
    .evaluate((el) => getComputedStyle(el).touchAction);
  expect(touchAction).toBe('none');
  // A touch move on the pad is cancelled, so the browser cannot take it for a page scroll.
  const cancelled = await page.getByTestId('touchpad').evaluate((el) => {
    const ev = new Event('touchmove', { bubbles: true, cancelable: true });
    el.dispatchEvent(ev);
    return ev.defaultPrevented;
  });
  expect(cancelled).toBe(true);
  // Leaving the page releases the lock.
  await page.getByRole('tab', { name: 'Keyboard' }).click();
  await expect(page.locator('html')).not.toHaveClass(/remote-lock/);
});
