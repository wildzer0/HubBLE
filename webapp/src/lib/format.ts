/**
 * @file format.ts
 * @description Small formatting helpers for the diagnostics views.
 */

/**
 * Format a byte count as KiB/MiB.
 *
 * @param n Number of bytes.
 * @returns Text such as "142.0 KiB".
 */
export function formatBytes(n: number): string {
  if (n >= 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(1)} MiB`;
  return `${(n / 1024).toFixed(1)} KiB`;
}

/**
 * Format a duration in seconds as "1d 2h 3m 4s" (omitting leading zero units).
 *
 * @param s Seconds.
 * @returns Human readable duration.
 */
export function formatDuration(s: number): string {
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = Math.floor(s % 60);
  const parts: string[] = [];
  if (d) parts.push(`${String(d)}d`);
  if (d || h) parts.push(`${String(h)}h`);
  if (d || h || m) parts.push(`${String(m)}m`);
  parts.push(`${String(sec)}s`);
  return parts.join(' ');
}
