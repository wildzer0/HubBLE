/**
 * @file backup.ts
 * @description Validation of a settings backup file before it is sent to the dongle (the firmware
 * validates again; this gives the user an immediate, translated error).
 */
import type { Backup } from '../api/types';

/**
 * Parse and validate backup file text.
 *
 * @param text File contents.
 * @returns The backup, or null if the file is not a HubBLE backup.
 */
export function parseBackup(text: string): Backup | null {
  let v: unknown;
  try {
    v = JSON.parse(text);
  } catch {
    return null;
  }
  const b = v as Partial<Backup> | null;
  if (b?.format !== 'hubble-backup' || b.version !== 1) return null;
  const s = b.settings as Partial<Backup['settings']> | undefined;
  if (
    typeof s?.require_confirmation !== 'boolean' ||
    typeof s.ap_boot_window_s !== 'number' ||
    typeof s.ap_idle_timeout_s !== 'number'
  ) {
    return null;
  }
  return b as Backup;
}
