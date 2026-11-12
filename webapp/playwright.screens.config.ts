/**
 * @file playwright.screens.config.ts
 * @description Config for `npm run screenshots`: runs only tests/screenshots against the mock API.
 */
import { defineConfig, devices } from '@playwright/test';

export default defineConfig({
  testDir: 'tests/screenshots',
  workers: 1,
  reporter: 'list',
  use: { baseURL: 'http://localhost:5173' },
  projects: [{ name: 'mobile', use: { ...devices['Pixel 7'], deviceScaleFactor: 1 } }],
  webServer: {
    command: 'npm run dev -- --port 5173 --strictPort',
    url: 'http://localhost:5173',
    reuseExistingServer: true,
  },
});
