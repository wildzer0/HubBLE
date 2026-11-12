/**
 * @file WifiPicker.tsx
 * @description Chooses the Wi-Fi network the dongle uses for "check for updates". The dongle scans the networks
 * around it and the user taps one and types the password, instead of typing the network name by hand (the name
 * stays available through "Other network…" for hidden networks). The credentials are write-only: the dongle never
 * sends them back (CLAUDE.md §3.12), so the component only knows whether something is saved.
 */
import { useState } from 'preact/hooks';
import { ApiError, scanWifi, setStaCredentials } from '../api/client';
import type { WifiNetwork } from '../api/types';
import { useT } from '../i18n/context';
import { Alert, Icon, Pill, SignalBars } from './ui';

/** Longest Wi-Fi network name. */
const SSID_MAX = 32;
/** WPA2 passphrase length limits. */
const PASS_MIN = 8;
const PASS_MAX = 63;

/** Screens of the picker. */
type Mode =
  | { kind: 'summary' }
  | { kind: 'scanning' }
  | { kind: 'list'; networks: WifiNetwork[] }
  | { kind: 'password'; ssid: string; secure: boolean; manual: boolean };

/**
 * Render the network picker.
 *
 * @param props.configured Whether credentials are saved.
 * @param props.onChanged Called with the new "configured" flag after a save or forget.
 * @returns The section element.
 */
export function WifiPicker({
  configured,
  onChanged,
}: {
  configured: boolean;
  onChanged: (configured: boolean) => void;
}) {
  const t = useT();
  const [mode, setMode] = useState<Mode>({ kind: 'summary' });
  const [pass, setPass] = useState('');
  const [nets, setNets] = useState<WifiNetwork[]>([]);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const scan = async () => {
    setMsg(null);
    setMode({ kind: 'scanning' });
    try {
      const found = await scanWifi();
      setNets(found);
      setMode({ kind: 'list', networks: found });
    } catch (e) {
      setMode({ kind: 'summary' });
      setMsg({
        ok: false,
        text:
          e instanceof ApiError && e.code === 'busy' ? t('wifi.scan_busy') : t('wifi.scan_failed'),
      });
    }
  };

  const apply = async (ssid: string, password: string) => {
    try {
      const r = await setStaCredentials(ssid, password);
      onChanged(r.sta_configured);
      setMsg({
        ok: true,
        text: ssid === '' ? t('settings.sta_forgotten') : t('settings.sta_saved'),
      });
      setPass('');
      setMode({ kind: 'summary' });
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <section class="card" aria-labelledby="sta-h" data-testid="wifi-picker">
      <h3 class="card-title" id="sta-h">
        {t('settings.sta')}
      </h3>
      <p class="hint">{t('settings.sta_hint')}</p>

      {mode.kind === 'summary' && (
        <>
          <div>
            <Pill tone={configured ? 'ok' : 'off'} testId="sta-state">
              {configured ? t('settings.sta_set') : t('settings.sta_unset')}
            </Pill>
          </div>
          <div class="actions">
            <button
              type="button"
              class={`btn ${configured ? '' : 'primary'}`}
              onClick={() => {
                void scan();
              }}
            >
              <Icon name="wifi" />
              {configured ? t('wifi.change') : t('wifi.scan')}
            </button>
            {configured && (
              <button
                type="button"
                class="btn danger"
                onClick={() => {
                  void apply('', '');
                }}
              >
                {t('settings.sta_forget')}
              </button>
            )}
          </div>
        </>
      )}

      {mode.kind === 'scanning' && (
        <>
          <p role="status">{t('wifi.scanning')}</p>
          <p class="hint">{t('wifi.scan_note')}</p>
        </>
      )}

      {mode.kind === 'list' && (
        <>
          <p>{t('wifi.pick')}</p>
          {mode.networks.length === 0 && <p class="muted">{t('wifi.none')}</p>}
          <ul class="nets">
            {mode.networks.map((n) => (
              <li key={n.ssid}>
                <button
                  type="button"
                  data-testid="network"
                  onClick={() => {
                    setPass('');
                    setMode({ kind: 'password', ssid: n.ssid, secure: n.secure, manual: false });
                  }}
                >
                  <span class="grow">{n.ssid}</span>
                  {n.secure ? (
                    <Icon name="lock" />
                  ) : (
                    <span class="muted">{t('wifi.open_net')}</span>
                  )}
                  <SignalBars rssi={n.rssi} label={t('wifi.signal_label')} />
                </button>
              </li>
            ))}
            <li>
              <button
                type="button"
                data-testid="network-other"
                onClick={() => {
                  setPass('');
                  setMode({ kind: 'password', ssid: '', secure: true, manual: true });
                }}
              >
                <span class="grow">{t('wifi.other')}</span>
              </button>
            </li>
          </ul>
          <div class="actions">
            <button
              type="button"
              class="btn"
              onClick={() => {
                void scan();
              }}
            >
              {t('wifi.rescan')}
            </button>
            <button
              type="button"
              class="btn"
              onClick={() => {
                setMode({ kind: 'summary' });
              }}
            >
              {t('common.cancel')}
            </button>
          </div>
        </>
      )}

      {mode.kind === 'password' && (
        <form
          class="stack"
          onSubmit={(e) => {
            e.preventDefault();
            void apply(mode.ssid, pass);
          }}
        >
          {mode.manual ? (
            <label class="field">
              <span>{t('wifi.hidden_name')}</span>
              <input
                class="input"
                value={mode.ssid}
                maxLength={SSID_MAX}
                autocomplete="off"
                onInput={(e) => {
                  setMode({ ...mode, ssid: e.currentTarget.value });
                }}
              />
            </label>
          ) : (
            <p class="card-title">{mode.ssid}</p>
          )}
          {(mode.secure || mode.manual) && (
            <label class="field">
              <span>
                {mode.manual
                  ? t('settings.sta_password')
                  : t('wifi.password_for', { ssid: mode.ssid })}
              </span>
              <input
                class="input"
                type="password"
                value={pass}
                autocomplete="new-password"
                onInput={(e) => {
                  setPass(e.currentTarget.value);
                }}
              />
            </label>
          )}
          <div class="actions">
            <button
              type="submit"
              class="btn primary"
              disabled={
                mode.ssid.length < 1 ||
                mode.ssid.length > SSID_MAX ||
                (pass !== '' && (pass.length < PASS_MIN || pass.length > PASS_MAX)) ||
                (mode.secure && !mode.manual && pass === '')
              }
            >
              {t('wifi.save')}
            </button>
            <button
              type="button"
              class="btn"
              onClick={() => {
                setMode({ kind: 'list', networks: nets });
              }}
            >
              {t('wifi.back')}
            </button>
          </div>
        </form>
      )}

      {msg && (
        <Alert kind={msg.ok ? 'ok' : 'bad'} role={msg.ok ? 'status' : 'alert'}>
          {msg.text}
        </Alert>
      )}
    </section>
  );
}
