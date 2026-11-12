/**
 * @file System.tsx
 * @description System page: diagnostics, live log, reboot actions and Wi-Fi password change
 * backup/restore and factory reset (CLAUDE.md §3.7 page 8).
 */
import { useEffect, useState } from 'preact/hooks';
import {
  factoryReset,
  getBackup,
  getDiagnostics,
  reboot,
  rebootDownload,
  restoreBackup,
} from '../api/client';
import { Alert } from '../components/ui';
import { ConfirmButton } from '../components/ConfirmButton';
import { PasswordForm } from '../components/PasswordForm';
import { subscribe } from '../api/live';
import { useT } from '../i18n/context';
import { formatBytes, formatDuration } from '../lib/format';
import { parseBackup } from '../lib/backup';
import { downloadText } from '../lib/download';
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

  // Show what was logged before the page opened (the dongle keeps the last lines in RAM): without it the panel
  // is empty exactly when something has just gone wrong.
  useEffect(() => {
    let alive = true;
    fetch('/api/v1/logs')
      .then((r) => r.text())
      .then(
        (text) => {
          if (!alive) return;
          const past = text.split('\n').filter((l) => l.length > 0);
          setLines((old) => [...past, ...old].slice(-MAX_LOG_LINES));
        },
        () => undefined, // the live lines still work
      );
    return () => {
      alive = false;
    };
  }, []);

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
    <section class="card" aria-labelledby="log-h">
      <h3 class="card-title" id="log-h">
        {t('system.logs')}
      </h3>
      <pre class="log" tabIndex={0} aria-live="off" data-testid="log">
        {lines.length === 0 ? t('system.logs_empty') : lines.join('\n')}
      </pre>
      <a class="btn" href="/api/v1/logs" download="hubble.log">
        {t('system.logs_download')}
      </a>
    </section>
  );
}

/**
 * Backup and restore of the settings. Bonds are not part of the backup because they contain keys.
 *
 * @returns The section element.
 */
function BackupSection() {
  const t = useT();
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const download = async () => {
    try {
      downloadText(
        'hubble-backup.json',
        JSON.stringify(await getBackup(), null, 2),
        'application/json',
      );
      setMsg({ ok: true, text: t('system.backup_done') });
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };
  const restore = async (file: File) => {
    const backup = parseBackup(await file.text());
    if (!backup) {
      setMsg({ ok: false, text: t('system.restore_invalid') });
      return;
    }
    try {
      await restoreBackup(backup);
      setMsg({ ok: true, text: t('system.restore_done') });
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <section class="card" aria-labelledby="bk-h">
      <h3 class="card-title" id="bk-h">
        {t('system.backup')}
      </h3>
      <p class="hint">{t('system.backup_hint')}</p>
      <button type="button" class="btn" onClick={() => void download()}>
        {t('system.backup_download')}
      </button>
      <label class="field">
        <span>{t('system.restore_choose')}</span>
        <input
          class="input"
          type="file"
          accept="application/json,.json"
          onChange={(e) => {
            const f = e.currentTarget.files?.[0];
            if (f) void restore(f);
            e.currentTarget.value = '';
          }}
        />
      </label>
      {msg && (
        <Alert kind={msg.ok ? 'ok' : 'bad'} role={msg.ok ? 'status' : 'alert'}>
          {msg.text}
        </Alert>
      )}
    </section>
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

  const row = (k: string, v: string | number, testId?: string) => (
    <div class="row">
      <dt>{k}</dt>
      <dd data-testid={testId}>{v}</dd>
    </div>
  );

  return (
    <>
      <h2 id="sys-h">{t('system.heading')}</h2>

      <section class="card" aria-labelledby="diag-h">
        <h3 class="card-title" id="diag-h">
          {t('system.diagnostics')}
        </h3>
        {failed && !d && (
          <Alert kind="bad" role="alert">
            {t('common.error')}
          </Alert>
        )}
        {d && (
          <dl class="rows">
            {row(t('system.uptime'), formatDuration(d.uptime_s))}
            {row(t('system.reset_reason'), d.reset_reason)}
            {row(t('system.heap'), formatBytes(d.heap.free), 'heap')}
            {row(t('system.psram'), formatBytes(d.psram.free))}
            {row(t('system.partition'), d.partitions.running.label)}
            {row(t('system.usb'), d.usb_state)}
            {row(
              t('system.power'),
              d.power.mode === 'low' ? t('system.power_low') : t('system.power_active'),
              'power',
            )}
            {row(
              t('system.ble'),
              t('system.ble_value', { c: d.ble.connected, b: d.ble.bonded }),
              'ble',
            )}
            {row(t('system.wifi_clients'), d.wifi.clients)}
            {d.chip_temp_c !== undefined &&
              row(t('system.temperature'), `${d.chip_temp_c.toFixed(1)} °C`, 'temp')}
            {row(t('system.boot_failures'), d.boot_failures)}
          </dl>
        )}
      </section>

      <LiveLog />
      <PasswordForm />
      <BackupSection />

      <section class="card" aria-labelledby="act-h">
        <h3 class="card-title" id="act-h">
          {t('system.actions')}
        </h3>
        <ConfirmButton
          danger={false}
          label={t('system.reboot')}
          onConfirm={() => {
            send(reboot);
          }}
        />
        <ConfirmButton
          danger={false}
          label={t('system.reboot_download')}
          onConfirm={() => {
            send(rebootDownload);
          }}
        />
        <p class="hint">{t('system.reboot_download_hint')}</p>
        {sent && (
          <Alert kind="ok" role="status">
            {t('system.action_done')}
          </Alert>
        )}
      </section>

      <section class="card" aria-labelledby="fac-h">
        <h3 class="card-title" id="fac-h">
          {t('system.factory')}
        </h3>
        <p class="hint">{t('system.factory_hint')}</p>
        <ConfirmButton
          label={t('system.factory_reset')}
          onConfirm={() => {
            send(factoryReset);
          }}
        />
      </section>
    </>
  );
}
