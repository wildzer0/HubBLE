/**
 * @file Pair.tsx
 * @description Pairing flow (CLAUDE.md §3.4 and §3.7 page 3): scan list, passkey screen, quarantine
 * challenge, physical confirmation and result, with live countdown.
 */
import { useState } from 'preact/hooks';
import { ApiError, cancelPairing, startPairing, startScan } from '../api/client';
import type { Pairing, PairFailure } from '../api/types';
import { SecurityBadge } from '../components/SecurityBadge';
import { useT } from '../i18n/context';
import type { TFunction } from '../i18n/context';
import { usePairing } from '../lib/usePairing';

/** GAP appearance of a keyboard. */
const APPEARANCE_KEYBOARD = 961;
/** GAP appearance of a mouse. */
const APPEARANCE_MOUSE = 962;

/**
 * Icon for an advertised appearance.
 *
 * @param appearance GAP appearance value.
 * @returns An emoji (decorative).
 */
function iconFor(appearance: number): string {
  if (appearance === APPEARANCE_KEYBOARD) return '⌨️';
  if (appearance === APPEARANCE_MOUSE) return '🖱️';
  return '🔘';
}

/**
 * Translated text of a pairing failure.
 *
 * @param t Translation function.
 * @param reason Failure reason from the dongle.
 * @returns A sentence for the user.
 */
function failureText(t: TFunction, reason: PairFailure | undefined): string {
  return t(`pair.fail.${reason ?? 'security_failed'}`);
}

/**
 * Challenge display: the click sequence (with progress) or the code to type.
 *
 * @param props.p Pairing status with a challenge.
 * @returns The challenge element.
 */
function Challenge({ p }: { p: Pairing }) {
  const t = useT();
  const ch = p.challenge;
  if (!ch) return null;
  if (ch.kind === 'code') {
    return (
      <>
        <p>{t('pair.challenge_code')}</p>
        <p class="big" data-testid="challenge" aria-label={ch.value.split('').join(' ')}>
          {ch.value}
        </p>
      </>
    );
  }
  const names: Record<string, string> = {
    L: t('pair.click_left'),
    R: t('pair.click_right'),
    M: t('pair.click_middle'),
  };
  return (
    <>
      <p>{t('pair.challenge_clicks')}</p>
      <ol class="clicks" data-testid="challenge">
        {ch.value.split('').map((c, i) => (
          <li key={i} class={i < ch.progress ? 'done' : ''}>
            {names[c] ?? c}
          </li>
        ))}
      </ol>
      <p role="status">{t('pair.progress', { n: ch.progress, total: ch.length })}</p>
    </>
  );
}

/**
 * Render the pairing page.
 *
 * @returns The page element.
 */
export function Pair() {
  const t = useT();
  const { pairing: p, results, failed, refresh } = usePairing();
  const [allowLegacy, setAllowLegacy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const act = async (fn: () => Promise<void>) => {
    setError(null);
    try {
      await fn();
    } catch (e) {
      setError(
        e instanceof ApiError && e.code === 'pairing_locked'
          ? t('pair.locked_error')
          : e instanceof Error
            ? e.message
            : String(e),
      );
    }
    refresh();
  };

  const state = p?.state ?? 'idle';
  const locked = (p?.lock_remaining_s ?? 0) > 0;

  return (
    <section aria-labelledby="pair-h">
      <h2 id="pair-h">{t('pair.heading')}</h2>
      {failed && !p && <p role="alert">{t('common.error')}</p>}
      {error && <p role="alert">{error}</p>}
      {locked && (
        <p role="alert" data-testid="locked">
          {t('pair.locked', { s: p?.lock_remaining_s ?? 0 })}
        </p>
      )}

      {(state === 'idle' || state === 'bonded' || state === 'failed') && (
        <>
          {state === 'bonded' && p && (
            <div role="status" class="notice" data-testid="result-ok">
              <p>{t('pair.bonded', { name: p.device })}</p>
              <SecurityBadge level={p.level} />
              <p>
                <a href="#/dashboard">{t('pair.done')}</a>
              </p>
            </div>
          )}
          {state === 'failed' && (
            <p role="alert" data-testid="result-fail">
              {failureText(t, p?.reason)}
            </p>
          )}
          <p class="muted">{t('pair.intro')}</p>
          <label>
            <input
              type="checkbox"
              checked={allowLegacy}
              onChange={(e) => {
                setAllowLegacy(e.currentTarget.checked);
              }}
            />{' '}
            {t('pair.allow_legacy')}
          </label>
          {allowLegacy && <p class="warn">{t('pair.legacy_warning')}</p>}
          <p>
            <button
              type="button"
              disabled={locked}
              onClick={() => {
                void act(startScan);
              }}
            >
              {t('pair.scan')}
            </button>
          </p>
        </>
      )}

      {state === 'scanning' && (
        <>
          <p role="status">{t('pair.scanning')}</p>
          {results.length === 0 && <p class="muted">{t('pair.no_results')}</p>}
          <ul class="cards">
            {results.map((r) => (
              <li key={r.address} class="card" data-testid="scan-result">
                <span aria-hidden="true">{iconFor(r.appearance)}</span>{' '}
                <strong>{r.name || r.address}</strong>
                <span class="muted"> {r.rssi} dBm</span>
                <p>
                  <button
                    type="button"
                    onClick={() => {
                      void act(() => startPairing(r.address, r.address_type, allowLegacy));
                    }}
                  >
                    {t('pair.pair_with', { name: r.name || r.address })}
                  </button>
                </p>
              </li>
            ))}
          </ul>
        </>
      )}

      {state === 'connecting' && p && (
        <p role="status">{t('pair.connecting', { name: p.device })}</p>
      )}

      {state === 'pairing' && p && (
        <>
          <p>{t('pair.type_passkey', { name: p.device })}</p>
          {p.passkey ? (
            <p class="big" data-testid="passkey" aria-label={p.passkey.split('').join(' ')}>
              {p.passkey}
            </p>
          ) : (
            <p role="status">{t('pair.waiting_device')}</p>
          )}
        </>
      )}

      {state === 'quarantine' && p && (
        <>
          <p class="notice">{t('pair.quarantine', { name: p.device })}</p>
          <Challenge p={p} />
        </>
      )}

      {state === 'confirm' && p && (
        <p class="notice" data-testid="confirm">
          {t('pair.confirm')}
        </p>
      )}

      {p && ['connecting', 'pairing', 'quarantine', 'confirm'].includes(state) && (
        <p class="muted" role="timer">
          {t('pair.remaining', { s: p.remaining_s })}
        </p>
      )}

      {(state === 'scanning' ||
        ['connecting', 'pairing', 'quarantine', 'confirm'].includes(state)) && (
        <p>
          <button
            type="button"
            onClick={() => {
              void act(cancelPairing);
            }}
          >
            {t('common.cancel')}
          </button>
        </p>
      )}
    </section>
  );
}
