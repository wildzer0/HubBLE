/**
 * @file OnlineUpdate.tsx
 * @description "Check for updates" section of the Update page (CLAUDE.md §3.7 page 7, §3.8): channel, check,
 * release notes, downgrade confirmation, physical confirmation hint, download progress and result.
 */
import { useEffect, useState } from 'preact/hooks';
import { ApiError, checkForUpdates, installUpdate } from '../api/client';
import { subscribe } from '../api/live';
import type { OnlineUpdate } from '../api/types';
import { useT } from '../i18n/context';
import { Alert } from './ui';

/** States in which the engine is working and the page must keep refreshing. */
const ACTIVE = ['connecting', 'checking', 'confirm', 'downloading', 'installed'];

/**
 * Render the online update controls.
 *
 * - `online`: State from `GET /ota/status`.
 * - `refresh`: Reload the status now.
 * @returns The section element.
 */
export function OnlineUpdateSection({
  online,
  refresh,
}: {
  online: OnlineUpdate;
  refresh: () => void;
}) {
  const t = useT();
  const [channel, setChannel] = useState<'stable' | 'prerelease'>(online.channel);
  const [downgradeOk, setDowngradeOk] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Live updates: the dongle pushes `ota.state`; polling in the parent is the fallback.
  useEffect(
    () =>
      subscribe((m) => {
        if (m.event === 'ota.state') refresh();
      }),
    [],
  );
  // Keep refreshing while the engine works (it changes state without being asked).
  useEffect(() => {
    if (!ACTIVE.includes(online.state)) return;
    const id = setInterval(refresh, 1000);
    return () => {
      clearInterval(id);
    };
  }, [online.state]);

  const run = async (fn: () => Promise<void>) => {
    setError(null);
    try {
      await fn();
    } catch (e) {
      setError(
        e instanceof ApiError && e.code === 'no_wifi_credentials'
          ? t('online.error.no_wifi_credentials')
          : e instanceof Error
            ? e.message
            : String(e),
      );
    }
    refresh();
  };

  const rel = online.release;
  const working = ACTIVE.includes(online.state);
  const pct =
    online.total && online.received !== undefined
      ? Math.floor((online.received * 100) / online.total)
      : 0;

  return (
    <section class="card" aria-labelledby="online-h" data-testid="online">
      <h3 class="card-title" id="online-h">
        {t('online.heading')}
      </h3>
      {!online.wifi_configured ? (
        <Alert>
          {t('online.no_wifi')} <a href="#/settings">{t('online.go_settings')}</a>
        </Alert>
      ) : (
        <>
          <label class="field">
            <span>{t('online.channel')}</span>
            <select
              class="input"
              value={channel}
              disabled={working}
              onChange={(e) => {
                setChannel(e.currentTarget.value === 'prerelease' ? 'prerelease' : 'stable');
              }}
            >
              <option value="stable">{t('online.channel_stable')}</option>
              <option value="prerelease">{t('online.channel_prerelease')}</option>
            </select>
          </label>
          <button
            type="button"
            class="btn primary"
            disabled={working}
            onClick={() => {
              setDowngradeOk(false);
              void run(() => checkForUpdates(channel));
            }}
          >
            {t('online.check')}
          </button>
          <p class="hint">{t('online.ap_hint')}</p>
        </>
      )}
      {error && (
        <Alert kind="bad" role="alert">
          {error}
        </Alert>
      )}

      <div aria-live="polite" data-testid="online-status" class="stack">
        {online.state === 'connecting' && <p>{t('online.connecting')}</p>}
        {online.state === 'checking' && <p>{t('online.checking')}</p>}
        {online.state === 'up_to_date' && <Alert kind="ok">{t('online.up_to_date')}</Alert>}
        {online.state === 'error' && online.error && (
          <Alert kind="bad" role="alert">
            {t(`online.error.${online.error}`)}
            {online.detail && (
              <p class="hint" data-testid="online-detail">
                {t('online.detail')} <code>{online.detail}</code>
              </p>
            )}
          </Alert>
        )}
        {online.state === 'confirm' && (
          <Alert kind="warn">{t('online.confirm', { s: online.confirm_remaining_s ?? 0 })}</Alert>
        )}
        {online.state === 'downloading' && (
          <>
            <progress aria-label={t('update.progress_label')} max={100} value={pct} />
            <p>{t('update.uploading', { pct })}</p>
          </>
        )}
        {online.state === 'installed' && <Alert kind="ok">{t('online.installed')}</Alert>}
      </div>

      {rel && (online.state === 'available' || online.state === 'error') && (
        <div class="stack" data-testid="release">
          <hr class="divider" />
          <h4 class="card-title">{t('online.found', { version: rel.version })}</h4>
          <p class="hint">
            {(rel.size / (1024 * 1024)).toFixed(2)} MiB ·{' '}
            {rel.signed ? t('online.signed') : t('online.unsigned')}
            {rel.notes_url && (
              <>
                {' · '}
                <a href={rel.notes_url} target="_blank" rel="noopener noreferrer">
                  {t('online.notes')}
                </a>
              </>
            )}
          </p>
          {rel.downgrade && online.state === 'available' && (
            <Alert kind="warn">
              <label>
                <input
                  type="checkbox"
                  checked={downgradeOk}
                  onChange={(e) => {
                    setDowngradeOk(e.currentTarget.checked);
                  }}
                />{' '}
                {t('online.downgrade', { version: rel.version })}
              </label>
            </Alert>
          )}
          {online.state === 'available' && (
            <>
              <button
                type="button"
                class="btn primary"
                disabled={rel.downgrade && !downgradeOk}
                onClick={() => void run(() => installUpdate(downgradeOk))}
              >
                {t('online.install', { version: rel.version })}
              </button>
              <p class="hint">{t('online.confirm_hint')}</p>
            </>
          )}
        </div>
      )}
    </section>
  );
}
