/**
 * @file vite.config.ts
 * @description Vite + Vitest configuration. The mock API plugin serves the
 * firmware REST contract during development and Playwright runs.
 */
import preact from '@preact/preset-vite';
import { defineConfig } from 'vitest/config';
import { mockApiPlugin } from './src/mocks/vitePlugin.ts';

export default defineConfig({
  plugins: [preact(), mockApiPlugin()],
  // Relative base: the dongle serves the app from its root, but this also
  // keeps the build usable when opened from any path.
  base: './',
  build: { target: 'es2020', outDir: 'dist', sourcemap: false },
  test: {
    environment: 'jsdom',
    include: ['tests/unit/**/*.test.ts', 'tests/unit/**/*.test.tsx'],
    coverage: {
      provider: 'v8',
      include: ['src/**'],
      exclude: ['src/main.tsx', 'src/mocks/vitePlugin.ts'],
    },
  },
});
