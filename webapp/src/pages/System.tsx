/**
 * @file System.tsx
 * @description System page: diagnostics, live log, reboot actions and Wi-Fi password change
 * (CLAUDE.md §3.7 page 8; backup/restore and factory reset arrive in M5).
 */
import { useEffect, useState } from 'preact/hooks';
import { getDiagnostics, rebootDownload, reboot, setWifiPassword } from '../api/client';
import { subscribe } from '../api/live';
import { useT } from '../i18n/context';
import { formatBytes, formatDuration } from '../lib/format';
import { passwordProblem } from '../lib/password';
import { useApi } from '../lib/useApi';

/** Diagnostics refresh period. */
const REFRESH_MS = 5000;
/** Maximum log lines kept in the page. */
const MAX_LOG_LINES = 200;

/**
 * Live log panel fed by `log.line` WebSocket events.
 *
 * @returns The panel element.
 */
function LiveLog() {
  const t = useT();
  const [lines, setLines] = useState<string[]>([]);

  useEffect(
    () =>
      subscribe((m) => {
        if (m.event !== 'log.line') return;
        const text = (m.data as { text?: unknown } | null)?.text;
        if (typeof text !== 'string') return;
        const fresh = text.split('\n').filter((l) => l.length > 0);
        setLines((old) => [...old, ...fresh].slice(-MAX_LOG_LINES));
      }),
    [],
  );

  return (
    <section aria-labelledby="log-h">
      <h3 id="log-h">{t('system.logs')}</h3>
      <pre class="log" tabIndex={0} aria-live="off" data-testid="log">
        {lines.length === 0 ? t('system.logs_empty') : lines.join('\n')}
      </pre>
      <p>
        <a href="/api/v1/logs" download="hubble.log">
          {t('system.logs_download')}
        </a>
      </p>
    </section>
  );
}

/**
 * A button that needs a second click to confirm a disruptive action.
 *
 * @param props.label Button text.
 * @param props.onConfirm Called after the user confirms.
 * @returns The control element.
 */
function ConfirmButton({ label, onConfirm }: { label: string; onConfirm: () => void }) {
  const t = useT();
  const [asking, setAsking] = useState(false);
  if (!asking) {
    return (
      <button
        type="button"
        onClick={() => {
          setAsking(true);
        }}
      >
        {label}
      </button>
    );
  }
  return (
    <span role="group" aria-label={label}>
      <button
        type="button"
        class="danger"
        onClick={() => {
          setAsking(false);
          onConfirm();
        }}
      >
        {t('common.confirm')}: {label}
      </button>{' '}
      <button
        type="button"
        onClick={() => {
          setAsking(false);
        }}
      >
        {t('common.cancel')}
      </button>
    </span>
  );
}

/**
 * Wi-Fi password form with client-side validation matching the firmware rules.
 *
 * @returns The form element.
 */
function PasswordForm() {
  const t = useT();
  const [pw, setPw] = useState('');
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const problem = pw === '' ? null : passwordProblem(pw);

  const save = async () => {
    try {
      await setWifiPassword(pw);
      setMsg({ ok: true, text: t('system.password_saved') });
      setPw('');
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <form
      aria-labelledby="pw-h"
      onSubmit={(e) => {
        e.preventDefault();
        void save();
      }}
    >
      <h3 id="pw-h">{t('system.password')}</h3>
      <label>
        {t('system.password_new')}{' '}
        <input
          type="password"
          autocomplete="new-password"
          value={pw}
          aria-invalid={problem !== null}
          aria-describedby="pw-hint"
          onInput={(e) => {
            setPw(e.currentTarget.value);
            setMsg(null);
          }}
        />
      </label>
      <p id="pw-hint" class="muted">
        {problem ? t(`password.${problem}`) : t('system.password_hint')}
      </p>
      <button type="submit" disabled={pw === '' || problem !== null}>
        {t('system.password_save')}
      </button>
      {msg && <p role={msg.ok ? 'status' : 'alert'}>{msg.text}</p>}
    </form>
  );
}

/**
 * Render the system page.
 *
 * @returns The page element.
 */
export function System() {
  const t = useT();
  const { data: d, failed } = useApi(getDiagnostics, REFRESH_MS);
  const [sent, setSent] = useState(false);

  const send = (fn: () => Promise<void>) => {
    fn().then(
      () => {
        setSent(true);
      },
      () => {
        setSent(false);
      },
    );
  };

  return (
    <section aria-labelledby="sys-h">
      <h2 id="sys-h">{t('system.heading')}</h2>
      <h3>{t('system.diagnostics')}</h3>
      {failed && !d && <p role="alert">{t('common.error')}</p>}
      {d && (
        <dl>
          <dt>{t('system.uptime')}</dt>
          <dd>{formatDuration(d.uptime_s)}</dd>
          <dt>{t('system.reset_reason')}</dt>
          <dd>{d.reset_reason}</dd>
          <dt>{t('system.heap')}</dt>
          <dd data-testid="heap">{formatBytes(d.heap.free)}</dd>
          <dt>{t('system.psram')}</dt>
          <dd>{formatBytes(d.psram.free)}</dd>
          <dt>{t('system.partition')}</dt>
          <dd>{d.partitions.running.label}</dd>
          <dt>{t('system.boot_failures')}</dt>
          <dd>{d.boot_failures}</dd>
        </dl>
      )}

      <LiveLog />

      <h3>{t('system.actions')}</h3>
      <p>
        <ConfirmButton
          label={t('system.reboot')}
          onConfirm={() => {
            send(reboot);
          }}
        />
      </p>
      <p>
        <ConfirmButton
          label={t('system.reboot_download')}
          onConfirm={() => {
            send(rebootDownload);
          }}
        />
      </p>
      <p class="muted">{t('system.reboot_download_hint')}</p>
      {sent && <p role="status">{t('system.action_done')}</p>}

      <PasswordForm />
    </section>
  );
}
