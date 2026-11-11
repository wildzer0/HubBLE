import { describe, expect, it } from 'vitest';
import { formatBytes, formatDuration } from '../../src/lib/format';

describe('format', () => {
  it('formats bytes', () => {
    expect(formatBytes(2048)).toBe('2.0 KiB');
    expect(formatBytes(3 * 1024 * 1024)).toBe('3.0 MiB');
  });
  it('formats durations without leading zero units', () => {
    expect(formatDuration(5)).toBe('5s');
    expect(formatDuration(65)).toBe('1m 5s');
    expect(formatDuration(90061)).toBe('1d 1h 1m 1s');
  });
});
