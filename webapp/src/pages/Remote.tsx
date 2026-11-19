/**
 * @file Remote.tsx
 * @description The phone as touchpad and keyboard. The dongle accepts this input only inside a window that the user
 * opens here on purpose (5 minutes, never kept over a restart) and never while the input is locked. Pointer, scroll
 * and click go as `remote.*` WebSocket events; text goes as typed characters.
 */
import { useEffect, useRef, useState } from 'preact/hooks';
import { ApiError, getStatus, setRemoteInput } from '../api/client';
import { sendLiveJson, subscribeState } from '../api/live';
import { Alert, Icon } from '../components/ui';
import type { MessageKey } from '../i18n';
import { useT } from '../i18n/context';
import { Touchpad } from '../lib/touchpad';
import type { Point, TouchAction } from '../lib/touchpad';
import { useApi } from '../lib/useApi';

/** Status refresh period (drives the countdown). */
const REFRESH_MS = 1000;
/** Time a tapped button or key stays down, ms. */
const TAP_HOLD_MS = 40;
/** A held button is repeated this often so the dongle does not take it for a lost phone (it expires at 1.5 s), ms. */
const HOLD_REPEAT_MS = 500;
/** Most characters in one `remote.text` message (the dongle accepts frames up to 128 bytes). */
const TEXT_CHUNK = 40;
/** Longest text kept in the typing field before it is cleared, so edits stay cheap. */
const TEXT_KEEP = 200;
/** Pointer speed choices (slider) and the stored choice. */
const GAIN_MIN = 0.8;
const GAIN_MAX = 3.2;
const GAIN_STEP = 0.1;
const GAIN_DEFAULT = 1.6;
const GAIN_KEY = 'hubble.touchpad.gain';

/** HID modifier bits. */
const MOD = { ctrl: 0x01, shift: 0x02, alt: 0x04, gui: 0x08 } as const;
/** Mouse button bits of `remote.buttons`. */
const BTN = { left: 1, right: 2 } as const;
/** Special keys (page 0x07 usages) with the translation key of their label. */
const SPECIAL_KEYS: readonly { usage: number; label: MessageKey }[] = [
  { usage: 0x29, label: 'remote.key.esc' },
  { usage: 0x2b, label: 'remote.key.tab' },
  { usage: 0x2a, label: 'remote.key.backspace' },
  { usage: 0x28, label: 'remote.key.enter' },
  { usage: 0x4c, label: 'remote.key.delete' },
  { usage: 0x50, label: 'remote.key.left' },
  { usage: 0x52, label: 'remote.key.up' },
  { usage: 0x51, label: 'remote.key.down' },
  { usage: 0x4f, label: 'remote.key.right' },
];

/**
 * Read the stored pointer speed.
 *
 * @returns The speed, or the default when nothing valid is stored or storage is unavailable.
 */
function loadGain(): number {
  try {
    const v = Number(localStorage.getItem(GAIN_KEY));
    return v >= GAIN_MIN && v <= GAIN_MAX ? v : GAIN_DEFAULT;
  } catch {
    return GAIN_DEFAULT;
  }
}

/**
 * Press and release a mouse button (a click).
 *
 * @param button Button bit.
 */
function click(button: number): void {
  sendLiveJson({ event: 'remote.buttons', b: button });
  setTimeout(() => {
    sendLiveJson({ event: 'remote.buttons', b: 0 });
  }, TAP_HOLD_MS);
}

/**
 * The touch surface.
 *
 * - `gain`: Pointer speed.
 * @returns The pad element.
 */
