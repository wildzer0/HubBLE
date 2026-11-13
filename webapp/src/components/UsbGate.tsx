/**
 * @file UsbGate.tsx
 * @description The screen shown by the hosted web app before it is connected to a dongle: a button that opens the browser's
 * device picker (WebHID), the reasons it may not work (unsupported browser, closed configuration window) and a short guide.
 */
import { useState } from 'preact/hooks';
import { LinkError, requestUsbLink, usbSupported } from '../api/usbLink';
import type { UsbLink } from '../api/usbLink';
import { useT } from '../i18n/context';
import { Alert, Icon } from './ui';

/** What went wrong, for the message shown. */
type Problem = 'unsupported' | 'none' | 'closed' | 'failed' | null;

/**
 * Render the gate.
 *
 * @param props.onConnected Called with the connected link once the dongle answered and its window is open.
 * @returns The screen element.
 */
export function UsbGate({ onConnected }: { onConnected: (link: UsbLink) => void }) {
  const t = useT();
  const [busy, setBusy] = useState(false);
  const [problem, setProblem] = useState<Problem>(usbSupported() ? null : 'unsupported');
  const supported = usbSupported();

  const connect = async () => {
    setBusy(true);
    setProblem(null);
    try {
      const link = await requestUsbLink();
      const info = await link.connect();
      if (!info.open) {
        await link.disconnect();
        setProblem('closed');
      } else {
        onConnected(link);
      }
    } catch (e) {
      // A cancelled picker (no device chosen) is not an error worth shouting about.
      setProblem(e instanceof LinkError && e.message === 'No dongle selected' ? 'none' : 'failed');
    } finally {
      setBusy(false);
    }
  };

  return (
    <>
      <h2>{t('usb.heading')}</h2>
      <p>{t('usb.intro')}</p>
      <div class="card">
        <button
          type="button"
          class="btn primary block"
          disabled={!supported || busy}
          onClick={() => {
            void connect();
          }}
        >
          <Icon name="bolt" />
          {busy ? t('usb.connecting') : t('usb.connect')}
        </button>
        <p class="hint">{t('usb.steps')}</p>
      </div>
      {problem === 'unsupported' && (
        <Alert kind="warn" role="alert" testId="usb-unsupported">
          {t('usb.unsupported')}
        </Alert>
      )}
      {problem === 'none' && <Alert>{t('usb.none')}</Alert>}
      {problem === 'closed' && (
        <Alert kind="warn" role="alert" testId="usb-closed">
          {t('usb.closed')}
        </Alert>
      )}
      {problem === 'failed' && (
        <Alert kind="bad" role="alert">
          {t('usb.failed')}
        </Alert>
      )}
      <p class="hint">{t('usb.linux')}</p>
    </>
  );
}
