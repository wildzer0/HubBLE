/**
 * @file touchpad.ts
 * @description Gesture engine of the phone touchpad (pure, no DOM): turns finger positions into the actions the
 * dongle understands, the way a laptop touchpad does. One finger moves the pointer (faster when the finger is faster),
 * a short tap clicks, a tap followed at once by a touch and a drag presses and holds the left button (drag and drop, a
 * second quick tap is a double click), two fingers scroll, a short two-finger tap is a right click.
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

/** Pointer movement or scrolling, in whole host units. */
export interface MoveAction {
  /** Move the pointer or scroll. */
  kind: 'move' | 'scroll';
  /** Horizontal amount. */
  dx: number;
  /** Vertical amount (for a scroll, positive scrolls up). */
  dy: number;
}

/** A complete click (press and release). */
export interface ClickAction {
  /** Always `click`. */
  kind: 'click';
  /** Which button. */
  button: 'left' | 'right';
}

/** The left button goes down or up (drag and drop). */
export interface ButtonAction {
  /** Always `button`. */
  kind: 'button';
  /** True for press, false for release. */
  down: boolean;
}

/** What the pad asks the dongle to do. */
export type TouchAction = MoveAction | ClickAction | ButtonAction;

/** Tunables of the engine. */
export interface TouchpadOptions {
  /** Pointer speed for a slow finger: host units per screen pixel. */
  gain: number;
  /** How much a faster finger adds: the speed is multiplied by `1 + accel * v` (v in pixels per ms). */
  accel: number;
  /** Pointer events closer together than this (ms) are treated as this far apart when estimating the speed: touch
   *  timestamps jitter by a few ms, and a tiny interval would make the speed, and with it the pointer, jump. */
  minDt: number;
  /** Weight (0 to 1) of the newest speed sample in the smoothed speed; lower is smoother. */
  smoothing: number;
  /** The speed multiplier never exceeds this. */
  maxBoost: number;
  /** Scroll speed: wheel units per screen pixel. */
  scrollGain: number;
  /** A touch shorter than this (ms) and with little travel is a tap. */
  tapMs: number;
  /** Total travel (pixels) up to which a touch still counts as a tap. */
  tapSlop: number;
  /** A touch that starts within this time (ms) after a tap presses and holds the left button. */
  dragMs: number;
  /** Content follows the fingers when scrolling (like a phone); false for the traditional direction. */
  naturalScroll: boolean;
}

/** Default tunables: a slow swipe is precise, a quick flick crosses a large screen. */
export const DEFAULT_TOUCHPAD: TouchpadOptions = {
  gain: 2.4,
  accel: 2.2,
  maxBoost: 6,
  minDt: 8,
  smoothing: 0.35,
  scrollGain: 0.35,
  tapMs: 250,
  tapSlop: 10,
  dragMs: 300,
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
  private lastT = 0;
  private speed = 0;
  private startT = 0;
  private travel = 0;
  private maxFingers = 0;
  private fingers = 0;
  private remX = 0;
  private remY = 0;
  private lastTapEnd: number | null = null;
  private dragging = false;

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
   * @returns A button press when this touch follows a tap closely (drag and drop).
   */
  start(pts: readonly Point[], t: number): TouchAction[] {
    const out: TouchAction[] = [];
    if (this.fingers === 0) {
      this.startT = t;
      this.speed = 0;
      this.travel = 0;
      this.maxFingers = 0;
      if (pts.length === 1 && this.lastTapEnd !== null && t - this.lastTapEnd <= this.opts.dragMs) {
        this.dragging = true;
        out.push({ kind: 'button', down: true });
      }
      this.lastTapEnd = null;
    }
    this.fingers = pts.length;
    this.maxFingers = Math.max(this.maxFingers, pts.length);
    this.last = pts.length > 0 ? centroid(pts) : null; // A new finger moves the centre: do not count it as travel.
    this.lastT = t;
    return out;
  }

  /**
   * Fingers moved.
   *
   * @param pts Every finger that is down.
   * @param t Time in ms.
   * @returns The actions to send (none while the touch could still be a tap, at most one otherwise).
   */
  move(pts: readonly Point[], t: number): TouchAction[] {
    if (pts.length === 0 || this.last === null) return [];
    const c = centroid(pts);
    const dx = c.x - this.last.x;
    const dy = c.y - this.last.y;
    const dt = Math.max(this.opts.minDt, t - this.lastT);
    this.last = c;
    this.lastT = t;
    this.travel += Math.abs(dx) + Math.abs(dy);
    if (!this.dragging && this.travel <= this.opts.tapSlop) return []; // Still a candidate tap: hold the pointer still.
    // Low-pass filter: a single odd interval must not change the pointer speed.
    this.speed += (Math.hypot(dx, dy) / dt - this.speed) * this.opts.smoothing; // pixels per ms
    const speed = this.speed;
    if (pts.length >= 2) {
      const sign = this.opts.naturalScroll ? 1 : -1;
      const boost = Math.min(2.5, 1 + speed);
      const k = this.opts.scrollGain * boost * sign;
      return this.carry('scroll', dx * k, dy * k);
    }
    const boost = Math.min(this.opts.maxBoost, 1 + this.opts.accel * speed);
    const k = this.opts.gain * boost;
    return this.carry('move', dx * k, dy * k);
  }

  /**
   * A finger was lifted.
   *
   * @param pts Fingers still down (empty when the last one left).
   * @param t Time in ms.
   * @returns A click when this was a short tap of one or two fingers, or the release of a drag.
   */
  end(pts: readonly Point[], t: number): TouchAction[] {
    const out: TouchAction[] = [];
    if (pts.length === 0) {
      const tap = this.travel <= this.opts.tapSlop && t - this.startT <= this.opts.tapMs;
      if (this.dragging) {
        this.dragging = false;
        out.push({ kind: 'button', down: false });
      } else if (tap && this.maxFingers === 1) {
        out.push({ kind: 'click', button: 'left' });
        this.lastTapEnd = t;
      } else if (tap && this.maxFingers === 2) {
        out.push({ kind: 'click', button: 'right' });
      }
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