function Pad({ gain }: { gain: number }) {
  const t = useT();
  const pad = useRef(new Touchpad({ gain }));
  const pointers = useRef(new Map<number, Point>());
  const pending = useRef({ dx: 0, dy: 0, sx: 0, sy: 0 });
  const frame = useRef(0);

  useEffect(() => {
    pad.current = new Touchpad({ gain });
  }, [gain]);

  /** Send what moved since the last frame as one message each (about 60 per second at most). */
  const flush = () => {
    frame.current = 0;
    const p = pending.current;
    if (p.dx !== 0 || p.dy !== 0) sendLiveJson({ event: 'remote.move', dx: p.dx, dy: p.dy });
    if (p.sx !== 0 || p.sy !== 0) sendLiveJson({ event: 'remote.scroll', dx: p.sx, dy: p.sy });
    pending.current = { dx: 0, dy: 0, sx: 0, sy: 0 };
  };

  /**
   * Apply the actions of the gesture engine.
   *
   * @param actions Actions to send.
   */
  const apply = (actions: TouchAction[]) => {
    for (const a of actions) {
      if (a.kind === 'click') click(a.button === 'left' ? BTN.left : BTN.right);
      else if (a.kind === 'move') {
        pending.current.dx += a.dx;
        pending.current.dy += a.dy;
      } else {
        pending.current.sx += a.dx;
        pending.current.sy += a.dy;
      }
    }
    if (frame.current === 0 && actions.some((a) => a.kind !== 'click')) {
      frame.current = requestAnimationFrame(flush);
    }
  };

  useEffect(
    () => () => {
      if (frame.current !== 0) cancelAnimationFrame(frame.current);
    },
    [],
  );

  const points = () => [...pointers.current.values()];
  const track = (e: PointerEvent) => {
    pointers.current.set(e.pointerId, { id: e.pointerId, x: e.clientX, y: e.clientY });
  };

  return (
    <div
      class="touchpad"
      role="application"
      aria-label={t('remote.pad')}
      data-testid="touchpad"
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        track(e);
        pad.current.start(points(), e.timeStamp);
      }}
      onPointerMove={(e) => {
        if (!pointers.current.has(e.pointerId)) return;
        track(e);
        apply(pad.current.move(points()));
      }}
      onPointerUp={(e) => {
        pointers.current.delete(e.pointerId);
        apply(pad.current.end(points(), e.timeStamp));
      }}
      onPointerCancel={(e) => {
        pointers.current.delete(e.pointerId);
        pad.current.end(points(), e.timeStamp);
      }}
    >
      <span class="muted">{t('remote.pad_hint')}</span>
    </div>
  );
}

/**
 * A mouse button that stays down while it is pressed, and keeps telling the dongle so.
 *
 * - `bit`: Button bit.
 * - `label`: Text of the button.
 * @returns The button element.
 */
function HoldButton({ bit, label }: { bit: number; label: string }) {
  const timer = useRef<ReturnType<typeof setInterval> | null>(null);
  const stop = () => {
    if (timer.current === null) return;
    clearInterval(timer.current);
    timer.current = null;
    sendLiveJson({ event: 'remote.buttons', b: 0 });
  };
  useEffect(() => stop, []);
  return (
    <button
      type="button"
      class="btn block hold"
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        sendLiveJson({ event: 'remote.buttons', b: bit });
        timer.current = setInterval(() => {
          sendLiveJson({ event: 'remote.buttons', b: bit });
        }, HOLD_REPEAT_MS);
      }}
      onPointerUp={stop}
      onPointerCancel={stop}
    >
      {label}
    </button>
  );
}

/**
 * Text field and special keys.
 *
 * @returns The keyboard element.
 */
function Keys() {
  const t = useT();
  const [mods, setMods] = useState(0);
  const [value, setValue] = useState('');
  const prev = useRef('');

  /**
   * Press and release a key with the modifiers that are switched on, then switch them off.
   *
   * @param usage Key usage.
   */
  const press = (usage: number) => {
    sendLiveJson({ event: 'remote.key', m: mods, k: [usage] });
    setTimeout(() => {
      sendLiveJson({ event: 'remote.key', m: 0, k: [] });
    }, TAP_HOLD_MS);
    setMods(0);
  };

  /**
   * Send characters as text, in chunks that fit a frame.
   *
   * @param s Text.
   */
  const type = (s: string) => {
    for (let i = 0; i < s.length; i += TEXT_CHUNK) {
      sendLiveJson({ event: 'remote.text', t: s.slice(i, i + TEXT_CHUNK) });
    }
  };

  /**
   * The field changed: turn the difference into backspaces and characters (this also copes with autocorrect, which
   * replaces the end of the word).
   *
   * @param now New content of the field.
   */
  const changed = (now: string) => {
    const before = prev.current;
    let common = 0;
    while (common < before.length && common < now.length && before[common] === now[common])
      common++;
    for (let i = before.length; i > common; i--) press(0x2a);
    type(now.slice(common));
    if (now.length > TEXT_KEEP) {
      prev.current = '';
      setValue('');
    } else {
      prev.current = now;
      setValue(now);
    }
  };

  const toggles: readonly { bit: number; label: string }[] = [
    { bit: MOD.ctrl, label: 'Ctrl' },
    { bit: MOD.alt, label: 'Alt' },
    { bit: MOD.shift, label: 'Shift' },
    { bit: MOD.gui, label: t('remote.key.gui') },
  ];

  return (
    <section class="card" aria-labelledby="keys-h">
      <h3 class="card-title" id="keys-h">
        {t('remote.keyboard')}
      </h3>
      <input
        type="text"
        class="block"
        value={value}
        aria-label={t('remote.text_label')}
        placeholder={t('remote.text_label')}
        autocomplete="off"
        autocapitalize="off"
        spellcheck={false}
        data-testid="remote-text"
        onInput={(e) => {
          changed(e.currentTarget.value);
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter') {
            e.preventDefault();
            press(0x28);
          }
        }}
      />
      <p class="hint">{t('remote.text_hint')}</p>
      <div class="keyrow" role="group" aria-label={t('remote.modifiers')}>
        {toggles.map((m) => (
          <button
            key={m.bit}
            type="button"
            class={`btn${(mods & m.bit) !== 0 ? ' primary' : ''}`}
            aria-pressed={(mods & m.bit) !== 0}
            onClick={() => {
              setMods((v) => v ^ m.bit);
            }}
          >
            {m.label}
          </button>
        ))}
      </div>
      <div class="keyrow" role="group" aria-label={t('remote.special')}>
        {SPECIAL_KEYS.map((k) => (
          <button
            key={k.usage}
            type="button"
            class="btn"
            onClick={() => {
              press(k.usage);
            }}
          >
            {t(k.label)}
          </button>
        ))}
      </div>
    </section>
  );
}

