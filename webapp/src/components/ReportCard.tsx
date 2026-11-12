/**
 * @file ReportCard.tsx
 * @description "Help improve compatibility" card of the device page: builds the device's compatibility report, shows
 * exactly what it contains, and lets the user download it or (when a collection service is configured at build time)
 * send it. Nothing leaves the phone without a tap on "Send" after the user has seen the content. The report holds no
 * keystrokes, no Bluetooth address, no name the user gave the device and no Wi-Fi data.
 */
import { useState } from 'preact/hooks';
import { ApiError, getReport } from '../api/client';
import type { DeviceReport } from '../api/types';
import { useT } from '../i18n/context';
import { downloadText } from '../lib/download';
import { Alert } from './ui';

/** Address of the report collection service, set at build time (VITE_REPORT_URL); empty disables "Send". */
const REPORT_URL = String(import.meta.env.VITE_REPORT_URL ?? '').replace(/\/+$/, '');
/** Page explaining the reports and where to upload a saved file. */
const DOCS_URL = 'https://wildzer0.github.io/HubBLE/compatibility/';
/** Longest note the report accepts. */
const NOTES_MAX = 500;

/**
 * Send a report to the collection service.
 *
 * @param report The report (with notes).
 * @throws Error when the service cannot be reached or refuses the report.
 */
async function sendReport(report: DeviceReport): Promise<void> {
  const res = await fetch(`${REPORT_URL}/v1/reports`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(report),
  });
  if (!res.ok) throw new Error(`HTTP ${String(res.status)}`);
}

/**
 * Render the card.
 *
 * @param props.id Device id.
 * @param props.connected Whether the device is connected (a report needs a live device).
 * @returns The card element.
 */
export function ReportCard({ id, connected }: { id: string; connected: boolean }) {
  const t = useT();
  const [report, setReport] = useState<DeviceReport | null>(null);
  const [notes, setNotes] = useState('');
  const [agree, setAgree] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const prepare = () => {
    setMsg(null);
    getReport(id).then(
      (r) => {
        setReport(r);
        setAgree(false);
      },
      (e: unknown) => {
        setMsg({
          ok: false,
          text:
            e instanceof ApiError && e.status === 409 ? t('report.offline') : t('report.failed'),
        });
      },
    );
  };
  const withNotes = (r: DeviceReport): DeviceReport => ({
    ...r,
    notes: notes.trim() === '' ? null : notes.trim(),
  });

  return (
    <section class="card" aria-labelledby="rep-h" data-testid="report-card">
      <h3 class="card-title" id="rep-h">
        {t('report.heading')}
      </h3>
      <p class="hint">{t('report.intro')}</p>
      {!report && (
        <button type="button" class="btn" disabled={!connected} onClick={prepare}>
          {t('report.prepare')}
        </button>
      )}
      {!connected && !report && <p class="hint">{t('report.offline')}</p>}
      {report && (
        <>
          <p>{t('report.preview')}</p>
          <pre class="log" tabIndex={0} data-testid="report-json">
            {JSON.stringify(report, null, 2)}
          </pre>
          <label class="field">
            <span>{t('report.notes')}</span>
            <textarea
              class="input"
              rows={2}
              maxLength={NOTES_MAX}
              value={notes}
              onInput={(e) => {
                setNotes(e.currentTarget.value);
              }}
            />
          </label>
          <button
            type="button"
            class="btn"
            onClick={() => {
              downloadText(
                `hubble-report-${report.device.pnp ? `${report.device.pnp.vendor_id.toString(16)}-${report.device.pnp.product_id.toString(16)}` : report.device.kind}.json`,
                JSON.stringify(withNotes(report), null, 2),
                'application/json',
              );
            }}
          >
            {t('report.download')}
          </button>
          {REPORT_URL !== '' ? (
            <>
              <label class="switch-row">
                <span class="text">
                  <b>{t('report.agree')}</b>
                </span>
                <input
                  type="checkbox"
                  role="switch"
                  class="switch"
                  checked={agree}
                  onChange={(e) => {
                    setAgree(e.currentTarget.checked);
                  }}
                />
              </label>
              <button
                type="button"
                class="btn primary"
                disabled={!agree}
                onClick={() => {
                  sendReport(withNotes(report)).then(
                    () => {
                      setMsg({ ok: true, text: t('report.sent') });
                    },
                    () => {
                      setMsg({ ok: false, text: t('report.send_failed') });
                    },
                  );
                }}
              >
                {t('report.send')}
              </button>
            </>
          ) : (
            <p class="hint">{t('report.how_to_send')}</p>
          )}
          <p class="hint">
            <a href={DOCS_URL} target="_blank" rel="noopener noreferrer">
              {t('report.learn_more')}
            </a>
          </p>
        </>
      )}
      {msg && (
        <Alert kind={msg.ok ? 'ok' : 'bad'} role={msg.ok ? 'status' : 'alert'}>
          {msg.text}
        </Alert>
      )}
    </section>
  );
}
