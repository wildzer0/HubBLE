/**
 * @file DeviceDetail.tsx
 * @description Device detail page (CLAUDE.md §3.7 page 4): rename, forget, connection parameters,
 * statistics, session-only battery history and report-map export (for debugging and test fixtures).
 */
import { useEffect, useState } from 'preact/hooks';
import { ApiError, forgetDevice, getDevice, getReportMap, renameDevice } from '../api/client';
import { ConfirmButton } from '../components/ConfirmButton';
import { SecurityBadge } from '../components/SecurityBadge';
import { useT } from '../i18n/context';
import { downloadText } from '../lib/download';
import { useApi } from '../lib/useApi';

/** Refresh period of the device data. */
const REFRESH_MS = 2000;
/** Battery samples kept for the session chart. */
const MAX_SAMPLES = 120;
/** Maximum device name length accepted by the firmware. */
const NAME_MAX = 31;

/**
 * Tiny line chart of the battery history of this session.
 *
 * @param props.samples Battery percentages, oldest first.
 * @returns An SVG, or null with fewer than two samples.
 */
function BatteryChart({ samples }: { samples: number[] }) {
  const t = useT();
  if (samples.length < 2) return <p class="muted">{t('device.history_wait')}</p>;
  const w = 200;
  const h = 40;
  const pts = samples
    .map((v, i) => `${String((i / (samples.length - 1)) * w)},${String(h - (v / 100) * h)}`)
    .join(' ');
  return (
    <svg
      viewBox={`0 0 ${String(w)} ${String(h)}`}
      role="img"
      aria-label={t('device.history')}
      class="chart"
    >
      <polyline points={pts} fill="none" stroke="currentColor" stroke-width="2" />
    </svg>
  );
}

/**
 * Render the device detail page.
 *
 * @param props.id Device id from the route.
 * @returns The page element.
 */
export function DeviceDetail({ id }: { id: string }) {
  const t = useT();
  const { data: d, failed, reload } = useApi(() => getDevice(id), REFRESH_MS);
  const [name, setName] = useState<string | null>(null);
  const [samples, setSamples] = useState<number[]>([]);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    if (d?.battery != null) {
      const v = d.battery;
      setSamples((s) => [...s, v].slice(-MAX_SAMPLES));
    }
  }, [d]);

  if (!d) {
    return (
      <section>
        {failed ? <p role="alert">{t('device.not_found')}</p> : <p>{t('common.loading')}</p>}
        <a href="#/dashboard">{t('device.back')}</a>
      </section>
    );
  }

  const shownName = name ?? d.name;
  const rename = async () => {
    try {
      await renameDevice(id, shownName);
      setName(null);
      setMsg({ ok: true, text: t('device.renamed') });
      reload();
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };
  const exportMap = async () => {
    try {
      const r = await getReportMap(id);
      downloadText(`report-map-${id}.hex`, `${r.hex}\n`);
      setMsg({ ok: true, text: t('device.exported', { n: r.length }) });
    } catch (e) {
      setMsg({
        ok: false,
        text: e instanceof ApiError && e.status === 409 ? t('device.export_offline') : String(e),
      });
    }
  };
  const forget = async () => {
    try {
      await forgetDevice(id);
      location.hash = '#/dashboard';
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <section aria-labelledby="dev-h">
      <p>
        <a href="#/dashboard">{t('device.back')}</a>
      </p>
      <h2 id="dev-h">{d.name}</h2>
      <SecurityBadge level={d.security} />
      <p class="muted">
        {t(`device.type.${d.type}`)} · {d.connected ? t('device.connected') : t('device.offline')}
      </p>

      <form
        onSubmit={(e) => {
          e.preventDefault();
          void rename();
        }}
      >
        <label>
          {t('device.name')}{' '}
          <input
            value={shownName}
            maxLength={NAME_MAX}
            onInput={(e) => {
              setName(e.currentTarget.value);
              setMsg(null);
            }}
          />
        </label>{' '}
        <button type="submit" disabled={shownName.trim() === '' || shownName === d.name}>
          {t('device.rename')}
        </button>
      </form>

      <h3>{t('device.connection')}</h3>
      <dl>
        <dt>{t('device.interval')}</dt>
        <dd data-testid="interval">{d.connection.interval_ms} ms</dd>
        <dt>{t('device.slave_latency')}</dt>
        <dd>{d.connection.latency}</dd>
        <dt>{t('device.timeout')}</dt>
        <dd>{d.connection.timeout_ms} ms</dd>
        <dt>{t('device.rssi')}</dt>
        <dd>{d.rssi === null ? '–' : `${String(d.rssi)} dBm`}</dd>
        <dt>{t('device.battery')}</dt>
        <dd>{d.battery === null ? '–' : `${String(d.battery)} %`}</dd>
        <dt>{t('device.reconnects')}</dt>
        <dd>{d.reconnects}</dd>
        <dt>{t('device.last_seen')}</dt>
        <dd>{t('device.seconds_ago', { s: d.last_seen_s })}</dd>
      </dl>

      <h3>{t('device.latency')}</h3>
      {d.latency_us.count === 0 ? (
        <p class="muted">–</p>
      ) : (
        <dl>
          <dt>{t('device.lat_min')}</dt>
          <dd>{(d.latency_us.min / 1000).toFixed(2)} ms</dd>
          <dt>{t('device.lat_avg')}</dt>
          <dd data-testid="lat-avg">{(d.latency_us.avg / 1000).toFixed(2)} ms</dd>
          <dt>{t('device.lat_p95')}</dt>
          <dd>{(d.latency_us.p95 / 1000).toFixed(2)} ms</dd>
          <dt>{t('device.lat_max')}</dt>
          <dd>{(d.latency_us.max / 1000).toFixed(2)} ms</dd>
        </dl>
      )}

      <h3>{t('device.history')}</h3>
      <BatteryChart samples={samples} />

      <h3>{t('device.report_map')}</h3>
      <p class="muted">{t('device.report_map_hint')}</p>
      <button type="button" onClick={() => void exportMap()}>
        {t('device.export')}
      </button>

      <h3>{t('device.forget_h')}</h3>
      <ConfirmButton label={t('device.forget')} onConfirm={() => void forget()} />
      {msg && <p role={msg.ok ? 'status' : 'alert'}>{msg.text}</p>}
    </section>
  );
}
