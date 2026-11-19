/**
 * @file remoteSettings.ts
 * @description The few preferences of the phone touchpad, kept in this browser only (they say nothing about the dongle).
 */

/** Preferences of the touchpad. */
export interface RemoteSettings {
  /** Pointer speed, 1 (slow) to 5 (fast). */
  speed: number;
  /** Two-finger scrolling moves the content with the fingers (like a phone). */
  naturalScroll: boolean;
  /** The computer is a Mac: shortcuts use Cmd instead of Ctrl. */
  mac: boolean;
}

/** Slowest pointer speed level. */
export const SPEED_MIN = 1;
/** Fastest pointer speed level. */
export const SPEED_MAX = 5;
/** Defaults: a middle speed, phone-style scrolling, a PC. */
export const DEFAULT_REMOTE_SETTINGS: RemoteSettings = {
  speed: 3,
  naturalScroll: true,
  mac: false,
};
/** Storage key. */
const KEY = 'hubble.remote';
/** Host units per pixel for speed level 1; each level adds the same amount (level 3 gives the engine's default). */
const GAIN_PER_LEVEL = 0.8;

/**
 * Pointer gain for a speed level.
 *
 * @param speed Level, 1 to 5.
 * @returns Host units per pixel for a slow finger (a faster finger is accelerated on top of it).
 */
export function gainFor(speed: number): number {
  return Math.min(SPEED_MAX, Math.max(SPEED_MIN, speed)) * GAIN_PER_LEVEL;
}

/**
 * Read the stored preferences; anything missing or invalid falls back to the default, and so does unavailable storage
 * (private windows block it).
 *
 * @returns The preferences.
 */
export function loadRemoteSettings(): RemoteSettings {
  try {
    const raw = localStorage.getItem(KEY);
    if (raw === null) return DEFAULT_REMOTE_SETTINGS;
    const v = JSON.parse(raw) as Partial<RemoteSettings>;
    return {
      speed:
        typeof v.speed === 'number' && v.speed >= SPEED_MIN && v.speed <= SPEED_MAX
          ? v.speed
          : DEFAULT_REMOTE_SETTINGS.speed,
      naturalScroll:
        typeof v.naturalScroll === 'boolean'
          ? v.naturalScroll
          : DEFAULT_REMOTE_SETTINGS.naturalScroll,
      mac: typeof v.mac === 'boolean' ? v.mac : DEFAULT_REMOTE_SETTINGS.mac,
    };
  } catch {
    return DEFAULT_REMOTE_SETTINGS;
  }
}

/**
 * Store the preferences (ignored when storage is unavailable).
 *
 * @param s Preferences.
 */
export function saveRemoteSettings(s: RemoteSettings): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(s));
  } catch {
    /* Not remembered: the page still works. */
  }
}
