/**
 * @file Setup.tsx
 * @description First-run wizard (CLAUDE.md §3.7 page 1): 1) set a new Wi-Fi password, 2) print the QR
 * sticker (Wi-Fi QR + `hubble.local` URL), 3) pair the first device. The password only lives in memory
 * of this page, long enough to draw the QR code; it is never stored in the browser.
 */
import { useState } from 'preact/hooks';
import { getDiagnostics } from '../api/client';
import { PasswordForm } from '../components/PasswordForm';
import { useT } from '../i18n/context';
import { qrSvg, wifiPayload } from '../lib/wifiqr';

/** URL printed on the sticker. */
const DONGLE_URL = 'http://hubble.local';

/**
 * Printable sticker with both QR codes.
 *
 * @param props.ssid Access point name.
 * @param props.password The password just chosen.
 * @returns The sticker element.
 */
function Sticker({ ssid, password }: { ssid: string; password: string }) {
  const t = useT();
  return (
    <div class="sticker" data-testid="sticker">
      <figure>
        <div dangerouslySetInnerHTML={{ __html: qrSvg(wifiPayload(ssid, password)) }} />
        <figcaption>
          {t('setup.qr_wifi')}
          <br />
          <strong>{ssid}</strong>
        </figcaption>
      </figure>
      <figure>
        <div dangerouslySetInnerHTML={{ __html: qrSvg(DONGLE_URL) }} />
        <figcaption>
          {t('setup.qr_url')}
          <br />
          <strong>hubble.local</strong>
        </figcaption>
      </figure>
    </div>
  );
}

/**
 * Render the wizard.
 *
 * @returns The page element.
 */
export function Setup() {
  const t = useT();
  const [step, setStep] = useState<1 | 2>(1);
  const [password, setPassword] = useState('');
  const [ssid, setSsid] = useState('HubBLE');

  const saved = (pw: string) => {
    setPassword(pw);
    getDiagnostics().then(
      (d) => {
        setSsid(d.wifi.ssid);
      },
      () => undefined, // keep the generic name: the sticker is still usable
    );
    setStep(2);
  };

  return (
    <section aria-labelledby="setup-h">
      <h2 id="setup-h">{t('setup.heading')}</h2>
      <p class="muted" aria-live="polite">
        {t('setup.step', { n: step, total: 3 })}
      </p>
      {step === 1 && (
        <>
          <p class="notice">{t('setup.intro')}</p>
          <PasswordForm onSaved={saved} />
        </>
      )}
      {step === 2 && (
        <>
          <h3>{t('setup.sticker')}</h3>
          <p>{t('setup.sticker_hint')}</p>
          <Sticker ssid={ssid} password={password} />
          <p>
            <button
              type="button"
              onClick={() => {
                window.print();
              }}
            >
              {t('setup.print')}
            </button>{' '}
            <a class="button" href="#/pair">
              {t('setup.pair_first')}
            </a>{' '}
            <a href="#/dashboard">{t('setup.skip')}</a>
          </p>
        </>
      )}
    </section>
  );
}
