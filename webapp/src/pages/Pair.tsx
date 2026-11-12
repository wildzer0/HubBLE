/**
 * @file Pair.tsx
 * @description Pairing flow (CLAUDE.md §3.4 and §3.7 page 3): scan list, passkey screen, quarantine
 * challenge, physical confirmation and result, with live countdown.
 */
import { useState } from 'preact/hooks';
import { ApiError, cancelPairing, startPairing, startScan } from '../api/client';
import type { Pairing, PairFailure } from '../api/types';
import { SecurityBadge } from '../components/SecurityBadge';
import { Alert, Icon, SwitchRow } from '../components/ui';
import type { IconName } from '../components/ui';
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
 * @returns The icon name.
 */
function iconFor(appearance: number): IconName {
  if (appearance === APPEARANCE_KEYBOARD) return 'keyboard';
  if (appearance === APPEARANCE_MOUSE) return 'mouse';
  return 'devices';
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
        <p class="hint" data-testid="rx">
          {t('pair.rx', { n: p.rx_reports ?? 0 })}
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
      <p class="muted" role="status">
        {t('pair.progress', { n: ch.progress, total: ch.length })}
      </p>
      <p class="hint" data-testid="rx">
        {t('pair.rx', { n: p.rx_reports ?? 0 })}
      </p>
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

  const busyStates = ['connecting', 'pairing', 'quarantine', 'confirm'];

  return (
    <>
      <div class="page-head">
        <a class="back" href="#/dashboard">
          <Icon name="back" />
          {t('common.back')}
        </a>
      </div>
      <h2 id="pair-h">{t('pair.heading')}</h2>
      {failed && !p && (
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
        <Alert kind="warn" role="alert" testId="locked">
          {t('pair.locked', { s: p?.lock_remaining_s ?? 0 })}
        </Alert>
      )}

      {(state === 'idle' || state === 'bonded' || state === 'failed') && (
        <>
          {state === 'bonded' && p && (
            <div class="card" role="status" data-testid="result-ok">
              <p class="card-title">{t('pair.bonded', { name: p.device })}</p>
              <div>
                <SecurityBadge level={p.level} />
              </div>
              <a class="btn primary" href="#/dashboard">
                {t('pair.done')}
              </a>
            </div>
          )}
          {state === 'failed' && (
            <Alert kind="bad" role="alert" testId="result-fail">
              {failureText(t, p?.reason)}
            </Alert>
          )}
          <p class="hint">{t('pair.intro')}</p>
          <div class="card">
            <SwitchRow
              label={t('pair.allow_legacy')}
              checked={allowLegacy}
              onChange={setAllowLegacy}
            />
            {allowLegacy && <Alert kind="warn">{t('pair.legacy_warning')}</Alert>}
          </div>
          <button
            type="button"
            class="btn primary block"
            disabled={locked}
            onClick={() => {
              void act(startScan);
            }}
          >
            {t('pair.scan')}
          </button>
        </>
      )}

      {state === 'scanning' && (
        <>
          <p role="status">{t('pair.scanning')}</p>
          {results.length === 0 && <p class="muted">{t('pair.no_results')}</p>}
          <ul class="found">
            {results.map((r) => (
              <li key={r.address} data-testid="scan-result">
                <button
                  type="button"
                  aria-label={t('pair.pair_with', { name: r.name || r.address })}
                  onClick={() => {
                    void act(() => startPairing(r.address, r.address_type, allowLegacy));
                  }}
                >
                  <Icon name={iconFor(r.appearance)} />
                  <span class="grow">{r.name || r.address}</span>
                  <span class="muted">{r.rssi} dBm</span>
                </button>
              </li>
            ))}
          </ul>
        </>
      )}

      {state === 'connecting' && p && (
        <p role="status">{t('pair.connecting', { name: p.device })}</p>
      )}

      {state === 'pairing' && p && (
        <div class="card">
          <p>{t('pair.type_passkey', { name: p.device })}</p>
          {p.passkey ? (
            <p class="big" data-testid="passkey" aria-label={p.passkey.split('').join(' ')}>
              {p.passkey}
            </p>
          ) : (
            <p role="status">{t('pair.waiting_device')}</p>
          )}
        </div>
      )}

      {state === 'quarantine' && p && (
        <div class="card">
          <Alert>{t('pair.quarantine', { name: p.device })}</Alert>
          <Challenge p={p} />
        </div>
      )}

      {state === 'confirm' && p && <Alert testId="confirm">{t('pair.confirm')}</Alert>}

      {p && busyStates.includes(state) && (
        <p class="muted" role="timer">
          {t('pair.remaining', { s: p.remaining_s })}
        </p>
      )}

      {(state === 'scanning' || busyStates.includes(state)) && (
        <button
          type="button"
          class="btn block"
          onClick={() => {
            void act(cancelPairing);
          }}
        >
          {t('common.cancel')}
        </button>
      )}
    </>
  );
}
