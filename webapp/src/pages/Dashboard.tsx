/**
 * @file Dashboard.tsx
 * @description Dashboard page (CLAUDE.md §3.7 page 2): dongle status and one card per bonded device
 * (name, type, connection, battery, RSSI, security badge, measured latency) plus "Add device".
 */
import { getStatus } from '../api/client';
import type { Device } from '../api/types';
import { SecurityBadge } from '../components/SecurityBadge';
import { useT } from '../i18n/context';
import { useApi } from '../lib/useApi';
import { useDevices } from '../lib/useDevices';

/** Status refresh period. */
const REFRESH_MS = 5000;

/**
 * One device card.
 *
 * @param props.device The device.
 * @returns The card element.
 */
function DeviceCard({ device: d }: { device: Device }) {
  const t = useT();
  return (
    <li class="card" data-testid="device-card">
      <h3>
        <a href={`#/device/${d.id}`}>{d.name}</a>
      </h3>
      <p class="muted">
        {t(`device.type.${d.type}`)} · {d.connected ? t('device.connected') : t('device.offline')}
      </p>
      <SecurityBadge level={d.security} />
      <dl>
        <dt>{t('device.battery')}</dt>
        <dd>{d.battery === null ? '–' : `${String(d.battery)} %`}</dd>
        <dt>{t('device.rssi')}</dt>
        <dd>{d.rssi === null ? '–' : `${String(d.rssi)} dBm`}</dd>
        <dt>{t('device.latency')}</dt>
        <dd>
          {d.latency_us.count === 0
            ? '–'
            : t('device.latency_value', {
                avg: (d.latency_us.avg / 1000).toFixed(1),
                p95: (d.latency_us.p95 / 1000).toFixed(1),
              })}
        </dd>
      </dl>
    </li>
  );
}

/**
 * Render the dashboard, with loading and error states.
 *
 * @returns The page element.
 */
export function Dashboard() {
  const t = useT();
  const { data: status, failed } = useApi(getStatus, REFRESH_MS);
  const devices = useDevices();
  const list = devices.data;

  return (
    <section aria-labelledby="status-h">
      <h2 id="status-h">{t('status.heading')}</h2>
      {failed && !status && <p role="alert">{t('common.error')}</p>}
      {!failed && !status && <p>{t('common.loading')}</p>}
      {status && (
        <dl>
          <dt>{t('status.version')}</dt>
          <dd data-testid="version">{status.version}</dd>
          <dt>{t('status.board')}</dt>
          <dd>{status.board}</dd>
          <dt>{t('status.usb')}</dt>
          <dd>{status.usb_state}</dd>
          <dt>{t('status.ap')}</dt>
          <dd>
            {status.ap_timeout_s === null
              ? t('status.ap_on')
              : t('status.ap_timeout', { s: status.ap_timeout_s })}
          </dd>
        </dl>
      )}

      <h2>{t('dashboard.devices')}</h2>
      {list && list.devices.length === 0 && <p>{t('dashboard.no_devices')}</p>}
      {list && list.devices.length > 0 && (
        <ul class="cards">
          {list.devices.map((d) => (
            <DeviceCard key={d.id} device={d} />
          ))}
        </ul>
      )}
      {list && list.devices.length >= list.max ? (
        <p class="muted">{t('dashboard.full', { max: list.max })}</p>
      ) : (
        <p>
          <a class="button" href="#/pair">
            {t('dashboard.add')}
          </a>
        </p>
      )}
    </section>
  );
}
