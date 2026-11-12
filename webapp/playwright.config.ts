/**
 * @file playwright.config.ts
 * @description End-to-end tests run against the Vite dev server with the mock
 * API, using a phone-sized viewport (the phone is the primary client).
 */
import { defineConfig, devices } from '@playwright/test';

export default defineConfig({
  testDir: 'tests/e2e',
  // The mock API keeps its state in the dev server, so tests must not run in parallel.
  workers: 1,
  forbidOnly: !!process.env['CI'],
  reporter: process.env['CI'] ? 'github' : 'list',
  use: { baseURL: 'http://localhost:5173', trace: 'on-first-retry' },
  projects: [{ name: 'mobile', use: { ...devices['Pixel 7'] } }],
  webServer: {
    command: 'npm run dev -- --port 5173 --strictPort',
    url: 'http://localhost:5173',
    reuseExistingServer: !process.env['CI'],
  },
});
