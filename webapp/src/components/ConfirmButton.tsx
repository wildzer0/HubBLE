/**
 * @file ConfirmButton.tsx
 * @description A button that needs a second click to confirm a disruptive action.
 */
import { useState } from 'preact/hooks';
import { useT } from '../i18n/context';

/**
 * Two-step button.
 *
 * @param props.label Button text.
 * @param props.onConfirm Called after the user confirms.
 * @returns The control element.
 */
export function ConfirmButton({ label, onConfirm }: { label: string; onConfirm: () => void }) {
  const t = useT();
  const [asking, setAsking] = useState(false);
  if (!asking) {
    return (
      <button
        type="button"
        onClick={() => {
          setAsking(true);
        }}
      >
        {label}
      </button>
    );
  }
  return (
    <span role="group" aria-label={label}>
      <button
        type="button"
        class="danger"
        onClick={() => {
          setAsking(false);
          onConfirm();
        }}
      >
        {t('common.confirm')}: {label}
      </button>{' '}
      <button
        type="button"
        onClick={() => {
          setAsking(false);
        }}
      >
        {t('common.cancel')}
      </button>
    </span>
  );
}
