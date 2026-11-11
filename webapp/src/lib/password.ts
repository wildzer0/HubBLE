/**
 * @file password.ts
 * @description Wi-Fi password rules; must match `settings_password_valid()` in the firmware
 * (10-63 printable ASCII characters, not the documented default password).
 */

/** Documented default setup password (CLAUDE.md §3.5). */
export const DEFAULT_PASSWORD = 'hubble-setup';
/** Minimum length of a user-chosen password. */
export const PASSWORD_MIN_LEN = 10;
/** Maximum length of a WPA2 passphrase. */
export const PASSWORD_MAX_LEN = 63;

/** Why a password was rejected. */
export type PasswordProblem = 'too_short' | 'too_long' | 'bad_chars' | 'is_default';

/**
 * Check a candidate Wi-Fi password.
 *
 * @param pw Candidate password.
 * @returns The first problem found, or null if the password is acceptable.
 */
export function passwordProblem(pw: string): PasswordProblem | null {
  if (pw.length < PASSWORD_MIN_LEN) return 'too_short';
  if (pw.length > PASSWORD_MAX_LEN) return 'too_long';
  if (!/^[\x20-\x7e]+$/.test(pw)) return 'bad_chars';
  if (pw === DEFAULT_PASSWORD) return 'is_default';
  return null;
}
