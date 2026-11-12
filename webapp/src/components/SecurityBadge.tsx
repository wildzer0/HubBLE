/**
 * @file SecurityBadge.tsx
 * @description Security level badge (green / yellow / red) with a text label so colour is never the
 * only carrier of the information (accessibility).
 */
import type { SecurityLevel } from '../api/types';
import { useT } from '../i18n/context';

/** Emoji shown for each level. */
const ICON: Record<SecurityLevel, string> = { green: '🟢', yellow: '🟡', red: '🔴', unknown: '⚪' };

/**
 * Render the badge.
 *
 * @param props.level Security level.
 * @returns The badge element.
 */
export function SecurityBadge({ level }: { level: SecurityLevel }) {
  const t = useT();
  return (
    <span class={`badge badge-${level}`} data-testid="security">
      <span aria-hidden="true">{ICON[level]}</span> {t(`security.${level}`)}
    </span>
  );
}
