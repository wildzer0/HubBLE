/**
 * @file Settings.tsx
 * @description Settings page (CLAUDE.md §3.7 page 6): physical confirmation, AP boot window and idle
 * timeout, and language. Settings that depend on later milestones (STA credentials, LED, remote
 * wakeup, Shift-combo time) appear when their firmware support lands (M6/M7).
 */
import { useEffect, useState } from 'preact/hooks';
import { getSettings, putSettings, setStaCredentials } from '../api/client';
import type { Settings as SettingsData } from '../api/types';
import { useT } from '../i18n/context';
import type { Locale } from '../i18n';
import { pickLocale } from '../i18n';
import { useApi } from '../lib/useApi';

/** Allowed range of the AP windows, seconds (matches the firmware limits). */
const WINDOW_MIN_S = 30;
const WINDOW_MAX_S = 86400;
/** Longest Wi-Fi network name. */
const STA_SSID_MAX = 32;

/**
 * Wi-Fi network used only for "check for updates". The credentials are write-only: the dongle never sends
 * them back (CLAUDE.md §3.12), so the form only shows whether something is saved.
 *
 * @param props.configured Whether credentials are saved.
 * @param props.onChanged Called with the new "configured" flag after a save/forget.
 * @returns The form element.
 */
function StaForm({
  configured,
  onChanged,
}: {
  configured: boolean;
  onChanged: (configured: boolean) => void;
}) {
  const t = useT();
  const [ssid, setSsid] = useState('');
  const [pass, setPass] = useState('');
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const valid =
    ssid.length >= 1 &&
    ssid.length <= STA_SSID_MAX &&
    (pass === '' || (pass.length >= 8 && pass.length <= 63));

  const apply = async (s: string, p: string) => {
    try {
      const r = await setStaCredentials(s, p);
      onChanged(r.sta_configured);
      setMsg({ ok: true, text: s === '' ? t('settings.sta_forgotten') : t('settings.sta_saved') });
      setSsid('');
      setPass('');
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <form
      aria-labelledby="sta-h"
      onSubmit={(e) => {
        e.preventDefault();
        void apply(ssid, pass);
      }}
    >
      <h3 id="sta-h">{t('settings.sta')}</h3>
      <p class="muted">{t('settings.sta_hint')}</p>
      <p data-testid="sta-state">{configured ? t('settings.sta_set') : t('settings.sta_unset')}</p>
      <p>
        <label>
          {t('settings.sta_ssid')}{' '}
          <input
            value={ssid}
            maxLength={STA_SSID_MAX}
            autocomplete="off"
            onInput={(e) => {
              setSsid(e.currentTarget.value);
              setMsg(null);
            }}
          />
        </label>
      </p>
      <p>
        <label>
          {t('settings.sta_password')}{' '}
          <input
            type="password"
            value={pass}
            autocomplete="new-password"
            onInput={(e) => {
              setPass(e.currentTarget.value);
              setMsg(null);
            }}
          />
        </label>
      </p>
      <button type="submit" disabled={!valid}>
        {t('settings.sta_save')}
      </button>{' '}
      {configured && (
        <button
          type="button"
          onClick={() => {
            void apply('', '');
          }}
        >
          {t('settings.sta_forget')}
        </button>
      )}
      {msg && <p role={msg.ok ? 'status' : 'alert'}>{msg.text}</p>}
    </form>
  );
}

/**
 * Render the settings page.
 *
 * @param props.locale Active language.
 * @param props.onLocale Language change callback.
 * @returns The page element.
 */
export function Settings({ locale, onLocale }: { locale: Locale; onLocale: (l: Locale) => void }) {
  const t = useT();
  const { data, failed } = useApi(getSettings, 0);
  const [form, setForm] = useState<SettingsData | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    if (data) setForm(data);
  }, [data]);

  if (!form) {
    return (
      <section>
        {failed ? <p role="alert">{t('common.error')}</p> : <p>{t('common.loading')}</p>}
      </section>
    );
  }

  const valid = (v: number) => Number.isInteger(v) && v >= WINDOW_MIN_S && v <= WINDOW_MAX_S;
  const ok = valid(form.ap_boot_window_s) && valid(form.ap_idle_timeout_s);
  const save = async () => {
    try {
      setForm(await putSettings(form));
      setMsg({ ok: true, text: t('settings.saved') });
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };
  const num = (key: 'ap_boot_window_s' | 'ap_idle_timeout_s', label: string, id: string) => (
    <p>
      <label>
        {label}{' '}
        <input
          id={id}
          type="number"
          inputMode="numeric"
          min={WINDOW_MIN_S}
          max={WINDOW_MAX_S}
          value={form[key]}
          aria-invalid={!valid(form[key])}
          onInput={(e) => {
            setForm({ ...form, [key]: e.currentTarget.valueAsNumber });
            setMsg(null);
          }}
        />
      </label>
    </p>
  );

  return (
    <section aria-labelledby="set-h">
      <h2 id="set-h">{t('settings.heading')}</h2>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          void save();
        }}
      >
        <h3>{t('settings.security')}</h3>
        <p>
          <label>
            <input
              type="checkbox"
              checked={form.require_confirmation}
              onChange={(e) => {
                setForm({ ...form, require_confirmation: e.currentTarget.checked });
                setMsg(null);
              }}
            />{' '}
            {t('settings.require_confirmation')}
          </label>
        </p>
        <p class="muted">{t('settings.require_confirmation_hint')}</p>

        <h3>{t('settings.wifi')}</h3>
        {num('ap_boot_window_s', t('settings.boot_window'), 'boot-window')}
        {num('ap_idle_timeout_s', t('settings.idle_timeout'), 'idle-timeout')}
        <p class="muted">
          {t('settings.window_hint', { min: WINDOW_MIN_S, max: WINDOW_MAX_S })}{' '}
          {t('settings.restart_hint')}
        </p>

        <h3>{t('settings.updates')}</h3>
        <p>
          <label>
            {t('online.channel')}{' '}
            <select
              value={form.update_channel}
              onChange={(e) => {
                setForm({
                  ...form,
                  update_channel: e.currentTarget.value === 'prerelease' ? 'prerelease' : 'stable',
                });
                setMsg(null);
              }}
            >
              <option value="stable">{t('online.channel_stable')}</option>
              <option value="prerelease">{t('online.channel_prerelease')}</option>
            </select>
          </label>
        </p>

        <button type="submit" disabled={!ok}>
          {t('settings.save')}
        </button>
        {msg && <p role={msg.ok ? 'status' : 'alert'}>{msg.text}</p>}
      </form>

      <StaForm
        configured={form.sta_configured}
        onChanged={(c) => {
          setForm({ ...form, sta_configured: c });
        }}
      />

      <h3>{t('settings.language')}</h3>
      <label>
        {t('lang.label')}{' '}
        <select
          value={locale}
          onChange={(e) => {
            onLocale(pickLocale(e.currentTarget.value));
          }}
        >
          <option value="en">English</option>
          <option value="it">Italiano</option>
        </select>
      </label>
    </section>
  );
}
