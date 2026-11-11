#!/usr/bin/env node
// Enforce the web app size budget (CLAUDE.md §4.3): total gzipped size of all
// files in <dist-dir> must not exceed <limit-KB>.
// Usage: check-webapp-size.mjs <dist-dir> <limit-KB>
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { gzipSync } from 'node:zlib';

const [dir, limitKb] = process.argv.slice(2);
if (!dir || !limitKb) {
  console.error('usage: check-webapp-size.mjs <dist-dir> <limit-KB>');
  process.exit(2);
}

/** Recursively list files below `d`. */
function walk(d) {
  return readdirSync(d).flatMap((n) => {
    const p = join(d, n);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

let total = 0;
for (const f of walk(dir)) {
  const size = gzipSync(readFileSync(f), { level: 9 }).length;
  total += size;
  console.log(`${String(size).padStart(8)}  ${f}`);
}
const limit = Number(limitKb) * 1024;
console.log(`total gzipped: ${total} bytes (limit ${limit})`);
if (total > limit) {
  console.error('FAIL: web app exceeds the size budget');
  process.exit(1);
}
