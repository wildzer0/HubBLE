/**
 * @file modifiers.ts
 * @description Sticky modifier keys of the on-screen keyboard (pure). A tap arms a modifier for the next key, a second
 * tap keeps it on until it is tapped again, the way the Shift key of a phone keyboard works.
 */

/** HID modifier bits. */
export const MOD = {
  /** Ctrl (either side). */
  ctrl: 0x01,
  /** Shift. */
  shift: 0x02,
  /** Alt (Option on a Mac). */
  alt: 0x04,
  /** Windows key (Command on a Mac). */
  gui: 0x08,
} as const;

/** State of one modifier. */
export type ModState = 'off' | 'once' | 'locked';

/** The four modifiers and their states. */
export class Modifiers {
  private state = new Map<number, ModState>();

  /**
   * State of a modifier.
   *
   * @param bit Modifier bit.
   * @returns Its state.
   */
  get(bit: number): ModState {
    return this.state.get(bit) ?? 'off';
  }

  /**
   * Tap on a modifier: off, then armed for one key, then locked, then off again.
   *
   * @param bit Modifier bit.
   */
  tap(bit: number): void {
    const next: Record<ModState, ModState> = { off: 'once', once: 'locked', locked: 'off' };
    this.state.set(bit, next[this.get(bit)]);
  }

  /**
   * The modifier byte to send with the next key.
   *
   * @returns Bits of every modifier that is armed or locked.
   */
  mask(): number {
    let m = 0;
    for (const [bit, s] of this.state) if (s !== 'off') m |= bit;
    return m;
  }

  /**
   * Use up the armed modifiers: call after a key was sent with {@link mask}. Locked ones stay.
   */
  consume(): void {
    for (const [bit, s] of this.state) if (s === 'once') this.state.set(bit, 'off');
  }

  /**
   * Whether anything is armed or locked.
   *
   * @returns true if {@link mask} is not zero.
   */
  any(): boolean {
    return this.mask() !== 0;
  }
}
