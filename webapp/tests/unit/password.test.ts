import { describe, expect, it } from 'vitest';
import { DEFAULT_PASSWORD, passwordProblem } from '../../src/lib/password';

describe('passwordProblem', () => {
  it('accepts a good password', () => {
    expect(passwordProblem('correct horse')).toBeNull();
    expect(passwordProblem('a'.repeat(63))).toBeNull();
  });
  it('reports each rule', () => {
    expect(passwordProblem('short')).toBe('too_short');
    expect(passwordProblem('a'.repeat(64))).toBe('too_long');
    expect(passwordProblem('café-password-long')).toBe('bad_chars');
    expect(passwordProblem(DEFAULT_PASSWORD)).toBe('is_default');
  });
});
