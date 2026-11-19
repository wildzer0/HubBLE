/**
 * @file remoteLink.ts
 * @description Sender of the phone-touchpad messages (see docs/api/websocket.md, "Phone touchpad"). It coalesces pointer
 * moves, keeps the held buttons and keys alive (the dongle releases what the phone stops mentioning for 1.5 s) and
 * guarantees a minimum press time, so the page only says what happened.
 */

/** Sends one message to the dongle; returns false if the connection is not open. */
export type SendFn = (msg: Record<string, unknown>) => boolean;

/** Mouse button bits of `remote.buttons`. */
export const BTN = {
  /** Primary button. */
  left: 1,
  /** Secondary button. */
  right: 2,
  /** Wheel button. */
  middle: 4,
} as const;

/** Moves are sent at most this often, ms (about 60 per second). */
const FLUSH_MS = 16;
/** A held button or key is repeated this often, ms (the dongle drops it after 1.5 s of silence). */
const REPEAT_MS = 500;
/** A key or button stays down at least this long, ms, so the host always sees it. */
const MIN_PRESS_MS = 35;
/** Most characters in one `remote.text` message (frames are at most 128 bytes). */
const TEXT_CHUNK = 40;

/** One link to the dongle. */
export class RemoteLink {
  private readonly send: SendFn;
  private px = 0;
  private py = 0;
  private sx = 0;
  private sy = 0;
  private flushTimer: ReturnType<typeof setTimeout> | null = null;
  private readonly owners = new Map<string, number>();
  private keyState: { m: number; k: number[] } | null = null;
  private keyDownAt = 0;
  private repeatTimer: ReturnType<typeof setInterval> | null = null;
  private clicks = 0;

  /**
   * Create a link.
   *
   * @param send Function that sends a message to the dongle.
   */
  constructor(send: SendFn) {
    this.send = send;
  }

  /**
   * Move the pointer (coalesced).
   *
   * @param dx Horizontal amount.
   * @param dy Vertical amount.
   */
  move(dx: number, dy: number): void {
    this.px += dx;
    this.py += dy;
    this.schedule();
  }

  /**
   * Scroll (coalesced).
   *
   * @param dx Horizontal amount.
   * @param dy Vertical amount (positive scrolls up).
   */
  scroll(dx: number, dy: number): void {
    this.sx += dx;
    this.sy += dy;
    this.schedule();
  }

  /**
   * Press and release a button.
   *
   * @param bit Button bit.
   */
  click(bit: number): void {
    const owner = `click${String(this.clicks++)}`; // A quick second click must not be cut short by the first one's timer.
    this.hold(owner, bit);
    setTimeout(() => {
      this.release(owner);
    }, MIN_PRESS_MS);
  }

  /**
   * Hold a button on behalf of an owner (a pad drag, a screen button); several owners can hold different buttons.
   *
   * @param owner Who holds it.
   * @param bit Button bit.
   */
  hold(owner: string, bit: number): void {
    this.owners.set(owner, bit);
    this.sendButtons();
    this.startRepeat();
  }

  /**
   * Let go of what an owner holds.
   *
   * @param owner Who held it.
   */
  release(owner: string): void {
    if (!this.owners.delete(owner)) return;
    this.sendButtons();
    this.stopRepeatIfIdle();
  }

  /**
   * Press a key (with modifiers) and hold it until {@link keyUp}: the host repeats it by itself while it is held.
   *
   * @param usage Key usage, or 0 for a modifier alone.
   * @param mods Modifier byte.
   */
  keyDown(usage: number, mods: number): void {
    this.keyState = { m: mods, k: usage === 0 ? [] : [usage] };
    this.keyDownAt = Date.now();
    this.sendKey();
    this.startRepeat();
  }

  /**
   * Release the held key, but not before it has been down for a minimum time.
   */
  keyUp(): void {
    const held = this.keyState;
    const wait = Math.max(0, MIN_PRESS_MS - (Date.now() - this.keyDownAt));
    setTimeout(() => {
      if (this.keyState !== held) return; // Another key went down meanwhile: it owns the state now.
      this.keyState = null;
      this.send({ event: 'remote.key', m: 0, k: [] });
      this.stopRepeatIfIdle();
    }, wait);
  }

  /**
   * Press and release a key.
   *
   * @param usage Key usage, or 0 for a modifier alone.
   * @param mods Modifier byte.
   */
  tapKey(usage: number, mods: number): void {
    this.keyDown(usage, mods);
    this.keyUp();
  }

  /**
   * Type text (US layout), in chunks that fit a frame.
   *
   * @param text Characters.
   * @param mods Modifier byte held with every character.
   */
  type(text: string, mods = 0): void {
    for (let i = 0; i < text.length; i += TEXT_CHUNK) {
      this.send({ event: 'remote.text', t: text.slice(i, i + TEXT_CHUNK), m: mods });
    }
  }

  /**
   * Release everything and cancel the timers (call when the page closes).
   */
  dispose(): void {
    if (this.flushTimer !== null) clearTimeout(this.flushTimer);
    this.flushTimer = null;
    if (this.repeatTimer !== null) clearInterval(this.repeatTimer);
    this.repeatTimer = null;
    if (this.owners.size > 0) {
      this.owners.clear();
      this.sendButtons();
    }
    if (this.keyState !== null) {
      this.keyState = null;
      this.send({ event: 'remote.key', m: 0, k: [] });
    }
  }

  private schedule(): void {
    if (this.flushTimer !== null) return;
    this.flushTimer = setTimeout(() => {
      this.flushTimer = null;
      if (this.px !== 0 || this.py !== 0)
        this.send({ event: 'remote.move', dx: this.px, dy: this.py });
      if (this.sx !== 0 || this.sy !== 0)
        this.send({ event: 'remote.scroll', dx: this.sx, dy: this.sy });
      this.px = this.py = this.sx = this.sy = 0;
    }, FLUSH_MS);
  }

  private sendButtons(): void {
    let b = 0;
    for (const v of this.owners.values()) b |= v;
    this.send({ event: 'remote.buttons', b });
  }

  private sendKey(): void {
    if (this.keyState !== null)
      this.send({ event: 'remote.key', m: this.keyState.m, k: this.keyState.k });
  }

  private startRepeat(): void {
    if (this.repeatTimer !== null) return;
    this.repeatTimer = setInterval(() => {
      if (this.owners.size > 0) this.sendButtons();
      this.sendKey();
    }, REPEAT_MS);
  }

  private stopRepeatIfIdle(): void {
    if (this.owners.size === 0 && this.keyState === null && this.repeatTimer !== null) {
      clearInterval(this.repeatTimer);
      this.repeatTimer = null;
    }
  }
}
