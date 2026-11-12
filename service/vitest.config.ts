import { defineConfig } from 'vitest/config';

// The handler only needs Web-standard APIs (Request, Response, crypto.subtle), all present in Node.
export default defineConfig({ test: { environment: 'node', include: ['test/**/*.test.ts'] } });
