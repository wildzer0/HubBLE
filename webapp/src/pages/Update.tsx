/**
 * @file Update.tsx
 * @description Update page: slot versions, upload of a `.bin`, progress and result (CLAUDE.md §3.7 page 7).
 * M6 adds "check GitHub for updates"; M1 only supports file upload.
 */
import { useRef, useState } from 'preact/hooks';
import { ApiError, getOtaStatus, getStatus, uploadFirmware } from '../api/client';
import { Alert } from '../components/ui';
import { useT } from '../i18n/context';
import { OnlineUpdateSection } from '../components/OnlineUpdate';
import { isLimitedBrowser } from '../lib/embeddedBrowser';
import { useApi } from '../lib/useApi';

/** Interval between "is the dongle back?" polls after an install. */
const POLL_MS = 2000;
/** Give up waiting for the dongle after this many polls (~2 min). */
const POLL_MAX = 60;

/** Phases of an update. */
type Phase =
  | { kind: 'idle' }
  | { kind: 'uploading'; pct: number }
  | { kind: 'rebooting'; version: string }
  | { kind: 'done'; version: string }
  | { kind: 'error'; message: string };

/**
 * Wait until the dongle answers `/status` again after a reboot.
 *
 * @returns The version it reports, or null if it did not come back in time.
 */
async function waitForDongle(): Promise<string | null> {
  // The restart takes a moment to begin, so wait before the first poll.
  for (let i = 0; i < POLL_MAX; i++) {
    await new Promise((r) => setTimeout(r, POLL_MS));
    try {
      return (await getStatus()).version;
    } catch {
      /* Still rebooting: keep polling. */
    }
  }
  return null;
}

/**
 * Render the update page.
 *
 * @returns The page element.
 */
export function Update() {
  const t = useT();
  const { data: ota, reload } = useApi(getOtaStatus, 0);
  const [phase, setPhase] = useState<Phase>({ kind: 'idle' });
  const [file, setFile] = useState<File | null>(null);
  const input = useRef<HTMLInputElement>(null);

  const start = async () => {
    if (!file) return;
    setPhase({ kind: 'uploading', pct: 0 });
    try {
      const { version } = await uploadFirmware(file, (f) => {
        setPhase({ kind: 'uploading', pct: Math.round(f * 100) });
      });
      setPhase({ kind: 'rebooting', version });
      const back = await waitForDongle();
      setPhase({ kind: 'done', version: back ?? version });
    } catch (e) {
      setPhase({ kind: 'error', message: e instanceof ApiError ? e.message : String(e) });
    }
  };

  // Choosing a file does not work reliably in the phone browsers that reach the dongle over its own Wi-Fi (the sign-in
  // browser cannot open a file chooser at all), so the manual upload is offered only where it works: the hosted copy
  // of the app (over USB), the development server and the tests. Phones use the online update instead.
  const onDongle = location.hostname === '192.168.4.1' || location.hostname === 'hubble.local';
  const showManual = import.meta.env.DEV || !onDongle;
  const busy = phase.kind === 'uploading' || phase.kind === 'rebooting';

  return (
    <>
      <h2 id="update-h">{t('update.heading')}</h2>
      {ota && (
        <section class="card" aria-label={t('update.running')}>
          <dl class="rows">
            <div class="row">
              <dt>{t('update.running')}</dt>
              <dd data-testid="running-version">
                {ota.running.version} <small>({ota.running.slot})</small>
              </dd>
            </div>
            <div class="row">
              <dt>{t('update.other')}</dt>
              <dd>
                {ota.other.version || t('update.slot_empty')} <small>({ota.other.slot})</small>
              </dd>
            </div>
            {ota.last_result && (
              <div class="row">
                <dt>{t('update.last_result')}</dt>
                <dd>{ota.last_result}</dd>
              </div>
            )}
          </dl>
          {ota.pending_verify && <Alert kind="warn">{t('update.pending')}</Alert>}
          {ota.rollback_happened && <Alert kind="warn">{t('update.rollback')}</Alert>}
        </section>
      )}

      {ota && <OnlineUpdateSection online={ota.online} refresh={reload} />}

      {showManual && (
        <form
          class="card"
          onSubmit={(e) => {
            e.preventDefault();
            void start();
          }}
        >
          <h3 class="card-title">{t('update.manual')}</h3>
          {isLimitedBrowser(navigator.userAgent) && (
            <Alert kind="warn" testId="limited-browser">
              {t('update.limited_browser')}
            </Alert>
          )}
          <label class="field">
            <span>{t('update.choose')}</span>
            <input
              ref={input}
              class="input"
              type="file"
              // No `accept` filter: Android pickers grey out every file when only an extension is given for an unknown MIME type.
              disabled={busy}
              onChange={(e) => {
                setFile(e.currentTarget.files?.[0] ?? null);
              }}
            />
          </label>
          <button type="submit" class="btn primary" disabled={!file || busy}>
            {t('update.upload')}
          </button>
          <p class="hint">{t('update.hint')}</p>
          <div aria-live="polite" class="stack">
            {phase.kind === 'uploading' && (
              <>
                <progress aria-label={t('update.progress_label')} max={100} value={phase.pct} />
                <p>{t('update.uploading', { pct: phase.pct })}</p>
              </>
            )}
            {phase.kind === 'rebooting' && (
              <Alert>
                {t('update.installed', { version: phase.version })} {t('update.waiting')}
              </Alert>
            )}
            {phase.kind === 'done' && (
              <Alert kind="ok">{t('update.back_online', { version: phase.version })}</Alert>
            )}
            {phase.kind === 'error' && (
              <Alert kind="bad" role="alert">
                {t('update.failed', { message: phase.message })}
              </Alert>
            )}
          </div>
        </form>
      )}
    </>
  );
}
