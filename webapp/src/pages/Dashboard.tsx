/**
 * @file Dashboard.tsx
 * @description Devices page (CLAUDE.md §3.7 page 2): a compact dongle summary and one tappable card per bonded
 * device (name, type, connection, battery, signal, security badge, measured latency), plus "Add device" and the
 * input tester.
 */
import { useState } from 'preact/hooks';
import { ApiError, getStatus, wifiOff } from '../api/client';
import type { Device } from '../api/types';
import { SecurityBadge } from '../components/SecurityBadge';
import { ConfirmButton } from '../components/ConfirmButton';
import { Alert, Icon, Pill } from '../components/ui';
import { useT } from '../i18n/context';
import { useApi } from '../lib/useApi';
import { useDevices } from '../lib/useDevices';

/** Status refresh period. */
const REFRESH_MS = 5000;

/**
 * One device card; the whole card opens the detail page.
 *
 * @param props.device The device.
 * @returns The list item.
 */
function DeviceCard({ device: d }: { device: Device }) {
  const t = useT();
  return (
    <li>
      <a class="device" href={`#/device/${d.id}`} data-testid="device-card">
        <span class="avatar">
          <Icon name={d.type === 'pointer' ? 'mouse' : 'keyboard'} />
        </span>
        <span class="name">{d.name}</span>
        <span class="chev">
          <Icon name="chevron" />
        </span>
        <span class="meta">
          <Pill tone={d.connected ? 'ok' : 'off'}>
            {d.connected ? t('device.connected') : t('device.offline')}
          </Pill>
          <SecurityBadge level={d.security} />
          {d.battery !== null && <span>{`${String(d.battery)} %`}</span>}
          {d.rssi !== null && <span>{`${String(d.rssi)} dBm`}</span>}
          {d.latency_us.count > 0 && (
            <span>
              {t('device.latency_value', {
                avg: (d.latency_us.avg / 1000).toFixed(1),
                p95: (d.latency_us.p95 / 1000).toFixed(1),
              })}
            </span>
          )}
        </span>
      </a>
    </li>
  );
}

/**
 * Render the devices page, with loading and error states.
 *
 * @returns The page element.
 */
export function Dashboard() {
  const t = useT();
  const { data: status, failed } = useApi(getStatus, REFRESH_MS);
  const devices = useDevices();
  const list = devices.data;
  const full = list !== null && list.devices.length >= list.max;
  const [wifiMsg, setWifiMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const switchOff = () => {
    wifiOff().then(
      () => {
        setWifiMsg({ ok: true, text: t('wifi_off.done') });
      },
      (e: unknown) => {
        setWifiMsg({
          ok: false,
          text:
            e instanceof ApiError && e.code === 'no_bonded_devices'
              ? t('wifi_off.no_devices')
              : e instanceof Error
                ? e.message
                : String(e),
        });
      },
    );
  };

  return (
    <>
      <div class="page-head">
        <h2>{t('nav.dashboard')}</h2>
        <a class="btn" href="#/tester">
          {t('dashboard.test_input')}
        </a>
      </div>

      {failed && !status && (
        <Alert kind="bad" role="alert">
          {t('common.error')}
        </Alert>
      )}

      {list && list.devices.length === 0 && (
        <div class="card empty">
          <p>{t('dashboard.no_devices')}</p>
        </div>
      )}
      {list && list.devices.length > 0 && (
        <ul class="devices">
          {list.devices.map((d) => (
            <DeviceCard key={d.id} device={d} />
          ))}
        </ul>
      )}
      {!list && !failed && <p class="muted">{t('common.loading')}</p>}

      {full ? (
        <p class="hint">{t('dashboard.full', { max: list.max })}</p>
      ) : (
        <a class="btn primary block" href="#/pair">
          <Icon name="plus" />
          {t('dashboard.add')}
        </a>
      )}

      {status && (
        <section class="card" aria-labelledby="dongle-h">
          <h3 class="card-title" id="dongle-h">
            {t('dashboard.dongle')}
          </h3>
          <dl class="rows">
            <div class="row">
              <dt>{t('status.version')}</dt>
              <dd data-testid="version">{status.version}</dd>
            </div>
            <div class="row">
              <dt>{t('status.board')}</dt>
              <dd>{status.board}</dd>
            </div>
            <div class="row">
              <dt>{t('status.usb')}</dt>
              <dd>{status.usb_state}</dd>
            </div>
            <div class="row">
              <dt>{t('status.ap')}</dt>
              <dd>
                {status.ap_timeout_s === null
                  ? t('status.ap_on')
                  : t('status.ap_timeout', { s: status.ap_timeout_s })}
              </dd>
            </div>
          </dl>
          <ConfirmButton danger={false} label={t('wifi_off.button')} onConfirm={switchOff} />
          <p class="hint">{t('wifi_off.hint')}</p>
          {wifiMsg && (
            <Alert kind={wifiMsg.ok ? 'ok' : 'bad'} role={wifiMsg.ok ? 'status' : 'alert'}>
              {wifiMsg.text}
            </Alert>
          )}
        </section>
      )}
    </>
  );
}
