/**
 * @file Dashboard.tsx
 * @description Dashboard page: dongle status (M1: version, board, USB, AP countdown).
 */
import { getStatus } from '../api/client';
import { useT } from '../i18n/context';
import { useApi } from '../lib/useApi';

/** Status refresh period. */
const REFRESH_MS = 5000;

/**
 * Render the dongle status, with loading and error states.
 *
 * @returns The page element.
 */
export function Dashboard() {
  const t = useT();
  const { data: status, failed } = useApi(getStatus, REFRESH_MS);

  return (
    <section aria-labelledby="status-h">
      <h2 id="status-h">{t('status.heading')}</h2>
      {failed && !status && <p role="alert">{t('common.error')}</p>}
      {!failed && !status && <p>{t('common.loading')}</p>}
      {status && (
        <>
          {status.setup_required && <p class="notice">{t('status.setup_required')}</p>}
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
        </>
      )}
    </section>
  );
}
