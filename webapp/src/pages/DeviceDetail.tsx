/**
 * @file DeviceDetail.tsx
 * @description Device detail page (CLAUDE.md §3.7 page 4): rename, forget, connection parameters,
 * statistics, session-only battery history and report-map export (for debugging and test fixtures).
 */
import { useEffect, useState } from 'preact/hooks';
import { ApiError, forgetDevice, getDevice, getReportMap, renameDevice } from '../api/client';
import { ConfirmButton } from '../components/ConfirmButton';
import { SecurityBadge } from '../components/SecurityBadge';
import { Alert, Icon, Pill } from '../components/ui';
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
  if (samples.length < 2) return <p class="hint">{t('device.history_wait')}</p>;
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
      <>
        <a class="back" href="#/dashboard">
          <Icon name="back" />
          {t('device.back')}
        </a>
        {failed ? (
          <Alert kind="bad" role="alert">
            {t('device.not_found')}
          </Alert>
        ) : (
          <p class="muted">{t('common.loading')}</p>
        )}
      </>
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

  const row = (k: string, v: string | number, testId?: string) => (
    <div class="row">
      <dt>{k}</dt>
      <dd data-testid={testId}>{v}</dd>
    </div>
  );

  return (
    <>
      <div class="page-head">
        <a class="back" href="#/dashboard">
          <Icon name="back" />
          {t('device.back')}
        </a>
      </div>
      <div class="stack">
        <h2 id="dev-h">{d.name}</h2>
        <div class="actions">
          <Pill tone={d.connected ? 'ok' : 'off'}>
            {d.connected ? t('device.connected') : t('device.offline')}
          </Pill>
          <SecurityBadge level={d.security} />
          <span class="muted">{t(`device.type.${d.type}`)}</span>
        </div>
      </div>

      <form
        class="card"
        onSubmit={(e) => {
          e.preventDefault();
          void rename();
        }}
      >
        <label class="field">
          <span>{t('device.name')}</span>
          <input
            class="input"
            value={shownName}
            maxLength={NAME_MAX}
            onInput={(e) => {
              setName(e.currentTarget.value);
              setMsg(null);
            }}
          />
        </label>
        <button
          type="submit"
          class="btn primary"
          disabled={shownName.trim() === '' || shownName === d.name}
        >
          {t('device.rename')}
        </button>
      </form>

      <section class="card" aria-labelledby="conn-h">
        <h3 class="card-title" id="conn-h">
          {t('device.connection')}
        </h3>
        <dl class="rows">
          {row(t('device.interval'), `${String(d.connection.interval_ms)} ms`, 'interval')}
          {row(t('device.slave_latency'), d.connection.latency)}
          {row(t('device.timeout'), `${String(d.connection.timeout_ms)} ms`)}
          {row(t('device.rssi'), d.rssi === null ? '–' : `${String(d.rssi)} dBm`)}
          {row(t('device.battery'), d.battery === null ? '–' : `${String(d.battery)} %`)}
          {row(t('device.reconnects'), d.reconnects)}
          {row(t('device.last_seen'), t('device.seconds_ago', { s: d.last_seen_s }))}
        </dl>
      </section>

      <section class="card" aria-labelledby="lat-h">
        <h3 class="card-title" id="lat-h">
          {t('device.latency')}
        </h3>
        {d.latency_us.count === 0 ? (
          <p class="muted">–</p>
        ) : (
          <dl class="rows">
            {row(t('device.lat_min'), `${(d.latency_us.min / 1000).toFixed(2)} ms`)}
            {row(t('device.lat_avg'), `${(d.latency_us.avg / 1000).toFixed(2)} ms`, 'lat-avg')}
            {row(t('device.lat_p95'), `${(d.latency_us.p95 / 1000).toFixed(2)} ms`)}
            {row(t('device.lat_max'), `${(d.latency_us.max / 1000).toFixed(2)} ms`)}
          </dl>
        )}
      </section>

      <section class="card" aria-labelledby="hist-h">
        <h3 class="card-title" id="hist-h">
          {t('device.history')}
        </h3>
        <BatteryChart samples={samples} />
      </section>

      <section class="card" aria-labelledby="map-h">
        <h3 class="card-title" id="map-h">
          {t('device.report_map')}
        </h3>
        <p class="hint">{t('device.report_map_hint')}</p>
        <button type="button" class="btn" onClick={() => void exportMap()}>
          {t('device.export')}
        </button>
      </section>

      <section class="card" aria-labelledby="forget-h">
        <h3 class="card-title" id="forget-h">
          {t('device.forget_h')}
        </h3>
        <ConfirmButton label={t('device.forget')} onConfirm={() => void forget()} />
      </section>
      {msg && (
        <Alert kind={msg.ok ? 'ok' : 'bad'} role={msg.ok ? 'status' : 'alert'}>
          {msg.text}
        </Alert>
      )}
    </>
  );
}
