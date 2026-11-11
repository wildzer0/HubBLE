/**
 * @file Update.tsx
 * @description Update page: slot versions, upload of a `.bin`, progress and result (CLAUDE.md §3.7 page 7).
 * M6 adds "check GitHub for updates"; M1 only supports file upload.
 */
import { useRef, useState } from 'preact/hooks';
import { ApiError, getOtaStatus, getStatus, uploadFirmware } from '../api/client';
import { useT } from '../i18n/context';
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
  const { data: ota } = useApi(getOtaStatus, 0);
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

  const busy = phase.kind === 'uploading' || phase.kind === 'rebooting';

  return (
    <section aria-labelledby="update-h">
      <h2 id="update-h">{t('update.heading')}</h2>
      {ota && (
        <>
          <dl>
            <dt>{t('update.running')}</dt>
            <dd data-testid="running-version">
              {ota.running.version} <small>({ota.running.slot})</small>
            </dd>
            <dt>{t('update.other')}</dt>
            <dd>
              {ota.other.version || t('update.slot_empty')} <small>({ota.other.slot})</small>
            </dd>
            {ota.last_result && (
              <>
                <dt>{t('update.last_result')}</dt>
                <dd>{ota.last_result}</dd>
              </>
            )}
          </dl>
          {ota.pending_verify && <p class="notice">{t('update.pending')}</p>}
          {ota.rollback_happened && <p class="notice">{t('update.rollback')}</p>}
        </>
      )}

      <form
        onSubmit={(e) => {
          e.preventDefault();
          void start();
        }}
      >
        <label>
          {t('update.choose')}{' '}
          <input
            ref={input}
            type="file"
            accept=".bin"
            disabled={busy}
            onChange={(e) => {
              setFile(e.currentTarget.files?.[0] ?? null);
            }}
          />
        </label>
        <p>
          <button type="submit" disabled={!file || busy}>
            {t('update.upload')}
          </button>
        </p>
        <p class="muted">{t('update.hint')}</p>
      </form>

      <div aria-live="polite">
        {phase.kind === 'uploading' && (
          <p>
            <progress aria-label={t('update.progress_label')} max={100} value={phase.pct} />{' '}
            {t('update.uploading', { pct: phase.pct })}
          </p>
        )}
        {phase.kind === 'rebooting' && (
          <p>
            {t('update.installed', { version: phase.version })} {t('update.waiting')}
          </p>
        )}
        {phase.kind === 'done' && <p>{t('update.back_online', { version: phase.version })}</p>}
        {phase.kind === 'error' && (
          <p role="alert">{t('update.failed', { message: phase.message })}</p>
        )}
      </div>
    </section>
  );
}
