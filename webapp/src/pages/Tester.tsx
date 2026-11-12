/**
 * @file Tester.tsx
 * @description Input tester (CLAUDE.md §3.7 page 5): live view of what each device sends. Streaming runs
 * only while the user has explicitly enabled it and the page is open; nothing is stored (§3.12).
 */
import { useEffect, useState } from 'preact/hooks';
import { sendLive, subscribe, subscribeState } from '../api/live';
import type { InputEvent } from '../api/types';
import { Icon } from '../components/ui';
import { useT } from '../i18n/context';
import { describeInput } from '../lib/inputFormat';

/** Renewal period of the subscription (the dongle expires it after 15 s). */
const RENEW_MS = 5000;
/** Events kept on screen. */
const MAX_EVENTS = 50;

/** One line of the on-screen list. */
interface Line {
  /** Increasing id used as the list key. */
  n: number;
  /** Source slot. */
  slot: number;
  /** Description of the event. */
  text: string;
}

/**
 * Render the input tester.
 *
 * @returns The page element.
 */
export function Tester() {
  const t = useT();
  const [on, setOn] = useState(false);
  const [lines, setLines] = useState<Line[]>([]);

  useEffect(() => {
    if (!on) return;
    let n = 0;
    // The subscription is per connection, so send it again after every reconnect.
    const offState = subscribeState((c) => {
      if (c) sendLive('input.subscribe');
    });
    // The dongle drops a subscription 15 s after the last request, so a closed page stops the stream by itself.
    const renew = setInterval(() => {
      sendLive('input.subscribe');
    }, RENEW_MS);
    const off = subscribe((m) => {
      if (m.event !== 'input.event') return;
      const e = m.data as InputEvent;
      setLines((old) =>
        [{ n: n++, slot: e.slot, text: describeInput(e) }, ...old].slice(0, MAX_EVENTS),
      );
    });
    return () => {
      off();
      offState();
      clearInterval(renew);
      sendLive('input.unsubscribe');
    };
  }, [on]);

  return (
    <>
      <div class="page-head">
        <a class="back" href="#/dashboard">
          <Icon name="back" />
          {t('common.back')}
        </a>
      </div>
      <h2>{t('tester.heading')}</h2>
      <p class="hint">{t('tester.privacy')}</p>
      <button
        type="button"
        class={`btn block ${on ? '' : 'primary'}`}
        aria-pressed={on}
        onClick={() => {
          setOn(!on);
          if (on) setLines([]);
        }}
      >
        {on ? t('tester.stop') : t('tester.start')}
      </button>
      {on && lines.length === 0 && (
        <p class="muted" role="status">
          {t('tester.waiting')}
        </p>
      )}
      <ul class="events" data-testid="events" aria-live="off">
        {lines.map((l) => (
          <li key={l.n}>
            <span class="muted">#{l.slot + 1}</span> {l.text}
          </li>
        ))}
      </ul>
    </>
  );
}
