/**
 * @file Remote.tsx
 * @description The phone as touchpad and keyboard. The dongle accepts this input only inside a window that the user
 * opens here on purpose (5 minutes, never kept over a restart) and never while the input is locked. The page has two
 * modes: a full-height touchpad with a scroll strip and mouse buttons, and a keyboard with a typing field, sticky
 * modifiers, navigation keys and common shortcuts.
 */
import type { ComponentChildren } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { ApiError, getStatus, setRemoteInput } from '../api/client';
import { sendLiveJson, subscribeState } from '../api/live';
import { Alert, Icon, SwitchRow } from '../components/ui';
import type { MessageKey } from '../i18n';
import { useT } from '../i18n/context';
import { MOD, Modifiers } from '../lib/modifiers';
import type { ModState } from '../lib/modifiers';
import { BTN, RemoteLink } from '../lib/remoteLink';
import {
  gainFor,
  loadRemoteSettings,
  saveRemoteSettings,
  SPEED_MAX,
  SPEED_MIN,
} from '../lib/remoteSettings';
import type { RemoteSettings } from '../lib/remoteSettings';
import { Touchpad } from '../lib/touchpad';
import type { Point, TouchAction } from '../lib/touchpad';
import { useApi } from '../lib/useApi';

/** Status refresh period (drives the countdown). */
const REFRESH_MS = 1000;
/** Longest text kept in the typing field before it is cleared, so edits stay cheap. */
const TEXT_KEEP = 60;
/** Wheel units per pixel dragged on the scroll strip. */
const STRIP_GAIN = 0.4;
/** Vibration of a click on phones that support it, ms. */
const CLICK_BUZZ_MS = 8;

/** Key usages (HID page 0x07) used by the on-screen keys. */
const KEY = {
  a: 0x04,
  c: 0x06,
  v: 0x19,
  x: 0x1b,
  z: 0x1d,
  enter: 0x28,
  esc: 0x29,
  backspace: 0x2a,
  tab: 0x2b,
  space: 0x2c,
  pageUp: 0x4b,
  delete: 0x4c,
  end: 0x4d,
  pageDown: 0x4e,
  right: 0x4f,
  left: 0x50,
  down: 0x51,
  up: 0x52,
  home: 0x4a,
} as const;

/** A key button: what it shows, what it sends. */
interface KeyDef {
  /** Key usage. */
  usage: number;
  /** Visible text. */
  label: string;
  /** Spoken name. */
  name: MessageKey;
}

/** Editing and navigation keys, two rows of a five-column grid. */
const EDIT_KEYS: readonly KeyDef[] = [
  { usage: KEY.esc, label: 'Esc', name: 'remote.key.esc' },
  { usage: KEY.tab, label: 'Tab', name: 'remote.key.tab' },
  { usage: KEY.backspace, label: '⌫', name: 'remote.key.backspace' },
  { usage: KEY.delete, label: 'Del', name: 'remote.key.delete' },
  { usage: KEY.enter, label: '↵', name: 'remote.key.enter' },
  { usage: KEY.home, label: 'Home', name: 'remote.key.home' },
  { usage: KEY.end, label: 'End', name: 'remote.key.end' },
  { usage: KEY.pageUp, label: 'PgUp', name: 'remote.key.pgup' },
  { usage: KEY.pageDown, label: 'PgDn', name: 'remote.key.pgdn' },
];
/** Arrow keys in the usual inverted-T. */
const ARROWS: readonly (KeyDef & { area: string })[] = [
  { usage: KEY.up, label: '↑', name: 'remote.key.up', area: 'up' },
  { usage: KEY.left, label: '←', name: 'remote.key.left', area: 'left' },
  { usage: KEY.down, label: '↓', name: 'remote.key.down', area: 'down' },
  { usage: KEY.right, label: '→', name: 'remote.key.right', area: 'right' },
];

/**
 * Buzz briefly, where the browser allows it.
 */
