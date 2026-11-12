/**
 * @file PasswordForm.tsx
 * @description Wi-Fi password form with client-side validation matching the firmware rules. Used by the
 * setup wizard and by the System page.
 */
import { useState } from 'preact/hooks';
import { setWifiPassword } from '../api/client';
import { Alert } from './ui';
import { useT } from '../i18n/context';
import { passwordProblem } from '../lib/password';

/**
 * Render the password form.
 *
 * @param props.onSaved Called with the new password after the dongle accepted it.
 * @returns The form element.
 */
export function PasswordForm({ onSaved }: { onSaved?: (password: string) => void }) {
  const t = useT();
  const [pw, setPw] = useState('');
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const problem = pw === '' ? null : passwordProblem(pw);

  const save = async () => {
    try {
      await setWifiPassword(pw);
      setMsg({ ok: true, text: t('system.password_saved') });
      onSaved?.(pw);
      setPw('');
    } catch (e) {
      setMsg({ ok: false, text: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <form
      class="card"
      aria-labelledby="pw-h"
      onSubmit={(e) => {
        e.preventDefault();
        void save();
      }}
    >
      <h3 class="card-title" id="pw-h">
        {t('system.password')}
      </h3>
      <label class="field">
        <span>{t('system.password_new')}</span>
        <input
          class="input"
          type="password"
          autocomplete="new-password"
          value={pw}
          aria-invalid={problem !== null}
          aria-describedby="pw-hint"
          onInput={(e) => {
            setPw(e.currentTarget.value);
            setMsg(null);
          }}
        />
        <span id="pw-hint" class="hint">
          {problem ? t(`password.${problem}`) : t('system.password_hint')}
        </span>
      </label>
      <button type="submit" class="btn primary" disabled={pw === '' || problem !== null}>
        {t('system.password_save')}
      </button>
      {msg && (
        <Alert kind={msg.ok ? 'ok' : 'bad'} role={msg.ok ? 'status' : 'alert'}>
          {msg.text}
        </Alert>
      )}
    </form>
  );
}
