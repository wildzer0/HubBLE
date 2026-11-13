/**
 * @file Settings.tsx
 * @description Settings page (CLAUDE.md §3.7 page 6), organised in cards: pairing security, Wi-Fi hotspot, power
 * and LED, updates, the Wi-Fi network used for updates (chosen from a scan) and the language. Changes are kept
 * in the form and a save bar appears as soon as something differs from what the dongle holds.
 */
import { useEffect, useState } from 'preact/hooks';
import { getSettings, putSettings } from '../api/client';
import type { Settings as SettingsData } from '../api/types';
import { WifiPicker } from '../components/WifiPicker';
import { Alert, NumberField, SwitchRow } from '../components/ui';
import { useT } from '../i18n/context';
import type { Locale } from '../i18n';
import { pickLocale } from '../i18n';
import { useApi } from '../lib/useApi';

/** Allowed range of the AP windows, seconds (matches the firmware limits). */
const WINDOW_MIN_S = 30;
const WINDOW_MAX_S = 86400;
/** Allowed hold time of the Ctrl+Alt gesture, ms. */
const SHIFT_MIN_MS = 1000;
const SHIFT_MAX_MS = 10000;
/** Limits of the typing filter times, ms (match the firmware). */
const FILTER_BOUNCE_MAX_MS = 500;
const FILTER_HOLD_MAX_MS = 1000;
/** Milliseconds per second (the API stores the gesture time in ms, the UI shows seconds). */
const MS_PER_S = 1000;

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
  const [saved, setSaved] = useState<SettingsData | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    if (data) {
      setForm(data);
      setSaved(data);
    }
  }, [data]);

  if (!form || !saved) {
    return failed ? (
      <Alert kind="bad" role="alert">
        {t('common.error')}
      </Alert>
    ) : (
      <p class="muted">{t('common.loading')}</p>
    );
  }

  const set = (patch: Partial<SettingsData>) => {
    setForm({ ...form, ...patch });
    setMsg(null);
  };
  const windowOk = (v: number) => Number.isInteger(v) && v >= WINDOW_MIN_S && v <= WINDOW_MAX_S;
  const ledOk =
    Number.isInteger(form.led_brightness) && form.led_brightness >= 0 && form.led_brightness <= 100;
  const shiftOk =
    Number.isInteger(form.shift_hold_ms) &&
    form.shift_hold_ms >= SHIFT_MIN_MS &&
    form.shift_hold_ms <= SHIFT_MAX_MS;
  const filterOk =
    Number.isInteger(form.filter_bounce_ms) &&
    form.filter_bounce_ms >= 0 &&
    form.filter_bounce_ms <= FILTER_BOUNCE_MAX_MS &&
    Number.isInteger(form.filter_hold_ms) &&
    form.filter_hold_ms >= 0 &&
    form.filter_hold_ms <= FILTER_HOLD_MAX_MS;
  const ok =
    windowOk(form.ap_boot_window_s) &&
    windowOk(form.ap_idle_timeout_s) &&
    ledOk &&
    shiftOk &&
    filterOk;
  // The Wi-Fi network is saved by its own picker, so it does not count as an unsaved change.
  const dirty =
    JSON.stringify({ ...form, sta_configured: 0 }) !==
    JSON.stringify({ ...saved, sta_configured: 0 });

  const save = async () => {
    try {
      const next = await putSettings(form);
      setForm(next);
      setSaved(next);
      setMsg({ ok: true, text: t('settings.saved') });
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <>
      <h2 id="set-h">{t('settings.heading')}</h2>

      <section class="card" aria-labelledby="set-sec">
        <h3 class="card-title" id="set-sec">
          {t('settings.security')}
        </h3>
        <SwitchRow
          label={t('settings.require_confirmation')}
          hint={t('settings.require_confirmation_hint')}
          checked={form.require_confirmation}
          onChange={(v) => {
            set({ require_confirmation: v });
          }}
        />
      </section>

      <section class="card" aria-labelledby="set-wifi">
        <h3 class="card-title" id="set-wifi">
          {t('settings.wifi')}
        </h3>
        <SwitchRow
          label={t('settings.portal_auto')}
          hint={t('settings.portal_auto_hint')}
          checked={form.portal_auto}
          onChange={(v) => {
            set({ portal_auto: v });
          }}
        />
        <NumberField
          id="boot-window"
          label={t('settings.boot_window')}
          unit={t('common.unit_s')}
          min={WINDOW_MIN_S}
          max={WINDOW_MAX_S}
          value={form.ap_boot_window_s}
          onChange={(v) => {
            set({ ap_boot_window_s: v });
          }}
        />
        <NumberField
          id="idle-timeout"
          label={t('settings.idle_timeout')}
          unit={t('common.unit_s')}
          min={WINDOW_MIN_S}
          max={WINDOW_MAX_S}
          value={form.ap_idle_timeout_s}
          hint={`${t('settings.window_hint', { min: WINDOW_MIN_S, max: WINDOW_MAX_S })} ${t('settings.restart_hint')}`}
          onChange={(v) => {
            set({ ap_idle_timeout_s: v });
          }}
        />
      </section>

      <section class="card" aria-labelledby="set-power">
        <h3 class="card-title" id="set-power">
          {t('settings.power')}
        </h3>
        <SwitchRow
          label={t('settings.remote_wakeup')}
          hint={t('settings.remote_wakeup_hint')}
          checked={form.remote_wakeup}
          onChange={(v) => {
            set({ remote_wakeup: v });
          }}
        />
      </section>

      <section class="card" aria-labelledby="set-led">
        <h3 class="card-title" id="set-led">
          {t('settings.led')}
        </h3>
        <NumberField
          id="led-brightness"
          label={t('settings.led_brightness')}
          unit="%"
          min={0}
          max={100}
          value={form.led_brightness}
          onChange={(v) => {
            set({ led_brightness: v });
          }}
        />
        <SwitchRow
          label={t('settings.status_led')}
          checked={form.status_led}
          onChange={(v) => {
            set({ status_led: v });
          }}
        />
        <NumberField
          id="shift-hold"
          label={t('settings.shift_hold')}
          unit={t('common.unit_s')}
          min={SHIFT_MIN_MS / MS_PER_S}
          max={SHIFT_MAX_MS / MS_PER_S}
          step={0.5}
          value={form.shift_hold_ms / MS_PER_S}
          hint={t('settings.led_hint')}
          onChange={(v) => {
            set({ shift_hold_ms: Math.round(v * MS_PER_S) });
          }}
        />
      </section>

      <section class="card" aria-labelledby="set-usb">
        <h3 class="card-title" id="set-usb">
          {t('settings.usb_link')}
        </h3>
        <SwitchRow
          label={t('settings.usb_link_enabled')}
          hint={t('settings.usb_link_hint')}
          checked={form.usb_link}
          onChange={(v) => {
            set({ usb_link: v });
          }}
        />
      </section>

      <section class="card" aria-labelledby="set-filter">
        <h3 class="card-title" id="set-filter">
          {t('settings.filter')}
        </h3>
        <SwitchRow
          label={t('settings.filter_enabled')}
          hint={t('settings.filter_hint')}
          checked={form.filter_enabled}
          onChange={(v) => {
            set({ filter_enabled: v });
          }}
        />
        {form.filter_enabled && (
          <>
            <NumberField
              id="filter-bounce"
              label={t('settings.filter_bounce')}
              unit="ms"
              min={0}
              max={FILTER_BOUNCE_MAX_MS}
              value={form.filter_bounce_ms}
              hint={t('settings.filter_bounce_hint')}
              onChange={(v) => {
                set({ filter_bounce_ms: v });
              }}
            />
            <NumberField
              id="filter-hold"
              label={t('settings.filter_hold')}
              unit="ms"
              min={0}
              max={FILTER_HOLD_MAX_MS}
              value={form.filter_hold_ms}
              hint={t('settings.filter_hold_hint')}
              onChange={(v) => {
                set({ filter_hold_ms: v });
              }}
            />
          </>
        )}
      </section>

      <section class="card" aria-labelledby="set-upd">
        <h3 class="card-title" id="set-upd">
          {t('settings.updates')}
        </h3>
        <label class="field">
          <span>{t('online.channel')}</span>
          <select
            class="input"
            value={form.update_channel}
            onChange={(e) => {
              set({
                update_channel: e.currentTarget.value === 'prerelease' ? 'prerelease' : 'stable',
              });
            }}
          >
            <option value="stable">{t('online.channel_stable')}</option>
            <option value="prerelease">{t('online.channel_prerelease')}</option>
          </select>
        </label>
      </section>

      <WifiPicker
        configured={form.sta_configured}
        onChanged={(c) => {
          setForm({ ...form, sta_configured: c });
          setSaved({ ...saved, sta_configured: c });
        }}
      />

      <section class="card" aria-labelledby="set-lang">
        <h3 class="card-title" id="set-lang">
          {t('settings.language')}
        </h3>
        <label class="field">
          <select
            class="input"
            aria-label={t('lang.label')}
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

      {msg && !dirty && (
        <Alert kind={msg.ok ? 'ok' : 'bad'} role={msg.ok ? 'status' : 'alert'}>
          {msg.text}
        </Alert>
      )}
      {dirty && (
        <div class="savebar" role="region" aria-label={t('settings.unsaved')}>
          <span>{msg && !msg.ok ? msg.text : t('settings.unsaved')}</span>
          <button
            type="button"
            class="btn"
            onClick={() => {
              setForm(saved);
              setMsg(null);
            }}
          >
            {t('settings.discard')}
          </button>
          <button
            type="button"
            class="btn primary"
            disabled={!ok}
            onClick={() => {
              void save();
            }}
          >
            {t('settings.save')}
          </button>
        </div>
      )}
    </>
  );
}