function buzz(): void {
  try {
    navigator.vibrate(CLICK_BUZZ_MS);
  } catch {
    /* No vibration support: nothing to do. */
  }
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
 * A button that stays pressed for as long as the finger is on it.
 *
 * - `link`: The link to the dongle.
 * - `owner`: Name of the holder (so several buttons can be held at once).
 * - `bit`: Mouse button bit.
 * - `label`: Accessible name.
 * - `children`: Content.
 * - `class`: Extra class.
 * @returns The button element.
 */
function MouseButton({
  link,
  owner,
  bit,
  label,
  children,
  class: cls = '',
}: {
  link: RemoteLink;
  owner: string;
  bit: number;
  label: string;
  children: ComponentChildren;
  class?: string;
}) {
  const [down, setDown] = useState(false);
  const up = () => {
    setDown(false);
    link.release(owner);
  };
  return (
    <button
      type="button"
      class={`mbtn ${cls}${down ? ' down' : ''}`}
      aria-label={label}
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        setDown(true);
        link.hold(owner, bit);
      }}
      onPointerUp={up}
      onPointerCancel={up}
    >
      {children}
    </button>
  );
}

/**
 * The touch surface and the scroll strip next to it.
 *
 * - `link`: The link to the dongle.
 * - `settings`: Preferences.
 * @returns The pad element.
 */
function Pad({ link, settings }: { link: RemoteLink; settings: RemoteSettings }) {
  const t = useT();
  const engine = useMemo(
    () => new Touchpad({ gain: gainFor(settings.speed), naturalScroll: settings.naturalScroll }),
    [settings.speed, settings.naturalScroll],
  );
  const pointers = useRef(new Map<number, Point>());
  const area = useRef<HTMLDivElement>(null);
  const [touching, setTouching] = useState(false);
  const strip = useRef<{ y: number; t: number } | null>(null);

  const apply = (actions: TouchAction[]) => {
    for (const a of actions) {
      if (a.kind === 'button') {
        if (a.down) {
          link.hold('drag', BTN.left);
          buzz();
        } else {
          link.release('drag');
        }
      } else if (a.kind === 'click') {
        link.click(a.button === 'left' ? BTN.left : BTN.right);
        buzz();
      } else if (a.kind === 'move') {
        link.move(a.dx, a.dy);
      } else {
        link.scroll(a.dx, a.dy);
      }
    }
  };
  const points = () => [...pointers.current.values()];
  const track = (e: PointerEvent) => {
    pointers.current.set(e.pointerId, { id: e.pointerId, x: e.clientX, y: e.clientY });
  };
  const lift = (e: PointerEvent) => {
    pointers.current.delete(e.pointerId);
    apply(engine.end(points(), e.timeStamp));
    if (pointers.current.size === 0) setTouching(false);
  };
  useEffect(
    () => () => {
      link.release('drag');
    },
    [link],
  );
  // `touch-action: none` is not honoured everywhere (iOS Safari still scrolls or bounces the page): cancel the touch
  // moves too, with a non-passive listener, and keep a pinch from zooming the page.
  useEffect(() => {
    const el = area.current;
    if (!el) return;
    const stop = (e: Event) => {
      e.preventDefault();
    };
    el.addEventListener('touchmove', stop, { passive: false });
    el.addEventListener('gesturestart', stop);
    return () => {
      el.removeEventListener('touchmove', stop);
      el.removeEventListener('gesturestart', stop);
    };
  }, []);

  return (
    <div class="padwrap" ref={area}>
      <div
        class={`touchpad${touching ? ' touching' : ''}`}
        role="application"
        aria-label={t('remote.pad')}
        data-testid="touchpad"
        onPointerDown={(e) => {
          e.currentTarget.setPointerCapture(e.pointerId);
          track(e);
          setTouching(true);
          apply(engine.start(points(), e.timeStamp));
        }}
        onPointerMove={(e) => {
          if (!pointers.current.has(e.pointerId)) return;
          track(e);
          apply(engine.move(points(), e.timeStamp));
        }}
        onPointerUp={lift}
        onPointerCancel={lift}
      >
        <ul class="legend" aria-hidden="true">
          <li>{t('remote.gesture.move')}</li>
          <li>{t('remote.gesture.tap')}</li>
          <li>{t('remote.gesture.scroll')}</li>
          <li>{t('remote.gesture.right')}</li>
          <li>{t('remote.gesture.drag')}</li>
        </ul>
      </div>
      <div
        class="scrollstrip"
        role="slider"
        aria-label={t('remote.strip')}
        aria-orientation="vertical"
        aria-valuemin={0}
        aria-valuemax={0}
        aria-valuenow={0}
        data-testid="scrollstrip"
        onPointerDown={(e) => {
          e.currentTarget.setPointerCapture(e.pointerId);
          strip.current = { y: e.clientY, t: e.timeStamp };
        }}
        onPointerMove={(e) => {
          const s = strip.current;
          if (!s) return;
          const speed = Math.abs(e.clientY - s.y) / Math.max(1, e.timeStamp - s.t);
          const boost = Math.min(3, 1 + speed);
          link.scroll(0, -(e.clientY - s.y) * STRIP_GAIN * boost); // Like a wheel: down scrolls down.
          strip.current = { y: e.clientY, t: e.timeStamp };
        }}
        onPointerUp={() => {
          strip.current = null;
        }}
        onPointerCancel={() => {
          strip.current = null;
        }}
      >
        <span aria-hidden="true">▲</span>
        <span aria-hidden="true">▼</span>
      </div>
    </div>
  );
}