/**
 * Remaining time as m:ss.
 *
 * @param s Seconds.
 * @returns The text.
 */
function mmss(s: number): string {
  return `${String(Math.floor(s / 60))}:${String(s % 60).padStart(2, '0')}`;
}

/**
 * Render the page.
 *
 * @returns The page element.
 */
export function Remote() {
  const t = useT();
  const { data: status, failed, reload } = useApi(getStatus, REFRESH_MS);
  const [error, setError] = useState<string | null>(null);
  const [linked, setLinked] = useState(true);
  const [gain, setGain] = useState(loadGain);

  useEffect(
    () =>
      subscribeState((c) => {
        setLinked(c);
      }),
    [],
  );

  const toggle = (enabled: boolean) => {
    setError(null);
    setRemoteInput(enabled).then(reload, (e: unknown) => {
      setError(
        e instanceof ApiError && e.code === 'input_locked'
          ? t('remote.locked_error')
          : e instanceof Error
            ? e.message
            : String(e),
      );
    });
  };

  const left = status?.remote_s ?? 0;
  const locked = status?.locked === true;
  const on = left > 0 && !locked;

  return (
    <>
      <div class="page-head">
        <a class="back" href="#/dashboard">
          <Icon name="back" />
          {t('common.back')}
        </a>
      </div>
      <h2>{t('remote.heading')}</h2>
      {failed && !status && (
        <Alert kind="bad" role="alert">
          {t('common.error')}
        </Alert>
      )}
      {error && (
        <Alert kind="bad" role="alert">
          {error}
        </Alert>
      )}
      {locked && (
        <Alert kind="warn" role="status" testId="remote-locked">
          {t('remote.locked')}
        </Alert>
      )}
      {!linked && on && (
        <Alert kind="warn" role="status">
          {t('remote.no_link')}
        </Alert>
      )}

      {!on && !locked && (
        <section class="card">
          <p>{t('remote.intro')}</p>
          <p class="hint">{t('remote.security')}</p>
          <button
            type="button"
            class="btn primary block"
            data-testid="remote-enable"
            onClick={() => {
              toggle(true);
            }}
          >
            {t('remote.enable')}
          </button>
        </section>
      )}

      {on && (
        <>
          <section class="card">
            <p role="timer" data-testid="remote-timer">
              {t('remote.active', { time: mmss(left) })}
            </p>
            <div class="keyrow">
              <button
                type="button"
                class="btn"
                onClick={() => {
                  toggle(true);
                }}
              >
                {t('remote.extend')}
              </button>
              <button
                type="button"
                class="btn"
                data-testid="remote-stop"
                onClick={() => {
                  toggle(false);
                }}
              >
                {t('remote.stop')}
              </button>
            </div>
          </section>
          <Pad gain={gain} />
          <div class="keyrow">
            <HoldButton bit={BTN.left} label={t('remote.left')} />
            <HoldButton bit={BTN.right} label={t('remote.right')} />
          </div>
          <label class="row">
            <span>{t('remote.speed')}</span>
            <input
              type="range"
              min={GAIN_MIN}
              max={GAIN_MAX}
              step={GAIN_STEP}
              value={gain}
              onInput={(e) => {
                const v = Number(e.currentTarget.value);
                setGain(v);
                try {
                  localStorage.setItem(GAIN_KEY, String(v));
                } catch {
                  /* Storage may be blocked: the speed then simply is not remembered. */
                }
              }}
            />
          </label>
          <Keys />
        </>
      )}
    </>
  );
}
