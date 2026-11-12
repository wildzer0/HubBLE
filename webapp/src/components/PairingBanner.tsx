/**
 * @file PairingBanner.tsx
 * @description A bar shown on every page except the pairing page while the dongle is in the middle of a pairing
 * (scanning, connecting, passkey, challenge, confirmation). It offers to go back to the pairing page or to cancel,
 * so a pairing that was left behind can never trap the dongle in its challenge.
 */
import { useEffect, useState } from 'preact/hooks';
import { cancelPairing, getScan } from '../api/client';
import { subscribe } from '../api/live';
import type { Pairing } from '../api/types';
import { useT } from '../i18n/context';

/** Poll period; the WebSocket events make changes show up sooner. */
const POLL_MS = 3000;
/** States in which a pairing is under way. */
const ACTIVE = ['scanning', 'connecting', 'pairing', 'quarantine', 'confirm'];

/**
 * Render the banner (nothing when no pairing is under way).
 *
 * @returns The banner element or null.
 */
export function PairingBanner() {
  const t = useT();
  const [p, setP] = useState<Pairing | null>(null);

  useEffect(() => {
    let alive = true;
    const run = () => {
      getScan().then(
        (r) => {
          if (alive) setP(r.pairing);
        },
        () => undefined, // an unreachable dongle is reported by the pages themselves
      );
    };
    run();
    const timer = setInterval(run, POLL_MS);
    const off = subscribe((m) => {
      if (m.event.startsWith('pairing.') && alive) setP(m.data as Pairing);
    });
    return () => {
      alive = false;
      clearInterval(timer);
      off();
    };
  }, []);

  if (!p || !ACTIVE.includes(p.state)) return null;
  return (
    <div class="alert warn banner" role="status" data-testid="pairing-banner">
      <span>{t('pair.banner')}</span>
      <span class="actions">
        <a class="btn" href="#/pair">
          {t('pair.banner_open')}
        </a>
        <button
          type="button"
          class="btn danger"
          onClick={() => {
            cancelPairing().then(
              () => {
                setP({ ...p, state: 'idle' });
              },
              () => undefined,
            );
          }}
        >
          {t('common.cancel')}
        </button>
      </span>
    </div>
  );
}