/**
 * A key that is down while the finger is on it (the computer repeats it by itself while it is held).
 *
 * - `def`: The key.
 * - `press`: Called on touch with the key usage; returns nothing.
 * - `release`: Called on lift.
 * - `class`: Extra class.
 * @returns The button element.
 */
function Key({
  def,
  press,
  release,
  class: cls = '',
}: {
  def: KeyDef;
  press: (usage: number) => void;
  release: () => void;
  class?: string;
}) {
  const t = useT();
  const [down, setDown] = useState(false);
  const up = () => {
    setDown(false);
    release();
  };
  return (
    <button
      type="button"
      class={`key ${cls}${down ? ' down' : ''}`}
      aria-label={t(def.name)}
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        setDown(true);
        press(def.usage);
      }}
      onPointerUp={up}
      onPointerCancel={up}
    >
      {def.label}
    </button>
  );
}

/**
 * Sticky modifier key: tap to arm it for the next key, tap again to keep it on, once more to release it.
 *
 * - `label`: Visible text.
 * - `state`: Current state.
 * - `onTap`: Called on tap.
 * @returns The button element.
 */
function ModKey({ label, state, onTap }: { label: string; state: ModState; onTap: () => void }) {
  const t = useT();
  const stateName = t(`remote.mod.${state}`);
  return (
    <button
      type="button"
      class={`key mod ${state}`}
      aria-pressed={state !== 'off'}
      aria-label={`${label}: ${stateName}`}
      data-state={state}
      onClick={onTap}
    >
      {label}
      {state === 'locked' && <span class="lockmark" aria-hidden="true" />}
    </button>
  );
}

/**
 * The keyboard mode: typing field, modifiers, edit keys, arrows and shortcuts.
 *
 * - `link`: The link to the dongle.
 * - `settings`: Preferences (the Mac switch changes the shortcuts).
 * @returns The keyboard element.
 */
