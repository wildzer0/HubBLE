/**
 * @file touchpad.ts
 * @description Gesture engine of the phone touchpad (pure, no DOM): turns pointer positions into the actions that the
 * dongle understands. One finger moves the pointer, a short tap clicks, two fingers scroll, a short two-finger tap is
 * a right click.
 */

/** A finger (or the mouse) on the pad. */
export interface Point {
  /** Pointer id. */
  id: number;
  /** X in CSS pixels. */
  x: number;
  /** Y in CSS pixels. */
  y: number;
}

/** What the pad asks the dongle to do. */
export type TouchAction = MoveAction | ClickAction;

/** Pointer movement or scrolling, in whole host units. */
export interface MoveAction {
  /** Move the pointer or scroll. */
  kind: 'move' | 'scroll';
  /** Horizontal amount. */
  dx: number;
  /** Vertical amount (for a scroll, positive scrolls up). */
  dy: number;
}

/** A click. */
export interface ClickAction {
  /** Always `click`. */
  kind: 'click';
  /** Which button. */
  button: 'left' | 'right';
}

/** Tunables of the engine. */
export interface TouchpadOptions {
  /** Pointer speed: screen pixels to host units. */
  gain: number;
  /** Scroll speed: screen pixels to wheel units. */
  scrollGain: number;
  /** A touch shorter than this (ms) and with little travel is a tap. */
  tapMs: number;
  /** Total travel (pixels) up to which a touch still counts as a tap. */
  tapSlop: number;
  /** Content follows the fingers when scrolling (like a phone); false for the traditional direction. */
  naturalScroll: boolean;
}

/** Default tunables. */
export const DEFAULT_TOUCHPAD: TouchpadOptions = {
  gain: 1.6,
  scrollGain: 0.25,
  tapMs: 250,
  tapSlop: 10,
  naturalScroll: true,
};

/**
 * Centre of a set of points.
 *
 * @param pts Points (at least one).
 * @returns The mean position.
 */
function centroid(pts: readonly Point[]): { x: number; y: number } {
  let x = 0;
  let y = 0;
  for (const p of pts) {
    x += p.x;
    y += p.y;
  }
  return { x: x / pts.length, y: y / pts.length };
}

/**
 * Stateful gesture recogniser. Feed it the pointers that are down after every start, move and end event.
 */
export class Touchpad {
  private readonly opts: TouchpadOptions;
  private last: { x: number; y: number } | null = null;
  private startT = 0;
  private travel = 0;
  private maxFingers = 0;
  private fingers = 0;
  private remX = 0;
  private remY = 0;

  /**
   * Create a recogniser.
   *
   * @param opts Tunables; missing fields use {@link DEFAULT_TOUCHPAD}.
   */
  constructor(opts: Partial<TouchpadOptions> = {}) {
    this.opts = { ...DEFAULT_TOUCHPAD, ...opts };
  }

  /**
   * A finger went down.
   *
   * @param pts Every finger that is down now (including the new one).
   * @param t Time in ms.
   */
  start(pts: readonly Point[], t: number): void {
    if (this.fingers === 0) {
      this.startT = t;
      this.travel = 0;
      this.maxFingers = 0;
    }
    this.fingers = pts.length;
    this.maxFingers = Math.max(this.maxFingers, pts.length);
    this.last = pts.length > 0 ? centroid(pts) : null; // A new finger moves the centre: do not count it as travel.
  }

  /**
   * Fingers moved.
   *
   * @param pts Every finger that is down.
   * @returns The actions to send (none for a pure tap jitter, at most one otherwise).
   */
  move(pts: readonly Point[]): TouchAction[] {
    if (pts.length === 0 || this.last === null) return [];
    const c = centroid(pts);
    const dx = c.x - this.last.x;
    const dy = c.y - this.last.y;
    this.last = c;
    this.travel += Math.abs(dx) + Math.abs(dy);
    if (this.travel <= this.opts.tapSlop) return []; // Still a candidate tap: hold the pointer still.
    if (pts.length >= 2) {
      const sign = this.opts.naturalScroll ? 1 : -1;
      return this.carry(
        'scroll',
        dx * this.opts.scrollGain * sign,
        dy * this.opts.scrollGain * sign,
      );
    }
    return this.carry('move', dx * this.opts.gain, dy * this.opts.gain);
  }

  /**
   * A finger was lifted.
   *
   * @param pts Fingers still down (empty when the last one left).
   * @param t Time in ms.
   * @returns A click when this was a short tap of one or two fingers.
   */
  end(pts: readonly Point[], t: number): TouchAction[] {
    const out: TouchAction[] = [];
    if (pts.length === 0) {
      const tap = this.travel <= this.opts.tapSlop && t - this.startT <= this.opts.tapMs;
      if (tap && this.maxFingers === 1) out.push({ kind: 'click', button: 'left' });
      if (tap && this.maxFingers === 2) out.push({ kind: 'click', button: 'right' });
      this.last = null;
      this.fingers = 0;
      this.remX = 0;
      this.remY = 0;
    } else {
      this.fingers = pts.length;
      this.last = centroid(pts);
    }
    return out;
  }

  /**
   * Whole-number action with the fractional part carried to the next move, so slow movement is not lost to rounding.
   *
   * @param kind Move or scroll.
   * @param dx Horizontal amount (fractional).
   * @param dy Vertical amount (fractional).
   * @returns The action, or none if both amounts round to zero.
   */
  private carry(kind: 'move' | 'scroll', dx: number, dy: number): TouchAction[] {
    this.remX += dx;
    this.remY += dy;
    const ix = Math.trunc(this.remX);
    const iy = Math.trunc(this.remY);
    this.remX -= ix;
    this.remY -= iy;
    return ix === 0 && iy === 0 ? [] : [{ kind, dx: ix, dy: iy }];
  }
}
