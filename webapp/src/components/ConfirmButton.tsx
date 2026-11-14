/**
 * @file ConfirmButton.tsx
 * @description A button that needs a second tap to confirm a disruptive action.
 */
import { useState } from 'preact/hooks';
import { useT } from '../i18n/context';

/**
 * Two-step button.
 *
 * - `label`: Button text.
 * - `onConfirm`: Called after the user confirms.
 * - `danger`: Draw the button in the "danger" colour (default true).
 * @returns The control element.
 */
export function ConfirmButton({
  label,
  onConfirm,
  danger = true,
}: {
  label: string;
  onConfirm: () => void;
  danger?: boolean;
}) {
  const t = useT();
  const [asking, setAsking] = useState(false);
  if (!asking) {
    return (
      <button
        type="button"
        class={`btn ${danger ? 'danger' : ''}`}
        onClick={() => {
          setAsking(true);
        }}
      >
        {label}
      </button>
    );
  }
  return (
    <div class="actions" role="group" aria-label={label}>
      <button
        type="button"
        class="btn danger solid"
        onClick={() => {
          setAsking(false);
          onConfirm();
        }}
      >
        {t('common.confirm')}: {label}
      </button>
      <button
        type="button"
        class="btn"
        onClick={() => {
          setAsking(false);
        }}
      >
        {t('common.cancel')}
      </button>
    </div>
  );
}
