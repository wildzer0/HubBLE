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
    <section aria-labelledby="bk-h">
      <h3 id="bk-h">{t('system.backup')}</h3>
      <p class="muted">{t('system.backup_hint')}</p>
      <p>
        <button type="button" onClick={() => void download()}>
          {t('system.backup_download')}
        </button>
      </p>
      <p>
        <label>
          {t('system.restore_choose')}{' '}
          <input
            type="file"
            accept="application/json,.json"
            onChange={(e) => {
              const f = e.currentTarget.files?.[0];
              if (f) void restore(f);
              e.currentTarget.value = '';
            }}
          />
        </label>
      </p>
      {msg && <p role={msg.ok ? 'status' : 'alert'}>{msg.text}</p>}
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
          <dt>{t('system.usb')}</dt>
          <dd>{d.usb_state}</dd>
          <dt>{t('system.power')}</dt>
          <dd data-testid="power">
            {d.power.mode === 'low' ? t('system.power_low') : t('system.power_active')}
          </dd>
          <dt>{t('system.ble')}</dt>
          <dd data-testid="ble">
            {t('system.ble_value', { c: d.ble.connected, b: d.ble.bonded })}
          </dd>
          <dt>{t('system.wifi_clients')}</dt>
          <dd>{d.wifi.clients}</dd>
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
      <BackupSection />

      <h3>{t('system.factory')}</h3>
      <p class="muted">{t('system.factory_hint')}</p>
      <ConfirmButton
        label={t('system.factory_reset')}
        onConfirm={() => {
          send(factoryReset);
        }}
      />
    </section>
  );
}