function Keys({ link, settings }: { link: RemoteLink; settings: RemoteSettings }) {
  const t = useT();
  const mods = useRef(new Modifiers());
  const [, redraw] = useState(0);
  const [value, setValue] = useState('');
  const prev = useRef('');
  const refresh = () => {
    redraw((n) => n + 1);
  };

  /**
   * Take the armed modifiers for a key and use them up.
   *
   * @returns The modifier byte.
   */
  const takeMods = () => {
    const m = mods.current.mask();
    mods.current.consume();
    refresh();
    return m;
  };
  const press = (usage: number) => {
    link.keyDown(usage, takeMods());
  };
  const tapCombo = (usage: number, m: number) => {
    link.tapKey(usage, m | takeMods());
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
    for (let i = before.length; i > common; i--) link.tapKey(KEY.backspace, 0);
    const typed = now.slice(common);
    if (typed.length > 0) link.type(typed, takeMods());
    if (now.length > TEXT_KEEP) {
      prev.current = '';
      setValue('');
    } else {
      prev.current = now;
      setValue(now);
    }
  };

  const primary = settings.mac ? MOD.gui : MOD.ctrl;
  const shortcuts: readonly { label: MessageKey; usage: number; mods: number }[] = [
    { label: 'remote.sc.copy', usage: KEY.c, mods: primary },
    { label: 'remote.sc.paste', usage: KEY.v, mods: primary },
    { label: 'remote.sc.cut', usage: KEY.x, mods: primary },
    { label: 'remote.sc.undo', usage: KEY.z, mods: primary },
    { label: 'remote.sc.all', usage: KEY.a, mods: primary },
    { label: 'remote.sc.switch', usage: KEY.tab, mods: settings.mac ? MOD.gui : MOD.alt },
    settings.mac
      ? { label: 'remote.sc.spotlight', usage: KEY.space, mods: MOD.gui }
      : { label: 'remote.sc.start', usage: 0, mods: MOD.gui },
  ];
  const mapping: readonly { bit: number; label: string }[] = [
    { bit: MOD.ctrl, label: 'Ctrl' },
    { bit: MOD.alt, label: settings.mac ? '⌥' : 'Alt' },
    { bit: MOD.shift, label: 'Shift' },
    { bit: MOD.gui, label: settings.mac ? '⌘' : 'Win' },
  ];
  const armed = mapping.filter((m) => mods.current.get(m.bit) !== 'off').map((m) => m.label);

  return (
    <div class="keys">
      <input
        type="text"
        class="typefield"
        value={value}
        aria-label={t('remote.text_label')}
        placeholder={t('remote.text_label')}
        autocomplete="off"
        autocapitalize="off"
        autocorrect="off"
        spellcheck={false}
        enterkeyhint="send"
        data-testid="remote-text"
        onInput={(e) => {
          changed(e.currentTarget.value);
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter') {
            e.preventDefault();
            link.tapKey(KEY.enter, takeMods());
            prev.current = '';
            setValue('');
          }
        }}
      />

      <section aria-label={t('remote.modifiers')}>
        <div class="modrow">
          {mapping.map((m) => (
            <ModKey
              key={m.bit}
              label={m.label}
              state={mods.current.get(m.bit)}
              onTap={() => {
                mods.current.tap(m.bit);
                refresh();
              }}
            />
          ))}
        </div>
        <p class="hint" role="status">
          {armed.length > 0
            ? t('remote.armed', { keys: armed.join(' + ') })
            : t('remote.modifiers_hint')}
        </p>
      </section>

      <section class="editgrid" aria-label={t('remote.special')}>
        {EDIT_KEYS.map((k) => (
          <Key
            key={k.usage}
            def={k}
            press={press}
            release={() => {
              link.keyUp();
            }}
          />
        ))}
      </section>

      <section class="arrows" aria-label={t('remote.arrows')}>
        {ARROWS.map((k) => (
          <Key
            key={k.usage}
            def={k}
            class={`a-${k.area}`}
            press={press}
            release={() => {
              link.keyUp();
            }}
          />
        ))}
      </section>

      <section aria-label={t('remote.shortcuts')}>
        <h3 class="mini">{t('remote.shortcuts')}</h3>
        <div class="chips">
          {shortcuts.map((s) => (
            <button
              key={s.label}
              type="button"
              class="chip"
              onClick={() => {
                tapCombo(s.usage, s.mods);
              }}
            >
              {t(s.label)}
            </button>
          ))}
        </div>
      </section>
    </div>
  );
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
  const [mode, setMode] = useState<'pad' | 'keys'>('pad');
  const [settings, setSettings] = useState(loadRemoteSettings);
  const link = useMemo(() => new RemoteLink(sendLiveJson), []);

  useEffect(
    () => () => {
      link.dispose();
    },
    [link],
  );
  useEffect(
    () =>
      subscribeState((c) => {
        setLinked(c);
      }),
    [],
  );

  const change = (patch: Partial<RemoteSettings>) => {
    const next = { ...settings, ...patch };
    setSettings(next);
    saveRemoteSettings(next);
  };
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
  const padMode = on && mode === 'pad';

  // In pad mode the page must not move at all: no scrolling, no pull-to-refresh, no rubber band.
  useEffect(() => {
    if (!padMode) return;
    const root = document.documentElement;
    root.classList.add('remote-lock');
    return () => {
      root.classList.remove('remote-lock');
    };
  }, [padMode]);

  return (
    <div class={`remote${padMode ? ' padmode' : ''}`}>
      <div class="remote-head">
        <a class="back" href="#/dashboard">
          <Icon name="back" />
          {t('common.back')}
        </a>
        <h2>{t('remote.heading')}</h2>
        {on && (
          <span class="timer" role="timer" data-testid="remote-timer">
            {t('remote.active', { time: mmss(left) })}
          </span>
        )}
      </div>

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
        <section class="card remote-off">
          <p>{t('remote.intro')}</p>
          <ul class="how">
            <li>{t('remote.gesture.move')}</li>
            <li>{t('remote.gesture.tap')}</li>
            <li>{t('remote.gesture.scroll')}</li>
            <li>{t('remote.gesture.right')}</li>
            <li>{t('remote.gesture.drag')}</li>
          </ul>
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
          <div class="seg" role="tablist" aria-label={t('remote.mode')}>
            <button
              type="button"
              role="tab"
              aria-selected={mode === 'pad'}
              class={mode === 'pad' ? 'on' : ''}
              onClick={() => {
                setMode('pad');
              }}
            >
              <Icon name="mouse" />
              {t('remote.mode.pad')}
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={mode === 'keys'}
              class={mode === 'keys' ? 'on' : ''}
              onClick={() => {
                setMode('keys');
              }}
            >
              <Icon name="keyboard" />
              {t('remote.mode.keys')}
            </button>
          </div>

          {mode === 'pad' && (
            <>
              <Pad link={link} settings={settings} />
              <div class="mbtns">
                <MouseButton link={link} owner="b-left" bit={BTN.left} label={t('remote.left')}>
                  {t('remote.left')}
                </MouseButton>
                <MouseButton
                  link={link}
                  owner="b-mid"
                  bit={BTN.middle}
                  label={t('remote.middle')}
                  class="mid"
                >
                  {t('remote.middle_short')}
                </MouseButton>
                <MouseButton link={link} owner="b-right" bit={BTN.right} label={t('remote.right')}>
                  {t('remote.right')}
                </MouseButton>
              </div>
            </>
          )}
          {mode === 'keys' && <Keys link={link} settings={settings} />}

          <details class="remote-settings">
            <summary>{t('remote.settings')}</summary>
            <label class="speed">
              <span>
                {t('remote.speed')}: <b>{settings.speed}</b>
              </span>
              <input
                type="range"
                min={SPEED_MIN}
                max={SPEED_MAX}
                step={1}
                value={settings.speed}
                aria-label={t('remote.speed')}
                data-testid="remote-speed"
                onInput={(e) => {
                  change({ speed: Number(e.currentTarget.value) });
                }}
              />
              <span class="scale" aria-hidden="true">
                <span>{t('remote.slow')}</span>
                <span>{t('remote.fast')}</span>
              </span>
            </label>
            <SwitchRow
              label={t('remote.natural')}
              hint={t('remote.natural_hint')}
              checked={settings.naturalScroll}
              onChange={(v) => {
                change({ naturalScroll: v });
              }}
            />
            <SwitchRow
              label={t('remote.mac')}
              hint={t('remote.mac_hint')}
              checked={settings.mac}
              onChange={(v) => {
                change({ mac: v });
              }}
            />
          </details>

          <div class="remote-foot">
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
        </>
      )}
    </div>
  );
}
