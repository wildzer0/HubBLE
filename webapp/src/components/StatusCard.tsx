/**
 * @file StatusCard.tsx
 * @description Card showing the dongle status fetched from `/api/v1/status`.
 */
import { useEffect, useState } from 'preact/hooks';
import { getStatus } from '../api/client';
import type { Status } from '../api/types';
import { translate } from '../i18n';
import type { Locale } from '../i18n';

/** Props of {@link StatusCard}. */
interface Props {
  /** Active UI language. */
  locale: Locale;
}

/**
 * Render the dongle status, with loading and error states.
 *
 * @param props Component props.
 * @returns The card element.
 */
export function StatusCard({ locale }: Props) {
  const [status, setStatus] = useState<Status | null>(null);
  const [failed, setFailed] = useState(false);

  useEffect(() => {
    getStatus().then(setStatus, () => {
      setFailed(true);
    });
  }, []);

  return (
    <section aria-labelledby="status-h">
      <h2 id="status-h">{translate(locale, 'status.heading')}</h2>
      {failed && <p role="alert">{translate(locale, 'status.error')}</p>}
      {!failed && !status && <p>{translate(locale, 'status.loading')}</p>}
      {status && (
        <dl>
          <dt>{translate(locale, 'status.version')}</dt>
          <dd data-testid="version">{status.version}</dd>
          <dt>{translate(locale, 'status.board')}</dt>
          <dd>{status.board}</dd>
          <dt>{translate(locale, 'status.usb')}</dt>
          <dd>{status.usb_state}</dd>
        </dl>
      )}
    </section>
  );
}
