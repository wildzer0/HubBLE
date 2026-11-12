/**
 * @file inputFormat.ts
 * @description Turns `input.event` payloads into short readable text for the input tester.
 */
import type { InputEvent } from '../api/types';

/** Modifier bit names in HID order (bit 0..7). */
const MODIFIERS = ['LCtrl', 'LShift', 'LAlt', 'LGui', 'RCtrl', 'RShift', 'RAlt', 'RGui'];
/** Mouse button names in bit order. */
const BUTTONS = ['Left', 'Right', 'Middle', 'Back', 'Forward'];

/** Names of the non-letter keys of HID usage page 0x07 that people recognise (usage id -> name). */
const KEY_NAMES: Record<number, string> = {
  0x28: 'Enter',
  0x29: 'Esc',
  0x2a: 'Backspace',
  0x2b: 'Tab',
  0x2c: 'Space',
  0x39: 'CapsLock',
  0x4f: 'Right',
  0x50: 'Left',
  0x51: 'Down',
  0x52: 'Up',
};

/**
 * Name of a key usage (page 0x07).
 *
 * @param usage Usage id.
 * @returns "A".."Z", "1".."0", F1..F12, a few named keys, or "0x.." for the rest.
 */
export function keyName(usage: number): string {
  if (usage >= 0x04 && usage <= 0x1d) return String.fromCharCode(65 + usage - 0x04);
  if (usage >= 0x1e && usage <= 0x26) return String(usage - 0x1d);
  if (usage === 0x27) return '0';
  if (usage >= 0x3a && usage <= 0x45) return `F${String(usage - 0x39)}`;
  return KEY_NAMES[usage] ?? `0x${usage.toString(16).padStart(2, '0')}`;
}

/**
 * Names of the set bits of a mask.
 *
 * @param mask Bit mask.
 * @param names Names indexed by bit.
 * @returns The names of the bits that are set.
 */
function bits(mask: number, names: string[]): string[] {
  return names.filter((_, i) => (mask & (1 << i)) !== 0);
}

/**
 * Describe one input event.
 *
 * @param e The event.
 * @returns Text such as "LShift + A" or "dx=5 dy=-3 [Left]".
 */
export function describeInput(e: InputEvent): string {
  if (e.kind === 'key') {
    const parts = [...bits(e.modifiers ?? 0, MODIFIERS), ...(e.keys ?? []).map(keyName)];
    return parts.length === 0 ? '(released)' : parts.join(' + ');
  }
  if (e.kind === 'consumer') {
    return e.usage ? `usage 0x${e.usage.toString(16).padStart(4, '0')}` : '(released)';
  }
  const parts: string[] = [];
  if (e.dx || e.dy) parts.push(`dx=${String(e.dx ?? 0)} dy=${String(e.dy ?? 0)}`);
  if (e.wheel) parts.push(`wheel=${String(e.wheel)}`);
  if (e.pan) parts.push(`pan=${String(e.pan)}`);
  const b = bits(e.buttons ?? 0, BUTTONS);
  if (b.length > 0) parts.push(`[${b.join(', ')}]`);
  return parts.length === 0 ? '(idle)' : parts.join(' ');
}
