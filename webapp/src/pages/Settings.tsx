/**
 * @file Settings.tsx
 * @description Settings page (CLAUDE.md §3.7 page 6): physical confirmation, AP boot window and idle
 * timeout, and language. Settings that depend on later milestones (STA credentials, LED, remote
 * wakeup, Shift-combo time) appear when their firmware support lands (M6/M7).
 */
import { useEffect, useState } from 'preact/hooks';
import { getSettings, putSettings } from '../api/client';
import type { Settings as SettingsData } from '../api/types';
import { useT } from '../i18n/context';
import type { Locale } from '../i18n';
import { pickLocale } from '../i18n';
import { useApi } from '../lib/useApi';

/** Allowed range of the AP windows, seconds (matches the firmware limits). */
const WINDOW_MIN_S = 30;
const WINDOW_MAX_S = 86400;

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

        <button type="submit" disabled={!ok}>
          {t('settings.save')}
        </button>
        {msg && <p role={msg.ok ? 'status' : 'alert'}>{msg.text}</p>}
      </form>

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
